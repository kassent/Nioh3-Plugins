#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include "Relocation.h"

// --- Enums: Item / Equipment ---

enum ITEM_CATEGORY {
    ITEM_CATEGORY_NINJA_BOMB = 0,       // Ninja bomb
    ITEM_CATEGORY_WEAPON = 1,            // Weapon
    ITEM_CATEGORY_GUN = 2,               // Gun
    ITEM_CATEGORY_ARMOR = 3,             // Armor
    ITEM_CATEGORY_CONSUMABLE = 4,        // Consumable
    ITEM_CATEGORY_MATERIAL = 5,          // Enhancement material
    ITEM_CATEGORY_KEY = 6,               // Key
    ITEM_CATEGORY_RUNE = 7,              // Rune
    ITEM_CATEGORY_AMMO = 8,              // Ammo
    ITEM_CATEGORY_SOUL_CORE = 9,          // Soul core
    ITEM_CATEGORY_SKILL = 10,            // Skill
    ITEM_CATEGORY_LEGENDARY_EQUIPMENT = 11, // Legendary equipment
    ITEM_CATEGORY_UNKNOWN12 = 12,        // Unknown
    ITEM_CATEGORY_NINJA_SKILL = 13,      // Ninja skill
    ITEM_CATEGORY_UNKNOWN14 = 14,        // Unknown
};

enum ARMOR_TYPE {
    ARMOR_TYPE_HEAD = 3577,    // Helmet
    ARMOR_TYPE_CHEST = 11055,  // Chest
    ARMOR_TYPE_ARMS = 1975,    // Arms
    ARMOR_TYPE_KNEE = 16443,   // Knee
    ARMOR_TYPE_LEGS = 2473,    // Legs
    ARMOR_TYPE_AMULET = 11570, // Amulet
};

enum WEAPON_TYPE {
    WEAPON_TYPE_DAITO = 29361, // Odachi
};

enum ITEM_RARITY : int8_t {
    ITEM_RARITY_UNKNOWN = -1,  // Unknown
    ITEM_RARITY_C = 0,         // Common
    ITEM_RARITY_B = 1,         // Rare
    ITEM_RARITY_A = 2,         // Epic
    ITEM_RARITY_S = 3,         // Legendary
    ITEM_RARITY_SS = 4,        // Mythic
    ITEM_RARITY_SSS = 5,       // Mythic
    ITEM_RARITY_MAX = 6,       // Max
};

// --- Packed structs: Item / Equipment ---

#pragma pack(push, 1)

// 7 enchantment slots (24 bytes each)
struct EnchantmentSlot {
    uint16_t enchantmentId;  // +0x0
    uint16_t padding2;       // +0x2
    uint32_t affixId;       // +0x4
    uint32_t value;         // +0x8
    uint8_t percent;        // +0xC
    uint8_t flagD;          // +0xD: 0x3F ruleId, 0x80 rerollable, 0x40 fixed affix
    uint8_t flag;           // +0xE: 0x04 perfect star, 0x40 amulet
    uint8_t paddingF;       // +0xF
    uint32_t unk10;         // +0x10
    uint32_t unk14;         // +0x14

    void Dump() const;
};
static_assert(sizeof(EnchantmentSlot) == 0x18);

struct InventoryItemData {
    uint16_t itemId;            // +0x0
    uint16_t transmogItemId;    // +0x2
    uint16_t unk4;              // +0x4
    uint16_t level;             // +0x6 (e.g. 160, 170)
    uint16_t forgeLevel;        // +0x8
    uint16_t rarityLevel;       // +0xA: rarity level (0-15) 装等 sub_14202F74C
    uint8_t unkC;               // +0xC
    uint8_t flagD;              // +0xD
    uint8_t unkE;               // +0xE
    uint8_t unkF;               // +0xF
    uint16_t hellSkillId;       // +0x10
    uint16_t unk12;             // +0x12
    uint32_t usage;             // +0x14
    uint32_t unk18;             // +0x18: flags (0x200000 leveled, 0x100000 hell skill)
    uint32_t unk1C;             // +0x1C
    uint32_t randomSeed;        // +0x20
    uint16_t unk24;             // +0x24
    uint16_t unk26;             // +0x26
    uint32_t unk28;             // +0x28: counter
    uint32_t unk2C;             // +0x2C
    uint8_t rarity;             // +0x30
    uint8_t unk31;              // +0x31
    uint8_t unk32;              // +0x32
    uint8_t unk33;              // +0x33
    EnchantmentSlot enchantments[7];  // +0x34
    uint32_t unkDC;             // +0xDC
    uint64_t unkE0;             // +0xE0
    // Total size: 0xE8

