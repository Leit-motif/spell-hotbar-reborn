#include "custom_ability_runtime.h"
#include "custom_ability_config.h"
#include "game_data.h"
#include "../logger/logger.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <windows.h>

namespace SpellHotbar {

namespace {

[[nodiscard]] int folder_number_of(const ArtDefinition& art)
{
	return static_cast<int>(art.id - custom_art_id_base);
}

[[nodiscard]] AbilitySpellFormKind form_kind_of(const RE::TESForm* form)
{
	if (!form) {
		return AbilitySpellFormKind::Other;
	}
	switch (form->GetFormType()) {
	case RE::FormType::Spell:
		return AbilitySpellFormKind::Spell;
	case RE::FormType::Scroll:
		return AbilitySpellFormKind::Scroll;
	case RE::FormType::AlchemyItem:
		return AbilitySpellFormKind::Alchemy;
	case RE::FormType::Shout:
		return AbilitySpellFormKind::Shout;
	default:
		return AbilitySpellFormKind::Other;
	}
}

[[nodiscard]] AbilitySpellCasting casting_of(const RE::SpellItem* spell)
{
	if (!spell) {
		return AbilitySpellCasting::Other;
	}
	switch (spell->GetCastingType()) {
	case RE::MagicSystem::CastingType::kFireAndForget:
		return AbilitySpellCasting::FireAndForget;
	case RE::MagicSystem::CastingType::kConcentration:
		return AbilitySpellCasting::Concentration;
	case RE::MagicSystem::CastingType::kConstantEffect:
		return AbilitySpellCasting::ConstantEffect;
	default:
		return AbilitySpellCasting::Other;
	}
}

[[nodiscard]] std::filesystem::path find_aabl(const std::filesystem::path& folder)
{
	std::error_code ec;
	if (folder.empty() || !std::filesystem::exists(folder, ec)) {
		return {};
	}
	for (const auto& entry : std::filesystem::recursive_directory_iterator(folder, ec)) {
		if (ec || !entry.is_regular_file()) {
			continue;
		}
		auto name = entry.path().filename().string();
		std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		if (name == "aabl_attack_a.hkx") {
			return entry.path();
		}
	}
	return {};
}

[[nodiscard]] std::filesystem::path pi_config_path()
{
	const auto runtime = REL::Module::get().filePath();
	return std::filesystem::path(runtime).parent_path() / "Data" / "SKSE" / "PayloadInterpreter" / "Config" /
		   "SpellHotbar2_CustomAbilities.ini";
}

bool run_process(const std::wstring& exe, const std::wstring& args)
{
	STARTUPINFOW si{};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi{};
	std::wstring cmd = L"\"" + exe + L"\" " + args;
	std::vector<wchar_t> mutable_cmd(cmd.begin(), cmd.end());
	mutable_cmd.push_back(L'\0');
	if (!CreateProcessW(exe.c_str(), mutable_cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
			&si, &pi)) {
		return false;
	}
	WaitForSingleObject(pi.hProcess, 15000);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return true;
}

void stamp_hkx_with_hkxc(const std::filesystem::path& hkx, int folder_number, std::string_view release_sound_editor_id)
{
	wchar_t* env = nullptr;
	std::size_t env_len = 0;
	if (_wdupenv_s(&env, &env_len, L"HKXC_ANNO_CLI") != 0 || !env || env_len == 0) {
		if (env) {
			free(env);
		}
		return;
	}
	const std::wstring exe{ env };
	free(env);
	const auto txt = hkx.parent_path() / (hkx.stem().wstring() + L".anno.txt");
	const std::wstring hkx_s = hkx.wstring();
	const std::wstring txt_s = txt.wstring();
	if (!run_process(exe, L"dump -i \"" + hkx_s + L"\" -o \"" + txt_s + L"\"")) {
		return;
	}
	std::ifstream in(txt);
	if (!in) {
		return;
	}
	std::ostringstream buf;
	buf << in.rdbuf();
	in.close();
	const auto updated = ensure_custom_ability_pie_in_annotation_txt(buf.str(), folder_number, release_sound_editor_id);
	std::ofstream out(txt, std::ofstream::trunc);
	if (!out) {
		return;
	}
	out << updated;
	out.close();
	run_process(exe, L"update -a \"" + txt_s + L"\" -i \"" + hkx_s + L"\"");
}

[[nodiscard]] std::string release_sound_editor_id_for(const ArtDefinition& art)
{
	auto* handler = RE::TESDataHandler::GetSingleton();
	if (!handler) {
		return {};
	}
	const auto plugin = art.spell_plugin.empty() ? vanilla_firebolt_plugin : art.spell_plugin;
	const auto form = art.spell_local_form == 0 ? vanilla_firebolt_local_form : art.spell_local_form;
	auto* spell = handler->LookupForm<RE::SpellItem>(form, plugin);
	if (!spell) {
		return {};
	}
	for (auto* effect : spell->effects) {
		if (!effect || !effect->baseEffect) {
			continue;
		}
		RE::BGSSoundDescriptorForm* release = nullptr;
		RE::BGSSoundDescriptorForm* fallback = nullptr;
		for (const auto& pair : effect->baseEffect->effectSounds) {
			if (!pair.sound) {
				continue;
			}
			if (pair.id == RE::MagicSystem::SoundID::kRelease) {
				release = pair.sound;
				break;
			}
			if (!fallback && pair.id != RE::MagicSystem::SoundID::kCastLoop &&
				pair.id != RE::MagicSystem::SoundID::kHit) {
				fallback = pair.sound;
			}
		}
		auto* snd = release ? release : fallback;
		if (!snd) {
			continue;
		}
		const char* edid = snd->GetFormEditorID();
		if (edid && edid[0] != '\0') {
			return edid;
		}
	}
	return {};
}

}  // namespace

bool is_assignable_custom_ability_spell(const RE::TESForm* form)
{
	const auto* spell = form ? form->As<RE::SpellItem>() : nullptr;
	return SpellHotbar::is_assignable_custom_ability_spell(form_kind_of(form), casting_of(spell));
}

void assign_custom_ability_spell(ArtDefinition& art, RE::SpellItem* spell)
{
	if (!spell) {
		return;
	}
	art.spell_local_form = spell->GetLocalFormID();
	art.spell_plugin = spell->GetFile(0) ? std::string(spell->GetFile(0)->GetFilename()) : vanilla_firebolt_plugin;
	art.self_target = spell->GetDelivery() == RE::MagicSystem::Delivery::kSelf;
}

std::string custom_ability_spell_label(const ArtDefinition& art)
{
	auto* handler = RE::TESDataHandler::GetSingleton();
	if (handler && art.spell_local_form != 0 && !art.spell_plugin.empty()) {
		if (auto* spell = handler->LookupForm<RE::SpellItem>(art.spell_local_form, art.spell_plugin)) {
			if (const char* name = spell->GetName(); name && name[0] != '\0') {
				return name;
			}
		}
	}
	if (art.spell_local_form == vanilla_firebolt_local_form) {
		return "Firebolt";
	}
	return std::format("{:06X}:{}", art.spell_local_form, art.spell_plugin);
}

std::vector<RE::SpellItem*> list_known_custom_ability_spells()
{
	std::vector<RE::SpellItem*> out;
	auto* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) {
		return out;
	}
	std::vector<RE::TESForm*> forms;
	GameData::get_player_known_spells(pc, forms, false);
	for (auto* form : forms) {
		if (is_assignable_custom_ability_spell(form)) {
			out.push_back(form->As<RE::SpellItem>());
		}
	}
	return out;
}

