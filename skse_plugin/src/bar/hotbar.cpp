#include "hotbar.h"
#include "hotbar_serialization.h"
#include "../logger/logger.h"
#include "../storage/storage.h"
#include "hotbars.h"
#include "../game_data/action_definition.h"
#include "../game_data/game_data.h"
#include "../rendering/render_manager.h"
#include "../casts/casting_controller.h"
#include "../input/modes.h"
#include "../casts/spell_proc.h"
#include "../game_data/localization.h"

#include <array>
#include <numbers>

namespace SpellHotbar
{
    namespace rj = rapidjson;

    Hotbar::Hotbar(const std::string& name, uint8_t& barsize)
        : m_enabled(true),
        m_bar(),
        m_ctrl_bar(),
        m_shift_bar(),
        m_alt_bar(),
        m_parent_bar(0),
        m_name(name),
        m_inherit_mode(inherit_mode::def),
        m_barsize(barsize)
    {
    }
    void Hotbar::set_parent(uint32_t parent)
    {
        m_parent_bar = parent;
    }

    void serialize_bar(const SubBar& bar, SKSE::SerializationInterface* serializer, const std::string & name) {

        // write number off filled slots
        uint8_t filled_slots = static_cast<uint8_t>(std::count_if(bar.m_slotted_skills.begin(), bar.m_slotted_skills.end(), [](const auto& elem) { return !elem.isEmpty(); }));
#ifdef DEBUG_LOG_SERIALIZATION
        //logger::info("-{}: {} slots", name, filled_slots);
#endif
        if (!serializer->WriteRecordData(&filled_slots, sizeof(uint8_t))) {
            logger::error("Failed to write bar size for {}", name);
            return;
        }

        // write index + formID + hand_mode
        for (int i = 0U; i < bar.m_slotted_skills.size(); i++) {
            if (!bar.m_slotted_skills.at(i).isEmpty()) {
#ifdef DEBUG_LOG_SERIALIZATION
//                logger::info("Saving: {}-{:08x}", i, bar.m_slotted_skills.at(i).formID);
#endif
                bar.m_slotted_skills.at(i).serialize_skill(static_cast<uint8_t>(i), serializer, name);
            }
        }

    }

    void deserialize_bar(SubBar& bar, SKSE::SerializationInterface* serializer, const std::string& name, uint32_t version, uint32_t length)
    {       
#ifdef DEBUG_LOG_SERIALIZATION
        logger::info("Deseralizing {} with version {} and length {}", name, version, length);
#endif // DEBUG_LOG_SERIALIZATION

        //read number of filled slots
        uint8_t slots{0Ui8};
        if (!serializer->ReadRecordData(&slots, sizeof(uint8_t))) {
            logger::error("Failed to read slots count for Hotbar {}!", name);
        }

        for (uint8_t i = 0U; i < slots; i++) {
            BarSerialization::SlotRecord record;
            const auto read_record_data = [serializer](void* destination, std::size_t size) {
                return serializer->ReadRecordData(destination, static_cast<std::uint32_t>(size));
            };
            if (!BarSerialization::read_slot_record(read_record_data, version, record)) {
                logger::error("Failed to load Hotbar {}!", name);
                break;
            }

            if (!BarSerialization::slot_is_in_range(record, bar.m_slotted_skills.size())) {
                logger::warn("Ignoring out-of-range slot {} while loading Hotbar {} (valid range 0-{}).",
                    record.slot, name, bar.m_slotted_skills.size() - 1);
                continue;
            }

            auto& skill = bar.m_slotted_skills[record.slot];
            if (BarSerialization::is_action_record(record, version)) {
                skill.update_action_assignment(record.payload);
                if (!GameData::get_action(record.payload)) {
                    logger::info("Restored unknown Action {} on slot {}; bind kept for a later data load.", record.payload, record.slot);
                }
            } else if (version >= 6 && record.kind == BarSerialization::kArtSlotKind) {
                skill.update_art_assignment(record.payload);
                if (!GameData::get_art(record.payload)) {
                    logger::info("Restored unknown art {} on slot {}; bind kept for a later data load.", record.payload, record.slot);
                }
            } else if (version < 6 || record.kind == BarSerialization::kFormSlotKind) {
                RE::FormID resolved_id{0};
                serializer->ResolveFormID(record.payload, resolved_id);
                RE::TESForm* form = RE::TESForm::LookupByID(resolved_id);
                if (form != nullptr && Hotbar::is_valid_formtype_for_hotbar(form)) {
                    skill = resolved_id;
                }
                else {
                    logger::info("Removing {:8x} from bar, form no longer exists or not valid for hotbar.", resolved_id);
                    skill = 0;
                }
            } else {
                // A future/unsupported kind must not fall through to the FormID path. In
                // particular, a format-7 record carrying kind 2 is not an Action slot: old
                // builds do not know how to interpret its payload.
                logger::warn("Ignoring unsupported slot kind {} on slot {} in Hotbar {}.",
                    record.kind, record.slot, name);
                skill.clear();
            }
            skill.hand = hand_mode(std::clamp(record.hand, 0Ui8, static_cast<uint8_t>(hand_mode::end-1)));
        }

    }

    void Hotbar::serialize(SKSE::SerializationInterface* serializer, uint32_t key) const
    {
        if (!serializer->OpenRecord(key, Storage::save_format)) {
            if (Bars::bar_names.contains(key)) {
                logger::error("Failed to open record for {}!", Bars::bar_names.at(key));
            } else {
                logger::error("No barname known for {}!", key);
            }
        } else {

#ifdef DEBUG_LOG_SERIALIZATION
            if (Bars::bar_names.contains(key)) logger::trace("Storing bar {}, {}", Bars::bar_names.at(key), key);
#endif // DEBUG_LOG_SERIALIZATION

            if (!serializer->WriteRecordData(&m_enabled, sizeof(bool))) {
                logger::error("Failed to write enabled state for {}", m_name);
            } else {
                // write inherit_mode
                uint8_t inherit_type = static_cast<uint8_t>(m_inherit_mode);
                if (!serializer->WriteRecordData(&inherit_type, sizeof(uint8_t))) {
                    logger::error("Failed to write data inherit mode for {}", Bars::bar_names.at(key));
                }

                serialize_bar(m_bar, serializer, m_name + "_main");
                serialize_bar(m_ctrl_bar, serializer, m_name + "_ctrl");
                serialize_bar(m_shift_bar, serializer, m_name + "_shift");
                serialize_bar(m_alt_bar, serializer, m_name + "_alt");
            }
        }
    }