    void Dump() const;
    bool IsLeveled() const { return (unk18 & 0x200000) != 0; }
    bool IsHellSkill() const { return (unk18 & 0x100000) != 0; }

    /*
     * When in equipment box, two extra fields for equipped state:
     * uint32_t samuraiSlot; // default 0x11
     * uint32_t ninjaSlot;   // default 0x11
     */
};
static_assert(offsetof(InventoryItemData, hellSkillId) == 0x10);
static_assert(offsetof(InventoryItemData, enchantments) == 0x34);
static_assert(sizeof(InventoryItemData) == 0xE8);

struct ItemData {
    uint64_t unk00[0x48 >> 3];
    uint32_t unk48;           // +0x48
    uint32_t weaponType;      // +0x4C
    uint32_t gunType;         // +0x50
    uint32_t armorType;       // +0x54
    uint32_t unk58;           // +0x58
    uint32_t nameHash;        // +0x5C
    uint64_t unk60[(0xA0 - 0x60) >> 3];
    uint32_t unkA0;           // +0xA0
    uint32_t flagA4;          // +0xA4
    uint64_t unkA8[(0x150 - 0xA8) >> 3];
    uint16_t unk150;          // +0x150
    uint16_t itemId;          // +0x152
    uint16_t fixedAffixId;    // +0x154
    uint16_t unk156;         // +0x156
    uint16_t affixIdGroup[2];  // +0x158
    uint32_t unk15C;          // +0x15C
    uint64_t unk160[(0x180 - 0x160) >> 3];
    uint16_t monsterId;       // +0x180
    int8_t category;          // +0x182
    uint8_t unk183;           // +0x183
    int8_t rarity;            // +0x184: item rarity (see loot table)
    uint8_t unk185;           // +0x185
    uint16_t unk186;          // +0x186
    uint64_t unk188[(0x1A0 - 0x188) >> 3];

    void Dump() const;
    bool IsSamuraiItem() const { return (flagA4 & 0x800) != 0; }
    bool IsNinjaItem() const { return (flagA4 & 0x1000) != 0; }
    std::string_view GetRarityName() const;
    std::string GetDisplayName() const;
    std::string GetName() const;

    struct GameWStringWrapper {
        union {
            wchar_t* bigString;
            wchar_t smallString[8];
        } str;
        uint64_t size;
        uint64_t capacity; 

        GameWStringWrapper() {
            size = 0;
            capacity = 0;
            std::memset(str.smallString, 0, sizeof(str.smallString));
        }

        ~GameWStringWrapper() {
            Release();
        }
        DEF_MEMBER_FN(Release, void, 0x01FC5D0);
    };
    static_assert(sizeof(GameWStringWrapper) == 0x20);

    // GetItemDisplayName_1405A900C(ItemData_1404B9470, v6, 0, 255);
    DEF_MEMBER_FN_CONST(GetItemDisplayName, wchar_t*, 0x02BC264, GameWStringWrapper* a_outName, int32_t a_param1, int32_t a_param2);
};
static_assert(offsetof(ItemData, unk58) == 0x58);
static_assert(offsetof(ItemData, nameHash) == 0x5C);
static_assert(offsetof(ItemData, category) == 0x182);
static_assert(offsetof(ItemData, rarity) == 0x184);
static_assert(offsetof(ItemData, flagA4) == 0xA4);
static_assert(sizeof(ItemData) == 0x1A0);

// --- Enchantment ---

