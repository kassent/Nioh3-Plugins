#include "GameType.h"
#include "CommonUtils.h"
#include "LogUtils.h"
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <format>
#include <fmt/core.h>
// --- Constant definitions ---

static constexpr int kMaxLevel = 500;

// Rating strings (index 0 = AAA+, index 17 = D-)
static const char* kRatingStrings[18] = {
    "AAA+", "AAA", "AAA-",
    "AA+", "AA", "AA-",
    "A+", "A", "A-",
    "B+", "B", "B-",
    "C+", "C", "C-",
    "D+", "D", "D-"
};

// Rating thresholds (descending)
static const int kRatingThresholds[18] = {
    17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0
};

// --- Helper functions ---

static uint16_t LookupScaleTable(const GearAffixData* gearAffix, int16_t scaleType, uint32_t level)
{
    if (!(*g_resManager) || !(*g_resManager)->scaleTableData) {
        return 0;
    }
    ScaleTableDataManager* scaleTableMgr = (*g_resManager)->scaleTableData;
    if (level >= scaleTableMgr->GetDataCount()) {
        return 0;
    }
    if (scaleType < 0 || scaleType > 4) {
        return 0;
    }
    const ScaleTableData* row = scaleTableMgr->GetAt(level);
    return row ? row->columns[scaleType] : 0;
}

static int ValueToRatingIndex(uint32_t value)
{
    for (int i = 0; i < 18; ++i) {
        if (value >= static_cast<uint32_t>(kRatingThresholds[i])) {
            return i;
        }
    }
    return 17;
}

// --- ItemData ---

std::string_view ItemData::GetRarityName() const {
    if (rarity == ITEM_RARITY_UNKNOWN || rarity >= ITEM_RARITY_MAX) {
        return "[C]";
    }
    switch (rarity) {
        case 0: return "[C]";
        case 1: return "[B]";
        case 2: return "[A]";
        case 3: return "[S]";
        case 4: return "[SS]";
        case 5: return "[SSS]";
        default: return "[?]";
    }
}

std::string ItemData::GetName() const {
    auto *localizedString = GetLocalizedString(nameHash);
    if (localizedString) {
        return CommonUtils::ConvertWStringToCString(localizedString);
    }
    return "";
}

std::string ItemData::GetDisplayName() const {
    GameWStringWrapper outName;
    GetItemDisplayName(&outName, 0, 255);

    wchar_t *name = nullptr;
    if (outName.size >= 8) {
        name = outName.str.bigString;
    } else {
        name = outName.str.smallString;
    }
    return CommonUtils::ConvertWStringToCString(name);
}

std::string SoulCoreData::GetDisplayName() const {
    auto *localizedString = GetLocalizedString(nameHash);
    if (localizedString) {
        return CommonUtils::ConvertWStringToCString(localizedString);
    }
    return "";
}

void SoulCoreDataManager::Dump() const {
    _MESSAGE("--------------------------------");
    _MESSAGE("[SoulCoreDataManager]");
    for (uint16_t i = 0; i < 0xFFFF; i++) {
        auto *SoulCoreData = GetSoulCoreData(i);
        if (SoulCoreData) {
            _MESSAGE("  [%d] addr=0x%016I64X nameHash=0x%08X name=\"%s\"", i, SoulCoreData, SoulCoreData->nameHash, SoulCoreData->GetDisplayName().c_str());
        }
    }
    _MESSAGE("========================================");
}

GameStateManager::PlayerManager* GameStateManager::GetPlayerManager(int32_t playerIndex)
{
    if (playerIndex < 0 || playerIndex >= 4) {
        return nullptr;
    }
    if ((*g_gameState) == nullptr) {
        return nullptr;
    }
    return (*g_gameState)->players[playerIndex].playerManager;
}

GameStateManager::PlayerData* GameStateManager::GetPlayerData(int32_t playerIndex)
{
    auto* playerManager = GetPlayerManager(playerIndex);
    if (playerManager == nullptr) {
        return nullptr;
    }
    return playerManager->playerData;
}