    void Hotbar::deserialize(SKSE::SerializationInterface* serializer, uint32_t type, uint32_t version,
                             uint32_t length) {
        std::string name = Bars::bar_names.contains(type) ? Bars::bar_names.at(type) : "?";
        //logger::trace("Reading hotbar {} from save...", name);

        // bar record length can vary, no length check
        if (!serializer->ReadRecordData(&m_enabled, sizeof(bool))) {
            logger::error("Failed to read enabled state for Hotbar {}!", name);
        } else {

            uint8_t inherit_type{0};
            if (!serializer->ReadRecordData(&inherit_type, sizeof(uint8_t))) {
                logger::error("Failed to read inherit type for Hotbar {}!", name);
            } else {
                m_inherit_mode = inherit_mode(static_cast<int>(inherit_type));
            }

            deserialize_bar(m_bar, serializer, name, version, length);
            deserialize_bar(m_ctrl_bar, serializer, name, version, length);
            deserialize_bar(m_shift_bar, serializer, name, version, length);
            deserialize_bar(m_alt_bar, serializer, name, version, length);
        }

        if (type == Bars::MAIN_BAR) {
            m_enabled = true; // Mainbar MUST be enabled
            m_inherit_mode = inherit_mode::none; // Mainbar can't inherit
        }
    }

    SubBar& Hotbar::get_sub_bar(key_modifier mod)
    {
        switch (mod) {
            case SpellHotbar::key_modifier::ctrl:
                return m_ctrl_bar;
            case SpellHotbar::key_modifier::shift:
                return m_shift_bar;
            case SpellHotbar::key_modifier::alt:
                return m_alt_bar;
            default:
                return m_bar;
        }
    }

    const SubBar& Hotbar::get_sub_bar(key_modifier mod) const
    {
        return const_cast<Hotbar*>(this)->get_sub_bar(mod);
    }

    ImU32 Hotbar::calculate_potion_color(RE::Effect* effect)
    {
        ImU32 ret = IM_COL32_WHITE;
        auto bEffect = effect->baseEffect;
        if (bEffect) {
            RE::EffectArchetypes::ArchetypeID arch = bEffect->GetArchetype();
            if (arch == RE::EffectArchetypes::ArchetypeID::kValueModifier || arch == RE::EffectArchetypes::ArchetypeID::kPeakValueModifier) 
            {
                RE::ActorValue av = bEffect->data.primaryAV;
                if (GameData::potion_color_mapping.contains(av)) {
                    ret = GameData::potion_color_mapping.at(av);
                }
            }
            else if (arch == RE::EffectArchetypes::ArchetypeID::kInvisibility) {
                if (GameData::potion_color_mapping.contains(RE::ActorValue::kInvisibility)) {
                    ret = GameData::potion_color_mapping.at(RE::ActorValue::kInvisibility);
                }
            }

        }

        return ret;
    }

    bool Hotbar::is_valid_formtype_for_hotbar(const RE::TESForm* form)
    {
        if (form != nullptr) {
            switch (form->GetFormType()) {
            case RE::FormType::Scroll:
                [[fallthrough]];
            case RE::FormType::Spell:
                [[fallthrough]];
            case RE::FormType::Shout:
                [[fallthrough]];
            case RE::FormType::AlchemyItem:
                return true;
            default:
                return false;
            }
        }
        return false;;
    }

    void Hotbar::rotate_skill_hand_assingment(RE::SpellItem* spell, SlottedSkill & skill)
    {
        switch (skill.hand) {
        case hand_mode::auto_hand:
            skill.hand = hand_mode::left_hand;
            break;
        case hand_mode::left_hand:
            skill.hand = hand_mode::right_hand;
            break;
        case hand_mode::right_hand:
            if (!spell->GetNoDualCastModifications() && GameData::player_can_dualcast_spell(spell)) {
                skill.hand = hand_mode::dual_hand;
            }
            else {
                skill.hand = hand_mode::auto_hand;
            }
            break;
        case hand_mode::dual_hand:
        default:
            skill.hand = hand_mode::auto_hand;
        }
    }

    std::tuple<SlottedSkill, bool> Hotbar::get_skill_in_bar_with_inheritance(
        int index, key_modifier mod, bool hide_clear_spell, bool inherited, std::optional<key_modifier> original_mod)
    {
        auto skill = get_sub_bar(mod).m_slotted_skills[index];

        //if dynamic form -> check if still valid
        if (skill.formID >= 0xFF000000) {
            RE::TESForm* form = RE::TESForm::LookupByID(skill.formID);
            if (!form || !Hotbar::is_valid_formtype_for_hotbar(form)) {
                // unbind from bar
                get_sub_bar(mod).m_slotted_skills[index] = SlottedSkill();
                skill = get_sub_bar(mod).m_slotted_skills[index];
            }
        }

        if (!skill.isEmpty() && this->is_enabled())
        {
            if (hide_clear_spell && GameData::is_clear_spell(skill.formID)) {
                return std::make_tuple(SlottedSkill(), inherited);
            } else {
                return std::make_tuple(skill, inherited);
            }
        } else {
            if (m_parent_bar != 0U && Bars::hotbars.contains(m_parent_bar) && (m_inherit_mode != inherit_mode::none)) {
                //logger::info("Asking Parent Bar: {} {}", m_parent_bar.get()->get_name(), (int) &(*m_parent_bar));
                if (mod != key_modifier::none && m_inherit_mode == inherit_mode::def) {
                    //if we have a modifier we need inherit from non-mod bar
                    return get_skill_in_bar_with_inheritance(index, key_modifier::none, hide_clear_spell, true, mod);
                }

                return Bars::hotbars.at(m_parent_bar).get_skill_in_bar_with_inheritance(index, original_mod ? original_mod.value() : mod, hide_clear_spell, true);
            } else {
                return std::make_tuple(SlottedSkill(), false);
            }
        }
    }

    SlottedSkill& Hotbar::get_skill_in_bar_by_ref(int index, key_modifier mod)
    {
        return get_sub_bar(mod).m_slotted_skills[index];
    }

    SlottedSkill* Hotbar::get_skill_in_bar_ptr(int index, key_modifier mod)
    {
        return &get_sub_bar(mod).m_slotted_skills[index];
    }

    int Hotbar::set_inherit_mode(int value)
    {
        int mode = std::clamp(value, 0, 2);
        m_inherit_mode = static_cast<inherit_mode>(mode);
        return mode;
    }

    namespace {
        void push_image(std::vector<Flick::DockImage>& images, const SubTextureImage* img, float x, float y, float w, float h, ImU32 col)
        {
            if (img == nullptr || img->source_path.empty()) {
                return;
            }
            images.push_back(Flick::DockImage{ img->source_path,
                                               x, y, x + w, y + h, img->uv0.x, img->uv0.y, img->uv1.x, img->uv1.y,
                                               static_cast<unsigned>(col) });
        }
        void push_texture(std::vector<Flick::DockImage>& images, const TextureImage* tex, float x, float y, float w, float h, ImU32 col)
        {
            if (tex == nullptr || tex->source_path.empty()) {
                return;
            }
            images.push_back(Flick::DockImage{ tex->source_path,
                                               x, y, x + w, y + h, 0.0f, 0.0f, 1.0f, 1.0f, static_cast<unsigned>(col) });
        }
        // IM_COL32 with its alpha byte replaced.
        ImU32 with_alpha(ImU32 col, int alpha)
        {
            return (col & ~IM_COL32_A_MASK) | (static_cast<ImU32>(std::clamp(alpha, 0, 255)) << IM_COL32_A_SHIFT);
        }
    }

