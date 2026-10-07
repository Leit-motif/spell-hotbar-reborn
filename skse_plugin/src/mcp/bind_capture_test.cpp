#include "bind_capture.h"

#include <cstdlib>
#include <iostream>

using SpellHotbar::ActionInputDevice;
using SpellHotbar::Mcp::BindCaptureState;
using SpellHotbar::Mcp::CaptureApply;

namespace {

int g_failures = 0;

void expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		++g_failures;
	}
}

void action_capture_waits_for_a_supported_button_edge()
{
	BindCaptureState state;
	expect(state.arm_action(100), "an Action id arms the shared capture state");
	expect(state.action_armed() && state.pending_action_id() == 100,
		"an armed Action is visible as the pending capture target");

	expect(state.apply_action_down_edge(false, ActionInputDevice::keyboard, -1) == CaptureApply::ignored,
		"an unsupported ButtonEvent does not bind an Action");
	expect(state.action_armed(), "an unsupported ButtonEvent leaves capture armed");
	expect(state.apply_action_down_edge(false, ActionInputDevice::mouse, 261) == CaptureApply::rebound,
		"the next supported ButtonEvent commits the capture");
	expect(!state.armed() && !state.action_armed(), "a committed Action capture disarms the shared state");

	const auto result = state.take_action_capture_result(100);
	expect(result.has_value(), "the capture seam exposes the committed Action result");
	if (result) {
		expect(result->action_id == 100, "the result identifies the Action being edited");
		expect(result->input.device == ActionInputDevice::mouse,
			"the result preserves the native input device");
		expect(result->input.dx_scancode == 261,
			"the result preserves the device-independent DX scancode");
	}
	expect(!state.take_action_capture_result(100).has_value(),
		"a capture result is consumed exactly once");
}

void a_zero_dx_scancode_is_ignored_and_leaves_capture_armed()
{
	BindCaptureState state;
	state.arm_action(102);
	expect(state.apply_action_down_edge(false, ActionInputDevice::keyboard, 0) == CaptureApply::ignored,
		"a zero DX value is not a binding");
	expect(state.action_armed(), "a zero DX value leaves the Action capture armed");
	expect(!state.take_action_capture_result(102).has_value(),
		"a zero DX value produces no capture result");
}

void escape_cancels_action_capture_without_a_result()
{
	BindCaptureState state;
	state.arm_action(101);
	expect(state.apply_action_down_edge(true, ActionInputDevice::keyboard, -1) == CaptureApply::cancelled,
		"Escape cancels an armed Action capture");
	expect(!state.armed() && !state.action_armed(), "Escape disarms the Action capture");
	expect(!state.take_action_capture_result(101).has_value(),
		"Escape does not create an Action binding");
}

void keybind_capture_remains_unchanged()
{
	BindCaptureState state;
	expect(state.arm(3), "ordinary keybind capture still arms through the shared seam");
	expect(state.armed() && !state.action_armed(), "keybind capture is distinct from Action capture");
	expect(state.apply_down_edge(false) == CaptureApply::rebound,
		"ordinary keybind capture still consumes its next down edge");
	expect(!state.armed() && !state.action_armed(), "ordinary keybind capture disarms after its edge");
}

}  // namespace

int main()
{
	action_capture_waits_for_a_supported_button_edge();
	a_zero_dx_scancode_is_ignored_and_leaves_capture_armed();
	escape_cancels_action_capture_without_a_result();
	keybind_capture_remains_unchanged();
	return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