uint64_t GameStateManager::GetCurrentHealth(int32_t playerIndex)
{
    auto* playerData = GetPlayerData(playerIndex);
    return playerData ? playerData->GetCurrentHealth() : 0;
}

uint64_t GameStateManager::GetMaxHealthRaw(int32_t playerIndex)
{
    auto* playerData = GetPlayerData(playerIndex);
    return playerData ? playerData->GetMaxHealthRaw() : 0;
}

float GameStateManager::GetHealthScale(int32_t playerIndex)
{
    auto* playerData = GetPlayerData(playerIndex);
    return playerData ? playerData->GetHealthScale() : 0.0f;
}

float GameStateManager::GetCurrentGuardianSpiritProgress(int32_t playerIndex)
{
    auto* playerData = GetPlayerData(playerIndex);
    return playerData ? playerData->GetCurrentGuardianSpiritProgress() : 0.0f;
}

int32_t GameStateManager::GetMaxGuardianSpiritProgress(int32_t playerIndex)
{
    auto* playerData = GetPlayerData(playerIndex);
    return playerData ? playerData->GetMaxGuardianSpiritProgress() : 0;
}

float GameStateManager::GetGuardianSpiritProgressRatio(int32_t playerIndex)
{
    auto* playerData = GetPlayerData(playerIndex);
    return playerData ? playerData->GetGuardianSpiritProgressRatio() : 0.0f;
}

// PlayerData + 0x570 = samurai, +0x14E0 = ninja
void* GameStateManager::GetEquipmentSlotBase(int32_t slotIndex, int32_t setIndex)
{
    if (slotIndex < 0 || slotIndex > 16) {
        return nullptr;
    }
    if (setIndex < 0 || setIndex >= 2) {
        return nullptr;
    }
    auto* playerData = GetPlayerData();
    if (playerData == nullptr) {
        return nullptr;
    }
    return (setIndex == 0)
        ? static_cast<void*>(playerData->samuraiEquipments)
        : static_cast<void*>(playerData->ninjaEquipments);
}

int32_t GameStateManager::GetActiveSetIndex()
{
    auto* playerData = GetPlayerData();
    if (playerData == nullptr) {
        return 0;
    }
    return playerData->activeSlotIndex;
}

InventoryItemData* GameStateManager::GetEquipmentItemFromSlot(int32_t slotIndex, int32_t setIndex)
{
    auto* slotBasePtr = reinterpret_cast<InventoryItemData*>(GetEquipmentSlotBase(slotIndex, setIndex));
    if (slotBasePtr == nullptr) {
        return nullptr;
    }
    return &slotBasePtr[slotIndex];
}