struct EnchantmentData {
    uint8_t unk00[12];                    // +0x00
    uint16_t enchantId;                   // +0x0C: enchant ID (special value 21758 has special handling)
    uint16_t unk0E;                       // +0x0E
    uint64_t unk10[(0x20 - 0x10) >> 3];  // +0x10
    uint32_t unk20;                       // +0x20
    uint16_t categoryRuleId;              // +0x24: low 6 bits store category
    uint16_t unk26;                       // +0x26
    uint32_t nameHash;                    // +0x28
    uint32_t descHash;                    // +0x2C
    uint32_t formatType;                  // +0x30: format template ID (0 = none)
    uint32_t buffHash;                    // +0x34
    uint32_t unk38[(0x4C - 0x38) >> 2];
    uint32_t flags;                       // +0x4C: 0x8 buff, 0x10 debuff, 0x4 ranked/leveled buff
    uint32_t unk50[(0x68 - 0x50) >> 2];
    uint8_t displayMode;                  // +0x68: 0=integer, 1=tenths, 2=percent
    uint8_t unk69;                        // +0x69
    uint16_t unk6A;                       // +0x6A

    void Dump() const;
    bool IsRanked() const { return (flags & 0x4) != 0; }
    bool IsDebuff() const { return (flags & 0x10) != 0; }
    bool IsBuf() const { return (flags & 0x8) != 0; }


    DEF_MEMBER_FN_CONST(GetEnchantmentName, wchar_t*, 0x2091C44, wchar_t* a_name, int32_t a_nameBufLen);
    DEF_MEMBER_FN_CONST(GetEnchantmentDesc, void, 0x208E9E0, wchar_t* a_name, int32_t a_nameBufLen);
};
static_assert(offsetof(EnchantmentData, unk00) == 0x00);
static_assert(offsetof(EnchantmentData, nameHash) == 0x28);
static_assert(offsetof(EnchantmentData, categoryRuleId) == 0x24);
static_assert(sizeof(EnchantmentData) == 0x6C);

struct EnchantRuleData {
    uint16_t unk00;           // +0x00
    uint16_t ruleMaxUsage;    // +0x02
    uint32_t unk04;           // +0x04
    uint16_t ruleId;          // +0x08
    uint16_t unk0A;           // +0x0A
    uint32_t categoryNameHash; // +0x0C
    uint32_t unk10;           // +0x10
    uint32_t unk14;           // +0x14
    uint32_t unk18;           // +0x18
    uint32_t unk1C;           // +0x1C
    uint32_t unk20;           // +0x20
    float weaponWeight;      // +0x24
    uint16_t unk28;          // +0x28
    float gunWeight;         // +0x2A
    uint16_t unk2E;          // +0x2E
    float helmetWeight;      // +0x30
    uint16_t unk34;          // +0x34
    float chestWeight;       // +0x36
    uint16_t unk3A;          // +0x3A
    float armsWeight;        // +0x3C
    uint16_t unk40;          // +0x40
    float kneeWeight;        // +0x42
    uint16_t unk46;          // +0x46
    float legsWeight;        // +0x48
    uint16_t unk4C;          // +0x4C
    float amuletWeight;      // +0x4E
    uint16_t unk52;          // +0x52
    float unknownWeight;     // +0x54 (category 9)
    uint16_t unk58;          // +0x58
    uint32_t unk5A;          // +0x5A
    uint16_t unk5E;          // +0x5E
    uint32_t unk60;          // +0x60

    void Dump() const;
};
static_assert(offsetof(EnchantRuleData, unk00) == 0x00);
static_assert(offsetof(EnchantRuleData, ruleMaxUsage) == 0x02);
static_assert(offsetof(EnchantRuleData, ruleId) == 0x08);
static_assert(offsetof(EnchantRuleData, categoryNameHash) == 0x0C);
static_assert(offsetof(EnchantRuleData, unk10) == 0x10);
static_assert(offsetof(EnchantRuleData, weaponWeight) == 0x24);
static_assert(offsetof(EnchantRuleData, unknownWeight) == 0x54);
static_assert(sizeof(EnchantRuleData) == 0x64);

struct SpiritGuardData {
    uint64_t unk00[0x28 >> 3];
    uint32_t unk28;
    uint32_t nameHash;
};
static_assert(offsetof(SpiritGuardData, unk00) == 0x00);
static_assert(offsetof(SpiritGuardData, unk28) == 0x28);
static_assert(offsetof(SpiritGuardData, nameHash) == 0x2C);

