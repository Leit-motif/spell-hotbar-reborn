#include "input.h"
#include <chrono>
#include <optional>
#include <utility>
#include <unordered_map>
#include "input_event_adapter.h"
#include "keybinds.h"
#include "../flick/flick_watch.h"
#include "../logger/logger.h"
#include "../mcp/bind_capture.h"
#include "../rendering/render_manager.h"
#include "../casts/casting_controller.h"
#include "../casts/combo_cache.h"
#include "../casts/msco_cast_driver.h"
#include "../casts/art_driver.h"
#include "../storage/storage.h"
#include "modes.h"
#include "../game_data/action_definition.h"

namespace {
    thread_local SpellHotbar::Input::InputEventAdapter<RE::InputEvent> input_event_adapter;
}
namespace SpellHotbar::Input {

    inline constexpr std::tuple<uint32_t, RE::INPUT_DEVICE> get_device_and_input(const RE::ButtonEvent* bEvent) {
        uint32_t key = 0;
        RE::INPUT_DEVICE dev = RE::INPUT_DEVICE::kNone;
        if (bEvent->GetDevice() == RE::INPUT_DEVICE::kKeyboard) {
            key = bEvent->GetIDCode();
            dev = RE::INPUT_DEVICE::kKeyboard;
        }
        else if (bEvent->GetDevice() == RE::INPUT_DEVICE::kMouse) {
            key = bEvent->GetIDCode();
            dev = RE::INPUT_DEVICE::kMouse;
        }
        else if (bEvent->GetDevice() == RE::INPUT_DEVICE::kGamepad) {
            dev = RE::INPUT_DEVICE::kGamepad;
            //Map Gamepad keys to 0-x
            switch (bEvent->GetIDCode()) {
            case RE::BSWin32GamepadDevice::Keys::Key::kUp:
                key = 0;
                break;
            case RE::BSWin32GamepadDevice::Keys::Key::kDown:
                key = 1;
                break;
            case RE::BSWin32GamepadDevice::Keys::Key::kLeft:
                key = 2;
                break;
            case RE::BSWin32GamepadDevice::Keys::Key::kRight:
                key = 3;
                break;
            case RE::BSWin32GamepadDevice::Keys::Key::kStart:
                key = 4;
                break;
            case RE::BSWin32GamepadDevice::Keys::Key::kBack:
                key = 5;
                break;
            case RE::BSWin32GamepadDevice::Keys::Key::kLeftThumb:
                key = 6;
                break;
            case RE::BSWin32GamepadDevice::Keys::Key::kRightThumb:
                key = 7;
                break;
            case RE::BSWin32GamepadDevice::Keys::Key::kLeftShoulder:
                key = 8;
                break;
            case RE::BSWin32GamepadDevice::Keys::Key::kRightShoulder:
                key = 9;
                break;
            case RE::BSWin32GamepadDevice::Keys::Key::kA:
                key = 10;
                break;
            case RE::BSWin32GamepadDevice::Keys::Key::kB:
                key = 11;
                break;
            case RE::BSWin32GamepadDevice::Keys::Key::kX:
                key = 12;
                break;
            case RE::BSWin32GamepadDevice::Keys::Key::kY:
                key = 13;
                break;
            case RE::BSWin32GamepadDevice::Keys::Key::kLeftTrigger:
                key = 14;
                break;
            case RE::BSWin32GamepadDevice::Keys::Key::kRightTrigger:
                key = 15;
                break;
            default:
                //Invalid key
                key = 0;
                dev = RE::INPUT_DEVICE::kNone;
            }
        }

        return std::make_tuple(key, dev);
    }

    std::optional<ActionInputDevice> action_input_device(RE::INPUT_DEVICE device)
    {
        switch (device) {
        case RE::INPUT_DEVICE::kKeyboard:
            return ActionInputDevice::keyboard;
        case RE::INPUT_DEVICE::kMouse:
            return ActionInputDevice::mouse;
        case RE::INPUT_DEVICE::kGamepad:
            return ActionInputDevice::gamepad;
        default:
            return std::nullopt;
        }
    }

    namespace {

        uint32_t get_left_attack_key(RE::INPUT_DEVICE key_device)
        {
            if (key_device != RE::INPUT_DEVICE::kKeyboard && key_device != RE::INPUT_DEVICE::kMouse) {
                return RE::ControlMap::kInvalid;
            }
            auto control_map = RE::ControlMap::GetSingleton();
            auto user_events = RE::UserEvents::GetSingleton();
            if (!control_map || !user_events) {
                return RE::ControlMap::kInvalid;
            }
            return control_map->GetMappedKey(user_events->leftAttack, key_device);
        }

        bool left_hand_holds_spell(RE::PlayerCharacter* pc)
        {
            if (!pc) {
                return false;
            }
            auto* obj = pc->GetEquippedObject(true);
            return obj && (obj->Is(RE::FormType::Spell) || obj->Is(RE::FormType::Scroll));
        }

        // A left-hand cast press during a committed hotbar cast: the left control is block
        // when the left hand holds a weapon or shield, so this only matches when it would
        // actually start an MSCO hand cast.
        bool is_left_hand_cast_press(RE::PlayerCharacter* pc, uint32_t key_code, RE::INPUT_DEVICE key_device)
        {
            const uint32_t left_key = get_left_attack_key(key_device);
            return casts::cut_committed_cast_for_left_hand_press(
                casts::CastingController::is_committed_cast_holding_graph(),
                left_hand_holds_spell(pc),
                left_key != RE::ControlMap::kInvalid && key_code == left_key);
        }
    }