void InventoryItemData::Dump() const {
    // Get item base data
    auto *itemData = GetItemData((*g_resManager)->itemData, itemId);
    if (!itemData) {
        _MESSAGE("========================================");
        _MESSAGE("[Equipment Info] Invalid Item ID: %d", itemId);
        _MESSAGE("========================================");
        return;
    }
    
    // Get item name
    std::string itemName = itemData->GetDisplayName();
    
    // Get rarity name
    std::string rarityName = std::string(itemData->GetRarityName());
    
    // Output equipment basic info
    _MESSAGE("========================================");
    _MESSAGE("[Equipment Info]");
    _MESSAGE("  Item ID: %d", itemId);
    _MESSAGE("  Name: %s %s", itemName.c_str(), rarityName.c_str());
    _MESSAGE("  Rarity: %d", rarity);
    _MESSAGE("  Level: %d", level);
    _MESSAGE("  Flag: 0x%08X", unk18);
    _MESSAGE("  IsLeveled: %s", IsLeveled() ? "Yes" : "No");
    _MESSAGE("  Random Seed: 0x%08X", randomSeed);
    
    // Iterate and output enchantment info
    int validEnchantCount = 0;
    for (int i = 0; i < 7; i++) {
        const auto& enchSlot = enchantments[i];
        
        // Skip invalid enchantment slots
        if (enchSlot.enchantmentId == 0) {
            continue;
        }
        
        validEnchantCount++;
        
        // Get enchantment data
        auto* enchantmentData = GetEnchantmentData((*g_resManager)->enchantmentData, enchSlot.enchantmentId);
        if (!enchantmentData) {
            _MESSAGE("  [Enchantment Slot #%d] Invalid Enchantment ID: %d", i + 1, enchSlot.enchantmentId);
            continue;
        }
        
        // Get enchantment name
        wchar_t enchantNameBuf[256] = {0};
        GetEnchantmentName(enchantmentData, enchantNameBuf, 256);
        std::string enchantName = CommonUtils::ConvertWStringToCString(std::wstring_view(enchantNameBuf));
        
        // Get enchantment description
        wchar_t enchantDescBuf[256] = {0};
        GetEnchantmentDesc(enchantmentData, enchantDescBuf, 256);
        std::string enchantDesc = CommonUtils::ConvertWStringToCString(std::wstring_view(enchantDescBuf));
        
        // Get enchantment rule data
        std::string categoryName = "N/A";
        auto* ruleData = GetEnchantRuleData((*g_resManager)->enchantmentRuleData, enchantmentData->categoryRuleId);
        if (ruleData && ruleData->categoryNameHash != 0) {
            auto* categoryNamePtr = GetLocalizedString(ruleData->categoryNameHash);
            if (categoryNamePtr) {
                categoryName = CommonUtils::ConvertWStringToCString(std::wstring_view(categoryNamePtr));
            }
        }
        
        // Output enchantment slot info
        _MESSAGE("  [Enchantment Slot #%d]", i + 1);
        _MESSAGE("    Enchantment ID: %d", enchSlot.enchantmentId);
        _MESSAGE("    Affix ID: %d", enchSlot.affixId);
        _MESSAGE("    Name: %s", enchantName.c_str());
        _MESSAGE("    Type: %s (Rule ID: %d)", categoryName.c_str(), enchantmentData->categoryRuleId);
        _MESSAGE("    Value: %d", enchSlot.value);
        _MESSAGE("    Percent: %d%%", enchSlot.percent);
        _MESSAGE("    Flag: 0x%02X", enchSlot.flag);
        _MESSAGE("    Flag2: 0x%02X", enchSlot.flagD & 0xC0);
        _MESSAGE("    Category: %d", enchSlot.flagD & 0x3F);
        // category
        if (!enchantDesc.empty()) {
            _MESSAGE("    Description: %s", enchantDesc.c_str());
        }
    }
    
    _MESSAGE("========================================");
}

void ItemData::Dump() const {
    std::string nameStr = GetDisplayName();
    if (nameStr.empty()) nameStr = "Unknown";
    _MESSAGE("[ItemData] itemId=%u nameHash=0x%08X name=\"%s\" category=%d rarity=%d weaponType=%u armorType=%u fixedAffixId=%u affixIdGroup=[%u,%u]",
        itemId, nameHash, nameStr.c_str(), category, rarity, weaponType, armorType, fixedAffixId, affixIdGroup[0], affixIdGroup[1]);
}

void EnchantmentSlot::Dump() const {
    if (enchantmentId == 0) return;
    _MESSAGE("  [Slot] enchantId=%u affixId=%u value=%u percent=%u flag=0x%02X flagD=0x%02X (ruleId=%u)",
        enchantmentId, affixId, value, percent, flag, flagD, flagD & 0x3F);
}

void EnchantmentData::Dump() const {
    wchar_t nameBuf[256] = {0};
    auto* self = const_cast<EnchantmentData*>(this);
    this->GetEnchantmentName(nameBuf, 256);
    std::string nameStr = CommonUtils::ConvertWStringToCString(std::wstring_view(nameBuf));
    this->GetEnchantmentDesc(nameBuf, 256);
    std::string descStr = CommonUtils::ConvertWStringToCString(std::wstring_view(nameBuf));
    _MESSAGE("[EnchantmentData] enchantId=%u nameHash=0x%08X categoryRuleId=%u formatType=%u displayMode=%u flags=0x%X (ranked=%s debuff=%s buff=%s)",
        enchantId, nameHash, categoryRuleId, formatType, displayMode, flags,
        IsRanked() ? "Y" : "N", IsDebuff() ? "Y" : "N", IsBuf() ? "Y" : "N");
    _MESSAGE("  name=\"%s\" desc=\"%s\"", nameStr.c_str(), descStr.c_str());
}

