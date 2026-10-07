#include "art_definition.h"
#include "custom_ability_config.h"

#include <algorithm>
#include <charconv>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace SpellHotbar {

namespace {

constexpr float hours_to_days = 1.0f / 24.0f;
constexpr float minutes_to_days = 1.0f / (24.0f * 60.0f);
constexpr float seconds_to_days = 1.0f / (24.0f * 60.0f * 60.0f);

const std::regex re_time_str(R"((-?\d+(\.\d*)?)([smh]?))");

std::vector<std::string> split_tabs(std::string_view line)
{
	std::vector<std::string> out;
	std::string current;
	for (char c : line) {
		if (c == '\t') {
			out.push_back(std::move(current));
			current.clear();
		} else if (c != '\r') {
			current.push_back(c);
		}
	}
	out.push_back(std::move(current));
	return out;
}

std::optional<int> to_int(std::string_view s)
{
	if (s.empty()) {
		return std::nullopt;
	}
	int value = 0;
	const auto* begin = s.data();
	const auto* end = s.data() + s.size();
	const auto [ptr, ec] = std::from_chars(begin, end, value);
	if (ec != std::errc{} || ptr != end) {
		return std::nullopt;
	}
	return value;
}

std::optional<float> to_float(std::string_view s)
{
	if (s.empty()) {
		return std::nullopt;
	}
	std::string copy{s};
	std::replace(copy.begin(), copy.end(), ',', '.');
	try {
		size_t idx = 0;
		const float value = std::stof(copy, &idx);
		if (idx != copy.size()) {
			return std::nullopt;
		}
		return value;
	} catch (const std::exception&) {
		return std::nullopt;
	}
}

std::string cell(const std::vector<std::string>& cols,
	const std::unordered_map<std::string, std::size_t>& index, const char* name)
{
	const auto it = index.find(name);
	if (it == index.end() || it->second >= cols.size()) {
		return {};
	}
	return cols[it->second];
}

std::string first_nonempty_line(std::string_view text)
{
	std::istringstream stream{std::string{text}};
	std::string line;
	while (std::getline(stream, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		const auto start = line.find_first_not_of(" \t");
		if (start == std::string::npos) {
			continue;
		}
		const auto end = line.find_last_not_of(" \t");
		return line.substr(start, end - start + 1);
	}
	return {};
}

}  // namespace

std::optional<float> parse_art_duration_days(std::string_view time_str)
{
	std::string input{time_str};
	std::replace(input.begin(), input.end(), ',', '.');

	std::smatch m;
	if (!std::regex_match(input, m, re_time_str) || m.size() < 2) {
		return std::nullopt;
	}

	float value = 0.0f;
	try {
		value = std::stof(m[1].str());
	} catch (const std::exception&) {
		return std::nullopt;
	}

	float factor = seconds_to_days;
	if (m.size() > 2) {
		const std::string dur = m[m.size() - 1].str();
		if (dur == "m") {
			factor = minutes_to_days;
		} else if (dur == "h") {
			factor = hours_to_days;
		}
	}

	if (value > 0.0f) {
		return value * factor;
	}
	return -1.0f;
}

ArtClass parse_art_class(std::string_view text)
{
	if (text == "1H") {
		return ArtClass::OneHand;
	}
	if (text == "2H") {
		return ArtClass::TwoHand;
	}
	if (text == "Dual") {
		return ArtClass::Dual;
	}
	if (text == "Bow") {
		return ArtClass::Bow;
	}
	return ArtClass::Generic;
}

std::vector<ArtDefinition> parse_art_tsv(std::string_view text)
{
	std::vector<ArtDefinition> arts;
	std::istringstream stream{std::string{text}};
	std::string line;
	if (!std::getline(stream, line)) {
		return arts;
	}
	if (!line.empty() && line.back() == '\r') {
		line.pop_back();
	}

	const auto headers = split_tabs(line);
	std::unordered_map<std::string, std::size_t> index;
	for (std::size_t i = 0; i < headers.size(); ++i) {
		index.emplace(headers[i], i);
	}

	const bool ok = index.contains("ArtID") && index.contains("DisplayName") &&
					index.contains("Icon") && index.contains("Selector") &&
					index.contains("ArtClass") && index.contains("StaminaCost") &&
					index.contains("Cooldown") && index.contains("GlobalCooldown");
	if (!ok) {
		return arts;
	}

	while (std::getline(stream, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		if (line.empty()) {
			continue;
		}
		const auto cols = split_tabs(line);
		ArtDefinition art;
		const auto id = to_int(cell(cols, index, "ArtID"));
		if (!id || *id <= 0) {
			continue;
		}
		art.id = static_cast<std::uint32_t>(*id);
		art.display_name = cell(cols, index, "DisplayName");
		if (art.display_name.empty()) {
			continue;
		}
		art.icon = cell(cols, index, "Icon");
		art.selector = to_int(cell(cols, index, "Selector")).value_or(0);
		art.art_class = parse_art_class(cell(cols, index, "ArtClass"));
		art.stamina_cost = to_float(cell(cols, index, "StaminaCost")).value_or(0.0f);
		art.magicka_cost = to_float(cell(cols, index, "MagickaCost")).value_or(0.0f);
		art.health_cost = to_float(cell(cols, index, "HealthCost")).value_or(0.0f);
		// Arts sit in the action class: an unset GlobalCooldown is the 1.5s action number, not 1.0.
		art.gcd = to_float(cell(cols, index, "GlobalCooldown")).value_or(1.5f);
		art.cooldown_text = cell(cols, index, "Cooldown");
		if (const auto cd = parse_art_duration_days(art.cooldown_text)) {
			art.cooldown_days = *cd;
		} else {
			art.cooldown_days = -1.0f;
		}
		arts.push_back(std::move(art));
	}
	return arts;
}

std::optional<int> parse_custom_art_folder_number(std::string_view folder_name)
{
	static const std::regex re(R"(^Custom_Ability_(\d+)$)", std::regex::icase);
	std::string input{folder_name};
	std::smatch m;
	if (!std::regex_match(input, m, re) || m.size() < 2) {
		return std::nullopt;
	}
	return to_int(m[1].str());
}

ArtDefinition custom_art_from_folder(int folder_number, std::string_view /*folder_name*/,
	std::string_view name_file, std::string_view icon_file, bool has_clip,
	std::string_view sidecar_text)
{
	CustomAbilitySidecar sidecar = sidecar_text.empty()
		? default_custom_ability_sidecar(folder_number)
		: parse_custom_ability_sidecar(sidecar_text, folder_number);
	if (sidecar_text.empty()) {
		const auto name = first_nonempty_line(name_file);
		if (!name.empty()) {
			sidecar.name = name;
		}
		const auto icon = first_nonempty_line(icon_file);
		if (!icon.empty()) {
			sidecar.icon = icon;
		}
	}
	ArtDefinition art;
	art.id = custom_art_id_base + static_cast<std::uint32_t>(folder_number);
	art.selector = static_cast<int>(art.id);
	art.has_clip = has_clip;
	apply_custom_ability_sidecar(art, sidecar, folder_number);
	return art;
}

void apply_art_player_overlay(ArtDefinition& art, const ArtPlayerOverlay& overlay)
{
	if (!overlay.display_name.empty()) {
		art.display_name = overlay.display_name;
	}
	art.icon = overlay.icon;
	art.icon_form = overlay.icon_form;
	art.art_class = overlay.art_class;
	art.stamina_cost = overlay.stamina_cost;
	art.magicka_cost = overlay.magicka_cost;
	art.health_cost = overlay.health_cost;
	art.gcd = overlay.gcd;
	art.damage_mult = overlay.damage_mult;
	if (!overlay.cooldown.empty()) {
		art.cooldown_text = overlay.cooldown;
		if (const auto days = parse_art_duration_days(art.cooldown_text)) {
			art.cooldown_days = *days;
		}
	}
}

ArtPlayerOverlay art_player_overlay_from(const ArtDefinition& art)
{
	return ArtPlayerOverlay{
		.display_name = art.display_name,
		.icon = art.icon,
		.icon_form = art.icon_form,
		.art_class = art.art_class,
		.stamina_cost = art.stamina_cost,
		.magicka_cost = art.magicka_cost,
		.health_cost = art.health_cost,
		.cooldown = art.cooldown_text,
		.gcd = art.gcd,
		.damage_mult = art.damage_mult,
	};
}

bool art_matches_catalogue_tuning(const ArtDefinition& live, const ArtDefinition& catalogue) noexcept
{
	return live.display_name == catalogue.display_name && live.icon == catalogue.icon &&
		   live.icon_form == catalogue.icon_form && live.art_class == catalogue.art_class &&
		   live.stamina_cost == catalogue.stamina_cost && live.magicka_cost == catalogue.magicka_cost &&
		   live.health_cost == catalogue.health_cost && live.gcd == catalogue.gcd &&
		   live.damage_mult == catalogue.damage_mult &&
		   live.cooldown_text == catalogue.cooldown_text;
}

}  // namespace SpellHotbar