using FnGetSpiritGuard = SpiritGuardData* (*)(void* a_spiritGuardManager, uint32_t a_spiritGuardId);
inline REL::Relocation<FnGetSpiritGuard> GetSpiritGuard(REL::Offset(0x06CE818));

// --- Gear affix ---

struct GearAffixData {
    uint16_t affixId;         // +0x00
    uint16_t enchantId;       // +0x02
    uint16_t unk04;           // +0x04
    uint16_t scaleType;       // +0x06
    uint16_t baseValue;       // +0x08
    uint16_t minValue;        // +0x0A
    uint16_t maxValue;        // +0x0C
    uint16_t extraScaleType;  // +0x0E
    uint16_t extraBaseValue;  // +0x10
    uint16_t extraBonus;     // +0x12
    uint16_t secScaleType;    // +0x14
    uint16_t secBaseValue;    // +0x16
    uint16_t secMinValue;     // +0x18
    uint16_t secMaxValue;     // +0x1A

    enum Flags {
        Grace = 0x2,       // Grace affix
        Samurai = 0x4,     // Samurai-only
        Ninja = 0x8,       // Ninja-only
        Legendary = 0x10,  // Appears on tier-3 equipment
        Random = 0x40,     // Random roll affix
        Rerollable = 0x80, // Can be rerolled
    };
    uint8_t flag1C;   // +0x1C
    uint8_t unk1D;    // +0x1D
    uint16_t unk1E;   // +0x1E
    uint8_t flag20;   // +0x20: 0x08 starred affix 0x02
    uint8_t unk21;    // +0x21
    uint16_t unk22;   // +0x22
    int8_t rarity;    // +0x24 (used by GetRarityString)
    uint8_t unk25;   // +0x25
    uint16_t unk26;   // +0x26
    uint64_t unk28[(0x50 - 0x28) >> 3];
    uint32_t flag50;  // +0x50
    uint32_t flag54;  // +0x54
    uint64_t unk58[(0xD8 - 0x58) >> 3];

    static GearAffixData* GetGearAffixFromId(uint16_t a_affixId);
    void Dump() const;
    int32_t CalculateAffixValue(uint8_t qualityPercent, int32_t itemLevel) const;
    std::string GetAffixValueString(int32_t value) const;
    std::string_view GetRarityName() const;
    bool IsSpecialItemBonus() const { return (flag20 & 0x02) != 0; }
};
static_assert(sizeof(GearAffixData) == 0xD8);
static_assert(offsetof(GearAffixData, enchantId) == 0x02);
static_assert(offsetof(GearAffixData, flag50) == 0x50);
static_assert(offsetof(GearAffixData, flag54) == 0x54);
static_assert(offsetof(GearAffixData, rarity) == 0x24);

// --- Data array managers (template) ---

template<typename T>
struct DataArrayManager {
    struct Header {
        uint32_t unk00;
        uint32_t dataCount;
        T data[0];
    }* dataArray;

    uint32_t GetDataCount() const {
        return dataArray ? dataArray->dataCount : 0u;
    }
    T* GetAt(uint32_t index) const {
        if (!dataArray || index >= dataArray->dataCount) return nullptr;
        return &dataArray->data[index];
    }
};

using GearAffixManager = DataArrayManager<GearAffixData>;

struct QualityConfig {
    uint32_t unk00[0xD0 >> 2];
};
static_assert(sizeof(QualityConfig) == 0xD0);

using QualityConfigManager = DataArrayManager<QualityConfig>;

struct SoulCoreData {
    uint32_t unk00[0x10 >> 2];
    uint32_t nameHash;
    uint32_t unk14[(0x54 - 0x14) >> 2];

    std::string GetDisplayName() const;
};
static_assert(offsetof(SoulCoreData, nameHash) == 0x10);
static_assert(sizeof(SoulCoreData) == 0x54);

struct SoulCoreDataManager : DataArrayManager<SoulCoreData> {
    DEF_MEMBER_FN_CONST(GetSoulCoreData, SoulCoreData*, 0x0277F70, uint16_t a_monsterId);
    void Dump() const;
};