void EnchantRuleData::Dump() const {
    std::string catName = "N/A";
    if (categoryNameHash != 0) {
        wchar_t* p = GetLocalizedString(categoryNameHash);
        if (p) catName = CommonUtils::ConvertWStringToCString(std::wstring_view(p));
    }
    _MESSAGE("[EnchantRuleData] ruleId=%u ruleMaxUsage=%u categoryNameHash=0x%08X (\"%s\")",
        ruleId, ruleMaxUsage, categoryNameHash, catName.c_str());
    _MESSAGE("  weights: weapon=%.2f gun=%.2f helmet=%.2f chest=%.2f arms=%.2f knee=%.2f legs=%.2f amulet=%.2f unknown=%.2f",
        weaponWeight, gunWeight, helmetWeight, chestWeight, armsWeight, kneeWeight, legsWeight, amuletWeight, unknownWeight);
}

void LootTableEntry::Dump() const {
    _MESSAGE("[LootTableEntry] lootTableId=%u flags=0x%X", lootTableId, flags);
    for (int i = 0; i < 5; i++) {
        const LootDropItem& d = drops[i];
        if (d.itemId == 0) continue;
        _MESSAGE("  drop[%d] weight=%u minQty=%u maxQty=%u itemId=%u", i, d.dropWeight, d.minQuantity, d.maxQuantity, d.itemId);
    }
}

GearAffixData* GearAffixData::GetGearAffixFromId(uint16_t a_affixId) {
    if (!(*g_resManager)) {
        return nullptr;
    }
    return GetGearAffixData((*g_resManager)->gearAffixData, a_affixId);
}

void GearAffixData::Dump() const {
    auto* enchantmentData = ::GetEnchantmentData((*g_resManager)->enchantmentData, this->enchantId);
    if (!enchantmentData) {
        _MESSAGE("Invalid enchantment ID: %d", this->enchantId);
        return;
    }
    wchar_t name[256] = {0};
    GetEnchantmentName(enchantmentData, name, 256);
    std::string nameStr = CommonUtils::ConvertWStringToCString(std::wstring_view(name));
    GetEnchantmentDesc(enchantmentData, name, 256);
    std::string descStr = CommonUtils::ConvertWStringToCString(std::wstring_view(name));
    auto* ruleData = ::GetEnchantRuleData((*g_resManager)->enchantmentRuleData, enchantmentData->categoryRuleId);
    std::string categoryStr = "N/A";
    if (ruleData && ruleData->categoryNameHash != 0) {
        auto* categoryName = ::GetLocalizedString(ruleData->categoryNameHash);
        if (categoryName) {
            categoryStr = CommonUtils::ConvertWStringToCString(std::wstring_view(categoryName));
        }
    }
    std::string_view rarityStr = this->GetRarityName();
    int32_t maxValue = this->CalculateAffixValue(100, 160);
    std::string valueStr = this->GetAffixValueString(maxValue);
    _MESSAGE("%d,%d,%s,%s,%s,%s,%s,%s,%s,%d,%s,%s,%s,%d,%s",
        this->affixId, this->enchantId,
        flag1C & Grace ? "0x02" : "",
        flag1C & Legendary ? "0x10" : "",
        flag1C & Rerollable ? "0x80" : "",
        flag1C & Samurai ? "0x04" : "",
        flag1C & Ninja ? "0x08" : "",
        IsSpecialItemBonus() ? "Special" : "",
        nameStr.c_str(), enchantmentData->categoryRuleId, categoryStr.c_str(), descStr.c_str(),
        std::string(rarityStr).c_str(), this->baseValue, valueStr.c_str());
}




