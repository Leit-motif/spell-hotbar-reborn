#pragma once

#include <cstdint>

namespace SpellHotbar::BindMenu {

// A bind-menu drop is either a TESForm FormID, an Ability id, an Action id,
// or empty. The identities must not share a slot: applying one clears the others.
struct SlotBind {
	uint32_t form_id{ 0 };
	uint32_t art_id{ 0 };
	uint32_t action_id{ 0 };
};

struct BindPayload {
	uint32_t form_id{ 0 };
	uint32_t art_id{ 0 };
	uint32_t action_id{ 0 };
};

inline BindPayload empty_bind()
{
	return {};
}

inline BindPayload form_bind(uint32_t form_id)
{
	return BindPayload{ .form_id = form_id, .art_id = 0, .action_id = 0 };
}

inline BindPayload art_bind(uint32_t art_id)
{
	return BindPayload{ .form_id = 0, .art_id = art_id, .action_id = 0 };
}

inline BindPayload action_bind(uint32_t action_id)
{
	return BindPayload{ .form_id = 0, .art_id = 0, .action_id = action_id };
}

inline SlotBind apply_bind_drop(SlotBind, BindPayload incoming)
{
	if (incoming.action_id != 0) {
		return SlotBind{ .form_id = 0, .art_id = 0, .action_id = incoming.action_id };
	}
	if (incoming.art_id != 0) {
		return SlotBind{ .form_id = 0, .art_id = incoming.art_id, .action_id = 0 };
	}
	if (incoming.form_id != 0) {
		return SlotBind{ .form_id = incoming.form_id, .art_id = 0, .action_id = 0 };
	}
	return {};
}

}  // namespace SpellHotbar::BindMenu
