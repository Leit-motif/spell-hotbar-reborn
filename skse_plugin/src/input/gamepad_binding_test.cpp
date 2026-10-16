#include "gamepad_binding_io.h"
#include "../mcp/bind_capture.h"
#include "../game_data/gamepad_key_names.h"
#include <cstdlib>
#include <iostream>

using namespace SpellHotbar::Input;
namespace {
    int failures = 0;
    void expect(bool value, const char* message)
    {
        if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
    }
    constexpr int A = 276, B = 277, LT = 280, RT = 281, LB = 274;
    const std::array bindings{
        GamepadBinding{ A, 0 }, GamepadBinding{ A, gamepad_bit(LT) },
        GamepadBinding{ A, gamepad_bit(RT) },
        GamepadBinding{ A, static_cast<std::uint16_t>(gamepad_bit(LT) | gamepad_bit(RT)) },
        GamepadBinding{ B, gamepad_bit(LB) }
    };

    void press(GamepadPresses& state, int token, int code, bool down, bool held, bool consume = false)
    {
        state.raw(token, code, down, held, bindings, true);
        state.filtered(token, down, held);
        if (consume) { state.consume(); }
    }

    void combinations_are_distinct_and_only_the_final_button_is_consumed()
    {
        for (int expected = 0; expected < 4; ++expected) {
            GamepadPresses state;
            state.begin_frame();
            if (expected & 1) { press(state, 1, LT, true, true); expect(!state.consumed() && state.binding_id() == -1, "LT remains native input"); }
            if (expected & 2) { press(state, 2, RT, true, true); expect(!state.consumed() && state.binding_id() == -1, "RT remains native input"); }
            press(state, 3, A, true, true, true);
            expect(state.binding_id() == expected && state.binding_held(expected), "the correct A combination owns the down");
            press(state, 4, A, false, true);
            expect(state.consumed() && state.binding_id() == expected, "the same final repeat remains consumed");
            expect(state.raw(5, A, false, false, bindings, true), "the final up is consumed before host filtering");
            expect(!state.binding_held(expected), "physical final up ends the binding hold");
            expect(!state.raw(6, LT, false, false, bindings, true), "modifier up is never captured by the A combination");
            expect(!state.raw(7, RT, false, false, bindings, true), "other modifier up passes through");
        }
        GamepadPresses state;
        press(state, 1, LB, true, true);
        press(state, 2, B, true, true);
        expect(state.binding_id() == 4, "an arbitrary shoulder modifier works without a trigger special case");
    }

    void releases_and_rebinds_do_not_redirect_a_held_final()
    {
        GamepadPresses state;
        press(state, 1, LT, true, true);
        press(state, 2, A, true, true, true);
        press(state, 3, LT, false, false);
        expect(state.binding_held(1) && !state.consumed(), "modifier-first release passes through and does not strand the final hold");
        press(state, 4, RT, true, true);
        press(state, 5, A, false, true);
        expect(state.binding_id() == 1 && state.consumed(), "changing modifiers cannot retarget a held final");
        state.cancel_binding(1);
        expect(!state.binding_held(1), "rebinding ends the old logical hold");
        expect(state.raw(6, A, false, false, bindings, true), "rebind preserves final-up capture");
        state.reset();
        expect(state.held() == 0 && !state.binding_held(1), "load reset clears physical and logical holds");
    }

    void raw_batch_order_and_host_filtering_are_respected()
    {
        GamepadPresses state;
        state.begin_frame();
        state.raw(1, LT, true, true, bindings, true);
        state.raw(2, A, true, true, bindings, true);
        state.raw(3, LT, false, false, bindings, true);
        state.filtered(1, true, true);
        state.filtered(2, true, true);
        expect(state.binding_id() == 1, "later raw modifier release cannot change an earlier final-down snapshot");
        state.consume();
        state.filtered(3, false, false);
        expect(!state.consumed() && state.binding_held(1), "a batched modifier release remains independent");

        state.reset();
        state.raw(1, LT, true, true, bindings, true);
        state.raw(2, A, true, true, bindings, true);
        state.raw(3, A, false, false, bindings, true);
        state.filtered(1, true, true);
        state.filtered(2, true, true);
        state.consume();
        state.filtered(3, false, false);
        expect(state.binding_id() == 1 && state.consumed(), "a down and up in one batch preserve final-button consumption");
        state.consume();
        expect(!state.binding_held(1), "consuming a same-batch up does not resurrect a held press");

        state.reset();
        state.raw(1, A, true, true, bindings, true);
        state.filtered(1, false, false);
        expect(state.binding_id() == -1 && !state.binding_held(0), "host-neutralized down never activates a binding");
        state.filtered(999, true, true);
        expect(state.binding_id() == -1, "injected events cannot borrow another event's combination snapshot");
    }