struct ItemDataManager : DataArrayManager<ItemData> {
    DEF_MEMBER_FN_CONST(GetItemData, ItemData*, 0x04A25F0, uint16_t a_itemId); // 1404A25F0
};


struct ScaleTableData {
    uint16_t columns[5];
};

using ScaleTableDataManager = DataArrayManager<ScaleTableData>;

using FnGetNthQualityConfig = QualityConfig* (*)(void* a_qualityConfigManager, uint32_t a_index);
inline REL::Relocation<FnGetNthQualityConfig> GetNthQualityConfig(REL::Offset(0x0277910));

// --- Loot ---

struct LootDropItem {
    uint32_t dropWeight;   // +0x00: cumulative weight
    uint32_t minQuantity;  // +0x04
    uint32_t maxQuantity;  // +0x08
    uint16_t itemId;       // +0x0C: non-zero = valid
    uint16_t padding;      // +0x0E
};
static_assert(sizeof(LootDropItem) == 0x10);

struct LootTableEntry {
    uint16_t lootTableId;   // +0x00
    uint16_t unk02;        // +0x02
    uint32_t unk04;        // +0x04
    uint32_t flags;        // +0x08: bit 3 = 0x8 special handling
    uint32_t unk0C;        // +0x0C
    uint32_t unk10;        // +0x10
    uint32_t unk14;        // +0x14
    uint32_t unk18;        // +0x18
    LootDropItem drops[5]; // +0x1C: 5 drop slots

    void Dump() const;
};
static_assert(sizeof(LootTableEntry) == 0x6C);

// --- Resource manager ---

struct ResourceManager {
    uint64_t unk00[0x60 >> 3];
    ItemDataManager* itemData;                    // +0x60
    uint64_t unk68[(0x78 - 0x68) >> 3];
    void* dropTableData;               // +0x78
    uint64_t unk80[(0xA0 - 0x80) >> 3];
    void* enchantmentData;            // +0xA0
    void* enchantmentRuleData;         // +0xA8
    uint64_t unkB0;
    ScaleTableDataManager* scaleTableData;  // +0xB8
    GearAffixManager* gearAffixData;        // +0xC0
    uint64_t unkC8[(0x110 - 0xC8) >> 3];
    SoulCoreDataManager* SoulCoreData; // +0x110
    uint64_t unk118[(0x9A0 - 0x118) >> 3];
    QualityConfigManager* qualityConfigData; // +0x9A0
};
static_assert(offsetof(ResourceManager, itemData) == 0x60);
static_assert(offsetof(ResourceManager, enchantmentData) == 0xA0);
static_assert(offsetof(ResourceManager, scaleTableData) == 0xB8);
static_assert(offsetof(ResourceManager, gearAffixData) == 0xC0);
static_assert(offsetof(ResourceManager, SoulCoreData) == 0x110);
static_assert(offsetof(ResourceManager, qualityConfigData) == 0x9A0);


inline REL::Relocation<ResourceManager**> g_resManager(REL::Offset(0x438B8E0));

// --- Game state ---

struct GameStateManager {
    // hpContext = playerData + 0x38, used by sub_1403C9398 (set current HP)
    struct HealthContext {
        uint64_t unk00;        // +0x00
        void* notifier;        // +0x08
        uint32_t state;        // +0x10
        uint32_t unk14;        // +0x14
        uint64_t maxHealthRaw; // +0x18
        uint64_t currentHealth; // +0x20
        uint32_t minHealth;    // +0x28
        uint32_t unk2C;        // +0x2C
        uint64_t unk30;        // +0x30
        float healthScale;     // +0x38
        uint32_t unk3C;        // +0x3C
    };
    static_assert(sizeof(HealthContext) == 0x40);
    static_assert(offsetof(HealthContext, currentHealth) == 0x20);

    
    struct GuardianSpiritProgressContext {
        uint64_t unk00;         // +0x00
        void* notifier;         // +0x08
        uint32_t unk10;         // +0x10
        float currentProgress;  // +0x14
        uint32_t unk18;         // +0x18
        int32_t maxProgress;    // +0x1C
        float cooldown;         // +0x20
        uint32_t unk24;         // +0x24
    };
    static_assert(sizeof(GuardianSpiritProgressContext) == 0x28);
    static_assert(offsetof(GuardianSpiritProgressContext, currentProgress) == 0x14);
    static_assert(offsetof(GuardianSpiritProgressContext, maxProgress) == 0x1C);
    struct PlayerData {
        uint8_t unk00[0x38];
        HealthContext healthContext;              // +0x38
        uint8_t unk78[0x140 - 0x78];
        GuardianSpiritProgressContext guardianSpiritCtx; // +0x140
        uint8_t unk168[0x570 - 0x168];
        InventoryItemData samuraiEquipments[17];  // +0x570
        uint64_t unk14D8;                         // +0x14D8
        InventoryItemData ninjaEquipments[17];    // +0x14E0
        uint64_t unk2448;                         // +0x2448
        int32_t activeSlotIndex;                  // +0x2450 // 0 = samurai, 1 = ninja