    Flick::DockFrame Hotbar::build_dock_frame(float screensize_x, float screensize_y, int highlight_slot,
                                              float highlight_factor, key_modifier mod, int hovered)
    {
        Flick::DockFrame frame;

        const float spacing = Bars::menu_slot_spacing;
        const int icon_size = static_cast<int>(get_hud_slot_height(screensize_y, Bars::menu_slot_scale));
        const float icon = static_cast<float>(icon_size);
        // Deeper inset than the HUD bar's 0.05/0.0125: at dock size those touch the slot
        // border.
        const float text_offset_x = icon * 0.07f;
        const float text_offset_y = icon * 0.06f;

        //Same wrap trigger as draw_in_menu: 60% of the display, never an input to the size.
        int row_len = static_cast<int>(std::floor((screensize_x * 0.60f + spacing) / (icon + spacing)));
        row_len = std::clamp(row_len, 1, static_cast<int>(m_barsize));
        const float key_strip_h = Bars::use_keybind_icons() ? icon * 0.5f : 0.0f;
        const float row_h = icon + key_strip_h + spacing;

        const ImU32 white = IM_COL32_WHITE;

        for (int i = 0; i < m_barsize; i++) {
            auto [skill, inherited] = get_skill_in_bar_with_inheritance(i, mod, false);

            GameData::Spell_cast_data skill_dat;
            auto form = RE::TESForm::LookupByID(skill.formID);
            if (form) {
                skill_dat = GameData::get_spell_data(form, true, true);
            }

            const int col_i = i % row_len, row_i = i / row_len;
            const float x = col_i * (icon + spacing);
            const float y = row_i * row_h;

            size_t count{ 0 };
            if (skill.consumed != consumed_type::none) {
                count = GameData::count_item_in_inv(skill.formID);
            }

            const SubTextureImage* art{ nullptr };
            if (skill.type == slot_type::action) {
                if (const ActionDefinition* action = GameData::get_action(skill.action_id)) {
                    art = RenderManager::resolve_art_icon_tex(action->icon_form, action->icon);
                }
            }
            else if (skill.type == slot_type::weapon_art) {
                if (const ArtDefinition* def = GameData::get_art(skill.art_id)) {
                    art = RenderManager::resolve_art_icon_tex(def->icon_form, def->icon);
                }
            }
            else {
                art = RenderManager::resolve_skill_tex(skill.formID);
            }

            if (art == nullptr) {
                push_image(frame.images, RenderManager::default_icon_tex(GameData::DefaultIconType::BAR_EMPTY), x, y, icon, icon, white);
            }
            else {
                push_image(frame.images, art, x, y, icon, icon, skill.color);
                if (RenderManager::should_overlay_be_rendered(skill_dat.overlay_icon)) {
                    push_image(frame.images, RenderManager::default_icon_tex(skill_dat.overlay_icon), x, y, icon, icon, white);
                }

                ImU32 col = white;
                if (highlight_slot == i) {
                    col = IM_COL32(255, 255, static_cast<int>(127 + 128 * (1.0 - highlight_factor)), 255);
                }
                else if (hovered == i) {
                    col = IM_COL32(255, 255, 160, 255);
                }
                if (skill.consumed != consumed_type::none && count == 0) {
                    push_image(frame.images, RenderManager::cooldown_tex(0.0f), x, y, icon, icon, col);
                }
                push_image(frame.images, RenderManager::default_icon_tex(GameData::DefaultIconType::BAR_OVERLAY), x, y, icon, icon, col);
            }

            if (Bars::use_keybind_icons()) {
                auto [tex_id_key, tex_id_mod] = GameData::get_keybind_icon_index(i, mod);
                const TextureImage* key = RenderManager::button_tex(tex_id_key);
                const TextureImage* modt = RenderManager::button_tex(tex_id_mod);
                float kx = x;
                if (modt != nullptr && key != nullptr) {
                    const float w = key_strip_h * static_cast<float>(modt->width) / static_cast<float>(modt->height);
                    push_texture(frame.images, modt, kx, y + icon, w, key_strip_h, white);
                    kx += w;
                }
                if (key != nullptr) {
                    const float w = key_strip_h * static_cast<float>(key->width) / static_cast<float>(key->height);
                    push_texture(frame.images, key, kx, y + icon, w, key_strip_h, white);
                }
            }
            else {
                frame.texts.push_back(Flick::DockText{ x + text_offset_x, y + text_offset_y, 0,
                                                       GameData::get_keybind_text(i, mod), static_cast<unsigned>(white) });
            }

            if (skill.hand == hand_mode::left_hand || skill.hand == hand_mode::right_hand || skill.hand == hand_mode::dual_hand) {
                const char* key = skill.hand == hand_mode::left_hand ? "$HAND_TEXT_LEFT"
                                : skill.hand == hand_mode::right_hand ? "$HAND_TEXT_RIGHT" : "$HAND_TEXT_DUAL";
                frame.texts.push_back(Flick::DockText{ x + icon * 0.95f, y + text_offset_y, 1, translate(key),
                                                       static_cast<unsigned>(white) });
            }

            if (skill.consumed != consumed_type::none) {
                frame.texts.push_back(Flick::DockText{ x + icon, y + icon, 2,
                                                       std::to_string(std::clamp(count, 0Ui64, 999Ui64)),
                                                       static_cast<unsigned>(white) });
            }

            frame.slots.push_back(Flick::DockSlot{ x, y, icon, icon, describe_slot(i, mod) });
        }

        const int rows = (m_barsize + row_len - 1) / row_len;
        frame.width = row_len * icon + (row_len - 1) * spacing;
        frame.height = rows * (icon + key_strip_h) + (rows - 1) * spacing;
        return frame;
    }

    std::string Hotbar::describe_slot(int index, key_modifier mod)
    {
        auto [skill, inherited] = get_skill_in_bar_with_inheritance(index, mod, false);

        std::string name = GameData::resolve_slot_name(skill);
        if (name.empty()) {
            return "";
        }

        auto* form = RE::TESForm::LookupByID(skill.formID);
        if (form == nullptr) {
            return name;
        }
        GameData::Spell_cast_data dat = GameData::get_spell_data(form, true, true);

        std::string out = name;

        if (auto* spell = form->As<RE::SpellItem>(); spell != nullptr) {
            auto* pc = RE::PlayerCharacter::GetSingleton();
            float cost = spell->CalculateMagickaCost(pc);
            if (cost > 0.0f) {
                out += std::format("  |  {} {:.0f}", translate("$TOOLTIP_COST"), cost);
            }
        }
        if (dat.casttime > 0.0f) {
            out += std::format("  |  {} {:.1f}s", translate("$TOOLTIP_CAST"), dat.casttime);
        }
        const float cd = dat.cooldown > 0.0f ? dat.cooldown : dat.gcd;
        if (cd > 0.0f) {
            out += std::format("  |  {} {:.1f}s", translate("$TOOLTIP_COOLDOWN"), cd);
        }
        return out;
    }

