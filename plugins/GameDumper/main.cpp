#include <mutex>
#define NOMINMAX
#include <Windows.h>
#include <cstdint>
#include <PluginAPI.h>
#include <LogUtils.h>
#include <FileUtils.h>
#include <HookUtils.h>
#include <GameType.h>
#include <CommonUtils.h>

#define PLUGIN_NAME "GameDumper"
#define PLUGIN_VERSION_MAJOR 1
#define PLUGIN_VERSION_MINOR 0
#define PLUGIN_VERSION_PATCH 0

namespace {



    // 语义：近战武器 group -> type
    // 对应 IDA: sub_142393D40
    int32_t GetMeleeWeaponDisplayType(int weaponGroup) {
        switch (weaponGroup) {
            case 6409:  return 0;
            case 24575: return 1;
            case 28275: return 2;
            case 21589: return 3;
            case 20629: return 4;
            case 7191:  return 5;
            case 11583: return 6;
            case 29361: return 7;
            case 24091: return 8;
            case 636:   return 9;
            case 3375:  return 10;
            case 6102:  return 11;
            case 1254:  return 12;
            case 9554:  return 13;
            default:    return -1;
        }
    }
    
    // 语义：远程武器 group -> type
    // 对应 IDA: sub_142393E0C
    int32_t GetRangedWeaponDisplayType(int rangedGroup) {
        switch (rangedGroup) {
            case 59886: return 14;
            case 49224: return 15;
            case 51013: return 16;
            default:    return -1;
        }
    }
    
    // 语义：防具 group -> type
    // 这是 ConvertSlotTypeToArmorType_1423939DC 的逆向映射
    int32_t GetArmorDisplayType(int armorGroup) {
        switch (armorGroup) {
            case 3577:  return 17; // head
            case 11055: return 18; // chest
            case 1975:  return 19; // arms
            case 16443: return 20; // knee/waist
            case 2473:  return 21; // legs
            default:    return -1;
        }
    }

    // 可选：统一入口（按 item category 取 type）
    int32_t GetItemDisplayType(const ItemData* item) {
        if (!item) return -1;
        switch (item->category) {
            case ITEM_CATEGORY_WEAPON: return GetMeleeWeaponDisplayType(static_cast<int>(item->weaponType));
            case ITEM_CATEGORY_GUN: return GetRangedWeaponDisplayType(static_cast<int>(item->gunType));
            case ITEM_CATEGORY_ARMOR: return GetArmorDisplayType(static_cast<int>(item->armorType));
            default: return -1;
        }
    }

    typedef struct ResourceKey {
        uint32_t id;         // +0
        uint32_t subId;      // +4
        uint64_t packedTag;  // +8
        uint32_t extra;      // +16
    } ResourceKey;
    static_assert(sizeof(ResourceKey) == 0x18);