bool persist_custom_ability(ArtDefinition& art)
{
	if (!is_custom_ability(art.id) || art.folder_path.empty()) {
		return false;
	}
	const int n = folder_number_of(art);
	const auto sidecar = sidecar_from_art(art, n);
	const auto path = std::filesystem::path(art.folder_path) / custom_ability_sidecar_filename;
	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);
	std::ofstream out(path, std::ofstream::trunc);
	if (!out) {
		logger::error("SH2 art: could not write Ability sidecar '{}'", path.string());
		return false;
	}
	out << serialize_custom_ability_sidecar(sidecar);
	out.flush();
	if (!out) {
		logger::error("SH2 art: could not write Ability sidecar '{}'", path.string());
		return false;
	}
	emit_custom_ability_pi_config();
	stamp_custom_ability_clip(art);
	logger::info("SH2 art: saved Custom Ability {} sidecar", n);
	return true;
}

bool persist_ability(ArtDefinition& art)
{
	if (is_custom_ability(art.id)) {
		return persist_custom_ability(art);
	}
	return GameData::persist_art_player_overlay(art);
}

void emit_custom_ability_pi_config()
{
	if (!custom_ability_spell_assignment_enabled) {
		return;
	}
	std::vector<std::pair<int, CustomAbilitySidecar>> entries;
	for (const auto id : GameData::list_art_ids()) {
		const auto* art = GameData::get_art(id);
		if (!art || !is_custom_ability(id)) {
			continue;
		}
		entries.emplace_back(static_cast<int>(id - custom_art_id_base), sidecar_from_art(*art, static_cast<int>(id - custom_art_id_base)));
	}
	const auto path = pi_config_path();
	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);
	std::ofstream out(path, std::ofstream::trunc);
	if (!out) {
		logger::warn("SH2 art: could not write PI config '{}'", path.string());
		return;
	}
	out << custom_ability_pi_ini(entries);
}