    void specificity_and_invalid_combinations_are_safe()
    {
        const auto held = static_cast<std::uint16_t>(gamepad_bit(LT) | gamepad_bit(RT) | gamepad_bit(A));
        expect(GamepadPresses::select(A, held, bindings) == 3, "LT+RT+A takes precedence over the two shorter combinations");
        expect(GamepadPresses::select(A, held, std::span(bindings).first(3)) == -1, "equal-specificity overlaps do not choose an arbitrary slot");
        expect(!GamepadBinding{ A, gamepad_bit(A) }.valid(), "the final button cannot also be its own modifier");
        expect(!GamepadBinding{ 35, gamepad_bit(LT) }.valid(), "gamepad combinations reject keyboard final buttons");
    }

    void capture_commits_on_the_final_release_and_ignores_activation_release()
    {
        SpellHotbar::Mcp::BindCaptureState capture;
        capture.arm(0);
        expect(!capture.apply_gamepad_button(A, false, true, 0), "Rebind's activation-button release does not become the new binding");
        expect(!capture.apply_gamepad_button(LT, true, false, gamepad_bit(LT)), "first modifier does not prematurely finish capture");
        const auto both = static_cast<std::uint16_t>(gamepad_bit(LT) | gamepad_bit(RT));
        expect(!capture.apply_gamepad_button(RT, true, false, both), "second modifier keeps capture armed");
        capture.apply_gamepad_button(A, true, false, both | gamepad_bit(A));
        capture.apply_gamepad_button(LT, false, true, gamepad_bit(RT) | gamepad_bit(A));
        const auto result = capture.apply_gamepad_button(A, false, true, gamepad_bit(RT));
        expect(result && *result == GamepadBinding{ A, both }, "capture preserves modifiers at final down, regardless of release order");
        expect(!capture.armed(), "final release completes capture");
        capture.arm(1);
        capture.apply_gamepad_button(B, true, false, gamepad_bit(B));
        expect(capture.apply_gamepad_button(B, false, true, 0) == GamepadBinding{ B, 0 }, "single gamepad buttons still bind");
        capture.arm(2);
        capture.apply_gamepad_button(LT, true, false, gamepad_bit(LT));
        capture.apply_down_edge(true);
        expect(!capture.armed(), "Escape cancels an unfinished combination");
    }

    void persistence_is_bounded_transactional_and_legacy_compatible()
    {
        const std::vector rows{ GamepadBindingRecord{ 0, bindings[1] }, GamepadBindingRecord{ 1, bindings[2] }, GamepadBindingRecord{ 2, bindings[3] } };
        const auto bytes = encode_gamepad_bindings(rows);
        std::vector<GamepadBindingRecord> restored;
        expect(decode_gamepad_bindings(bytes, restored) && restored == rows, "co-save metadata round trips all combinations");
        for (std::size_t size = 0; size < bytes.size(); ++size) {
            expect(!decode_gamepad_bindings(std::span(bytes).first(size), restored), "truncated co-save metadata is rejected");
            expect(restored == rows, "failed decoding cannot partially overwrite bindings");
        }
        auto bad = bytes;
        bad[6] = bad[1];
        expect(!decode_gamepad_bindings(bad, restored), "duplicate binding ids are rejected");
        rapidjson::Document preset;
        preset.SetObject();
        write_gamepad_bindings_json(preset, rows);
        expect(read_gamepad_bindings_json(preset, restored) && restored == rows, "preset metadata round trips independently of member order");
        preset[gamepad_json_key][0]["modifiers"].PushBack(LT, preset.GetAllocator());
        expect(!read_gamepad_bindings_json(preset, restored), "malformed preset modifiers are rejected before settings change");
        preset.SetObject();
        expect(read_gamepad_bindings_json(preset, restored) && restored.empty(), "old presets have no implicit combinations");
        const std::vector<GamepadBindingRecord> duplicate{ rows[0], { 3, rows[0].binding } };
        expect(!valid_gamepad_records(duplicate), "exact duplicate combinations are invalid, while shared final buttons are valid");
    }
}
int main()
{
    using SpellHotbar::GameData::unambiguous_gamepad_key_name;
    expect(unambiguous_gamepad_key_name(274, "LS") == "LB", "left shoulder uses the bumper label, matching its native button and icon");
    expect(unambiguous_gamepad_key_name(275, "RS") == "RB", "right shoulder uses the bumper label, matching its native button and icon");
    expect(unambiguous_gamepad_key_name(272, "LTh") == "LTh", "stick-click labels retain their distinct native button");
    expect(unambiguous_gamepad_key_name(274, "L1") == "L1", "custom platform-specific shoulder labels are preserved");
    expect(unambiguous_gamepad_key_name(35, "LS") == "LS", "keyboard key names are unchanged");
    combinations_are_distinct_and_only_the_final_button_is_consumed();
    releases_and_rebinds_do_not_redirect_a_held_final();
    raw_batch_order_and_host_filtering_are_respected();
    specificity_and_invalid_combinations_are_safe();
    capture_commits_on_the_final_release_and_ignores_activation_release();
    persistence_is_bounded_transactional_and_legacy_compatible();
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