	void DumpItemData(uint16_t a_itemId) {
		auto *itemData = (*g_resManager)->itemData->GetItemData(a_itemId);
		if (!itemData) {
			return;
		}
		if (itemData->category != ITEM_CATEGORY_ARMOR && itemData->category != ITEM_CATEGORY_WEAPON && itemData->category != ITEM_CATEGORY_GUN) {
			return;
		}

		// 辅助函数：根据affixId获取词条名称
		// auto getAffixName = [](uint16_t affixId) -> std::string {
		// 	if (affixId == 0) {
		// 		return "";
		// 	}
			
		// 	auto* gearAffixData = GetGearAffixData((*g_resManager)->gearAffixData, affixId);
		// 	if (!gearAffixData) {
		// 		return "Unknown";
		// 	}

			
		// 	auto* enchantmentData = GetEnchantmentData((*g_resManager)->enchantmentData, gearAffixData->enchantId);
		// 	if (!enchantmentData) {
		// 		return "Unknown";
		// 	}
			
		// 	wchar_t name[256] = {0};
		// 	GetEnchantmentName(enchantmentData, name, 256);
		// 	return CommonUtils::ConvertWStringToCString(std::wstring_view(name)) + "(" + std::to_string(affixId) + ")";
		// };

		// ID,LV,CAT,ARMA,WEAP,NAME,AFFIX1,AFFIX2,AFFIX3

		std::string itemName = itemData->GetDisplayName();
		if (!itemName.empty()) {
			// 获取词条信息
			// std::string fixedAffixName = getAffixName(itemData->fixedAffixId);
			// std::string affixGroup0Name = getAffixName(itemData->affixIdGroup[0]);
			// std::string affixGroup1Name = getAffixName(itemData->affixIdGroup[1]);

            ObjectIndexData *objectIndexData = nullptr;
            uint32_t payload[3] = {0};
            payload[0] = itemData->modelId;
            uint32_t finalId = 0;
            if (itemData->modelId != 0) {
                objectIndexData = (*g_resManager)->objectIndexData->GetObjectIndexData(itemData->modelId);
                if (objectIndexData) {
                    payload[1] = objectIndexData->typeIndex;
                    payload[2] = objectIndexData->itemIndex;

                    // a_param1->id = objectIndexData->typeIndex;
                    // a_param1->subId = objectIndexData->itemIndex;
                    // _MESSAGE("id: %08X, subId: %08X", a_param1->id, a_param1->subId);
                }
            }

            // std::byte* gameObjectManagers[4] = {nullptr};

            // using FnGetGameObjectManager = void* (*)(void* a_gameObjectManager, ResourceKey* a_index);
            // REL::Relocation<FnGetGameObjectManager> GetGameObjectManager(REL::Offset(0x0621190));
            // GetGameObjectManager(gameObjectManagers, a_param1);


            // auto *gameObjectManager = gameObjectManagers[0];
            // _MESSAGE("gameObjectManager: %p", gameObjectManager);

            // // v17 = GetObjectIndex_1406209F0(*(__int64 **)(v11 + 0x3A0), v23, v22);
            // using FnGetObjectIndex = int32_t (*)(void* a_gameObjectManager, uint32_t a_typeIndex, uint32_t a_itemIndex);
            // REL::Relocation<FnGetObjectIndex> GetObjectIndex(REL::Offset(0x06209F0));

            // auto objectIndex = GetObjectIndex((*g_resManager)->gameObjects, payload[1], payload[2]);
            // _MESSAGE("objectIndex: %08X", objectIndex);

            // auto *unk96 = *(std::byte**)(gameObjectManagers + 96);
            // if (unk96) {
            //     auto *unk40 = *(std::byte**)(unk96 + 40);
            //     if (unk40) {
            //         // GetObjectByTypeWithIndex_1407EED00
            //         using FnGetObjectByTypeWithIndex = uint32_t (*)(void*, uint32_t a_index, uint32_t);
            //         REL::Relocation<FnGetObjectByTypeWithIndex> GetObjectByTypeWithIndex(REL::Offset(0x07EED00));
            //         finalId = GetObjectByTypeWithIndex(unk40, objectIndex, 0);
            //         _MESSAGE("finalId: %08X", finalId);
            //     }
            // }


            
			
			// CSV格式输出：addr,itemId,isSamurai,isNinja,rarity,category,armorType,weaponType,itemName,rarityName,fixedAffixId,fixedAffixName,affixGroup0,affixGroup0Name,affixGroup1,affixGroup1Name
			_MESSAGE("%p,%u,%s,%s,%s,%08X,%p", 
				itemData,
				static_cast<uint32_t>(a_itemId), 
				itemData->IsSamuraiItem() ? "Samurai" : "",
				itemData->IsNinjaItem() ? "Ninja" : "",
				itemName.c_str(),
                itemData->modelId,
                objectIndexData
			);
			// CommonUtils::DumpClass(itemData, 0x80 / 8);
			// _MESSAGE("--------------------------------");
		}
	}

	// void DumpAllEnchantmentData() {
	// 	auto *gearAffixManager = (GearAffixManager*)(*g_resManager)->gearAffixData;
	// 	// 输出CSV表头
	// 	_MESSAGE("affixId,enchantId,enchantName,ruleId,categoryName,descName");
	// 	for (uint32_t i = 0; i < gearAffixManager->dataArray->dataCount; i++) {
	// 		const auto* gearAffixData = GetAffixByIndex(gearAffixManager, i);
	// 		if (gearAffixData) {
	// 			gearAffixData->Dump();
	// 		}
	// 	}
	// }

