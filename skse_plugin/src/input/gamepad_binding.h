#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>

namespace SpellHotbar::Input {
    inline constexpr int gamepad_first = 266;
    inline constexpr int gamepad_count = 16;

    constexpr bool is_gamepad_button(int code) { return code >= gamepad_first && code < gamepad_first + gamepad_count; }
    constexpr std::uint16_t gamepad_bit(int code)
    {
        return is_gamepad_button(code) ? static_cast<std::uint16_t>(1U << (code - gamepad_first)) : 0;
    }

    struct GamepadBinding {
        int button{ -1 };
        std::uint16_t modifiers{ 0 };

        constexpr bool valid() const { return is_gamepad_button(button) && !(modifiers & gamepad_bit(button)); }
        bool operator==(const GamepadBinding&) const = default;
    };

    // Physical ownership is chosen on the final down, never recomputed on repeat/up.
    // This is also the final button's consumption latch: modifier events have their own
    // independent entries and are not captured merely because a combination uses them.
    class GamepadPresses {
    public:
        void reset() { *this = GamepadPresses{}; }
        std::uint16_t held() const { return held_; }
        int binding_id() const { return event_owner_; }
        bool consumed() const { return event_consumed_; }
        void begin_frame() { events_.clear(); event_owner_ = -1; event_consumed_ = false; }

        static int select(int button, std::uint16_t held, std::span<const GamepadBinding> bindings)
        {
            int binding_id = -1;
            int best = -1;
            bool ambiguous = false;
            for (int i = 0; i < static_cast<int>(bindings.size()); ++i) {
                const auto& binding = bindings[i];
                if (!binding.valid() || binding.button != button || (held & binding.modifiers) != binding.modifiers) { continue; }
                const int specificity = std::popcount(binding.modifiers);
                if (specificity > best) { best = specificity; binding_id = i; ambiguous = false; }
                else if (specificity == best) { ambiguous = true; }
            }
            return ambiguous ? -1 : binding_id;
        }

        // The whole raw event list runs before the whole filtered list. Snapshot each event,
        // rather than reading the last raw event's state when processing an earlier final down.
        bool raw(std::uintptr_t token, int button, bool down, bool pressed,
            std::span<const GamepadBinding> bindings, bool allow_start)
        {
            event_owner_ = -1;
            event_consumed_ = false;
            event_button_ = button;
            event_pressed_ = pressed;
            if (!is_gamepad_button(button)) { return false; }
            auto& press = presses_[button - gamepad_first];
            if (pressed) { held_ |= gamepad_bit(button); }
            else { held_ &= static_cast<std::uint16_t>(~gamepad_bit(button)); }
            if (down) { press = {}; } // A new physical down also clears a missed release.
            Event snapshot{ button, down, pressed, down ? (allow_start ? select(button, held_, bindings) : -1) : press.binding_id };
            events_[token] = snapshot;
            event_owner_ = down ? snapshot.candidate : press.binding_id;
            event_consumed_ = press.consumed;
            if (!pressed) {
                const bool consumed = press.consumed;
                press = {};
                return consumed; // Owned releases must survive a host/menu that swallows its list.
            }
            return false;
        }

        void filtered(std::uintptr_t token, bool down, bool pressed)
        {
            event_owner_ = -1;
            event_consumed_ = false;
            event_button_ = -1;
            event_pressed_ = false;
            const auto it = events_.find(token);
            if (it == events_.end()) { return; } // An injected event has no physical snapshot.
            const auto& event = it->second;
            event_button_ = event.button;
            event_pressed_ = pressed;
            auto& press = presses_[event.button - gamepad_first];
            if (event.down) {
                if (!down) { return; } // The host filtered/neutralized the physical down.
                press.binding_id = event.candidate;
            }
            event_owner_ = !pressed && press.binding_id < 0 ? event.candidate : press.binding_id;
            event_consumed_ = press.consumed;
            if (!pressed) { press = {}; }
        }

        void consume()
        {
            if (is_gamepad_button(event_button_) && event_pressed_) {
                auto& press = presses_[event_button_ - gamepad_first];
                press.binding_id = event_owner_;
                press.consumed = true;
            }
            event_consumed_ = true;
        }

        bool binding_held(int id) const
        {
            for (const auto& press : presses_) { if (press.binding_id == id) { return true; } }
            return false;
        }

        void cancel_binding(int id)
        {
            for (auto& press : presses_) {
                // Keep consumption until the physical final up even after a rebind.
                if (press.binding_id == id) { press.binding_id = -1; }
            }
            if (event_owner_ == id) { event_owner_ = -1; }
            for (auto& [token, event] : events_) {
                if (event.candidate == id) { event.candidate = -1; }
            }
        }

    private:
        struct Press { int binding_id{ -1 }; bool consumed{ false }; };
        struct Event { int button; bool down; bool pressed; int candidate; };
        std::array<Press, gamepad_count> presses_{};
        std::unordered_map<std::uintptr_t, Event> events_;
        std::uint16_t held_{ 0 };
        int event_button_{ -1 };
        int event_owner_{ -1 };
        bool event_consumed_{ false };
        bool event_pressed_{ false };
    };

    class GamepadCapture {
    public:
        void reset() { candidate_.reset(); }
        std::optional<GamepadBinding> observe(int button, bool down, bool up, std::uint16_t held)
        {
            if (!is_gamepad_button(button)) { return std::nullopt; }
            if (down) { candidate_ = GamepadBinding{ button, static_cast<std::uint16_t>(held & ~gamepad_bit(button)) }; }
            if (up && candidate_ && candidate_->button == button) {
                auto result = candidate_;
                reset();
                return result;
            }
            return std::nullopt;
        }
    private:
        std::optional<GamepadBinding> candidate_;
    };
}