    inline float determine_cd(RE::FormID skill, slot_type skill_type, float game_time, float time_scale, float gcd_prog,
                              float gcd_dur, float shout_cd, float shout_cd_dur) {
        float cd_prog {0.0f};

        constexpr float gt_to_sec_factor = 24.0f * 60.0f * 60.0f;

        float special_cd = GameData::get_special_cd(skill);
        if (special_cd > 0.0f)
        {
            float gcd = gcd_dur * (1.0f - gcd_prog);
            //get longer CD (special or global)
            cd_prog = (special_cd >= gcd) ? special_cd : gcd_prog;
        }
        else {

            auto [gt_prog, gt_dur] = GameData::get_gametime_cooldown(game_time, skill);
            if (gt_dur > 0.0f)
            {
                // get longer CD
                float gcd = gcd_dur * (1.0f - gcd_prog);
                float gt_cd = gt_dur * gt_to_sec_factor / time_scale * (1.0f - gt_prog);

                cd_prog = (gt_cd >= gcd) ? gt_prog : gcd_prog;

            }
            else {
                if (!GameData::individual_shout_cooldowns && skill_type == slot_type::shout && shout_cd_dur > 0.0f) {
                    // get longer cd
                    float gcd = gcd_dur * (1.0f - gcd_prog);
                    float scd = shout_cd_dur * (1.0f - shout_cd);
                    cd_prog = (scd >= gcd) ? shout_cd : gcd_prog;
                }
                else {
                    if (gcd_prog > 0.0f) {
                        cd_prog = gcd_prog;
                    }
                }
            }
        }
        return cd_prog;
    }

    ImVec2 rotate_around_origin(ImVec2 p, float _sin, float _cos) {
        float x_rot = p.x * _cos - p.y * _sin;
        float y_rot = p.x * _sin + p.y * _cos;
        return ImVec2(x_rot, y_rot);
    }

    bool spell_is_currently_equipped(const SpellHotbar::SlottedSkill& skill, RE::PlayerCharacter* pc) {
        //If oblivion or equip mode and currently equipped, highlight the slot
        if ((Input::is_oblivion_mode() || Input::is_equip_mode()) && pc) {
            RE::TESForm* equipped{ nullptr };
            if (skill.type == slot_type::spell) {
                if (Input::is_oblivion_mode()) {
                    const auto spell = GameData::oblivion_bar.get_slotted_spell();
                    if (!spell.isEmpty()) {
                        return spell.formID == skill.formID;
                    }
                }
                else {
                    if (skill.hand == SpellHotbar::hand_mode::auto_hand || skill.hand == SpellHotbar::hand_mode::left_hand) {
                        equipped = pc->GetEquippedObjectInSlot(GameData::equip_slot_left_hand);
                    }
                    else if (skill.hand == SpellHotbar::hand_mode::right_hand) {
                        equipped = pc->GetEquippedObjectInSlot(GameData::equip_slot_right_hand);
                    }
                    else if (skill.hand == SpellHotbar::hand_mode::dual_hand) {
                        equipped = pc->GetEquippedObjectInSlot(GameData::equip_slot_both_hand);
                    }
                    else if (skill.hand == SpellHotbar::hand_mode::voice) {
                        equipped = pc->GetEquippedObjectInSlot(GameData::equip_slot_voice);
                    }
                }
            }
            else if (skill.type == slot_type::lesser_power || skill.type == slot_type::power || skill.type == slot_type::shout) {
                equipped = pc->GetEquippedObjectInSlot(GameData::equip_slot_voice);

            }
            else if (skill.type == slot_type::potion) {
                if (Input::is_oblivion_mode()) {
                    const auto potion = GameData::oblivion_bar.get_slotted_potion();
                    if (!potion.isEmpty()) {
                        return potion.formID == skill.formID;
                    }
                }
                return false;
            }

            if (equipped != nullptr) {
                return equipped->GetFormID() == skill.formID;
            }
        }

       return false;
    }