/*
__int64 __fastcall SetEquippedItem_14047D974(__int64 a1, int slot, __int64 a3, int isNinja)
{
  __int64 result; // rax

  if ( isNinja >= 0 )
  {
    result = isNinja;
    if ( (unsigned __int64)isNinja < 2 && slot >= 0 && (unsigned __int64)slot < 17 )
      return SetEquipItemData_14047D9B8(a1 + 3952LL * isNinja + 0xE8LL * slot, a3);
  }
  return result;
}

void __fastcall UpdateLeveledItem_1405B7E4C(InventoryItemData *a1, uint16_t a2)
{
  if ( (a1->unk18 & 0x200000) == 0 )
  {
    if ( a2 >= 170u )
      a2 = 170;
    a1->level = a2;
    sub_1405B7E74((__int64)a1);
  }
}
*/

// struct AffixGenerateParams {
//     uint16_t itemId; // 0x00
//     int8_t rarity; // 0x02
//     int8_t pad03; // 0x03
//     uint32_t seed; // 0x04
//     uint32_t unk08; // 0x08 0
//     uint32_t unk0C; // 0x0C 0
//     int8_t missionMode; // 0x10 2 hard 3 very hard 4 super hard
//     int8_t pad11; // 0x11
//     int16_t pad12; // 0x12
//     int32_t missionProgress; // 0x14
// };
// static_assert(sizeof(AffixGenerateParams) == 0x18);

// --- GearAffixData member implementations ---

// finalValue = levelScaler * lerp(min, max, quality/100) + baseOffset
int32_t GearAffixData::CalculateAffixValue(uint8_t qualityPercent, int32_t itemLevel) const
{
    int32_t clampedLevel = std::min(itemLevel, kMaxLevel);
    uint16_t rawScaler = LookupScaleTable(this, this->scaleType, clampedLevel);
    float levelScaler = static_cast<float>(rawScaler) * 0.001f;
    float minVal = static_cast<float>(this->minValue);
    float maxVal = static_cast<float>(this->maxValue);
    float qualityRatio = static_cast<float>(qualityPercent) * 0.01f;
    float lerpedValue = minVal + (maxVal - minVal) * qualityRatio;
    float result = levelScaler * lerpedValue + static_cast<float>(this->baseValue);
    return static_cast<int32_t>(result);
}

std::string GearAffixData::GetAffixValueString(int32_t value) const
{
    auto* enchantmentData = GetEnchantmentData((*g_resManager)->enchantmentData, this->enchantId);
    if (!enchantmentData) {
        return std::to_string(value);
    }
    if (enchantmentData->flags & 0x04) {
        int ratingIdx = ValueToRatingIndex(static_cast<uint32_t>(value));
        return std::string(kRatingStrings[ratingIdx]);
    }
    if (enchantmentData->enchantId == 21758 || enchantmentData->formatType == 0) {
        return std::to_string(value);
    }
    std::string formatString = CommonUtils::ConvertWStringToCString(GetLocalizedString(enchantmentData->formatType));
    if (formatString.empty()) {
        return std::to_string(value);
    }
    auto dotIndex = formatString.find("<dot>");
    if (dotIndex != std::string::npos) {
        formatString.replace(dotIndex, 5, ".");
    }
    switch (enchantmentData->displayMode) {
        case 2: {
            int integerPart = value / 100;
            int decimalPart = std::abs(value % 100);
            return std::vformat(formatString, std::make_format_args(integerPart, decimalPart));
        }
        case 1: {
            int integerPart = value / 10;
            int decimalPart = std::abs(value % 10);
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "%d.%d", integerPart, decimalPart);
            return std::vformat(formatString, std::make_format_args(integerPart, decimalPart));
        }
        default:
            return std::to_string(value);
    }
}

std::string_view GearAffixData::GetRarityName() const {
    if (rarity == ITEM_RARITY_UNKNOWN || rarity >= ITEM_RARITY_MAX) {
        return "[C]";
    }
    switch (rarity) {
        case 0: return "C";
        case 1: return "B";
        case 2: return "A";
        case 3: return "S";
        case 4: return "SS";
        case 5: return "SSS";
        default: return "?";
    }
}
