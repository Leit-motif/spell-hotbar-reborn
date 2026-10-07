#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <rapidjson/document.h>

#include "action_definition.h"

namespace SpellHotbar {

/**
 * Parse one entry of the Action overlay sidecar. Pure: no file, log, or catalogue access, so the
 * rejection rules are testable on their own. `base` is the overlay the entry is edited onto --
 * normally the live Action's current values -- so a member the entry omits keeps what it had.
 *
 * A rejected entry answers with an empty `overlay` and an `error` naming the member at fault. An
 * absent member is not an error; a member that is present with the wrong JSON type is, because a
 * silently ignored `"captured_scancode": 48.0` loads as unbound and looks like data loss.
 */
struct ActionOverlayParse {
	std::uint32_t action_id{ 0 };
	std::optional<ActionPlayerOverlay> overlay;
	// Empty when accepted. Otherwise the member name that failed.
	std::string error_member;
	// True when the member was present but of the wrong JSON type, false for out of range.
	bool error_is_type{ false };
	// True when the entry named one of the retired reader-backed target sources.
	// The overlay is still accepted -- as an unbound captured row -- and the caller logs it.
	bool legacy_target{ false };
	// The stored value that was remapped, for that log line.
	std::uint32_t legacy_target_value{ 0 };
};

namespace action_overlay_json_detail {

enum class MemberRead : unsigned char {
	absent,
	ok,
	wrong_type,
};

inline MemberRead read_uint(
	const rapidjson::Value& object, const char* member, std::uint32_t& value)
{
	if (!object.HasMember(member)) {
		return MemberRead::absent;
	}
	if (!object[member].IsUint()) {
		return MemberRead::wrong_type;
	}
	value = object[member].GetUint();
	return MemberRead::ok;
}

inline MemberRead read_float(const rapidjson::Value& object, const char* member, float& value)
{
	if (!object.HasMember(member)) {
		return MemberRead::absent;
	}
	if (!object[member].IsNumber()) {
		return MemberRead::wrong_type;
	}
	value = static_cast<float>(object[member].GetDouble());
	return MemberRead::ok;
}

inline ActionOverlayParse rejected(std::uint32_t id, const char* member, bool is_type)
{
	ActionOverlayParse result;
	result.action_id = id;
	result.error_member = member;
	result.error_is_type = is_type;
	return result;
}

}  // namespace action_overlay_json_detail

[[nodiscard]] inline ActionOverlayParse parse_action_overlay_entry(
	const rapidjson::Value& object, const ActionPlayerOverlay& base)
{
	using namespace action_overlay_json_detail;

	if (!object.IsObject()) {
		return rejected(0, "action_id", true);
	}
	std::uint32_t id = 0;
	const auto id_read = read_uint(object, "action_id", id);
	if (id_read != MemberRead::ok || id == 0) {
		return rejected(0, "action_id", id_read == MemberRead::wrong_type);
	}

	ActionPlayerOverlay overlay = base;

	std::uint32_t kind = 0;
	switch (read_uint(object, "kind", kind)) {
	case MemberRead::wrong_type:
		return rejected(id, "kind", true);
	case MemberRead::ok:
		if (kind > static_cast<std::uint32_t>(ActionKind::power_attack)) {
			return rejected(id, "kind", false);
		}
		overlay.kind = static_cast<ActionKind>(kind);
		break;
	case MemberRead::absent:
		break;
	}

	// Values 0 and 1 were `ocpa_power` and `dodge_hotkey`: targets that resolved a key by
	// reading One Click Power Attack's or TK Dodge RE's ini on every press. The send-side attack
	// seam replaced those readers, so an old row loads as a captured row with no binding rather
	// than as a rejected entry -- the player keeps the row, its name, its icon and its costs, and
	// rebinds it. Rejecting it instead would look like data loss.
	bool legacy_target = false;
	std::uint32_t legacy_target_value = 0;
	std::uint32_t target = 0;
	switch (read_uint(object, "target", target)) {
	case MemberRead::wrong_type:
		return rejected(id, "target", true);
	case MemberRead::ok:
		if (target > static_cast<std::uint32_t>(ActionTargetSource::captured)) {
			return rejected(id, "target", false);
		}
		if (target <= last_legacy_action_target_value) {
			legacy_target = true;
			legacy_target_value = target;
		}
		overlay.target = ActionTargetSource::captured;
		break;
	case MemberRead::absent:
		break;
	}

	std::uint32_t captured_device = 0;
	const auto device_read = read_uint(object, "captured_device", captured_device);
	switch (device_read) {
	case MemberRead::wrong_type:
		return rejected(id, "captured_device", true);
	case MemberRead::ok:
		if (captured_device > static_cast<std::uint32_t>(ActionInputDevice::gamepad)) {
			return rejected(id, "captured_device", false);
		}
		overlay.captured_device = static_cast<ActionInputDevice>(captured_device);
		break;
	case MemberRead::absent:
		break;
	}

	if (object.HasMember("name") && object["name"].IsString()) {
		overlay.display_name = object["name"].GetString();
	}
	if (object.HasMember("icon") && object["icon"].IsString()) {
		overlay.icon = object["icon"].GetString();
	}
	if (read_uint(object, "icon_form", overlay.icon_form) == MemberRead::wrong_type) {
		return rejected(id, "icon_form", true);
	}
	if (read_uint(object, "captured_scancode", overlay.captured_scancode) == MemberRead::wrong_type) {
		return rejected(id, "captured_scancode", true);
	}
	// Version-1 overlays written before the device field used SH2's combined DX value. Infer its
	// device so those bindings continue to inject on the same native device.
	if (device_read == MemberRead::absent) {
		overlay.captured_device = action_input_device_from_dx_scancode(overlay.captured_scancode);
	}

	struct FloatMember {
		const char* name;
		float* value;
	};
	const FloatMember floats[]{
		{ "stamina_cost", &overlay.stamina_cost },
		{ "magicka_cost", &overlay.magicka_cost },
		{ "health_cost", &overlay.health_cost },
		{ "cooldown_days", &overlay.cooldown_days },
		{ "gcd", &overlay.gcd },
	};
	for (const auto& member : floats) {
		if (read_float(object, member.name, *member.value) == MemberRead::wrong_type) {
			return rejected(id, member.name, true);
		}
	}

	// A legacy target keeps no binding, even if the entry also carried a captured scancode: the
	// row it names was resolved from another mod's ini, so any DX value beside it is stale.
	if (legacy_target) {
		overlay.captured_device = ActionInputDevice::keyboard;
		overlay.captured_scancode = 0;
	}

	ActionOverlayParse result;
	result.action_id = id;
	result.overlay = overlay;
	result.legacy_target = legacy_target;
	result.legacy_target_value = legacy_target_value;
	return result;
}

}  // namespace SpellHotbar
