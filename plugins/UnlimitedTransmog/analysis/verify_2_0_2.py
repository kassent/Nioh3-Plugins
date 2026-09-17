"""Read-only checks against an unpacked 2.0.2.0 PE; does not build or patch it.

Usage: python verify_2_0_2.py PATH_TO_UNPACKED_EXE
Dependencies: pefile, capstone
"""

import argparse
import ast
import ctypes
from pathlib import Path
import re
import struct

import capstone
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_OP_REG
import pefile


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=Path)
    args = parser.parse_args()
    source = (Path(__file__).resolve().parents[1] / "main.cpp").read_text(encoding="utf-8-sig")
    common = (Path(__file__).resolve().parents[3] / "common/include/GameType.h").read_text(encoding="utf-8-sig")
    pe = pefile.PE(str(args.exe), fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    segments = [(s.VirtualAddress, s.get_data()) for s in pe.sections if s.Characteristics & 0x20000000]
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    decoder.detail = True

    def scan(pattern):
        tokens = pattern.split()
        raw = b"".join(b"." if t == "?" else re.escape(bytes([int(t, 16)])) for t in tokens)
        # Lookahead also counts overlapping matches.
        regex = re.compile(b"(?=" + raw + b")", re.DOTALL)
        return [rva + match.start() for rva, data in segments for match in regex.finditer(data)]

    def decode(rva, length=16):
        return list(decoder.disasm(pe.get_data(rva, length), base + rva))

    def instruction(rva, mnemonic, operands):
        decoded = decode(rva)[0]
        require((decoded.mnemonic, decoded.op_str) == (mnemonic, operands), f"Unexpected instruction at {rva:#x}: {decoded.mnemonic} {decoded.op_str}")

    def target(rva):
        require(pe.get_data(rva, 1) == b"\xe8", f"Not a direct call: {rva:#x}")
        return rva + 5 + struct.unpack("<i", pe.get_data(rva + 1, 4))[0]

    patterns = {
        "patchAddr1": re.search(r'patchAddr1 = HookUtils::ScanIDAPattern\("([^"]+)"', source).group(1),
        "patchAddr3": re.search(r'patchAddr3 = HookUtils::LookupFunctionPattern\([^;]*?"([^"]+)"', source).group(1),
        "GetLocalizedString": re.search(r'GetLocalizedString\(\s*REL::Pattern\(0x\w+, "([^"]+)"', common).group(1),
        "g_resManager": re.search(r'g_resManager\(REL::Pattern\(0x\w+, "([^"]+)"', common).group(1),
    }
    expected = {
        "patchAddr1": (0x2343227, 0x54F124),
        "patchAddr3": (0x2343242, 0x2347DC4),
        "GetLocalizedString": (0x34E26E, 0x5A4B04),
        "g_resManager": (0x23431C7, 0x45B9E30),
    }
    require(patterns.keys() == expected.keys(), "Unexpected/missing source patterns")
    for name, (call, callee) in expected.items():
        hits = scan(patterns[name])
        require(hits == [call], f"{name}: unexpected matches {hits}")
        resolved = call + 7 + struct.unpack("<i", pe.get_data(call + 3, 4))[0] if name == "g_resManager" else target(call)
        require(resolved == callee, f"{name}: wrong resolved target")
        print(f"PASS {name}: 1 executable-section match, RVA {call:#010x} -> {callee:#010x}")

    # The original code intentionally uses a local second-stage signature.
    prefix = re.search(r'patchAddr2 = HookUtils::LookupFunctionPattern\([^;]*?"([^"]+)"', source).group(1)
    all_hits = scan(prefix)
    local_hits = [rva for rva in all_hits if 0x2343227 <= rva < 0x2343227 + 0x100]
    require(local_hits == [0x2343230], "Wrong second-stage local match")
    require("patchAddr2 += 6;" in source and target(local_hits[0] + 6) == 0x2347D78, "Wrong local filter target")
    print(f"PASS patchAddr2: {len(all_hits)} global prefix matches; 1 in anchored +0x100 range, +6 -> call RVA 0x02343236 -> 0x02347d78")

    # Prove the original pre-call filter and post-call unlock contracts.
    instruction(0x23431EF, "mov", "r14, rax")
    instruction(0x23431FB, "movzx", "edx, word ptr [rax + 0x152]")
    instruction(0x2343202, "mov", "al, byte ptr [rax + 0x182]")
    instruction(0x2343230, "mov", "r8d, r13d")
    instruction(0x2343233, "mov", "rdx, r14")
    instruction(0x234322C, "test", "al, al")
    length = 0
    for decoded in decode(0x234322C):
        length += decoded.size
        if length >= 5:
            break
    ranges = [(0x234322C, 0x234322C + length), (0x2343236, 0x234323B), (0x2343242, 0x2343247)]
    require(all(a[1] <= b[0] for a, b in zip(ranges, ranges[1:])), "Mid-hook instruction ranges overlap")
    require("static_cast<uint32_t>(ctx.r8)" in source and "(ItemData*)ctx.rdx" in source and "(ItemData*)ctx.r14" in source, "Source hook register contract changed")
    print("PASS R14 unlock / RDX+R8 filter context and non-overlapping patch ranges")

    # Reconstruct the existing packed ItemData, then compare with native reads.
    body = re.search(r"struct ItemData\s*\{(.*?)void Dump", common, re.DOTALL).group(1)
    types = {"uint8_t": ctypes.c_uint8, "int8_t": ctypes.c_int8, "uint16_t": ctypes.c_uint16, "uint32_t": ctypes.c_uint32, "uint64_t": ctypes.c_uint64}

    def array_size(expression):
        def evaluate(node):
            if isinstance(node, ast.Constant) and isinstance(node.value, int):
                return node.value
            require(isinstance(node, ast.BinOp), "Unexpected array-size syntax")
            left, right = evaluate(node.left), evaluate(node.right)
            if isinstance(node.op, ast.Sub):
                return left - right
            require(isinstance(node.op, ast.RShift), "Unexpected array-size operator")
            return left >> right
        return evaluate(ast.parse(expression, mode="eval").body)

    fields = []
    for kind, name, count in re.findall(r"(uint8_t|int8_t|uint16_t|uint32_t|uint64_t)\s+(\w+)(?:\[([^\]]+)\])?;", body):
        field_type = types[kind]
        if count:
            field_type *= array_size(count)
        fields.append((name, field_type))
    layout = type("ItemData", (ctypes.Structure,), {"_pack_": 1, "_fields_": fields})
    offsets = {"weaponType": 0x58, "gunType": 0x5C, "armorType": 0x60, "nameHash": 0x68, "category": 0x182}
    for name, offset in offsets.items():
        require(getattr(layout, name).offset == offset, f"Wrong source offset: {name}")
    require(ctypes.sizeof(layout) == 0x1A0, "Wrong ItemData size")
    native_offsets = {operand.mem.disp for ins in decode(0x2347D78, 0x4C) for operand in ins.operands if operand.type == X86_OP_MEM}
    require({0x58, 0x5C, 0x60, 0x182} <= native_offsets, "Native type-filter fields differ")
    instruction(0x34E3CC, "mov", "eax, dword ptr [r8 + 0x68]")
    require(target(0x34E267) == 0x34E3B8, "Item name-hash getter changed")
    instruction(0x34E26C, "mov", "ecx, eax")
    print("PASS source field offsets, native filter reads and item-name lookup chain")

    def signed(value):
        value &= 0xFFFFFFFF
        return value - 0x100000000 if value & 0x80000000 else value

    def native_group(rva, item_type):
        # Deliberately tiny interpreter for these three pure register-only maps.
        # No execution of the PE and no process memory access.
        registers = {"ecx": item_type & 0xFFFFFFFF, "eax": 0}
        pc, zero, greater = rva, False, False
        for _ in range(100):
            ins = decode(pc)[0]
            mnemonic = ins.mnemonic
            operands = ins.operands
            next_pc = pc + ins.size

            def value(operand):
                if operand.type == X86_OP_IMM:
                    return operand.imm & 0xFFFFFFFF
                require(operand.type == X86_OP_REG, "Unexpected map operand")
                return registers[ins.reg_name(operand.reg)]

            if mnemonic == "ret":
                return registers["eax"]
            if mnemonic in ("je", "jne", "jg"):
                take = {"je": zero, "jne": not zero, "jg": greater}[mnemonic]
                if take:
                    next_pc = operands[0].imm - base
            elif mnemonic == "mov":
                registers[ins.reg_name(operands[0].reg)] = value(operands[1])
            elif mnemonic in ("cmp", "sub", "test", "xor"):
                left, right = value(operands[0]), value(operands[1])
                result = ((left & right) if mnemonic == "test" else (left ^ right) if mnemonic == "xor" else left - right) & 0xFFFFFFFF
                zero = result == 0
                greater = signed(left) > signed(right) if mnemonic in ("cmp", "sub") else signed(result) > 0
                if mnemonic in ("sub", "xor"):
                    registers[ins.reg_name(operands[0].reg)] = result
            else:
                raise AssertionError(f"Unexpected native map instruction: {mnemonic}")
            pc = next_pc
        raise AssertionError("Native map step limit reached")

    mapping_checks = [
        ("GetMeleeWeaponDisplayType", 0x2344940, range(16)),
        ("GetRangedWeaponDisplayType", 0x2345178, range(16, 19)),
        ("GetArmorDisplayType", 0x2344430, range(19, 24)),
    ]
    for name, native, valid_types in mapping_checks:
        body = re.search(rf"int32_t {name}\([^)]*\)\s*\{{(.*?)default:", source, re.DOTALL).group(1)
        pairs = {int(group): int(display) for group, display in re.findall(r"case (\d+):\s*return (\d+);", body)}
        require(len(pairs) == len(valid_types), f"Wrong source mapping count: {name}")
        for item_type in range(-2, 26):
            group = native_group(native, item_type)
            if item_type in valid_types:
                require(pairs.get(group) == item_type, f"Source/native map mismatch: {name}, type {item_type}")
            else:
                require(group == 0, f"Unexpected native map domain: {name}, type {item_type}")
        print(f"PASS {name}: source mapping matches native instructions, including out-of-range inputs")

    require("#include <GameType.h>" in source and "itemData->GetName()" in source, "Existing ItemData API is not reused")
    require("ctx.r12" not in source and "g_resManager" not in source, "Legacy item lookup remains in hook")
    print("PASS all offline checks (no build, installation or in-game test performed)")


if __name__ == "__main__":
    main()
