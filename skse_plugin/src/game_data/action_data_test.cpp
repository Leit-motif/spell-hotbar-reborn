#include "action_definition.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>

using SpellHotbar::ActionDefinition;
using SpellHotbar::ActionInputDevice;
using SpellHotbar::ActionKind;
using SpellHotbar::ActionTargetSource;
using SpellHotbar::ActionPlayerOverlay;
using SpellHotbar::action_matches_catalogue;
using SpellHotbar::action_player_overlay_from;
using SpellHotbar::action_press_is_admitted;
using SpellHotbar::apply_action_player_overlay;
using SpellHotbar::custom_action_id_base;
using SpellHotbar::custom_action_count;
using SpellHotbar::is_visible_action_id;
using SpellHotbar::action_would_recurse;
using SpellHotbar::default_action_catalogue;
using SpellHotbar::resolve_action_input;
using SpellHotbar::action_input_device_from_dx_scancode;
using SpellHotbar::ActionInput;

namespace {

int g_failures = 0;

void expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		++g_failures;
	}
}

void shipped_catalogue_is_the_twelve_custom_rows()
{
	const auto actions = default_action_catalogue();
	expect(actions.size() == custom_action_count,
		"the default catalogue is the twelve visible rows and nothing else");
	expect(actions.size() >= 1 && actions[0].id == custom_action_id_base,
		"the first custom Action starts at the reserved id base");
	expect(actions.size() >= 1 && actions[0].target == ActionTargetSource::captured,
		"custom rows use captured scancodes");
	expect(actions.size() >= 1 && actions[0].captured_scancode == 0,
		"custom rows start unbound");
	// The hidden Power Attack (1) and Dodge (2) rows went with the OCPA and TK Dodge readers.
	expect(std::none_of(actions.begin(), actions.end(), [](const auto& action) {
		return action.id < custom_action_id_base;
	}), "no reader-backed legacy row is seeded any more");
	for (std::uint32_t offset = 0; offset < custom_action_count; ++offset) {
		const auto id = custom_action_id_base + offset;
		const auto it = std::find_if(actions.begin(), actions.end(), [id](const auto& action) {
			return action.id == id;
		});
		expect(it != actions.end(), "every visible Action id is seeded");
		if (it != actions.end()) {
			expect(it->display_name == "Action " + std::to_string(offset + 1),
				"visible Action rows use the shipped Action 1..12 names");
		}
	}
	expect(!is_visible_action_id(1) && !is_visible_action_id(2),
		"the retired Power Attack and Dodge ids are still not visible ids");
	expect(is_visible_action_id(100) && is_visible_action_id(111) && !is_visible_action_id(112),
		"only Action ids 100..111 are visible");
}

void target_resolution_reads_the_captured_binding()
{
	const auto actions = default_action_catalogue();
	expect(!resolve_action_input(actions[0]).is_bound(),
		"a shipped row resolves as unbound until the player captures a key");
	ActionDefinition custom = actions[0];
	custom.captured_scancode = 42;
	const auto resolved = resolve_action_input(custom);
	expect(resolved.device == ActionInputDevice::keyboard && resolved.dx_scancode == 42,
		"a captured Action resolves its authored device and scancode");
}

void recursion_is_rejected_only_for_the_triggering_bind()
{
	expect(action_would_recurse(79, 79), "an Action cannot inject its own triggering bind");
	expect(!action_would_recurse(79, 80), "a different bind is not recursion");
	expect(!action_would_recurse(0, 0), "an unbound target is not reported as recursion");
	expect(!action_would_recurse(79, -1), "an unbound triggering slot cannot recurse");
}

void only_nonzero_meter_gcd_or_cooldown_makes_an_action_costed()
{
	ActionDefinition action{};
	expect(!action.is_costed(), "default Action is costless");
	action.stamina_cost = 1.0f;
	expect(action.is_costed(), "stamina makes an Action costed");
	action.stamina_cost = 0.0f;
	action.gcd = 0.1f;
	expect(action.is_costed(), "GCD makes an Action costed");
	action.gcd = 0.0f;
	action.cooldown_days = 0.1f;
	expect(action.is_costed(), "cooldown makes an Action costed");
}