	void DumpAllItemData() {

		for (uint16_t i = 0; i < 0xFFFF; i++) {
			DumpItemData(i);
		}
	}
    /*
    __int64 __fastcall PlayerAppearance_EnqueueModelLoadByDesc(
        __int64 a1,
        __int64 a2,
        __int64 a3,
        unsigned int *a4,
        __int64 a5)
    */
    void HookPlayerAppearance_EnqueueModelLoadByDesc() {
        using FnPlayerAppearance_EnqueueModelLoadByDesc = void* (*)(void*, void*, void*, ResourceKey*, void*);
        REL::Relocation<FnPlayerAppearance_EnqueueModelLoadByDesc> PlayerAppearance_EnqueueModelLoadByDesc(REL::Offset(0x06C56F0));
        HookLambda(PlayerAppearance_EnqueueModelLoadByDesc.get(), [](void* a_this, void* a_param2, void* a_param3, ResourceKey* a_param4, void* a_param5)->void* {


            // static std::once_flag onceFlag;
            // std::call_once(onceFlag, [&]() {
            //     auto oldTypeIndex = a_param4->id;
            //     auto oldItemIndex = a_param4->subId;
            //     DumpAllItemData(a_param4);

            //     a_param4->id = oldTypeIndex;
            //     a_param4->subId = oldItemIndex;
            // });

            _MESSAGE("--------------------------------");

            // print a_param4
            _MESSAGE("id: %08X, subId: %08X, packedTag: %016llX, extra: %08X", a_param4->id, a_param4->subId, a_param4->packedTag, a_param4->extra);

            auto *objectIndexData = (*g_resManager)->objectIndexData->GetObjectIndexData(a_param4->id);
            if (objectIndexData) {
                _MESSAGE("objectIndexData: %p, type: %d, typeIndex: %08X, itemIndex: %08X", objectIndexData, objectIndexData->type, objectIndexData->typeIndex, objectIndexData->itemIndex);

                std::byte* gameObjectManagers[4] = {nullptr};

                using FnGetGameObjectManager = void* (*)(void* a_gameObjectManager, ResourceKey* a_index);
                REL::Relocation<FnGetGameObjectManager> GetGameObjectManager(REL::Offset(0x0621190));
                GetGameObjectManager(gameObjectManagers, a_param4);

                auto *gameObjectManager = gameObjectManagers[0];
                _MESSAGE("gameObjectManager: %p", gameObjectManager);

                auto state = *(uint16_t*)((std::byte*)a_this + 8);
                _MESSAGE("state: %d", state);

                if (gameObjectManager) {
                    auto *unk96 = *(std::byte**)(gameObjectManager + 96);
                    if (unk96) {
                        auto *unk40 = *(std::byte**)(unk96 + 40);
                        if (unk40) {
                            _MESSAGE("unk40: %p", unk40);
                        }
                    }
                }

                using FnGetObjectIndex = int32_t (*)(void* a_gameObjectManager, uint32_t a_typeIndex, uint32_t a_itemIndex);
                REL::Relocation<FnGetObjectIndex> GetObjectIndex(REL::Offset(0x06209F0));
    
                auto objectIndex = GetObjectIndex((*g_resManager)->gameObjects, objectIndexData->typeIndex, objectIndexData->itemIndex);
                _MESSAGE("objectIndex: %08X", objectIndex);

            }
            return original(a_this, a_param2, a_param3, a_param4, a_param5);
        });
    }   
	/*
	using FnGetLocalizedString = wchar_t* (*)(uint32_t a_key);
inline RelocAddr<FnGetLocalizedString> GetLocalizedString(0x02D8F1C); // 1402D8F1C
	*/
	void HookGetLocalizedString() {
		HookLambda(GetLocalizedString.get(), [](uint32_t a_key)->wchar_t* {
			auto retVal = original(a_key);
			if (retVal) {
				_MESSAGE("%p: %08X -> %s", GetLocalizedString.address(), a_key, CommonUtils::ConvertWStringToCString(std::wstring_view(retVal)).c_str());
			}
			return retVal;
		});
	}

