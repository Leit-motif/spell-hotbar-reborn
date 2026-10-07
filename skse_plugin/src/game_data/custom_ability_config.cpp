#include "custom_ability_config.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace SpellHotbar {

namespace {

std::string trim(std::string_view s)
{
	const auto start = s.find_first_not_of(" \t\r");
	if (start == std::string_view::npos) {
		return {};
	}
	const auto end = s.find_last_not_of(" \t\r");
	return std::string{ s.substr(start, end - start + 1) };
}

std::optional<std::uint32_t> parse_hex_u32(std::string_view text)
{
	std::string s = trim(text);
	if (s.size() >= 2 && (s[0] == '0') && (s[1] == 'x' || s[1] == 'X')) {
		s.erase(0, 2);
	}
	if (s.empty()) {
		return std::nullopt;
	}
	std::uint32_t value = 0;
	const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value, 16);
	if (ec != std::errc{} || ptr != s.data() + s.size()) {
		return std::nullopt;
	}
	return value;
}

std::optional<float> parse_float(std::string_view text)
{
	std::string s = trim(text);
	if (s.empty()) {
		return std::nullopt;
	}
	std::replace(s.begin(), s.end(), ',', '.');
	try {
		size_t idx = 0;
		const float value = std::stof(s, &idx);
		if (idx != s.size()) {
			return std::nullopt;
		}
		return value;
	} catch (const std::exception&) {
		return std::nullopt;
	}
}

bool parse_bool01(std::string_view text)
{
	const auto s = trim(text);
	return s == "1" || s == "true" || s == "True";
}

std::string event_name(std::string_view text)
{
	const auto dot = text.find('.');
	if (dot == std::string_view::npos) {
		return std::string{ text };
	}
	return std::string{ text.substr(0, dot) };
}

}  // namespace

CustomAbilitySidecar default_custom_ability_sidecar(int folder_number)
{
	CustomAbilitySidecar sidecar;
	sidecar.name = "Custom Ability " + std::to_string(folder_number);
	sidecar.icon = "GREATER_POWER";
	return sidecar;
}