    // The HUD bar as a display list for the FLICK-hosted HUD window: the three layouts and the
    // per-slot art, as positions in pixels relative to the layer's origin. Text sizes come from
    // the screen height and slot_scale, below.
    void Hotbar::build_hud_layer(Flick::HudLayer& layer, float screensize_x, float screensize_y, int highlight_slot,
                                 float highlight_factor, key_modifier mod, bool highlight_isred, float alpha,
                                 float shout_cd, float shout_cd_dur, float text_alpha)
    {
        HudSlotContext ctx;
        ctx.alpha = alpha;
        ctx.icon_size = static_cast<int>(get_hud_slot_height(screensize_y, Bars::slot_scale));
        ctx.text_offset_x = ctx.icon_size * 0.05f;
        ctx.text_offset_y = ctx.icon_size * 0.0125f;
        if (!Input::is_oblivion_mode()) {
            ctx.gcd_prog = casts::CastingController::get_current_gcd_progress();
            ctx.gcd_dur = casts::CastingController::get_current_gcd_duration();
        }
        ctx.shout_cd = shout_cd;
        ctx.shout_cd_dur = shout_cd_dur;
        if (RE::Calendar* cal = RE::Calendar::GetSingleton()) {
            ctx.game_time = cal->GetCurrentGameTime();
            ctx.time_scale = cal->GetTimescale();
        }
        ctx.highlight_slot = highlight_slot;
        ctx.highlight_factor = highlight_factor;
        ctx.highlight_isred = highlight_isred;
        ctx.mod = mod;
        ctx.bar_name = this->get_name();
        ctx.pc = RE::PlayerCharacter::GetSingleton();
        ctx.equipped_type = ctx.pc ? GameData::getPlayerEquipmentType() : GameData::EquippedType::SPELL;

        const float icon = static_cast<float>(ctx.icon_size);
        const float spacing = Bars::slot_spacing;
        // The text sizes match the SMF-drawn bar, measured off a captured frame at 3440x1440: the
        // host font drew at ~28 px there, and the key glyph was that times (slot_scale + 0.25).
        // Sizing the text at 0.24 of the slot instead gives 18 px on a 77 px slot, visibly small.
        const float base_px = screensize_y * (28.0f / 1440.0f);
        layer.text_px = base_px * (Bars::slot_scale + 0.25f);

        // The bar's name, centred over the slots, on its own fade, at the host font's own size.
        const float name_px = base_px;
        const float name_h = name_px + icon * 0.05f;
        const int name_alpha = static_cast<int>(255 * alpha * text_alpha);

        float grid_w{ 0.0f }, grid_h{ 0.0f };
        const float top = name_h;

        if (Bars::layout == Bars::bar_layout::CIRCLE && m_barsize >= 3) {
            grid_w = (Bars::bar_circle_radius + 1.125f) * icon * 2.0f;
            grid_h = grid_w;
            const float c = (grid_w - icon) * 0.5f;
            const double angle_rad = (2.0 * std::numbers::pi) / static_cast<double>(m_barsize);
            const float _sin = std::sinf(static_cast<float>(angle_rad));
            const float _cos = std::cosf(static_cast<float>(angle_rad));
            ImVec2 offset{ 0.0f, -(Bars::bar_circle_radius + 0.5f) * icon };
            for (int i = 0; i < m_barsize; i++) {
                auto [skill, inherited] = get_skill_in_bar_with_inheritance(i, mod, true);
                push_single_skill(layer, skill, i, c + offset.x, top + c + offset.y, ctx);
                offset = rotate_around_origin(offset, _sin, _cos);
            }
        }
        else if (Bars::layout == Bars::bar_layout::CROSS && m_barsize >= 4) {
            const int numcrosses = static_cast<int>(std::ceil(static_cast<float>(m_barsize) / 4.0f));
            const float cross_gap = screensize_x * Bars::bar_cross_distance;
            const float pitch = icon + spacing;
            for (int cross = 0; cross < numcrosses; cross++) {
                const float base_x = cross * (3.0f * pitch + cross_gap);
                const int ind = cross * 4;
                const std::array<std::pair<float, float>, 4> at{ {
                    { base_x + pitch, 0.0f },          // top
                    { base_x, pitch },                 // left
                    { base_x + 2.0f * pitch, pitch },  // right
                    { base_x + pitch, 2.0f * pitch },  // bottom
                } };
                for (int k = 0; k < 4; ++k) {
                    auto [skill, inherited] = get_skill_in_bar_with_inheritance(ind + k, mod, true);
                    push_single_skill(layer, skill, ind + k, at[k].first, top + at[k].second, ctx);
                }
            }
            grid_w = numcrosses * 3.0f * pitch - spacing + cross_gap * (numcrosses - 1);
            grid_h = 3.0f * pitch - spacing;
        }
        else {
            const int row_len = std::max(1, static_cast<int>(Bars::bar_row_len));
            const float key_strip = Bars::use_keybind_icons() ? icon * 0.35f * keybind_icon_pos_factor : 0.0f;
            const float row_pitch = icon + spacing + key_strip;
            int rows = 0;
            for (int i = 0; i < m_barsize; i++) {
                auto [skill, inherited] = get_skill_in_bar_with_inheritance(i, mod, true);
                const int col_i = i % row_len, row_i = i / row_len;
                rows = row_i + 1;
                push_single_skill(layer, skill, i, col_i * (icon + spacing), top + row_i * row_pitch, ctx);
            }
            grid_w = std::min(row_len, static_cast<int>(m_barsize)) * (icon + spacing) - spacing;
            grid_h = rows * row_pitch - spacing;
            if (Bars::use_keybind_icons()) {
                // The key glyph hangs below the slot (drawn at 0.8 of it, half a slot tall).
                grid_h += icon * 0.3f;
            }
        }

        layer.texts.push_back(Flick::DockText{ grid_w * 0.5f, 0.0f, 3, this->get_name(),
                                               static_cast<unsigned>(IM_COL32(255, 255, 255, name_alpha)), name_px });
        layer.width = grid_w;
        layer.height = top + grid_h;
        layer.visible = true;
    }

