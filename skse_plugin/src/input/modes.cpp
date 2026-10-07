#include "modes.h"
#include "../logger/logger.h"
#include "../rendering/render_manager.h"
#include "../casts/casting_controller.h"
#include "../casts/cast_intent.h"

namespace SpellHotbar::Input {

    InputModeBase* InputModeBase::current_mode = InputModeCast::getSingleton();

    bool is_oblivion_mode()
    {
        return InputModeBase::current_mode == InputModeOblivion::getSingleton();
    }

    bool is_equip_mode()
    {
        return InputModeBase::current_mode == InputModeEquip::getSingleton();
    }

    int get_current_mode_index()
    {
        if (InputModeBase::current_mode == InputModeOblivion::getSingleton()) {
            return 2;
        }
        else if (InputModeBase::current_mode == InputModeEquip::getSingleton()) {
            return 1;
        }
        return 0;
    }

    void set_input_mode(int index)
    {
        casts::CastingController::release_all_action_inputs();
        if (index == 2) {
            InputModeBase::current_mode = InputModeOblivion::getSingleton();
        }
        else if (index == 1) {
            InputModeBase::current_mode = InputModeEquip::getSingleton();
        }
        else {
            InputModeBase::current_mode = InputModeCast::getSingleton();
        }
    }

	namespace {
		// Green means yes, and a buffered press is not a yes: it is a no until the buffer
		// releases.
		//
		// A press held on the local latch has not started anything and may still expire at
		// `kForeignSwingPressBufferMs` and drop. Painting it the same green as a started cast makes
		// the honest refusal that follows look like a contradiction -- green, then a red border on
		// the next tap of the same burst, which reads as one press being accepted and then
		// blocked.
		//
		// So a buffered press paints RED: as far as the player is concerned, a buffered press is
		// still a no. Green is reserved for a cast that actually went: it
		// arrives from `CastIntent::attempt_release` when the buffer releases into the graph.
		// A buffer that expires instead just leaves the red it already painted.
		void report_press(size_t slot, bool success)
		{
			if (casts::CastIntent::holding_for_leftover_clip()) {
				return;
			}
			const bool started = success && !casts::CastIntent::is_pending();
			SpellHotbar::RenderManager::highlight_skill_slot(static_cast<int>(slot), 0.5, !started);
		}
	}

	void InputModeCast::process_input(SlottedSkill& skill, RE::InputEvent*& addEvent, size_t& i, const KeyBind& bind, RE::INPUT_DEVICE& shoutKeyDev, uint8_t& shoutKey)
	{
		// Actions have no FormID. Their controller path owns the same conservative live-cast gate as
		// the other slot types; only a costless Power Attack can cut a committed Driver Cast.
		if (skill.type == slot_type::action) {
			bool success = false;
			if (allowed_to_instantcast(0)) {
				success = casts::CastingController::try_start_action(skill.action_id, i, bind);
			}
			SpellHotbar::RenderManager::highlight_skill_slot(static_cast<int>(i), 0.5, !success);
			return;
		}

        // One gate for every slot type, as stock SH2 shipped it. A press that arrives while a
        // cast instance is live dies here and paints the red flash in the else-branch -- press,
        // lockout, visible refusal, identical across spells, shouts, arts, powers and potions.
        //
        // The mid-swing art deferral survives because an MCO swing on its own holds no cast
        // instance of ours: the press passes here, and try_start_art defers it when the graph
        // refuses SH2_ArtStart (verified live -- deferred to ShoutMCO mid-swing, released 240ms
        // later, art fired). That is the ordinary case, not a universal rule. A swing can
        // overlap an SH2 instance -- a potion still inside its GCD, a cast or channel cut for the
        // attack but not yet retired -- and such a press is refused rather than deferred. That
        // lockout is intended, not an oversight: `current_cast` is the whole gate, and no slot
        // type buys an exemption from it.
        if (allowed_to_instantcast(skill.formID) && casts::CastingController::can_start_new_cast()) {
            if (skill.type == slot_type::weapon_art) {
                bool success = casts::CastingController::try_start_art(skill.art_id, i, bind);
                report_press(i, success);
            }
            else if (skill.formID > 0) {
                auto form = RE::TESForm::LookupByID(skill.formID);

                if (skill.type == slot_type::spell) {
                    if (allowed_to_cast(skill.formID)) {
                        bool success = casts::CastingController::try_start_cast(form, bind, i, skill.hand);
                        report_press(i, success);
                    }
                    else {
                        SpellHotbar::RenderManager::highlight_skill_slot(static_cast<int>(i), 0.5, true);
                    }
                }
                else if (skill.type == slot_type::shout || skill.type == slot_type::lesser_power || skill.type == slot_type::power)
                {
                    bool can_start{ true };
                    if (skill.type == slot_type::shout) {
                        if (!allowed_to_cast(skill.formID, true)) can_start = false;
                    }

                    if (can_start) {
                        //Start a shout
                        if (!addEvent) { //do not accidentaly create another event

                            if (casts::CastingController::try_cast_power(form, bind, i, skill.hand)) {
                                if (!casts::CastIntent::is_pending()) {
                                    addEvent = RE::ButtonEvent::Create(shoutKeyDev, "Shout", shoutKey, 1.0f, 0.0f); //default shout key
                                }
                            }
                            else {
                                // Stock SH2 never painted this branch -- its outer gate caught
                                // spam first, so the gap was invisible. A voice-recovery or
                                // shout-cooldown refusal reports like every other refusal.
                                SpellHotbar::RenderManager::highlight_skill_slot(static_cast<int>(i), 0.5, true);
                            }
                        }
                    }
                    else {
                        SpellHotbar::RenderManager::highlight_skill_slot(static_cast<int>(i), 0.5, true);
                    }
                }
                else if (skill.type == slot_type::potion) {
                    bool success = casts::CastingController::try_start_cast(form, bind, i, skill.hand);
                    report_press(i, success);
                }
            }
            else {
                //slot not bound
                SpellHotbar::RenderManager::highlight_skill_slot(static_cast<int>(i), 0.25, true);
            }
        }
        else {
            //error highlight
            logger::debug("SH2: slot {} refused by the press gate (type={}, cast live={})", i,
                static_cast<int>(skill.type), !casts::CastingController::can_start_new_cast());
            SpellHotbar::RenderManager::highlight_skill_slot(static_cast<int>(i), 0.5, true);
        }
    }
    InputModeCast* InputModeCast::getSingleton()
    {
        static InputModeCast instance;
        return &instance;
    }