void any_costless_action_can_cut_a_committed_cuttable_cast()
{
	expect(action_press_is_admitted(false, false, false),
		"an idle free custom Action is admitted");
	expect(!action_press_is_admitted(false, true, false),
		"a pre-commit live cast blocks every Action, costless included");
	expect(action_press_is_admitted(false, true, true),
		"a committed cuttable cast admits any costless Action");
	expect(!action_press_is_admitted(true, true, true),
		"a costed Action never cuts a committed cuttable cast");
	expect(!action_press_is_admitted(true, true, false),
		"a costed Action is refused while any cast is live");
	expect(action_press_is_admitted(false, false, true),
		"a retired cuttable follow-through admits a costless Action");
	expect(action_press_is_admitted(true, false, false),
		"an idle costed Action reaches ordinary cost checks");
}

void a_costless_action_is_admitted_once_the_ability_graph_is_gone()
{
	expect(action_press_is_admitted(false, true, false, true),
		"a costless Action is admitted once the Ability graph is already back in ready");
	expect(!action_press_is_admitted(true, true, false, true),
		"a costed Action still waits out a leftover Ability instance");
	expect(!action_press_is_admitted(false, true, false, false),
		"a live Ability whose graph is still playing blocks every Action");
}

void player_overlay_round_trips_the_action_fields()
{
	const auto actions = default_action_catalogue();
	ActionDefinition edited = actions.front();
	edited.display_name = "Heavy Power Attack";
	edited.icon = "DESTRUCTION_FIRE_ADEPT";
	edited.icon_form = 0x1234;
	edited.stamina_cost = 12.5f;
	edited.magicka_cost = 2.0f;
	edited.health_cost = 1.0f;
	edited.cooldown_days = 0.25f;
	edited.gcd = 1.25f;

	const ActionPlayerOverlay overlay = action_player_overlay_from(edited);
	ActionDefinition restored = actions.front();
	apply_action_player_overlay(restored, overlay);
	expect(restored.id == actions.front().id, "an overlay never changes the stable Action id");
	expect(restored.display_name == edited.display_name, "overlay restores the Action name");
	expect(restored.icon == edited.icon && restored.icon_form == edited.icon_form,
		"overlay restores the Action icon fields");
	expect(restored.stamina_cost == edited.stamina_cost && restored.magicka_cost == edited.magicka_cost &&
		restored.health_cost == edited.health_cost, "overlay restores all meter costs");
	expect(restored.cooldown_days == edited.cooldown_days && restored.gcd == edited.gcd,
		"overlay restores cooldown and GCD");
	expect(!action_matches_catalogue(restored, actions.front()), "an edited Action differs from its catalogue row");
}

void captured_device_and_target_round_trip_through_an_overlay()
{
	const auto actions = default_action_catalogue();
	ActionDefinition edited = actions[0];
	edited.target = ActionTargetSource::captured;
	edited.captured_device = ActionInputDevice::gamepad;
	edited.captured_scancode = 277; // a supported gamepad DX range value

	const ActionPlayerOverlay overlay = action_player_overlay_from(edited);
	ActionDefinition restored = actions[0];
	apply_action_player_overlay(restored, overlay);
	expect(restored.target == ActionTargetSource::captured,
		"overlay preserves a custom Action's captured target");
	expect(restored.captured_device == ActionInputDevice::gamepad,
		"overlay preserves the captured input device");
	expect(restored.captured_scancode == 277,
		"overlay preserves the captured DX scancode");
	const auto resolved = resolve_action_input(restored);
	expect(resolved.device == ActionInputDevice::gamepad && resolved.dx_scancode == 277,
		"captured target resolves both device and DX scancode");
}