    void Hotbar::push_single_skill(Flick::HudLayer& layer, SlottedSkill& skill, int slot_index, float x, float y,
                                   const HudSlotContext& ctx)
    {
        const int icon_size = ctx.icon_size;
        const float icon = static_cast<float>(icon_size);
        const float alpha = ctx.alpha;
        auto& images = layer.images;

        GameData::Spell_cast_data skill_dat;
        bool has_spell_proc{ false };
        auto form = RE::TESForm::LookupByID(skill.formID);
        const ActionDefinition* action = skill.type == slot_type::action ? GameData::get_action(skill.action_id) : nullptr;
        RE::SpellItem* spell_item = nullptr;
        if (form) {
            skill_dat = GameData::get_spell_data(form, true, true);
            if (form->GetFormType() == RE::FormType::Spell) {
                spell_item = form->As<RE::SpellItem>();
                has_spell_proc = casts::SpellProc::has_spell_proc(spell_item);
            }
        }

        int count{ -1 };
        bool has_charges{ false };
        if (skill.consumed != consumed_type::none) {
            count = GameData::count_item_in_inv(skill.formID);
            has_charges = true;
        }
        else if (spell_item != nullptr) {
            auto c = GameData::get_spell_charges_mod_compat(spell_item);
            if (c.has_value()) {
                has_charges = true;
                count = c.value();
            }
        }

        const int alpha_i = static_cast<int>(255 * alpha);
        const ImU32 white = IM_COL32(255, 255, 255, alpha_i);
        const ImU32 tinted = with_alpha(skill.color, alpha_i);

        const SubTextureImage* art{ nullptr };
        if (action != nullptr) {
            art = RenderManager::resolve_art_icon_tex(action->icon_form, action->icon);
        }
        else if (skill.type == slot_type::weapon_art) {
            if (const ArtDefinition* def = GameData::get_art(skill.art_id)) {
                art = RenderManager::resolve_art_icon_tex(def->icon_form, def->icon);
            }
        }
        else {
            art = RenderManager::resolve_skill_tex(skill.formID);
        }

        if (art == nullptr) {
            push_image(images, RenderManager::default_icon_tex(GameData::DefaultIconType::BAR_EMPTY), x, y, icon, icon, white);
        }
        else {
            push_image(images, art, x, y, icon, icon, tinted);
            if (RenderManager::should_overlay_be_rendered(skill_dat.overlay_icon)) {
                push_image(images, RenderManager::default_icon_tex(skill_dat.overlay_icon), x, y, icon, icon, white);
            }

            const auto highlight = [&](ImU32 col) {
                push_image(images, RenderManager::default_icon_tex(GameData::DefaultIconType::BAR_HIGHLIGHT), x, y, icon, icon, col);
            };
            const auto cooldown = [&](float cd) {
                push_image(images, RenderManager::cooldown_tex(cd), x, y, icon, icon, white);
            };

            //If vampire lord uses equipmode and this is the currently equipped spell -> highlight blue
            if (ctx.bar_name == Bars::bar_names.at(Bars::VAMPIRE_LORD_BAR) &&
                GameData::global_vampire_lord_equip_mode && GameData::global_vampire_lord_equip_mode->value > 0.0f) {
                if (ctx.pc) {
                    auto equipped_mh = ctx.pc->GetEquippedObject(false);
                    //If mainhand not empty, we in cast mode
                    if (equipped_mh) {
                        auto equipped_oh = ctx.pc->GetEquippedObject(true);
                        if (equipped_oh && equipped_oh->GetFormID() == skill.formID) {
                            highlight(IM_COL32(127, 127, 255, alpha_i));
                        }
                    }
                    else if (skill.type == slot_type::spell) {
                        //empty mh -> melee mode
                        cooldown(0.0f);
                    }
                }
            } else if (spell_is_currently_equipped(skill, ctx.pc) && !(ctx.bar_name == Bars::OblivionBar::oblivion_bar_name)) {
                highlight(IM_COL32(127, 127, 255, alpha_i));
            }

            if (skill.type == slot_type::action) {
                float cd_prog{ 0.0f };
                if (action != nullptr && action->is_costed()) {
                    auto [gt_prog, gt_dur] = GameData::get_action_gametime_cooldown(ctx.game_time, skill.action_id);
                    if (gt_dur > 0.0f) {
                        cd_prog = gt_prog;
                    }
                    else if (ctx.gcd_prog > 0.0f) {
                        cd_prog = ctx.gcd_prog;
                    }
                }
                if (cd_prog > 0.0f) {
                    cooldown(cd_prog);
                }
            }
            else if (skill.type == slot_type::weapon_art) {
                const ArtDefinition* art_def = GameData::get_art(skill.art_id);
                if (art_def && !art_class_is_live(art_def->art_class, ctx.equipped_type)) {
                    cooldown(0.0f);
                } else {
                    float cd_prog{ 0.0f };
                    auto [gt_prog, gt_dur] = GameData::get_art_gametime_cooldown(ctx.game_time, skill.art_id);
                    if (gt_dur > 0.0f) {
                        cd_prog = gt_prog;
                    } else if (ctx.gcd_prog > 0.0f) {
                        cd_prog = ctx.gcd_prog;
                    }
                    if (cd_prog > 0.0f) {
                        cooldown(cd_prog);
                    }
                }
            } else if ((has_charges && count == 0) || GameData::is_on_binary_cd(skill.formID)) {
                cooldown(0.0f);
            }
            else {
                const float cd_prog = determine_cd(skill.formID, skill.type, ctx.game_time, ctx.time_scale, ctx.gcd_prog,
                                                   ctx.gcd_dur, ctx.shout_cd, ctx.shout_cd_dur);
                if (cd_prog > 0.0f) {
                    cooldown(cd_prog);
                }
                else if (has_spell_proc) {
                    //Spell proc overlay: one animation cycle per second, fading over the last 1.5 s.
                    const float timer = casts::SpellProc::get_spell_proc_timer();
                    const float total = casts::SpellProc::get_spell_proc_total();
                    float proc_alpha = alpha;
                    constexpr float fade_out_time = 1.5f;
                    if (timer > (total - fade_out_time)) {
                        const float p = std::clamp(timer - (total - fade_out_time) / fade_out_time, 0.0f, 1.0f);
                        proc_alpha *= p;
                    }
                    push_image(images, RenderManager::spellproc_tex(timer), x, y, icon, icon,
                               IM_COL32(255, 255, 255, static_cast<int>(proc_alpha * 255)));
                }
            }

            push_image(images, RenderManager::default_icon_tex(GameData::DefaultIconType::BAR_OVERLAY), x, y, icon, icon, white);
        }
        if (ctx.highlight_slot == slot_index) {
            ImU32 col;
            if (ctx.highlight_isred) {
                const int f = static_cast<int>(255 * ctx.highlight_factor * alpha);
                col = IM_COL32(255, 0, 0, f);
            }
            else {
                col = IM_COL32(255, 255, static_cast<int>(255 * (1.0f - ctx.highlight_factor)), alpha_i);
            }
            push_image(images, RenderManager::default_icon_tex(GameData::DefaultIconType::BAR_HIGHLIGHT), x, y, icon, icon, col);
        }

        if (!Bars::use_keybind_icons()) {
            layer.texts.push_back(Flick::DockText{ x + ctx.text_offset_x, y + ctx.text_offset_y, 0,
                                                   GameData::get_keybind_text(slot_index, ctx.mod), static_cast<unsigned>(white) });
        }
        else {
            // The key glyph, half a slot tall, centred under the slot at 0.8 of its height; with a
            // modifier glyph beside it the pair is squeezed to 0.95 of the slot width.
            auto [icon_main, icon_mode] = GameData::get_keybind_icon_index(slot_index, ctx.mod);
            const TextureImage* key = RenderManager::button_tex(icon_main);
            const TextureImage* modt = icon_mode >= 0 ? RenderManager::button_tex(icon_mode) : nullptr;
            if (key != nullptr && key->height > 0) {
                float h = icon * 0.5f;
                const float aspect_key = static_cast<float>(key->width) / static_cast<float>(key->height);
                const float aspect_mod = (modt != nullptr && modt->height > 0)
                                             ? static_cast<float>(modt->width) / static_cast<float>(modt->height) : 0.0f;
                float total_w = h * aspect_key + h * aspect_mod;
                const float max_w = icon * 0.95f;
                if (modt != nullptr && total_w > max_w) {
                    h *= max_w / total_w;
                    total_w = h * aspect_key + h * aspect_mod;
                }
                float kx = x + icon * 0.5f - total_w * 0.5f;
                const float ky = y + icon * keybind_icon_pos_factor;
                if (modt != nullptr) {
                    push_texture(images, modt, kx, ky, h * aspect_mod, h, white);
                    kx += h * aspect_mod;
                }
                push_texture(images, key, kx, ky, h * aspect_key, h, white);
            }
        }

        if (has_charges) {
            ImU32 count_text_color = white;
            if (count <= 0 && spell_item != nullptr && GameData::player_has_ordinator_bloodmagic()) {
                count = static_cast<int>(GameData::get_health_cost_mod_ordinator(spell_item));
                count_text_color = IM_COL32(255, 50, 50, alpha_i);
            }
            const std::string text = std::to_string(std::clamp(count, -9999, 9999));
            if (Bars::use_keybind_icons()) {
                layer.texts.push_back(Flick::DockText{ x + ctx.text_offset_x, y + ctx.text_offset_y, 0, text,
                                                       static_cast<unsigned>(count_text_color) });
            }
            else {
                layer.texts.push_back(Flick::DockText{ x + icon, y + icon - ctx.text_offset_y, 2, text,
                                                       static_cast<unsigned>(count_text_color) });
            }
        }
    }


