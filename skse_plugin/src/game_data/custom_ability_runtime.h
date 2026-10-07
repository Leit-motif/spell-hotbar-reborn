#pragma once

#include "art_definition.h"

#include <cstdint>
#include <string>
#include <vector>

namespace RE {
	class TESForm;
	class SpellItem;
	class hkaAnimation;
}

namespace SpellHotbar {

[[nodiscard]] bool is_assignable_custom_ability_spell(const RE::TESForm* form);

void assign_custom_ability_spell(ArtDefinition& art, RE::SpellItem* spell);

[[nodiscard]] std::string custom_ability_spell_label(const ArtDefinition& art);

[[nodiscard]] std::vector<RE::SpellItem*> list_known_custom_ability_spells();

bool persist_custom_ability(ArtDefinition& art);

bool persist_ability(ArtDefinition& art);

void emit_custom_ability_pi_config();

void inject_custom_ability_pie(RE::hkaAnimation* animation, const ArtDefinition& art);

void stamp_custom_ability_clip(const ArtDefinition& art);

}  // namespace SpellHotbar