CustomAbilitySidecar parse_custom_ability_sidecar(std::string_view text, int folder_number)
{
	auto sidecar = default_custom_ability_sidecar(folder_number);
	std::istringstream stream{ std::string{ text } };
	std::string line;
	while (std::getline(stream, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		const auto eq = line.find('=');
		if (eq == std::string::npos) {
			continue;
		}
		const auto key = trim(line.substr(0, eq));
		const auto value = trim(line.substr(eq + 1));
		if (key == "name" && !value.empty()) {
			sidecar.name = value;
		} else if (key == "icon" && !value.empty()) {
			sidecar.icon = value;
		} else if (key == "icon_form") {
			if (const auto parsed = parse_hex_u32(value)) {
				sidecar.icon_form = *parsed;
			} else if (const auto dec = parse_float(value)) {
				sidecar.icon_form = static_cast<std::uint32_t>(*dec);
			}
		} else if (key == "spell_form") {
			if (const auto parsed = parse_hex_u32(value)) {
				sidecar.spell_local_form = *parsed;
			}
		} else if (key == "spell_plugin" && !value.empty()) {
			sidecar.spell_plugin = value;
		} else if (key == "self_target") {
			sidecar.self_target = parse_bool01(value);
		} else if (key == "art_class") {
			sidecar.art_class = parse_art_class(value);
		} else if (key == "stamina") {
			sidecar.stamina_cost = parse_float(value).value_or(sidecar.stamina_cost);
		} else if (key == "magicka") {
			sidecar.magicka_cost = parse_float(value).value_or(sidecar.magicka_cost);
		} else if (key == "health") {
			sidecar.health_cost = parse_float(value).value_or(sidecar.health_cost);
		} else if (key == "cooldown" && !value.empty()) {
			sidecar.cooldown = value;
		} else if (key == "gcd") {
			sidecar.gcd = parse_float(value).value_or(sidecar.gcd);
		} else if (key == "damage_mult") {
			sidecar.damage_mult = parse_float(value).value_or(sidecar.damage_mult);
		}
	}
	return sidecar;
}

std::string serialize_custom_ability_sidecar(const CustomAbilitySidecar& sidecar)
{
	return std::format(
		"name={}\n"
		"icon={}\n"
		"icon_form={:X}\n"
		"spell_form={:X}\n"
		"spell_plugin={}\n"
		"self_target={}\n"
		"art_class={}\n"
		"stamina={}\n"
		"magicka={}\n"
		"health={}\n"
		"cooldown={}\n"
		"gcd={}\n"
		"damage_mult={}\n",
		sidecar.name,
		sidecar.icon,
		sidecar.icon_form,
		sidecar.spell_local_form,
		sidecar.spell_plugin,
		sidecar.self_target ? 1 : 0,
		art_class_label(sidecar.art_class),
		sidecar.stamina_cost,
		sidecar.magicka_cost,
		sidecar.health_cost,
		sidecar.cooldown,
		sidecar.gcd,
		sidecar.damage_mult);
}

void apply_custom_ability_sidecar(ArtDefinition& art, const CustomAbilitySidecar& sidecar, int folder_number)
{
	art.display_name = sidecar.name.empty()
		? "Custom Ability " + std::to_string(folder_number)
		: sidecar.name;
	art.icon = sidecar.icon.empty() ? "GREATER_POWER" : sidecar.icon;
	art.icon_form = sidecar.icon_form;
	art.art_class = sidecar.art_class;
	art.stamina_cost = sidecar.stamina_cost;
	art.magicka_cost = sidecar.magicka_cost;
	art.health_cost = sidecar.health_cost;
	art.gcd = sidecar.gcd;
	art.damage_mult = sidecar.damage_mult;
	art.cooldown_text = sidecar.cooldown;
	if (const auto days = parse_art_duration_days(sidecar.cooldown)) {
		art.cooldown_days = *days;
	} else {
		art.cooldown_days = parse_art_duration_days("8s").value_or(-1.0f);
	}
	art.spell_local_form = sidecar.spell_local_form;
	art.spell_plugin = sidecar.spell_plugin;
	art.self_target = sidecar.self_target;
}

CustomAbilitySidecar sidecar_from_art(const ArtDefinition& art, int folder_number)
{
	auto sidecar = default_custom_ability_sidecar(folder_number);
	if (!art.display_name.empty()) {
		sidecar.name = art.display_name;
	}
	if (!art.icon.empty()) {
		sidecar.icon = art.icon;
	}
	sidecar.icon_form = art.icon_form;
	sidecar.art_class = art.art_class;
	sidecar.stamina_cost = art.stamina_cost;
	sidecar.magicka_cost = art.magicka_cost;
	sidecar.health_cost = art.health_cost;
	sidecar.gcd = art.gcd;
	sidecar.damage_mult = art.damage_mult;
	if (!art.cooldown_text.empty()) {
		sidecar.cooldown = art.cooldown_text;
	}
	if (art.spell_local_form != 0) {
		sidecar.spell_local_form = art.spell_local_form;
		sidecar.spell_plugin = art.spell_plugin.empty() ? vanilla_firebolt_plugin : art.spell_plugin;
	}
	sidecar.self_target = art.self_target;
	return sidecar;
}

std::string custom_ability_pi_name(int folder_number)
{
	return "custom_ability_" + std::to_string(folder_number);
}

std::string custom_ability_pie_annotation(int folder_number)
{
	return "PIE.$" + custom_ability_pi_name(folder_number);
}

std::string custom_ability_castspell_instruction(const CustomAbilitySidecar& sidecar)
{
	const auto plugin = sidecar.spell_plugin.empty() ? vanilla_firebolt_plugin : sidecar.spell_plugin;
	const auto form = sidecar.spell_local_form == 0 ? vanilla_firebolt_local_form : sidecar.spell_local_form;
	return std::format("@CASTSPELL|0x{:X}|{}|1|1|{}|0|0|0|0|0|0", form, plugin, sidecar.self_target ? 1 : 0);
}

std::string custom_ability_pi_line(int folder_number, const CustomAbilitySidecar& sidecar)
{
	return std::format("${} = {}", custom_ability_pi_name(folder_number),
		custom_ability_castspell_instruction(sidecar));
}

std::string custom_ability_pi_ini(const std::vector<std::pair<int, CustomAbilitySidecar>>& entries)
{
	std::string out = "[SpellHotbar2]\n";
	for (const auto& [number, sidecar] : entries) {
		out += custom_ability_pi_line(number, sidecar);
		out += '\n';
	}
	return out;
}

bool is_assignable_custom_ability_spell(AbilitySpellFormKind form_kind, AbilitySpellCasting casting) noexcept
{
	return form_kind == AbilitySpellFormKind::Spell && casting == AbilitySpellCasting::FireAndForget;
}

ArtMeter unaffordable_art_meter(float need_stamina, float need_magicka, float need_health,
	float have_stamina, float have_magicka, float have_health) noexcept
{
	if (need_stamina > 0.0f && have_stamina < need_stamina) {
		return ArtMeter::Stamina;
	}
	if (need_magicka > 0.0f && have_magicka < need_magicka) {
		return ArtMeter::Magicka;
	}
	if (need_health > 0.0f && have_health < need_health) {
		return ArtMeter::Health;
	}
	return ArtMeter::None;
}

bool annotation_is_custom_ability_pie(std::string_view text, int folder_number)
{
	const auto marker = custom_ability_pie_annotation(folder_number);
	return text == marker || text.starts_with(marker + ".");
}

bool clip_has_custom_ability_pie(const std::vector<ClipAnnotation>& annotations, int folder_number)
{
	for (const auto& annotation : annotations) {
		if (annotation_is_custom_ability_pie(annotation.text, folder_number)) {
			return true;
		}
	}
	return false;
}

bool is_author_spell_cast_annotation(std::string_view text) noexcept
{
	if (text.find("$custom_ability_") != std::string_view::npos) {
		return false;
	}
	std::string_view payload = text;
	if (payload.starts_with("PIE.")) {
		payload.remove_prefix(4);
	} else {
		const auto dot = payload.find('.');
		if (dot != std::string_view::npos) {
			const auto after = payload.substr(dot + 1);
			if (after.starts_with("PIE.")) {
				payload = after.substr(4);
			} else {
				payload = after;
			}
		}
	}
	return payload.starts_with("@CASTSPELL") || payload.starts_with("@CAST|");
}

bool is_sound_play_annotation(std::string_view text) noexcept
{
	return text.starts_with("SoundPlay.");
}

bool annotations_share_time(float a, float b) noexcept
{
	return std::fabs(a - b) <= 0.00051f;
}

std::string custom_ability_spell_sound_annotation(std::string_view sound_editor_id)
{
	if (sound_editor_id.empty()) {
		return {};
	}
	return std::string("SoundPlay.") + std::string{ sound_editor_id };
}

std::optional<std::string> keep_event_after_stripping_author_cast(std::string_view text)
{
	if (!is_author_spell_cast_annotation(text)) {
		return std::string{ text };
	}
	const auto dot = text.find('.');
	if (dot == std::string_view::npos) {
		return std::nullopt;
	}
	const auto event = text.substr(0, dot);
	if (event.empty() || event == "PIE") {
		return std::nullopt;
	}
	return std::string{ event };
}

std::optional<float> custom_ability_hit_frame_time(const std::vector<ClipAnnotation>& annotations)
{
	for (const auto& annotation : annotations) {
		if (event_name(annotation.text) == "HitFrame") {
			return annotation.time;
		}
	}
	return std::nullopt;
}

float custom_ability_pie_stamp_time(const std::vector<ClipAnnotation>& annotations, float duration) noexcept
{
	if (const auto hit = custom_ability_hit_frame_time(annotations)) {
		return *hit;
	}
	const float clamped = duration < 0.0f ? 0.0f : duration;
	return clamped * 0.05f;
}

namespace {

struct ParsedAnnoTxt {
	std::vector<std::string> header_lines;
	std::vector<ClipAnnotation> annotations;
	float duration{ 0.0f };
	int num_annotations_header_index{ -1 };
};

ParsedAnnoTxt parse_annotation_txt(std::string_view text)
{
	ParsedAnnoTxt parsed;
	std::istringstream stream{ std::string{ text } };
	std::string line;
	bool in_header = true;
	int index = 0;
	while (std::getline(stream, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		if (in_header && !line.empty() && line.front() == '#') {
			parsed.header_lines.push_back(line);
			if (line.find("duration:") != std::string::npos) {
				const auto colon = line.find(':');
				if (colon != std::string::npos) {
					parsed.duration = parse_float(line.substr(colon + 1)).value_or(0.0f);
				}
			}
			if (line.find("numAnnotations:") != std::string::npos) {
				parsed.num_annotations_header_index = static_cast<int>(parsed.header_lines.size() - 1);
			}
			++index;
			continue;
		}
		in_header = false;
		if (line.empty()) {
			continue;
		}
		std::istringstream row{ line };
		float time = 0.0f;
		row >> time;
		std::string rest;
		std::getline(row, rest);
		rest = trim(rest);
		parsed.annotations.push_back(ClipAnnotation{ .time = time, .text = rest });
	}
	return parsed;
}

std::string format_anno_time(float time)
{
	return std::format("{:.6f}", time);
}

}  // namespace

std::string ensure_custom_ability_pie_in_annotation_txt(
	std::string_view txt, int folder_number, std::string_view release_sound_editor_id)
{
	auto parsed = parse_annotation_txt(txt);
	std::vector<float> author_cast_times;
	for (const auto& annotation : parsed.annotations) {
		if (is_author_spell_cast_annotation(annotation.text)) {
			author_cast_times.push_back(annotation.time);
		}
	}

	std::vector<ClipAnnotation> kept;
	kept.reserve(parsed.annotations.size() + 2);
	for (const auto& annotation : parsed.annotations) {
		if (is_sound_play_annotation(annotation.text)) {
			bool paired = false;
			for (const float cast_time : author_cast_times) {
				if (annotations_share_time(annotation.time, cast_time)) {
					paired = true;
					break;
				}
			}
			if (paired) {
				continue;
			}
		}
		if (const auto kept_text = keep_event_after_stripping_author_cast(annotation.text)) {
			kept.push_back(ClipAnnotation{ .time = annotation.time, .text = *kept_text });
		}
	}
	parsed.annotations = std::move(kept);
	if (!clip_has_custom_ability_pie(parsed.annotations, folder_number)) {
		const float time = custom_ability_pie_stamp_time(parsed.annotations, parsed.duration);
		parsed.annotations.push_back(
			ClipAnnotation{ .time = time, .text = custom_ability_pie_annotation(folder_number) });
	}
	const auto sound_line = custom_ability_spell_sound_annotation(release_sound_editor_id);
	if (!sound_line.empty()) {
		const float time = custom_ability_pie_stamp_time(parsed.annotations, parsed.duration);
		bool has_ours = false;
		for (auto& annotation : parsed.annotations) {
			if (!is_sound_play_annotation(annotation.text) || !annotations_share_time(annotation.time, time)) {
				continue;
			}
			annotation.text = sound_line;
			has_ours = true;
		}
		if (!has_ours) {
			parsed.annotations.push_back(ClipAnnotation{ .time = time, .text = sound_line });
		}
	}
	std::stable_sort(parsed.annotations.begin(), parsed.annotations.end(),
		[](const ClipAnnotation& a, const ClipAnnotation& b) { return a.time < b.time; });

	if (parsed.num_annotations_header_index >= 0) {
		parsed.header_lines[static_cast<std::size_t>(parsed.num_annotations_header_index)] =
			std::format("# numAnnotations: {}", parsed.annotations.size());
	}

	std::string out;
	for (const auto& header : parsed.header_lines) {
		out += header;
		out += '\n';
	}
	for (const auto& annotation : parsed.annotations) {
		out += format_anno_time(annotation.time);
		out += ' ';
		out += annotation.text;
		out += '\n';
	}
	return out;
}

}  // namespace SpellHotbar