    void update_spell_assignment(std::array<SlottedSkill, 12Ui64> & skills, size_t index, RE::FormID id)
    {
        if (skills[index].formID == id) {
            //rotate hand type if either hand
            auto form = RE::TESForm::LookupByID(id);
            if (form && form->GetFormType() == RE::FormType::Spell) {
                RE::SpellItem* spell = form->As<RE::SpellItem>();

                if (spell && spell->GetEquipSlot() == GameData::equip_slot_either_hand)
                {
                    Hotbar::rotate_skill_hand_assingment(spell, skills[index]);
                    RE::PlaySound(Input::sound_UISkillsFocus);
                }
            }
        }
        else {
            skills[index] = id;
            if (id != 0) {
                RE::PlaySound(Input::sound_UISkillsForward);
            }
            else {
                RE::PlaySound(Input::sound_UISkillsBackward);
            }
        }
    }

    void SpellHotbar::Hotbar::slot_spell(size_t index, RE::FormID spell, key_modifier modifier)
    {
        if (GameData::is_clear_spell(spell)) {
            if (get_spell(index, modifier) > 0U) {
                spell = 0U;
            }
        }

        if (index < max_bar_size) {
            switch (modifier) {
                case key_modifier::ctrl:
                    update_spell_assignment(m_ctrl_bar.m_slotted_skills, index, spell);
                    break;
                case key_modifier::shift:
                    update_spell_assignment(m_shift_bar.m_slotted_skills, index, spell);
                    break;
                case key_modifier::alt:
                    update_spell_assignment(m_alt_bar.m_slotted_skills, index, spell);
                    break;
                case key_modifier::none:
                default:
                    update_spell_assignment(m_bar.m_slotted_skills, index, spell);
                    break;
            }
        }
    }

    void SpellHotbar::Hotbar::slot_art(size_t index, uint32_t art_id, key_modifier modifier)
    {
        if (index >= max_bar_size) {
            return;
        }
        if (art_id != 0 && !GameData::get_art(art_id)) {
            logger::error("Unknown ability {}", art_id);
            return;
        }
        auto& skill = get_skill_in_bar_by_ref(static_cast<int>(index), modifier);
        if (art_id == 0) {
            skill.clear();
        } else {
            skill.update_art_assignment(art_id);
        }
        logger::info("slotArt: bar slot {} bound to art {}", index, art_id);
    }

    void SpellHotbar::Hotbar::slot_action(size_t index, uint32_t action_id, key_modifier modifier)
    {
        if (index >= max_bar_size) {
            return;
        }
        if (action_id != 0 && !GameData::get_action(action_id)) {
            logger::error("Unknown Action {}", action_id);
            return;
        }
        auto& skill = get_skill_in_bar_by_ref(static_cast<int>(index), modifier);
        if (action_id == 0) {
            skill.clear();
        } else {
            skill.update_action_assignment(action_id);
        }
        logger::info("slotAction: bar slot {} bound to Action {}", index, action_id);
    }

    RE::FormID Hotbar::get_spell(size_t index, key_modifier modifier)
    {
        RE::FormID ret{0};
        if (index < max_bar_size) {
            switch (modifier) {
                case key_modifier::ctrl:
                    ret = m_ctrl_bar.m_slotted_skills[index].formID;
                    break;
                case key_modifier::shift:
                    ret = m_shift_bar.m_slotted_skills[index].formID;
                    break;
                case key_modifier::alt:
                    ret = m_alt_bar.m_slotted_skills[index].formID;
                    break;
                case key_modifier::none:
                default:
                    ret = m_bar.m_slotted_skills[index].formID;
                    break;
            }
        }
        return ret;
    }

    SlottedSkill::SlottedSkill() : SlottedSkill(0U){};

    SlottedSkill::SlottedSkill(RE::FormID id) : formID(0U), art_id(0U), action_id(0U), type(slot_type::empty), hand(hand_mode::auto_hand), consumed(consumed_type::none), color(0xFFFFFFFF)
    { 
        update_skill_assignment(id);
    }

    bool SlottedSkill::isEmpty() const {
        return type==slot_type::empty;
    }

    bool SlottedSkill::serialize_skill(uint8_t index, SKSE::SerializationInterface* serializer, const std::string & name) const
    {
        bool ok{true};
        if (!serializer->WriteRecordData(&index, sizeof(uint8_t))) {
            ok = false;
            logger::error("Failed to write data for {}_{}", name, index);
        }
        uint8_t kind = BarSerialization::kFormSlotKind;
        if (type == slot_type::weapon_art) {
            kind = BarSerialization::kArtSlotKind;
        } else if (type == slot_type::action) {
            kind = BarSerialization::kActionSlotKind;
        }
        if (ok) {
            if (!serializer->WriteRecordData(&kind, sizeof(uint8_t))) {
                ok = false;
                logger::error("Failed to write data for {}_{}", name, index);
            }
        }
        if (ok) {
            if (kind == BarSerialization::kArtSlotKind) {
                if (!serializer->WriteRecordData(&this->art_id, sizeof(uint32_t))) {
                    ok = false;
                    logger::error("Failed to write data for {}_{}", name, index);
                }
            } else if (kind == BarSerialization::kActionSlotKind) {
                if (!serializer->WriteRecordData(&this->action_id, sizeof(uint32_t))) {
                    ok = false;
                    logger::error("Failed to write data for {}_{}", name, index);
                }
            } else if (!serializer->WriteRecordData(&this->formID, sizeof(RE::FormID))) {
                ok = false;
                logger::error("Failed to write data for {}_{}", name, index);
            }
        }
        if (ok) {
            if (!serializer->WriteRecordData(&this->hand, sizeof(hand_mode))) {
                ok = false;
                logger::error("Failed to write data for {}_{}", name, index);
            }
        }
        return ok;
    }