void inject_custom_ability_pie(RE::hkaAnimation* animation, const ArtDefinition& art)
{
	if (!custom_ability_spell_assignment_enabled) {
		return;
	}
	if (!animation || !is_custom_ability(art.id) || animation->annotationTracks.empty()) {
		return;
	}
	const int n = folder_number_of(art);
	const auto sidecar = sidecar_from_art(art, n);
	const auto expanded = std::string("PIE.") + custom_ability_castspell_instruction(sidecar);
	const auto dollar = custom_ability_pie_annotation(n);
	const auto sound_line = custom_ability_spell_sound_annotation(release_sound_editor_id_for(art));

	std::vector<ClipAnnotation> collected;
	for (const auto& track : animation->annotationTracks) {
		for (std::int32_t i = 0; i < track.annotations.size(); ++i) {
			const char* text = track.annotations[i].text.c_str();
			collected.push_back(ClipAnnotation{ .time = track.annotations[i].time, .text = text ? text : "" });
		}
	}
	const float stamp_time = custom_ability_pie_stamp_time(collected, animation->duration);
	std::vector<float> author_cast_times;
	for (const auto& annotation : collected) {
		if (is_author_spell_cast_annotation(annotation.text)) {
			author_cast_times.push_back(annotation.time);
		}
	}

	bool placed = false;
	bool sound_placed = sound_line.empty();
	for (auto& track : animation->annotationTracks) {
		for (std::int32_t i = 0; i < track.annotations.size(); ++i) {
			const char* text = track.annotations[i].text.c_str();
			if (!text) {
				continue;
			}
			const std::string_view view{ text };
			if (view == dollar || view.starts_with(dollar + ".") || view == expanded) {
				track.annotations[i].time = stamp_time;
				track.annotations[i].text = expanded.c_str();
				placed = true;
				continue;
			}
			if (is_sound_play_annotation(view)) {
				bool paired = false;
				for (const float cast_time : author_cast_times) {
					if (annotations_share_time(track.annotations[i].time, cast_time)) {
						paired = true;
						break;
					}
				}
				if (paired) {
					track.annotations[i].text = "SH2_ReplacedSound";
					continue;
				}
				if (!sound_line.empty() && annotations_share_time(track.annotations[i].time, stamp_time)) {
					track.annotations[i].text = sound_line.c_str();
					sound_placed = true;
				}
				continue;
			}
			if (!is_author_spell_cast_annotation(view)) {
				continue;
			}
			const auto dot = view.find('.');
			if (dot != std::string_view::npos) {
				const auto event = view.substr(0, dot);
				if (!event.empty() && event != "PIE") {
					track.annotations[i].text = std::string{ event }.c_str();
					continue;
				}
			}
			track.annotations[i].text = "SH2_ReplacedCast";
		}
	}

	auto& track = animation->annotationTracks[0];
	if (!placed) {
		RE::hkaAnnotationTrack::Annotation anno{ stamp_time, 0, expanded.c_str() };
		track.annotations.push_back(anno);
	}
	if (!sound_placed) {
		RE::hkaAnnotationTrack::Annotation anno{ stamp_time, 0, sound_line.c_str() };
		track.annotations.push_back(anno);
	}
}

void stamp_custom_ability_clip(const ArtDefinition& art)
{
	if (!custom_ability_spell_assignment_enabled) {
		return;
	}
	if (!art.has_clip || art.folder_path.empty()) {
		return;
	}
	const auto hkx = find_aabl(art.folder_path);
	if (hkx.empty()) {
		return;
	}
	stamp_hkx_with_hkxc(hkx, folder_number_of(art), release_sound_editor_id_for(art));
}

}  // namespace SpellHotbar