    void InputModeEquip::process_input(SlottedSkill& skill, RE::InputEvent*& addEvent, size_t& i, const KeyBind& bind, RE::INPUT_DEVICE&, uint8_t&)
    {
        if (skill.type == slot_type::action) {
            logger::info("SH2 action: refused in equip mode");
            SpellHotbar::RenderManager::highlight_skill_slot(static_cast<int>(i), 0.5, true);
            return;
        }
        auto pc = RE::PlayerCharacter::GetSingleton();
        if (pc && allowed_to_instantcast(skill.formID)) {
            if (skill.type == slot_type::weapon_art) {
                logger::info("SH2 art: refused in equip mode");
                SpellHotbar::RenderManager::highlight_skill_slot(static_cast<int>(i), 0.5, true);
            }
            else if (skill.formID > 0) {
                auto form = RE::TESForm::LookupByID(skill.formID);

                if (skill.type == slot_type::spell || skill.type == slot_type::lesser_power || skill.type == slot_type::power) {
                    RE::BGSEquipSlot* slot = nullptr;

                    switch (skill.hand) {
                    case hand_mode::auto_hand:
                    case hand_mode::left_hand:
                        slot = GameData::equip_slot_left_hand;
                        break;
                    case hand_mode::right_hand:
                        slot = GameData::equip_slot_right_hand;
                        break;
                    case hand_mode::dual_hand:
                        slot = GameData::equip_slot_both_hand;
                        break;
                    case hand_mode::voice:
                        slot = GameData::equip_slot_voice;
                        break;
                    }

                    RE::ActorEquipManager::GetSingleton()->EquipSpell(pc, form->As<RE::SpellItem>(), slot);
                    SpellHotbar::RenderManager::highlight_skill_slot(static_cast<int>(i), 0.25);
                }
                else if (skill.type == slot_type::shout)
                {
                    RE::ActorEquipManager::GetSingleton()->EquipShout(pc, form->As<RE::TESShout>());
                    SpellHotbar::RenderManager::highlight_skill_slot(static_cast<int>(i), 0.25);
                }
                else if (skill.type == slot_type::potion) {
                    if (casts::CastingController::can_start_new_cast()) {
                        bool success = casts::CastingController::try_start_cast(form, bind, i, skill.hand);
                        SpellHotbar::RenderManager::highlight_skill_slot(static_cast<int>(i), 0.5, !success);
                    }
                    else {
                        //Potion on cd (cast still running)
                        SpellHotbar::RenderManager::highlight_skill_slot(static_cast<int>(i), 0.25, true);
                    }
                }
            }
            else {
                //slot not bound
                SpellHotbar::RenderManager::highlight_skill_slot(static_cast<int>(i), 0.25, true);
            }
        }
        else {
            //error highlight
            SpellHotbar::RenderManager::highlight_skill_slot(static_cast<int>(i), 0.5, true);
        }
    }
    InputModeEquip* InputModeEquip::getSingleton()
    {
        static InputModeEquip instance;
        return &instance;
    }

