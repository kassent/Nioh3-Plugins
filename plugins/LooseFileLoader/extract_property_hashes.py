import argparse
import csv
import re
from pathlib import Path

# Example line:
#     0: ParallelUnitNum(D787B5A5) 15000001
PROPERTY_LINE_RE = re.compile(r"^\s*\d+:\s*([^()\r\n]+?)\(([0-9A-Fa-f]{8})\)\s+[0-9A-Fa-f]+\s*$")


def extract_properties(input_path: Path) -> list[tuple[str, str]]:
    unique_pairs: set[tuple[str, str]] = set()

    with input_path.open("r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            match = PROPERTY_LINE_RE.match(line)
            if not match:
                continue

            name = match.group(1).strip()
            hash_hex = match.group(2).upper()
            unique_pairs.add((f"0x{hash_hex}", name))

    return sorted(unique_pairs, key=lambda item: (item[0], item[1].lower()))


def write_csv(rows: list[tuple[str, str]], output_path: Path) -> None:
    with output_path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["property_hash", "property_name"])
        writer.writerows(rows)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Extract unique property names and hashes from property.txt into CSV."
    )
    parser.add_argument(
        "input",
        nargs="?",
        default="property.txt",
        help="Input property file path (default: property.txt)",
    )
    parser.add_argument(
        "-o",
        "--output",
        default="property_hashes.csv",
        help="Output CSV path (default: property_hashes.csv)",
    )
    args = parser.parse_args()

    input_path = Path(args.input)
    output_path = Path(args.output)

    rows = extract_properties(input_path)
    write_csv(rows, output_path)

    print(f"Extracted {len(rows)} unique properties to: {output_path}")


if __name__ == "__main__":
    main()