        uint64_t GetCurrentHealth() const { return healthContext.currentHealth; }
        uint64_t GetMaxHealthRaw() const { return healthContext.maxHealthRaw; }
        float GetHealthScale() const { return healthContext.healthScale; }
        float GetCurrentGuardianSpiritProgress() const { return guardianSpiritCtx.currentProgress; }
        int32_t GetMaxGuardianSpiritProgress() const { return guardianSpiritCtx.maxProgress; }
        float GetGuardianSpiritProgressRatio() const {
            return guardianSpiritCtx.maxProgress > 0
                ? guardianSpiritCtx.currentProgress / static_cast<float>(guardianSpiritCtx.maxProgress)
                : 0.0f;
        }
    };
    static_assert(offsetof(PlayerData, healthContext) == 0x38);
    static_assert(offsetof(PlayerData, healthContext) == 0x38);
    static_assert(offsetof(PlayerData, guardianSpiritCtx) == 0x140);
    static_assert(offsetof(PlayerData, samuraiEquipments) == 0x570);
    static_assert(offsetof(PlayerData, activeSlotIndex) == 0x2450);
    static_assert(offsetof(PlayerData, unk14D8) == 0x14D8);
    static_assert(offsetof(PlayerData, unk2448) == 0x2448);

    struct PlayerManager {
        uint64_t unk00[0x3A0 >> 3];
        PlayerData* playerData;  // +0x3A0
    };
    static_assert(offsetof(PlayerManager, playerData) == 0x3A0);

    uint64_t unk00[0x1328 >> 3];

    struct PlayerManagerWrapper {
        PlayerManager* playerManager;
        uint64_t unk08;
        uint64_t unk10;
    };
    PlayerManagerWrapper players[4];  // +0x1328

    static PlayerManager* GetPlayerManager(int32_t playerIndex = 0);
    static PlayerData* GetPlayerData(int32_t playerIndex = 0);
    static uint64_t GetCurrentHealth(int32_t playerIndex = 0);
    static uint64_t GetMaxHealthRaw(int32_t playerIndex = 0);
    static float GetHealthScale(int32_t playerIndex = 0);
    static float GetCurrentGuardianSpiritProgress(int32_t playerIndex = 0);
    static int32_t GetMaxGuardianSpiritProgress(int32_t playerIndex = 0);
    static float GetGuardianSpiritProgressRatio(int32_t playerIndex = 0);
    static void* GetEquipmentSlotBase(int32_t slotIndex, int32_t isNinja);
    static InventoryItemData* GetEquipmentItemFromSlot(int32_t slotIndex, int32_t isNinja);
    static int32_t GetActiveSetIndex();

    // GetNthPlayer_1401469B8
};
static_assert(offsetof(GameStateManager, players) == 0x1328);

inline REL::Relocation<GameStateManager**> g_gameState(REL::Offset(0x4532A58));

// --- Mission ---

struct MissionData {
    uint64_t unk00[0x10 >> 3];
    uint8_t missionMode;          // +0x10: 2=hard, 3=very hard, 4=nightmare
    uint8_t pad11[0x2D - 0x11];
    uint8_t maxLevel;             // +0x2D: max level area GetMaxLevel_140322FAC
    uint8_t pad2E[0x30 - 0x2E];
    uint32_t missionProgress[5];  // +0x30: by difficulty (0=easy, 1=normal, 2=hard, 3=very hard, 4=nightmare)
};