    static InputEventDecision<RE::InputEvent> process_event_impl(RE::InputEvent* event)
    {
        if (!event) {
            return {};
        }

        bool action_event_handled = false;

        // Action targets are an event bridge, so their release must not depend on the current
        // mode, modifier, menu, capture state, or a still-loaded player.  Match the original DX
        // source key before those filters can change and mirror the physical lifecycle onto the
        // target frozen by try_start_action.
        if (event->eventType == RE::INPUT_EVENT_TYPE::kButton) {
            if (auto* button_event = event->AsButtonEvent()) {
                const auto [key_code, key_device] = get_device_and_input(button_event);
                const int trigger_dx_scancode = input_to_dx_scancode(
                    key_device, static_cast<uint8_t>(key_code));
                if (trigger_dx_scancode >= 0) {
                    if (button_event->IsUp()) {
                        action_event_handled = casts::CastingController::release_action_for_trigger(
                            static_cast<std::uint32_t>(trigger_dx_scancode), button_event->HeldDuration());
                    } else if (button_event->IsRepeating()) {
                        action_event_handled = casts::CastingController::mirror_action_hold(
                            static_cast<std::uint32_t>(trigger_dx_scancode), button_event->Value(),
                            button_event->HeldDuration());
                    }
                }
            }
        }

        //don't react to inputs outside of the game:
        auto pc = RE::PlayerCharacter::GetSingleton();
        if (!pc || !pc->Is3DLoaded()) {
            // The mirror above already answered this event; forwarding it too would send the
            // source key's own binding an up or repeat the Action already consumed.
            return InputEventDecision<RE::InputEvent>{ .capture = action_event_handled };
        }

        RE::InputEvent* addEvent = nullptr;
        auto [shoutKeyDev, shoutKey] = get_shout_key_and_device();

        bool captureEvent = action_event_handled; // Capture this event? (do not forward to Skyrim)

            // SMF runs this callback before TranslateInputEvent. Returning true
            // unlinks the event from the queue, so ImGui never sees the click.
            // Blocking SH2 windows already pause the game through
            // WindowInterface::BlockUserInput; do not capture cursor/key events
            // for that case. Bind-capture still consumes the next down edge.
            if (event->eventType == RE::INPUT_EVENT_TYPE::kButton) {
                RE::ButtonEvent* bEvent = event->AsButtonEvent();
                if (bEvent) {
                    auto [key_code, key_device] = get_device_and_input(bEvent);
                    bool is_pressed = bEvent->IsPressed();

                    auto& capture = Mcp::bind_capture();
                    // Armed Action capture swallows every button event, so the mouse cannot reach
                    // the dialog's Cancel button: any click is itself a candidate binding, and a
                    // capture that let one through would bind the button the player used to stop.
                    // Escape is the cancel, and the armed prompt says so.
                    if (capture.action_armed()) {
                        if (bEvent->IsDown()) {
                            const bool is_escape =
                                key_device == RE::INPUT_DEVICE::kKeyboard && key_code == 1;
                            if (is_escape) {
                                capture.apply_action_down_edge(true, ActionInputDevice::keyboard, -1);
                            } else if (const auto device = action_input_device(key_device)) {
                                const int dx = input_to_dx_scancode(
                                    key_device, static_cast<uint8_t>(key_code));
                                capture.apply_action_down_edge(false, *device, dx);
                            }
                        }
                        captureEvent = true;
                    } else if (capture.armed()) {
                        if (bEvent->IsDown()) {
                            const bool is_escape =
                                key_device == RE::INPUT_DEVICE::kKeyboard && key_code == 1;
                            const bool rebindable =
                                key_device == RE::INPUT_DEVICE::kKeyboard ||
                                key_device == RE::INPUT_DEVICE::kMouse ||
                                key_device == RE::INPUT_DEVICE::kGamepad;
                            if (is_escape) {
                                capture.apply_down_edge(true);
                            } else if (rebindable) {
                                const int pending_id = capture.pending_id();
                                if (capture.apply_down_edge(false) == Mcp::CaptureApply::rebound) {
                                    const int dx = input_to_dx_scancode(
                                        key_device, static_cast<uint8_t>(key_code));
                                    if (dx >= 0) {
                                        rebind_key(pending_id, dx);
                                    }
                                }
                            }
                        }
                        captureEvent = true;
                    } else {
                    if (key_device == RE::INPUT_DEVICE::kKeyboard) {
                        if (key_code == 56 || key_code == 184) {
                            mod_alt.update(key_code, key_device, is_pressed);
                        }
                    }
                    mod_1.update(key_code, key_device, is_pressed);
                    mod_2.update(key_code, key_device, is_pressed);
                    mod_3.update(key_code, key_device, is_pressed);
                    mod_dual_cast.update(key_code, key_device, is_pressed);
                    mod_show_bar.update(key_code, key_device, is_pressed);

                    //update all keybind states
                    for (size_t i = 0; i < key_spells.size(); ++i) {
                        key_spells[i].update(key_code, key_device, is_pressed);
                    }
                    key_oblivion_cast.update(key_code, key_device, is_pressed);
                    key_oblivion_potion.update(key_code, key_device, is_pressed);

                    const bool smf_blocking = RenderManager::should_block_game_key_inputs();

                    if (RenderManager::is_dragging_bar() && key_device == RE::INPUT_DEVICE::kKeyboard && key_code == 1 && bEvent->IsDown()) {
                        RenderManager::stop_bar_dragging();
                    }

                    // The key that dismissed an SH2 window, remembered until its release. Both
                    // edges have to be swallowed: capturing only the down edge hands the engine
                    // an up edge for a press it never saw, and the game is free to act on either.
                    static std::optional<std::pair<RE::INPUT_DEVICE, uint32_t>> dismiss_key;

                    if (smf_blocking && bEvent->IsDown()) {
                        const bool is_escape =
                            key_device == RE::INPUT_DEVICE::kKeyboard && key_code == 1;
                        // Typing "Fire Slash" into an Ability's name field must not close the
                        // menu on the H. A focused text field owns every letter; Escape stays
                        // the one key that still dismisses the window.
                        const bool is_bind_menu_key =
                            RenderManager::is_bind_menu_opened() &&
                            !Flick::host_is_taking_keystrokes() &&
                            Input::key_open_advanced_bind_menu.isValidBound() &&
                            Input::key_open_advanced_bind_menu.matches(key_code, key_device);

                        if (is_escape || is_bind_menu_key) {
                            RenderManager::close_key_blocking_frames();
                            // Escape must close the window without also opening the main menu.
                            // Closing the window is not enough on its own. SMF pauses
                            // the game through BlockUserInput only while one of our windows is
                            // open, so the close above ends the block on this very frame and the
                            // press travels on to the engine, which reads Escape as Journal. The
                            // key that dismissed our UI belongs to us, so take it off the queue.
                            dismiss_key = std::make_pair(key_device, key_code);
                            captureEvent = true;
                        }
                    }
                    else if (dismiss_key && dismiss_key->first == key_device &&
                             dismiss_key->second == key_code) {
                        if (bEvent->IsDown()) {
                            // A fresh press with no window of ours open, which means the release
                            // we were waiting for never arrived -- losing window focus mid-press
                            // is enough to eat one. Drop the latch and let this press through;
                            // the alternative is swallowing the player Escape key for good.
                            dismiss_key.reset();
                        } else {
                            // The matching release, or a repeat on the way to it.
                            if (bEvent->IsUp()) {
                                dismiss_key.reset();
                            }
                            captureEvent = true;
                        }
                    }

                    // Cut a committed cast for a LEFT-HAND cast press. The attack half of this
                    // cut lives at the send-side seam in `events/attack_seam_hook.cpp`: reading
                    // OCPA's ini to work out whether a key was a power attack is brittle and can
                    // never cover a source SH2 has not heard of, so that cut happens where the
                    // attack reaches the player's graph and every source looks alike. What is
                    // left here is the one press that is NOT an attack and therefore never
                    // reaches that seam.
                    //
                    // A left-hand cast press starts an MSCO hand cast, not a swing, so it sends
                    // no attack graph event at all -- but it still needs the shtb state out of the
                    // way for the same reason an attack does: the state has no transition to it,
                    // and a press during the cast is silently refused for the whole clip
                    // (live-verified).
                    //
                    // The press itself is not touched: it travels the rest of this dispatch and
                    // reaches the game exactly as it does today. Capturing it and re-queuing a
                    // copy to buy the graph a frame does not work, because this hook runs
                    // inside PollInputDevices and PushOntoInputQueue
                    // appends to the very chain being dispatched. Both events reach the graph's
                    // queue in the order they were sent, which is the ordering the cut needs and
                    // the only one available. Leaving the press alone is also what makes this
                    // fail-safe: a graph that refuses the cut gives the player today's behaviour
                    // rather than a swallowed attack.
                    //
                    // The gate is BOTH halves of the cuttable span. The instance retires at GCD
                    // expiry, so `is_committed_cast_holding_graph` -- which needs a live
                    // `current_cast` -- goes false while the clip plays on, and the follow-through
                    // is exactly the tail this cut exists to use. The follow-through predicate
                    // covers retirement to clip end; together they run
                    // from the commitment point to the end of the clip. A cast still CHARGING has
                    // a live instance and no commitment, so neither half admits it and a press
                    // then keeps today's behaviour.
                    if (!captureEvent && pc && bEvent->IsDown() && in_ingame_state() &&
                        (casts::CastingController::is_committed_cast_holding_graph() ||
                         casts::CastingController::is_cuttable_follow_through()))
                    {
                        // Traced for every press during a cast, matching or not, because this is
                        // the one branch scripted input cannot drive -- injected input never
                        // reaches this hook (verified), so a real keypress is the only test and
                        // it has to say why it failed without a second run.
                        logger::trace("SH2 cast: press during a committed cast (device={}, key={})",
                            static_cast<int>(key_device), key_code);

                        // A payload still owed when the cut lands is paid out first, the same as
                        // the five other cut seams. The armed poll's clip-end fallback would catch
                        // it a frame later anyway; delivering here keeps one delivery story and
                        // one log line per payload.
                        if (is_left_hand_cast_press(pc, key_code, key_device)) {
                            casts::CastingController::cut_committed_cast_for_attack(pc);
                        }
                    }

                    // The concentration chain-out and the Ability latch rule also live at the
                    // send-side seam (`events/attack_seam_hook.cpp`). Each keeps its own
                    // predicate -- `should_cut_channel_for_attack`,
                    // `should_capture_attack_during_ability`, `should_cut_ability_for_attack` --
                    // and is asked about an outgoing graph event rather than a key number.

                    if (!captureEvent && !smf_blocking) {
                        bool handled{ false };
                        if (!Bars::disable_non_modifier_bar || Input::mod_1.isDown() || Input::mod_2.isDown() || Input::mod_3.isDown()) {

                            for (size_t i = 0; i < key_spells.size() && !handled; ++i) {
                                const auto& bind = key_spells[i];

                                if (bind.matches(key_code, key_device))
                                {
                                    if (in_binding_menu())
                                    {
                                        if (!Bars::disable_menu_binding && bEvent->IsDown()) {
                                            handled = true;
                                            RE::TESForm* form = get_current_selected_spell_in_menu();
                                            if (form) {
                                                slot_spell(form, i);
                                            }
                                            if (mod_1.isDown() || mod_2.isDown() || mod_3.isDown()) {
                                                //Do not forward keypress to game if modifier was used, this allows easy double binding with modifiers
                                                captureEvent = true;
                                            }
                                        }
                                    }
                                    else if (in_ingame_state())
                                    {
                                        if (bEvent->IsDown()) {
                                            handled = true;
                                            auto skill = GameData::get_current_spell_info_in_slot(i);
                                            if (GameData::isVampireLord() &&
                                                GameData::global_vampire_lord_equip_mode && GameData::global_vampire_lord_equip_mode->value > 0.0f &&
                                                !Input::is_equip_mode())
                                            {
                                                //If Vampire Lord and using Not Equipmode -> use special VL mode (equip spells & cast powers) instead
                                                //The global can turn of this behaviour
                                                InputModeVampireLord::getSingleton()->process_input(skill, addEvent, i, bind, shoutKeyDev, shoutKey);
                                            }
                                            else if (InputModeBase::current_mode) {
                                                InputModeBase::current_mode->process_input(skill, addEvent, i, bind, shoutKeyDev, shoutKey);
                                            }
                                        }
                                        else if (bEvent->IsUp()) {
                                            handled = true;
                                            //check for release of power/shout key release event
                                            const auto skill = GameData::get_current_spell_info_in_slot(i);
                                            if (skill.type != slot_type::action &&
                                                casts::CastingController::is_currently_using_power()) {

                                                float ct = casts::CastingController::get_current_casttime();
                                                if (!addEvent) {
                                                    addEvent = RE::ButtonEvent::Create(shoutKeyDev, "Shout", shoutKey, 0.0f, ct); //default shout key
                                                }
                                            }
                                        }
                                        else if (bEvent->IsRepeating()) {
                                            //Check in Oblivion Mode for holding slot key down to show bar.
                                            const auto skill = GameData::get_current_spell_info_in_slot(i);
                                            if (skill.type == slot_type::action) {
                                                handled = true;
                                            } else {
                                                InputModeBase::current_mode->process_key_update(bind, i, bEvent->HeldDuration());
                                            }
                                        }
                                        if (handled && (mod_1.isDown() || mod_2.isDown() || mod_3.isDown())) {
                                            //Do not forward keypress to game if modifier was used, this allows easy double binding with modifiers
                                            captureEvent = true;
                                        } else if (handled && in_ingame_state()) {
                                            const auto skill = GameData::get_current_spell_info_in_slot(i);
                                            if (skill.type == slot_type::action) {
                                                // An Action owns its source key for the entire
                                                // lifecycle; forwarding it would fire the original
                                                // engine/mod binding alongside the mirrored target.
                                                captureEvent = true;
                                            } else if (casts::capture_hotbar_press_to_prevent_dual_fire(
                                                skill.type == slot_type::spell, left_hand_holds_spell(pc))) {
                                                captureEvent = true;
                                                logger::debug("SH2 cast: captured hotbar press to prevent dual fire");
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        if (!handled && Input::is_oblivion_mode() && in_ingame_state())
                        {
                            bool cast = key_oblivion_cast.matches(key_code, key_device);
                            bool potion = key_oblivion_potion.matches(key_code, key_device);
                            if (cast || potion)
                            {
                                if (bEvent->IsDown()) {
                                    size_t index = keybind_id::oblivion_potion;
                                    if (cast) {
                                        index = keybind_id::oblivion_cast;
                                    }
                                    handled = true;
                                    auto skill = GameData::get_current_spell_info_in_slot(index);

                                    if (InputModeBase::current_mode) {
                                        if (cast) {
                                            InputModeBase::current_mode->process_input(skill, addEvent, index, key_oblivion_cast, shoutKeyDev, shoutKey);
                                        }
                                        else if (potion) {
                                            InputModeBase::current_mode->process_input(skill, addEvent, index, key_oblivion_potion, shoutKeyDev, shoutKey);
                                        }
                                    }
                                }
                            }
                        }

                        if (!handled && in_binding_menu())
                        {
                            if (key_open_advanced_bind_menu.matches(key_code, key_device) && bEvent->IsDown()) {
                                handled = true;
                                RenderManager::open_advanced_binding_menu();
                                RE::PlaySound(sound_UISkillsForward);
                            }
                            else if (key_next.matches(key_code, key_device) && bEvent->IsDown()) {
                                handled = true;
                                Bars::menu_bar_id = Bars::getNextMenuBar(Bars::menu_bar_id);
                                RE::PlaySound(sound_UISkillsForward);
                            }
                            else if (key_prev.matches(key_code, key_device) && bEvent->IsDown()) {
                                handled = true;
                                Bars::menu_bar_id = Bars::getPreviousMenuBar(Bars::menu_bar_id);
                                RE::PlaySound(sound_UISkillsBackward);
                            }
                        }

                    }
                    }

                }
            }

        return InputEventDecision<RE::InputEvent>{ .capture = captureEvent, .injected = addEvent };
    }

    bool __stdcall process_event(RE::InputEvent* event)
    {
        return input_event_adapter.process(event, process_event_impl);
    }

    KeyModifier::KeyModifier(RE::INPUT_DEVICE device, uint8_t code1, uint8_t code2)
        : input_device(device), keycode(code1), keycode2(code2), isDown1(false), isDown2(false)
    {}

    void KeyModifier::update(uint32_t key_code, RE::INPUT_DEVICE key_device, bool is_pressed)
    {
        if (key_device == input_device) {
            if (keycode > 0 && key_code == keycode)
            {
                isDown1 = is_pressed;
            }
            else if (keycode2 > 0 && key_code == keycode2)
            {
                isDown2 = is_pressed;
            }
        }
    }

    void KeyModifier::rebind(int dx_scancode)
    {
        auto [device, code] = dx_scan_code_to_input(dx_scancode);
        input_device = device;
        keycode = code;
        keycode2 = code;

        if (input_device == RE::INPUT_DEVICE::kKeyboard) {
            //ctrl
            if (keycode == 29 || keycode == 157) {
                keycode = 29;
                keycode2 = 157;
            }
            //shift
            else if (keycode == 42 || keycode == 54) {
                keycode = 42;
                keycode2 = 54;
            }
            //alt
            else if (keycode == 56 || keycode == 184) {
                keycode = 56;
                keycode2 = 184;
            }
        }
        isDown1 = false;
        isDown2 = false;
    }

    int KeyModifier::get_dx_scancode()
    {
        return input_to_dx_scancode(input_device, keycode);
    }

    int KeyModifier::get_dx_scancode2()
    {
        return input_to_dx_scancode(input_device, keycode2);
    }

    bool KeyModifier::isValidBound()
    {
        return input_device == RE::INPUT_DEVICE::kKeyboard || input_device == RE::INPUT_DEVICE::kMouse || input_device == RE::INPUT_DEVICE::kGamepad;
    }


    KeyBind::KeyBind(RE::INPUT_DEVICE device, uint8_t code) : input_device(device), keycode(code), m_isDown(false)
    {
    }

    bool KeyBind::matches(uint32_t key_code, RE::INPUT_DEVICE key_device) const
    {
        return key_device == input_device && key_code == keycode;
    }

    int KeyBind::get_dx_scancode() const
    {
        return input_to_dx_scancode(input_device, keycode);
    }

    void KeyBind::assign_from_dx_scancode(int code)
    {
        auto [dev, key] = dx_scan_code_to_input(code);
        input_device = dev;
        keycode = key;
        m_isDown = false;
    }

    void KeyBind::update(uint32_t key_code, RE::INPUT_DEVICE key_device, bool is_pressed)
    {
        if (matches(key_code, key_device)) {
            m_isDown = is_pressed;
        }
    }

    void KeyBind::unbind()
    {
        input_device = RE::INPUT_DEVICE::kNone;
        keycode = 0Ui8;
        m_isDown = false;
    }

    bool in_ingame_state() {
        const auto ui = RE::UI::GetSingleton();
        
        if (!ui || ui->GameIsPaused() || !ui->IsCursorHiddenWhenTopmost() || !ui->IsShowingMenus() || !ui->GetMenu<RE::HUDMenu>() || ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) || ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME))
        {
            return false;
        }
        // An overlay is outside the menu: FLICK's own config menu and another
        // guest's window are invisible to RE::UI, and this hook now runs ahead of FLICK's own
        // (input_hook.h), so it sees the raw press before the host zeroes it. A hotbar key
        // typed into the host's menu must not cast.
        if (Flick::another_guest_owns_the_screen()) {
            return false;
        }
        return true;
    }

    std::tuple<RE::INPUT_DEVICE, uint8_t> dx_scan_code_to_input(int dx_scancode)
    {
        RE::INPUT_DEVICE input_device{ RE::INPUT_DEVICE::kNone };
        uint8_t keycode{ 0Ui8 };
        if (dx_scancode < 0)
        {
            input_device = RE::INPUT_DEVICE::kNone;
            keycode = 0Ui8;
        }
        else if (dx_scancode < 256) {
            input_device = RE::INPUT_DEVICE::kKeyboard;
            keycode = static_cast<uint8_t>(dx_scancode);
        }
        else if (dx_scancode < 266) {
            input_device = RE::INPUT_DEVICE::kMouse;
            keycode = static_cast<uint8_t>(dx_scancode - 256);
        }
        else {
            input_device = RE::INPUT_DEVICE::kGamepad;
            keycode = static_cast<uint8_t>(dx_scancode - 266);
        }

        return std::make_tuple(input_device, keycode);
    }

    int input_to_dx_scancode(RE::INPUT_DEVICE device, uint8_t code)
    {
        if (device == RE::INPUT_DEVICE::kNone) {
            return -1;
        }

        int offset{ 0 };
        if (device == RE::INPUT_DEVICE::kMouse) {
            offset = 256;
        }
        else if (device == RE::INPUT_DEVICE::kGamepad) {
            offset = 266;
        }
        return static_cast<int>(code) + offset;
    }

    namespace {

        std::optional<uint32_t> gamepad_event_code(uint8_t key)
        {
            using GamepadKey = RE::BSWin32GamepadDevice::Keys::Key;
            switch (key) {
            case 0:
                return static_cast<uint32_t>(GamepadKey::kUp);
            case 1:
                return static_cast<uint32_t>(GamepadKey::kDown);
            case 2:
                return static_cast<uint32_t>(GamepadKey::kLeft);
            case 3:
                return static_cast<uint32_t>(GamepadKey::kRight);
            case 4:
                return static_cast<uint32_t>(GamepadKey::kStart);
            case 5:
                return static_cast<uint32_t>(GamepadKey::kBack);
            case 6:
                return static_cast<uint32_t>(GamepadKey::kLeftThumb);
            case 7:
                return static_cast<uint32_t>(GamepadKey::kRightThumb);
            case 8:
                return static_cast<uint32_t>(GamepadKey::kLeftShoulder);
            case 9:
                return static_cast<uint32_t>(GamepadKey::kRightShoulder);
            case 10:
                return static_cast<uint32_t>(GamepadKey::kA);
            case 11:
                return static_cast<uint32_t>(GamepadKey::kB);
            case 12:
                return static_cast<uint32_t>(GamepadKey::kX);
            case 13:
                return static_cast<uint32_t>(GamepadKey::kY);
            case 14:
                return static_cast<uint32_t>(GamepadKey::kLeftTrigger);
            case 15:
                return static_cast<uint32_t>(GamepadKey::kRightTrigger);
            default:
                return std::nullopt;
            }
        }

        std::optional<RE::INPUT_DEVICE> native_action_device(ActionInputDevice device)
        {
            switch (device) {
            case ActionInputDevice::keyboard:
                return RE::INPUT_DEVICE::kKeyboard;
            case ActionInputDevice::mouse:
                return RE::INPUT_DEVICE::kMouse;
            case ActionInputDevice::gamepad:
                return RE::INPUT_DEVICE::kGamepad;
            }
            return std::nullopt;
        }

    }  // namespace

    // Actions mirror mod hotkeys, so an empty name is the ordinary answer: a mod hotkey is not in
    // the engine's controlmap and has no user-event name to carry. Measured live, every target
    // (47, 48, 79, 81) resolved empty and every one worked. A non-empty name only appears when the
    // target key is also mapped in the engine controlmap, and PlayerControls needs it there.
    std::string resolve_action_user_event(const ActionInput& input)
    {
        if (!input.is_bound() || input.dx_scancode > 281U) {
            return {};
        }

        const auto device = native_action_device(input.device);
        if (!device) {
            return {};
        }

        const auto [decoded_device, decoded_code] =
            dx_scan_code_to_input(static_cast<int>(input.dx_scancode));
        if (decoded_device != *device) {
            return {};
        }

        uint32_t event_code = decoded_code;
        if (*device == RE::INPUT_DEVICE::kGamepad) {
            const auto gamepad_code = gamepad_event_code(decoded_code);
            if (!gamepad_code) {
                return {};
            }
            event_code = *gamepad_code;
        }

        auto* control_map = RE::ControlMap::GetSingleton();
        if (!control_map) {
            return {};
        }
        const auto user_event = control_map->GetUserEventName(event_code, *device);
        if (user_event.empty()) {
            return {};
        }
        return std::string(user_event);
    }

    bool queue_action_event(const ActionInput& input, float value, float held_duration,
        std::string_view user_event)
    {
        auto* queue = RE::BSInputEventQueue::GetSingleton();
        if (!queue) {
            logger::error("SH2 action: BSInputEventQueue missing (device={}, scancode={})",
                static_cast<int>(input.device), input.dx_scancode);
            return false;
        }
        if (!input.is_bound() || input.dx_scancode > 281U) {
            logger::warn("SH2 action: invalid physical target (device={}, scancode={})",
                static_cast<int>(input.device), input.dx_scancode);
            return false;
        }

        const auto device = native_action_device(input.device);
        if (!device) {
            logger::warn("SH2 action: unsupported input device {}", static_cast<int>(input.device));
            return false;
        }

        const auto [decoded_device, decoded_code] =
            dx_scan_code_to_input(static_cast<int>(input.dx_scancode));
        if (decoded_device != *device) {
            logger::warn("SH2 action: device/scancode mismatch (device={}, scancode={})",
                static_cast<int>(input.device), input.dx_scancode);
            return false;
        }

        uint32_t event_code = decoded_code;
        if (*device == RE::INPUT_DEVICE::kGamepad) {
            const auto gamepad_code = gamepad_event_code(decoded_code);
            if (!gamepad_code) {
                logger::warn("SH2 action: unsupported gamepad scancode {}", input.dx_scancode);
                return false;
            }
            event_code = *gamepad_code;
        }

        const auto queued_button_events = static_cast<size_t>(queue->buttonEventCount);
        const auto max_button_events = static_cast<size_t>(RE::BSInputEventQueue::MAX_BUTTON_EVENTS);
        if (queued_button_events >= max_button_events) {
            logger::warn(
                "SH2 action: input queue has no room for event (device={}, scancode={}, queued={}, capacity={})",
                static_cast<int>(*device), input.dx_scancode, queued_button_events, max_button_events);
            return false;
        }

        // AddButtonEvent uses the queue's embedded ButtonEvent storage. This CommonLib build
        // exposes only the four-argument queue helper, so set the user-event field on that same
        // embedded slot immediately after insertion. That keeps the event lifetime owned by
        // Skyrim's queue while still letting PlayerControls handle engine actions such as Block;
        // an empty name is the ordinary case, because a mod hotkey has no controlmap entry.
        //
        // Which slot was just written: RE/B/BSInputEventQueue.h (vendored via vcpkg,
        // CommonLibSSE-NG) has AddButtonEvent(device, id, value, duration) fill
        // buttonEvents[buttonEventCount] and then increment the count, with
        // MAX_BUTTON_EVENTS = 10. So the count moving past the value read before the call
        // identifies the slot this event landed in.
        const std::string user_event_storage{ user_event };
        const RE::BSFixedString native_user_event{ user_event_storage.c_str() };
        queue->AddButtonEvent(*device, static_cast<std::int32_t>(event_code), value,
            held_duration);
        if (queue->buttonEventCount > queued_button_events) {
            queue->GetRuntimeData().buttonEvents[queued_button_events].userEvent = native_user_event;
        }
        const char* phase = value <= 0.0f ? "up" : held_duration > 0.0f ? "held" : "down";
        if (value > 0.0f && held_duration > 0.0f) {
            logger::debug(
                "SH2 action: queued {} (device={}, scancode={}, event_code={}, value={}, held={}, user_event='{}', accepted=true)",
                phase, static_cast<int>(*device), input.dx_scancode, event_code, value, held_duration,
                user_event_storage);
        } else {
            logger::info(
                "SH2 action: queued {} (device={}, scancode={}, event_code={}, value={}, held={}, user_event='{}', accepted=true)",
                phase, static_cast<int>(*device), input.dx_scancode, event_code, value, held_duration,
                user_event_storage);
        }
        return true;
    }

    std::tuple<RE::INPUT_DEVICE, uint8_t> get_shout_key_and_device()
    {
        RE::INPUT_DEVICE dev{ RE::INPUT_DEVICE::kNone };

        auto controlmap = RE::ControlMap::GetSingleton();
        uint32_t shoutkey = 0U;
        if (controlmap) {
            shoutkey = controlmap->GetMappedKey("Shout", RE::INPUT_DEVICE::kKeyboard);
            if (shoutkey >= 255) {
                shoutkey = controlmap->GetMappedKey("Shout", RE::INPUT_DEVICE::kMouse);

                if (shoutkey >= 255) {
                    shoutkey = controlmap->GetMappedKey("Shout", RE::INPUT_DEVICE::kGamepad);

                    if (shoutkey >= 255) {
                        shoutkey = 0;
                    }
                    else {
                        dev = RE::INPUT_DEVICE::kGamepad;
                    }
                }
                else {
                    dev = RE::INPUT_DEVICE::kMouse;
                }
            }
            else {
                dev = RE::INPUT_DEVICE::kKeyboard;
            }
        }
        return std::make_tuple(dev, static_cast<uint8_t>(shoutkey));
    }

    int get_shout_key_dxcode()
    {
       auto [dev, code] = get_shout_key_and_device();
       return input_to_dx_scancode(dev, code);
    }

    bool allowed_to_instantcast(RE::FormID skill)
    {
        auto pc = RE::PlayerCharacter::GetSingleton();
        if (!pc || !pc->Is3DLoaded()) {
            return false;
        }

        if (GameData::is_skill_on_cd(skill)) {
            return false;
        }

        const auto* control_map = RE::ControlMap::GetSingleton();
        if (!control_map || !control_map->IsMovementControlsEnabled())
        {
            return false;
        }

        if (pc->GetOccupiedFurniture()) {
            return false;
        }

        return !pc->IsOnMount();
    }

    bool allowed_to_cast(RE::FormID skill, bool allow_sprint)
    {
        auto pc = RE::PlayerCharacter::GetSingleton();
        if (allowed_to_instantcast(skill) && pc) {
            auto as = pc->AsActorState();

            bool inJumpState{ false };
            //bool bowDrawn{ false };
            pc->GetGraphVariableBool("bInJumpState"sv, inJumpState);
            //pc->GetGraphVariableBool("bInJumpState"sv, bowDrawn); //TODO look for bow anim

            //Check if player currently is casting, also check staffs
            bool isCasting = pc->IsCasting(nullptr);

            const bool sprinting = !allow_sprint && as->IsSprinting();
            const bool swimming = as->IsSwimming();
            if (isCasting || sprinting || swimming || inJumpState) { //|| bowDrawn);
                // Never refuse in silence here: a MagicCaster left charging by an interrupted
                // cast reads as IsCasting for as long as it stays stuck, and a silent refusal
                // costs a long live hunt for something this one line names.
                logger::debug("SH2 cast: refused, casting={} sprinting={} swimming={} jumping={}",
                    isCasting, sprinting, swimming, inJumpState);
                return false;
            }
            return true;
        }
        else return false;
    }

    RE::TESForm* get_current_selected_spell_in_menu()
    {
        RE::UI* ui = RE::UI::GetSingleton();
        if (!ui) return nullptr;
        if (!SpellHotbar::GameData::hasFavMenuSlotBinding()) {
            // code taken from Wheeler
            auto* magMenu = static_cast<RE::MagicMenu*>(ui->GetMenu(RE::MagicMenu::MENU_NAME).get());
            auto* invMenu = static_cast<RE::InventoryMenu*>(ui->GetMenu(RE::InventoryMenu::MENU_NAME).get());
            //bool valid_tab = false;
            
            /*if (invMenu) {
                valid_tab = RenderManager::current_inv_menu_tab_valid_for_hotbar();
            };*/
            if (!magMenu && !invMenu) return nullptr; //&& !valid_tab

            if (magMenu) {
                RE::GFxValue selection;
                magMenu->uiMovie->GetVariable(&selection, "_root.Menu_mc.inventoryLists.itemList.selectedEntry.formId");
                if (selection.GetType() == RE::GFxValue::ValueType::kNumber) {
                    RE::FormID formID = static_cast<std::uint32_t>(selection.GetNumber());
                    return RE::TESForm::LookupByID(formID);
                }
            }
            else if (invMenu) {
                //invMenu->uiMovie->GetVariable(&selection, "_root.Menu_mc.inventoryLists.itemList.selectedEntry.formId");
                RE::ItemList* item_list = invMenu->GetRuntimeData().itemList;
                if (item_list != nullptr) {
                    RE::ItemList::Item* item = item_list->GetSelectedItem();
                    if (item != nullptr && item->data.objDesc != nullptr) {
#undef GetObject // undefine stupid windows definition so GetObject() can be called
                        RE::TESBoundObject* obj = item->data.objDesc->GetObject();
#ifdef UNICODE //redefine it
#define GetObject  GetObjectW
#else
#define GetObject  GetObjectA
#endif // !UNICODE
                        if (obj != nullptr) {
                            RE::FormID formID = obj->GetFormID();
                            return RE::TESForm::LookupByID(formID);
                        }
                    }
                }
            }
        }
        else {
            auto* favMenu = static_cast<RE::FavoritesMenu*>(ui->GetMenu(RE::FavoritesMenu::MENU_NAME).get());
            if (!favMenu) return nullptr;

            auto& root = favMenu->GetRuntimeData().root;

            if (root.GetType() == RE::GFxValue::ValueType::kDisplayObject && root.HasMember("itemList")) {
                RE::GFxValue itemList;
                root.GetMember("itemList", &itemList);

                if (itemList.GetType() == RE::GFxValue::ValueType::kDisplayObject && itemList.HasMember("selectedEntry")) {
                    RE::GFxValue selectedEntry;
                    itemList.GetMember("selectedEntry", &selectedEntry);

                    if (selectedEntry.GetType() == RE::GFxValue::ValueType::kObject && selectedEntry.HasMember("formId")) {
                        RE::GFxValue formId;
                        selectedEntry.GetMember("formId", &formId);

                        if (formId.GetType() == RE::GFxValue::ValueType::kNumber) {
                            RE::FormID formID = static_cast<std::uint32_t>(formId.GetNumber());
                            return RE::TESForm::LookupByID(formID);
                        }
                    }
                }
            }
        }
        return nullptr;
    }

    bool slot_spell(RE::TESForm* form, size_t index)
    {
        if (form != nullptr)
        {
            if (!SpellHotbar::SlottedSkill::is_bindable_form(form->GetFormID())) {
                // Non-castable form (weapon, armor, ingredient, book, ...): refuse at the bind
                // seam so storage, the save and the renderer never see an unusable slot.
                logger::debug("Refused to bind form {:08X}: form type {} is not castable",
                              form->GetFormID(), static_cast<uint32_t>(form->GetFormType()));
                RE::PlaySound(sound_UIMenuCancel);
                return false;
            }

            SpellHotbar::Storage::menu_slot_type slot_type{ Storage::menu_slot_type::magic_menu };
            if (GameData::isVampireLord()) {
                slot_type = Storage::menu_slot_type::vampire_lord;
            }
            else if (GameData::isWerewolf()) {
                slot_type = Storage::menu_slot_type::werewolf;
            }
            else if (SpellHotbar::GameData::isCustomTransform()) {
                auto casttype = SpellHotbar::GameData::getCustomTransformCasttype();
                if (casttype == SpellHotbar::GameData::custom_transform_spell_type::fav_menu ||
                    casttype == SpellHotbar::GameData::custom_transform_spell_type::fav_menu_switch) {
                    slot_type = SpellHotbar::Storage::menu_slot_type::custom_favmenu;
                }
            }
            return SpellHotbar::Storage::slotSpell(form->GetFormID(), index, slot_type);
        }
        return false;
    }

    bool in_binding_menu()
    {
        auto ui = RE::UI::GetSingleton();
        if (!ui) {
            return false;
        }
        const auto* control_map = RE::ControlMap::GetSingleton();
        if (control_map && (control_map->GetRuntimeData().textEntryCount > 0))
        {
            return false;
        }

        // The same guard, for the fields RE::ControlMap never hears about. A FLICK window can sit
        // over an open InventoryMenu -- Fitting Room does exactly that -- and FLICK's text input
        // never calls AllowTextInput, so textEntryCount stays 0 while the player is typing. That
        // is how, without this guard, a bind-menu key bound to C eats the C out of Fitting Room's
        // search box. An overlay is outside the menu: claim no key while one owns the screen.
        if (Flick::another_guest_owns_the_screen() || Flick::host_is_taking_keystrokes())
        {
            return false;
        }

        if (GameData::hasFavMenuSlotBinding())
        {
            return ui->GetMenu(RE::FavoritesMenu::MENU_NAME).get() != nullptr;
        }
        else 
        {
            auto* magMenu = ui->GetMenu(RE::MagicMenu::MENU_NAME).get();
            if (magMenu) {
                return true;
            }
            else {
                return RenderManager::current_inv_menu_tab_valid_for_hotbar();
            }
        }
    }
}
