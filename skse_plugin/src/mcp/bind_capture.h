#pragma once

#include <cstdint>
#include <optional>

#include "../game_data/action_definition.h"

namespace SpellHotbar::Mcp {
    inline constexpr int k_first_keybind_id = 0;
    inline constexpr int k_last_keybind_id = 22;

    enum class CaptureApply : unsigned char {
        ignored,
        cancelled,
        rebound
    };

    struct ActionCaptureResult {
        std::uint32_t action_id{ 0 };
        ActionInput input{};
    };

    class BindCaptureState {
    public:
        bool arm(int key_id)
        {
            if (key_id < k_first_keybind_id || key_id > k_last_keybind_id) {
                return false;
            }
            pending_id_ = key_id;
            pending_action_id_ = 0;
            return true;
        }

        bool arm_action(std::uint32_t action_id)
        {
            if (action_id == 0) {
                return false;
            }
            pending_id_ = -1;
            pending_action_id_ = action_id;
            action_result_.reset();
            return true;
        }

        void cancel()
        {
            pending_id_ = -1;
            pending_action_id_ = 0;
        }

        bool armed() const { return pending_id_ >= 0; }

        bool action_armed() const { return pending_action_id_ != 0; }

        int pending_id() const { return pending_id_; }

        std::uint32_t pending_action_id() const { return pending_action_id_; }

        bool consume_escape()
        {
            if (!armed()) {
                return false;
            }
            cancel();
            return true;
        }

        bool consume_rebind()
        {
            if (!armed()) {
                return false;
            }
            cancel();
            return true;
        }

        CaptureApply apply_down_edge(bool is_escape)
        {
            if (!armed()) {
                return CaptureApply::ignored;
            }
            if (is_escape) {
                consume_escape();
                return CaptureApply::cancelled;
            }
            consume_rebind();
            return CaptureApply::rebound;
        }

        // Action capture uses the same one-down-edge policy as ordinary key rebinds, but keeps
        // the native device alongside the existing device-independent DX value. Escape cancels
        // without changing the draft. A non-positive DX value is treated as an ignored event so
        // an unsupported device cannot silently bind an Action to zero.
        CaptureApply apply_action_down_edge(
            bool is_escape, ActionInputDevice device, int dx_scancode)
        {
            if (!action_armed()) {
                return CaptureApply::ignored;
            }
            if (is_escape) {
                cancel();
                return CaptureApply::cancelled;
            }
            if (dx_scancode <= 0) {
                return CaptureApply::ignored;
            }

            action_result_ = ActionCaptureResult{
                pending_action_id_, ActionInput{ device, static_cast<std::uint32_t>(dx_scancode) } };
            cancel();
            return CaptureApply::rebound;
        }

        [[nodiscard]] std::optional<ActionCaptureResult> take_action_capture_result(
            std::uint32_t action_id)
        {
            if (!action_result_ || action_result_->action_id != action_id) {
                return std::nullopt;
            }
            auto result = action_result_;
            action_result_.reset();
            return result;
        }

        void discard_action_capture_result(std::uint32_t action_id)
        {
            if (action_result_ && action_result_->action_id == action_id) {
                action_result_.reset();
            }
        }

    private:
        int pending_id_{-1};
        std::uint32_t pending_action_id_{ 0 };
        std::optional<ActionCaptureResult> action_result_;
    };

    BindCaptureState& bind_capture();
}
