#pragma once

#include "art_definition.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace SpellHotbar {

constexpr std::uint32_t vanilla_firebolt_local_form = 0x12FD0;
constexpr const char* vanilla_firebolt_plugin = "Skyrim.esm";
constexpr const char* custom_ability_sidecar_filename = "ability.ini";
// When false, clips keep author CAST/SoundPlay; stamp/inject are no-ops.
inline constexpr bool custom_ability_spell_assignment_enabled = false;

struct CustomAbilitySidecar {
	std::string name;
	std::string icon;
	std::uint32_t icon_form{ 0 };
	std::uint32_t spell_local_form{ vanilla_firebolt_local_form };
	std::string spell_plugin{ vanilla_firebolt_plugin };
	bool self_target{ false };
	ArtClass art_class{ ArtClass::Generic };
	float stamina_cost{ 25.0f };
	float magicka_cost{ 0.0f };
	float health_cost{ 0.0f };
	std::string cooldown{ "8s" };
	float gcd{ 1.0f };
	float damage_mult{ 1.0f };
};

struct ClipAnnotation {
	float time{ 0.0f };
	std::string text;
};

enum class AbilitySpellFormKind {
	Spell,
	Scroll,
	Alchemy,
	Shout,
	Other
};

enum class AbilitySpellCasting {
	FireAndForget,
	Concentration,
	ConstantEffect,
	Other
};

enum class ArtMeter {
	None,
	Stamina,
	Magicka,
	Health
};

[[nodiscard]] CustomAbilitySidecar default_custom_ability_sidecar(int folder_number);

[[nodiscard]] CustomAbilitySidecar parse_custom_ability_sidecar(std::string_view text, int folder_number);

[[nodiscard]] std::string serialize_custom_ability_sidecar(const CustomAbilitySidecar& sidecar);

void apply_custom_ability_sidecar(ArtDefinition& art, const CustomAbilitySidecar& sidecar, int folder_number);

[[nodiscard]] CustomAbilitySidecar sidecar_from_art(const ArtDefinition& art, int folder_number);

[[nodiscard]] std::string custom_ability_pi_name(int folder_number);

[[nodiscard]] std::string custom_ability_pie_annotation(int folder_number);

[[nodiscard]] std::string custom_ability_castspell_instruction(const CustomAbilitySidecar& sidecar);

[[nodiscard]] std::string custom_ability_pi_line(int folder_number, const CustomAbilitySidecar& sidecar);

[[nodiscard]] std::string custom_ability_pi_ini(const std::vector<std::pair<int, CustomAbilitySidecar>>& entries);

[[nodiscard]] bool is_assignable_custom_ability_spell(
	AbilitySpellFormKind form_kind, AbilitySpellCasting casting) noexcept;

[[nodiscard]] ArtMeter unaffordable_art_meter(float need_stamina, float need_magicka, float need_health,
	float have_stamina, float have_magicka, float have_health) noexcept;

[[nodiscard]] bool clip_has_custom_ability_pie(const std::vector<ClipAnnotation>& annotations, int folder_number);

[[nodiscard]] bool is_author_spell_cast_annotation(std::string_view text) noexcept;

[[nodiscard]] bool is_sound_play_annotation(std::string_view text) noexcept;

[[nodiscard]] bool annotations_share_time(float a, float b) noexcept;

[[nodiscard]] std::string custom_ability_spell_sound_annotation(std::string_view sound_editor_id);

[[nodiscard]] std::optional<float> custom_ability_hit_frame_time(const std::vector<ClipAnnotation>& annotations);

[[nodiscard]] float custom_ability_pie_stamp_time(
	const std::vector<ClipAnnotation>& annotations, float duration) noexcept;

[[nodiscard]] std::string ensure_custom_ability_pie_in_annotation_txt(
	std::string_view txt, int folder_number, std::string_view release_sound_editor_id = {});

}  // namespace SpellHotbar