    void InputModeOblivion::process_input(SlottedSkill& skill, RE::InputEvent*& addEvent, size_t& i, const KeyBind& bind, RE::INPUT_DEVICE& shoutKeDev, uint8_t& shoutKey)
    {
        if (skill.type == slot_type::action) {
            logger::info("SH2 action: refused in oblivion mode");
            SpellHotbar::RenderManager::highlight_skill_slot(static_cast<int>(i), 0.5, true);
            return;
        }
        if (i == Input::keybind_id::oblivion_cast || i == Input::keybind_id::oblivion_potion) {
            //logger::info("Start Regular Cast");
            InputModeCast::getSingleton()->process_input(skill, addEvent, i, bind, shoutKeDev, shoutKey);
        }
        else {
            auto pc = RE::PlayerCharacter::GetSingleton();
            if (pc) {
                if (skill.type == slot_type::weapon_art) {
                    logger::info("SH2 art: refused in oblivion mode");
                    SpellHotbar::RenderManager::highlight_skill_slot(static_cast<int>(i), 0.5, true);
                }
                else if (skill.formID > 0) {
                    auto form = RE::TESForm::LookupByID(skill.formID);

                    if (skill.type == slot_type::spell) {
                        GameData::oblivion_bar.set_spell(skill);
                    }
                    else if (skill.type == slot_type::shout)
                    {
                        RE::ActorEquipManager::GetSingleton()->EquipShout(pc, form->As<RE::TESShout>());
                    }
                    else if (skill.type == slot_type::lesser_power || skill.type == slot_type::power) {
                        RE::ActorEquipManager::GetSingleton()->EquipSpell(pc, form->As<RE::SpellItem>(), GameData::equip_slot_voice);
                    }
                    else if (skill.type == slot_type::potion) {
                        GameData::oblivion_bar.set_potion(skill);
                    }
                }
                else {
                    //slot not bound
                    SpellHotbar::RenderManager::highlight_skill_slot(static_cast<int>(i), 0.25, true);
                }
            }
            else {
                //error highlight
                SpellHotbar::RenderManager::highlight_skill_slot(static_cast<int>(i), 0.5, true);
            }
        }
    }
    void InputModeOblivion::process_key_update(const KeyBind& bind, size_t i, float held_down_sec)
    {
        //Only called for main keys (0-11), no check needed
        constexpr float show_time = 1.0f;

        if (Bars::oblivion_bar_held_show_time_threshold > 0.0f) {
            if (Bars::oblivion_bar_press_show_timer > 0.0f) {
                Bars::oblivion_bar_press_show_timer = show_time;
            }
            else if (held_down_sec >= Bars::oblivion_bar_held_show_time_threshold) {
                Bars::oblivion_bar_press_show_timer = show_time;
            }
        }
    }

    InputModeOblivion* InputModeOblivion::getSingleton()
    {
        static InputModeOblivion instance;
        return &instance;
    }

    void InputModeVampireLord::process_input(SlottedSkill& skill, RE::InputEvent*& addEvent, size_t& i, const KeyBind& bind, RE::INPUT_DEVICE& shoutKeyDev, uint8_t& shoutKey)
    {
        if (skill.type == slot_type::action) {
            logger::info("SH2 action: refused in vampire-lord mode");
            SpellHotbar::RenderManager::highlight_skill_slot(static_cast<int>(i), 0.5, true);
            return;
        }
        auto pc = RE::PlayerCharacter::GetSingleton();
        if (pc && allowed_to_instantcast(skill.formID)) {
            if (skill.formID > 0) {
                auto form = RE::TESForm::LookupByID(skill.formID);

                if (skill.type == slot_type::spell) {

                    RE::BGSEquipSlot* equip_slot = GameData::equip_slot_right_hand;
                    if (skill.hand == left_hand) {
                        equip_slot = GameData::equip_slot_left_hand;
                    }

                    RE::ActorEquipManager::GetSingleton()->EquipSpell(pc, form->As<RE::SpellItem>(), equip_slot);
                    SpellHotbar::RenderManager::highlight_skill_slot(static_cast<int>(i), 0.25);

                } else if(skill.type == slot_type::lesser_power || skill.type == slot_type::power || skill.type == slot_type::shout) {
                    bool can_start{ true };
                    if (skill.type == slot_type::shout) {
                        if (!allowed_to_cast(skill.formID, true)) can_start = false;
                    }

                    if (can_start) {
                        //Start a shout
                        if (!addEvent) { //do not accidentaly create another event

                            if (casts::CastingController::try_cast_power(form, bind, i, skill.hand)) {
                                addEvent = RE::ButtonEvent::Create(shoutKeyDev, "Shout", shoutKey, 1.0f, 0.0f); //default shout key
                            }
                        }
                    }
                    else {
                        SpellHotbar::RenderManager::highlight_skill_slot(static_cast<int>(i), 0.5, true);
                    }
                }
                else if (skill.type == slot_type::potion) {
                    bool success = casts::CastingController::try_start_cast(form, bind, i, skill.hand);
                    SpellHotbar::RenderManager::highlight_skill_slot(static_cast<int>(i), 0.5, !success);
                }
            }
        }
        else {
            //error highlight
            SpellHotbar::RenderManager::highlight_skill_slot(static_cast<int>(i), 0.5, true);
        }
    }

    InputModeVampireLord* InputModeVampireLord::getSingleton()
    {
        static InputModeVampireLord instance;
        return &instance;
    }

    void InputModeBase::process_key_update(const KeyBind& bind, size_t i, float held_down_sec)
    {
        //do nothing
    }

}
