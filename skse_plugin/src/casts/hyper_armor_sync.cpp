#include "hyper_armor_sync.h"

#include <atomic>
#include <filesystem>

#include "art_driver.h"
#include "hyper_armor.h"
#include "../game_data/game_data.h"
#include "../logger/logger.h"

namespace SpellHotbar::casts::HyperArmor {

	namespace {
		RE::BGSPerk* bound_perk{ nullptr };
		// The perk entries' `BGSEntryPointFunctionDataOneValue::data`. Written on the main thread only.
		float* incoming_damage_value{ nullptr };
		float* incoming_stagger_value{ nullptr };
		// Staggers dropped during the live Ability. The first one logs; `sync` reports the total
		// when the Ability ends, so a sender that retries every frame costs two lines, not sixty a
		// second.
		std::atomic<int> swallowed_this_ability{ 0 };
		bool was_live{ false };

		// The Mod Incoming <x> multiply entry on `perk`, or null when the perk lacks it.
		float* find_multiply_value(RE::BGSPerk* perk, RE::BGSEntryPoint::ENTRY_POINT entry_point)
		{
			for (auto* entry : perk->perkEntries) {
				if (entry == nullptr || entry->GetType() != RE::PERK_ENTRY_TYPE::kEntryPoint) {
					continue;
				}
				auto* ep = static_cast<RE::BGSEntryPointPerkEntry*>(entry);
				if (ep->entryData.entryPoint != entry_point ||
					ep->entryData.function != RE::BGSEntryPointFunction::ENTRY_POINT_FUNCTION::kMultiplyValue) {
					continue;
				}
				auto* data = ep->functionData;
				if (data != nullptr && data->GetType() == RE::BGSEntryPointFunctionData::ENTRY_POINT_FUNCTION_DATA::kOneValue) {
					return &static_cast<RE::BGSEntryPointFunctionDataOneValue*>(data)->data;
				}
			}
			return nullptr;
		}

		// "SkyrimSE.exe+0x6A1B2C" for a code address, so a swallowed stagger names its sender.
		std::string describe_caller(const void* address)
		{
			HMODULE module{ nullptr };
			if (address == nullptr ||
				!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					static_cast<LPCWSTR>(address), &module)) {
				return std::format("{}", address);
			}
			wchar_t path[MAX_PATH]{};
			GetModuleFileNameW(module, path, MAX_PATH);
			const auto offset = reinterpret_cast<std::uintptr_t>(address) - reinterpret_cast<std::uintptr_t>(module);
			return std::format("{}+0x{:X}", std::filesystem::path(path).filename().string(), offset);
		}
	}

	void bind(RE::BGSPerk* perk)
	{
		bound_perk = perk;
		incoming_damage_value = nullptr;
		incoming_stagger_value = nullptr;
		if (perk == nullptr) {
			logger::error("SH2 hyperarmor: SpellHotbar_AbilityHyperArmorPerk is missing; Ability damage reduction and stagger immunity are off");
			return;
		}
		incoming_damage_value = find_multiply_value(perk, RE::BGSEntryPoint::ENTRY_POINT::kModIncomingDamage);
		incoming_stagger_value = find_multiply_value(perk, RE::BGSEntryPoint::ENTRY_POINT::kModIncomingStagger);
		if (incoming_damage_value == nullptr) {
			logger::error("SH2 hyperarmor: {:08X} has no Mod Incoming Damage multiply entry; Ability damage reduction is off",
				perk->GetFormID());
		}
		if (incoming_stagger_value == nullptr) {
			logger::error("SH2 hyperarmor: {:08X} has no Mod Incoming Stagger multiply entry; stagger immunity relies on the graph hook alone",
				perk->GetFormID());
		}
		logger::info("SH2 hyperarmor: bound {:08X} (Mod Incoming Damage: {}, Mod Incoming Stagger: {})",
			perk->GetFormID(), incoming_damage_value != nullptr, incoming_stagger_value != nullptr);
	}

	void grant(RE::PlayerCharacter* player)
	{
		if (player == nullptr || bound_perk == nullptr || player->HasPerk(bound_perk)) {
			return;
		}
		player->AddPerk(bound_perk);
		logger::info("SH2 hyperarmor: granted the Ability hyperarmor perk to the player");
	}

	void sync()
	{
		const bool live = ArtDriver::is_active();
		if (was_live && !live) {
			const int swallowed = swallowed_this_ability.exchange(0, std::memory_order_relaxed);
			if (swallowed > 0) {
				logger::info("SH2 hyperarmor: {} stagger event(s) swallowed during that Ability", swallowed);
			}
		}
		was_live = live;
		if (incoming_damage_value != nullptr) {
			const float want = incoming_damage_multiplier(live, GameData::ability_damage_reduction);
			if (*incoming_damage_value != want) {
				*incoming_damage_value = want;
				logger::info("SH2 hyperarmor: incoming weapon damage x{:.2f} ({})", want, live ? "Ability live" : "idle");
			}
		}
		if (incoming_stagger_value != nullptr) {
			const float want = incoming_stagger_multiplier(GameData::ability_stagger_immunity, live);
			if (*incoming_stagger_value != want) {
				*incoming_stagger_value = want;
				logger::info("SH2 hyperarmor: incoming stagger x{:.0f}", want);
			}
		}
	}

	bool try_swallow_stagger(std::string_view event, const void* caller)
	{
		if (!hyper_armor_swallows(GameData::ability_stagger_immunity, ArtDriver::is_active(), event)) {
			return false;
		}
		if (swallowed_this_ability.fetch_add(1, std::memory_order_relaxed) == 0) {
			logger::info("SH2 hyperarmor: swallowed {} during an Ability (sent from {})", event, describe_caller(caller));
		}
		return true;
	}
}