	/*
	using FnDispatchGameEvent = void(*)(void* a_gameEventManager, uint32_t a_eventId, uint32_t a_eventData);
inline RelocAddr<FnDispatchGameEvent> DispatchGameEvent(0x0821E4); // 1400821E4
	*/
	bool HookDispatchGameEvent() {
		HookLambda(DispatchGameEvent.get(), [](void* a_gameEventManager, uint32_t a_eventId, uint32_t a_eventData)->void {
			original(a_gameEventManager, a_eventId, a_eventData);

			{
				// auto case10Name = CommonUtils::ConvertWStringToCString(GetLocalizedString(16817079));
				// auto case12Name = CommonUtils::ConvertWStringToCString(GetLocalizedString(17814193));
				// auto case16Name = CommonUtils::ConvertWStringToCString(GetLocalizedString(17094961));
				// auto case13Name = CommonUtils::ConvertWStringToCString(GetLocalizedString(17814193));
				// auto case11Name = CommonUtils::ConvertWStringToCString(GetLocalizedString(17966036));
				// auto case14Name = CommonUtils::ConvertWStringToCString(GetLocalizedString(18649201));
				// _MESSAGE("case10Name: %s", case10Name.c_str());
				// _MESSAGE("case12Name: %s", case12Name.c_str());
				// _MESSAGE("case16Name: %s", case16Name.c_str());
				// _MESSAGE("case13Name: %s", case13Name.c_str());
				// _MESSAGE("case11Name: %s", case11Name.c_str());
				// _MESSAGE("case14Name: %s", case14Name.c_str());
				// 10 -> 秘传书: {}
				// 12 -> 指南书: {}
				// 13 -> 指南书: {}
				// 16 -> 武艺指南：{}
				// 11 -> 工法书
				// 14 -> 制作指南
			}
			// _MESSAGE("-------DispatchGameEvent-------");
			// _MESSAGE("eventId: %d", a_eventId);
			// _MESSAGE("eventData: %d", a_eventData);
			// 285 
			constexpr uint32_t EVENT_ID_OPEN_EQUIPMENT_MENU = 285;
			if (a_eventId == EVENT_ID_OPEN_EQUIPMENT_MENU) {
				_MESSAGE("-------Open Equipment Menu-------");
				// for (uint32_t characterIndex = 0; characterIndex < 2; characterIndex++) {
				// 	for (uint32_t i = 0; i < 17; i++) {
				// 		auto *equippedItem = GetEquippedItem(*g_inventoryManager, i, characterIndex);
				// 		if (equippedItem) {
				// 			_MESSAGE("equippedItem: %p, slot: %d, characterIndex: %d", equippedItem, i, characterIndex);
				// 			DumpItemData(equippedItem->itemId);
				// 			// mission mode
				// 			auto *missionManager = *g_missionManager;
				// 			if (missionManager) {
				// 				auto difficultyMode = GetDifficultyMode(missionManager);
				// 				_MESSAGE("difficulty mode: %d", difficultyMode);
				// 				_MESSAGE("mission progress: %d", missionManager->GetMissionProgress(difficultyMode));
				// 			}

				// 			auto *equipmentItem = GameStateManager::GetEquipmentItemFromSlot(i, characterIndex);
				// 			if (equipmentItem) {
				// 				_MESSAGE("equipmentItem: %p", equipmentItem);
				// 				DumpItemData(equipmentItem->itemId);
				// 				_MESSAGE("--------------------------------");
				// 				equipmentItem->Dump();
				// 			}
				// 		}
				// 	}
				// }
				// DumpAllEnchantmentData();
				// _MESSAGE("--------------------------------");
				DumpAllItemData();
				// _MESSAGE("--------------------------------");
				// _MESSAGE("--------------------------------");
				// (*g_resManager)->SoulCoreData->Dump();

				// cmake --build build --config Release
			}
		});
		return true;
	}



}

extern "C" __declspec(dllexport) bool nioh3_plugin_initialize(const Nioh3PluginInitializeParam * param) {
    _MESSAGE("Plugin initialized");
    _MESSAGE("Game version: %s", param->game_version_string);
    _MESSAGE("Plugin dir: %s", param->plugins_dir);

    // RELOC_MEMBER_FN(ItemDataManager, GetItemData, "E8 ? ? ? ? 45 33 C0 48 85 C0 74 ? 48 8B 87", 0, 1, 5);
    // RELOC_GLOBAL_VAL(GetLocalizedString, "E8 ? ? ? ? 33 F6 48 C7 45 ? ? ? ? ? 48 8D 1D", 0, 1, 5);
    // RELOC_GLOBAL_VAL(g_resManager, "48 8B 05 ? ? ? ? 41 8B D7 48 8B 98", 0, 3, 7);

    HookDispatchGameEvent();
    HookPlayerAppearance_EnqueueModelLoadByDesc();
    return true;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        _MESSAGE("Initializing plugin: %s, version: %d.%d.%d", PLUGIN_NAME, PLUGIN_VERSION_MAJOR, PLUGIN_VERSION_MINOR, PLUGIN_VERSION_PATCH);
    }
    return TRUE;
}