void legacy_dx_values_map_to_the_device_they_were_written_for()
{
	expect(action_input_device_from_dx_scancode(255) == ActionInputDevice::keyboard,
		"255 is the last keyboard DX value");
	expect(action_input_device_from_dx_scancode(256) == ActionInputDevice::mouse,
		"256 is the first mouse DX value");
	expect(action_input_device_from_dx_scancode(265) == ActionInputDevice::mouse,
		"265 is the last mouse DX value");
	expect(action_input_device_from_dx_scancode(266) == ActionInputDevice::gamepad,
		"266 is the first gamepad DX value");
}

void a_silent_overlay_keeps_the_icon_and_the_name()
{
	const auto actions = default_action_catalogue();
	ActionPlayerOverlay overlay = action_player_overlay_from(actions.front());
	overlay.display_name.clear();
	overlay.icon.clear();
	overlay.icon_form = 0;

	ActionDefinition restored = actions.front();
	apply_action_player_overlay(restored, overlay);
	expect(restored.display_name == actions.front().display_name,
		"an empty overlay name keeps the catalogue name");
	expect(restored.icon == actions.front().icon,
		"an overlay naming neither icon path nor icon form keeps the catalogue icon");

	overlay.icon = "DESTRUCTION_FIRE_ADEPT";
	ActionDefinition with_icon = actions.front();
	apply_action_player_overlay(with_icon, overlay);
	expect(with_icon.icon == "DESTRUCTION_FIRE_ADEPT", "an overlay icon path still wins");

	overlay.icon.clear();
	overlay.icon_form = 0x1234;
	ActionDefinition with_form = actions.front();
	apply_action_player_overlay(with_form, overlay);
	expect(with_form.icon.empty() && with_form.icon_form == 0x1234,
		"an overlay icon form alone still wins and clears the path");
}

// The graph kinds do not use the input bridge at all: they send an attack event to the player's
// animation graph, so a captured scancode beside one is not a binding and must never resolve as a
// target.
void a_graph_kind_action_resolves_no_input()
{
	ActionDefinition action = default_action_catalogue().front();
	action.captured_scancode = 42;
	expect(resolve_action_input(action).is_bound(), "a physical row with a scancode is bound");

	action.kind = ActionKind::light_attack;
	expect(!resolve_action_input(action).is_bound(),
		"a light_attack Action is unbound even with a captured scancode");
	action.kind = ActionKind::power_attack;
	expect(!resolve_action_input(action).is_bound(), "a power_attack Action is unbound too");
}

void the_kind_is_part_of_the_catalogue_comparison_and_the_overlay()
{
	const auto actions = default_action_catalogue();
	ActionDefinition edited = actions.front();
	edited.kind = ActionKind::power_attack;
	expect(!action_matches_catalogue(edited, actions.front()),
		"a changed kind alone makes an Action differ from its catalogue row");

	const ActionPlayerOverlay overlay = action_player_overlay_from(edited);
	expect(overlay.kind == ActionKind::power_attack, "the overlay carries the kind out");
	ActionDefinition restored = actions.front();
	apply_action_player_overlay(restored, overlay);
	expect(restored.kind == ActionKind::power_attack, "the overlay carries the kind back in");
	expect(action_matches_catalogue(restored, edited), "the kind round-trips exactly");
}

}  // namespace

int main()
{
	a_graph_kind_action_resolves_no_input();
	the_kind_is_part_of_the_catalogue_comparison_and_the_overlay();
	shipped_catalogue_is_the_twelve_custom_rows();
	target_resolution_reads_the_captured_binding();
	recursion_is_rejected_only_for_the_triggering_bind();
	only_nonzero_meter_gcd_or_cooldown_makes_an_action_costed();
	any_costless_action_can_cut_a_committed_cuttable_cast();
	a_costless_action_is_admitted_once_the_ability_graph_is_gone();
	player_overlay_round_trips_the_action_fields();
	captured_device_and_target_round_trip_through_an_overlay();
	legacy_dx_values_map_to_the_device_they_were_written_for();
	a_silent_overlay_keeps_the_icon_and_the_name();
	return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