struct MissionManager {
    uint64_t unk00;
    MissionData* missionData;  // +0x08

    uint8_t GetMissionMode() const { return missionData->missionMode; }
    uint32_t GetMissionProgress(int32_t a_difficulty) const { return missionData->missionProgress[a_difficulty]; }

    DEF_MEMBER_FN_CONST(GetDifficultyMode, int32_t, 0x0142768);
};

inline REL::Relocation<MissionManager**> g_missionManager(REL::Offset(0x438DDC0));

using FnGetDifficultyMode = int32_t (*)(MissionManager* a_missionManager);
inline REL::Relocation<FnGetDifficultyMode> GetDifficultyMode(REL::Offset(0x0142768));

using FnGetLocalizedString = wchar_t* (*)(uint32_t a_key);
inline REL::Relocation<FnGetLocalizedString> GetLocalizedString(REL::Offset(0x02D8F1C));

using FnGetItemData = ItemData* (*)(void* a_resourceManager, uint16_t a_itemId);
inline REL::Relocation<FnGetItemData> GetItemData(REL::Offset(0x04A25F0));

using FnGetEnchantmentData = EnchantmentData* (*)(void* a_enchantmentManager, uint16_t a_enchantmentId);
inline REL::Relocation<FnGetEnchantmentData> GetEnchantmentData(REL::Offset(0x02E6B00));

using FnGetEnchantmentName = wchar_t* (*)(void* enchantmentData, wchar_t* a_name, int32_t a_nameBufLen);
inline REL::Relocation<FnGetEnchantmentName> GetEnchantmentName(REL::Offset(0x2091C44));

using FnGetEnchantmentDesc = void (*)(void* enchantmentData, wchar_t* a_name, int32_t a_nameBufLen);
inline REL::Relocation<FnGetEnchantmentDesc> GetEnchantmentDesc(REL::Offset(0x208E9E0));

using FnGetAffixByIndex = GearAffixData* (*)(void* a_gearAffixManager, uint32_t a_affixIndex);
inline REL::Relocation<FnGetAffixByIndex> GetAffixByIndex(REL::Offset(0x02E6C74));

using FnGetRarityName = wchar_t* (*)(char a_rarity);
inline REL::Relocation<FnGetRarityName> GetRarityName(REL::Offset(0x02BC930));

using FnGetGearAffixData = GearAffixData* (*)(void* a_gearAffixManager, uint32_t a_gearAffixId);
inline REL::Relocation<FnGetGearAffixData> GetGearAffixData(REL::Offset(0x02E5F08));

using FnGetLootTableData = LootTableEntry* (*)(void* a_lootTableData, uint32_t a_lootTableId);
inline REL::Relocation<FnGetLootTableData> GetLootTableData(REL::Offset(0x0804478));

using FnGetEnchantRuleData = EnchantRuleData* (*)(void* a_enchantRuleManager, uint16_t a_ruleId);
inline REL::Relocation<FnGetEnchantRuleData> GetEnchantRuleData(REL::Offset(0x047BE60));

using FnDispatchGameEvent = void (*)(void* a_gameEventManager, uint32_t a_eventId, uint32_t a_eventData);
inline REL::Relocation<FnDispatchGameEvent> DispatchGameEvent(REL::Offset(0x0821E4));

using FnSetEquippedItem = void (*)(void* a_playerEquipmentManager, uint32_t a_slotIndex, InventoryItemData* a_inventoryItemData, uint32_t a_isNinja);
inline REL::Relocation<FnSetEquippedItem> SetEquippedItem(REL::Offset(0x047D974));

inline REL::Relocation<void**> g_inventoryManager(REL::Offset(0x438DE20));

using FnGetEquippedItem = InventoryItemData* (*)(void* a_inventoryManager, uint32_t a_inventoryIndex, uint32_t a_isNinja);
inline REL::Relocation<FnGetEquippedItem> GetEquippedItem(REL::Offset(0x047DA94));

#pragma pack(pop)