    void SlottedSkill::update_skill_assignment(RE::FormID p_formID)
    {
        clear();
        if (p_formID > 0U)
        {
            formID = p_formID;
            if (GameData::is_clear_spell(formID)) {
                type = slot_type::blocked;
                hand = hand_mode::voice;
            }
            else {
                auto form = RE::TESForm::LookupByID(formID);
                RE::SpellItem* spell{ nullptr };
                if (form) {
                    switch (form->GetFormType()) {

                    case RE::FormType::Scroll:
                        consumed = consumed_type::scroll;
                        [[fallthrough]]; //scrolls are also spells
                    case RE::FormType::Spell:
                        spell = form->As<RE::SpellItem>();
                        if (spell) {  // this should not be able to be null
                            if (spell->GetSpellType() == RE::MagicSystem::SpellType::kLesserPower) {
                                type = slot_type::lesser_power;
                            }
                            else if (spell->GetSpellType() == RE::MagicSystem::SpellType::kPower) {
                                type = slot_type::power;
                            }
                            else {
                                type = slot_type::spell;
                            }

                            if (spell->GetEquipSlot() == GameData::equip_slot_left_hand) {
                                hand = hand_mode::left_hand;
                            }
                            else if (spell->GetEquipSlot() == GameData::equip_slot_right_hand) {
                                hand = hand_mode::right_hand;
                            }
                            else if (spell->GetEquipSlot() == GameData::equip_slot_either_hand) {
                                hand = hand_mode::auto_hand;
                            }
                            else if (spell->GetEquipSlot() == GameData::equip_slot_both_hand) {
                                hand = hand_mode::dual_hand;
                            }
                            else {
                                hand = hand_mode::voice;
                            }

                            if ((type == slot_type::lesser_power || type == slot_type::power) && hand != hand_mode::voice) {
                                //fixes spells that are flagged as power
                                type = slot_type::spell;
                            }

                        }
                        else {
                            type = slot_type::unknown;
                        }
                        break;
                    case RE::FormType::Shout:
                        type = slot_type::shout;
                        hand = hand_mode::voice;
                        break;
                    case RE::FormType::AlchemyItem:
                        type = slot_type::potion;
                        hand = hand_mode::voice;
                        consumed = consumed_type::potion;

                        //If brewed potion, chose color dynamically
                        if (form->IsDynamicForm()) {
                            auto* alch_item = form->As<RE::AlchemyItem>();
                            if (alch_item) {
                                auto* effect = alch_item->GetCostliestEffectItem();
                                if (effect) {
                                    color = Hotbar::calculate_potion_color(effect);
                                }
                            }
                        }
                        break;
                    default:
                        type = slot_type::unknown;
                        break;
                    }

                }
                else {
                    type = slot_type::unknown;
                }
            }
        }
    }

    bool SlottedSkill::is_bindable_form(RE::FormID p_formID)
    {
        if (p_formID == 0U) {
            return false;
        }
        // Ask the classifier itself instead of restating its accepted form types here.
        // Anything it drops into slot_type::unknown has no arm in InputModeCast::process_input
        // and would seat a slot that renders but can never fire.
        SlottedSkill probe{};
        probe.update_skill_assignment(p_formID);
        return probe.type != slot_type::unknown && probe.type != slot_type::empty;
    }

    void SlottedSkill::update_art_assignment(uint32_t p_art_id)
    {
        clear();
        if (p_art_id == 0) {
            return;
        }
        art_id = p_art_id;
        type = slot_type::weapon_art;
        hand = hand_mode::auto_hand;
    }

    void SlottedSkill::update_action_assignment(uint32_t p_action_id)
    {
        clear();
        if (p_action_id == 0) {
            return;
        }
        action_id = p_action_id;
        type = slot_type::action;
        hand = hand_mode::auto_hand;
    }

    void SlottedSkill::clear()
    { 
        type = slot_type::empty;
        formID = 0U;
        art_id = 0U;
        action_id = 0U;
        hand = hand_mode::auto_hand;
        consumed = consumed_type::none;
        color = 0xFFFFFFFF;
    }

    bool SubBar::is_empty()
    { 
        return !std::any_of(m_slotted_skills.begin(), m_slotted_skills.end(), [](auto& elem) { return !elem.isEmpty(); });
    }

    void SubBar::clear() {
        for (auto& slot : m_slotted_skills) {
            slot.clear();
        }
    }

    void subbar_to_json(rj::Document& doc, SubBar& bar, rj::Value& bar_node, key_modifier mod)
    {
        rj::Value bar_array(rj::kArrayType);

        for (size_t i = 0U; i < bar.m_slotted_skills.size(); i++) {
            const auto& skill = bar.m_slotted_skills[i];
            // Abilities and Actions have no form (formID 0): they are written by id, like the
            // co-save's slot kinds, and every entry carries its hand so a round trip keeps it.
            if (skill.type == slot_type::weapon_art || skill.type == slot_type::action) {
                rj::Value slot_data(rj::kObjectType);
                slot_data.AddMember("index", i, doc.GetAllocator());
                if (skill.type == slot_type::weapon_art) {
                    slot_data.AddMember("ability", skill.art_id, doc.GetAllocator());
                } else {
                    slot_data.AddMember("action", skill.action_id, doc.GetAllocator());
                }
                slot_data.AddMember("hand", static_cast<uint32_t>(skill.hand), doc.GetAllocator());
                bar_array.PushBack(slot_data, doc.GetAllocator());
            } else if (!skill.isEmpty()) {
                RE::FormID id = skill.formID;
                auto form = RE::TESForm::LookupByID(id);
                if (form) {
                    auto file = form->GetFile(0);
                    if (file) {
                        const std::string_view& name = file->GetFilename();
                        RE::FormID localid = form->GetLocalFormID();

                        rj::Value slot_data(rj::kObjectType);
                        slot_data.AddMember("index", i, doc.GetAllocator());
                        slot_data.AddMember("form", localid, doc.GetAllocator());
                        rj::Value fn(name.data(), doc.GetAllocator());
                        slot_data.AddMember("file", fn, doc.GetAllocator());
                        slot_data.AddMember("hand", static_cast<uint32_t>(skill.hand), doc.GetAllocator());

                        bar_array.PushBack(slot_data, doc.GetAllocator());
                    } else {
                        logger::error("Could not save {}, no origin file found", id);
                    }
                } else {
                    logger::error("Could not save {}, form not found", id);
                }


            }
        }

        std::string bar_mod;
        switch (mod) {
            case key_modifier::ctrl:
                bar_mod = "ctrl";
                break;
            case key_modifier::shift:
                bar_mod = "shift";
                break;
            case key_modifier::alt:
                bar_mod = "alt";
                break;
            case key_modifier::none:
            default:
                bar_mod = "none";
                break;
        }

        rj::Value tag(bar_mod.c_str(), doc.GetAllocator());

        bar_node.AddMember(tag, bar_array, doc.GetAllocator());
    }

    void Hotbar::to_json(rj::Document& doc, uint32_t key, rj::Value& bars)
    { 
        rj::Value bar_node(rj::kObjectType);
        bar_node.AddMember("id", key, doc.GetAllocator());
        bool add{false};
        if (!m_bar.is_empty())
        {
            subbar_to_json(doc, m_bar, bar_node, key_modifier::none);
            add = true;
        }
        if (!m_ctrl_bar.is_empty()) {
            subbar_to_json(doc, m_ctrl_bar, bar_node, key_modifier::ctrl);
            add = true;
        }
        if (!m_shift_bar.is_empty()) {
            subbar_to_json(doc, m_shift_bar, bar_node, key_modifier::shift);
            add = true;
        }
        if (!m_alt_bar.is_empty()) {
            subbar_to_json(doc, m_alt_bar, bar_node, key_modifier::alt);
            add = true;
        }

        if (add) {
            bars.PushBack(bar_node, doc.GetAllocator());
        }
    }

    void Hotbar::clear() {
        m_bar.clear();
        m_ctrl_bar.clear();
        m_shift_bar.clear();
        m_alt_bar.clear();
    }
   
}
