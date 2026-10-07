#include "spell_cast_data.h"
#include "cast_anim_ids.h"
#include "game_data.h"

namespace SpellHotbar::GameData {

    Spell_cast_data::Spell_cast_data() : gcd(-1.0f), cooldown(-1.0f), casttime(-1.0f), animation(-1), animation2(-1), casteffectid(0U), overlay_icon(GameData::DefaultIconType::UNKNOWN) {};
    bool Spell_cast_data::is_empty()
    {
        return animation <= 0 && animation2 <= 0 && gcd < 0.0f && cooldown < 0.0f && casttime < 0.0f && casteffectid == 0U && overlay_icon == GameData::DefaultIconType::UNKNOWN;
    }

    void Spell_cast_data::fill_default_values_from_spell(const RE::SpellItem* spell)
    {
        if (casttime < 0.0f) {
            casttime = spell->GetChargeTime();
        }
        if (spell->GetSpellType() == RE::MagicSystem::SpellType::kSpell || spell->GetSpellType() == RE::MagicSystem::SpellType::kScroll) {
            //min casttime of 0.25f for actual spells
            casttime = std::max(0.25f, casttime);
        }

        if (gcd < 0.0f) {
            gcd = 0.0f;
        }
        if (cooldown < 0.0f) {
            cooldown = 0.0f;
        }
        if (animation < 0) {
            animation = Spell_cast_data::chose_default_anim_for_spell(spell, -1, false);
        }
        if (animation2 < 0) {
            animation2 = Spell_cast_data::chose_default_anim_for_spell(spell, -1, true);
        }
        if (overlay_icon == DefaultIconType::UNKNOWN && spell->GetSpellType() == RE::MagicSystem::SpellType::kScroll) {
            overlay_icon = DefaultIconType::SCROLL_OVERLAY;
        }

    }

    void Spell_cast_data::fill_default_values_from_shout(const RE::TESShout* shout)
    {
        if (casttime < 0.0f) {
            casttime = 0.0f;
        }
        if (gcd < 0.0f) {
            gcd = 0.0f;
        }
        if (cooldown < 0.0f) {
            cooldown = 0.0f;
        }
        if (animation < 0) {
            animation = -1;
        }
        if (animation2 < 0) {
            animation2 = -1;
        }
        //TODO generic shout overlay?
    }

    void Spell_cast_data::fill_and_override_from_non_default_values(const Spell_cast_data& other)
    {
        if (other.casttime > 0.0f) {
            casttime = other.casttime;
        }

        if (other.gcd > 0.0f) {
            gcd = other.gcd;
        }
        if (other.cooldown > 0.0f) {
            cooldown = other.cooldown;
        }
        if (other.animation >= 0) {
            animation = other.animation;
        }
        if (other.animation2 >= 0) {
            animation2 = other.animation2;
        }
  
        if (other.overlay_icon != DefaultIconType::UNKNOWN) {
            overlay_icon = other.overlay_icon;
        }

        casteffectid = other.casteffectid;

    }

    inline bool is_ward_spell(const RE::SpellItem* spell) {
        return spell->effects.size() > 0 && spell->effects[0]->baseEffect && spell->effects[0]->baseEffect->HasKeywordID(0x1EA69);
    }

    uint16_t Spell_cast_data::chose_default_anim_for_spell(const RE::TESForm* form, int anim, bool anim2)
    {
        uint16_t ret{ 0U };

        if (anim < 0) {
            const CastAnimSlot slot = anim2 ? CastAnimSlot::variant : CastAnimSlot::primary;
            if (form->GetFormType() == RE::FormType::Spell || form->GetFormType() == RE::FormType::Scroll) {
                const RE::SpellItem* spell = form->As<RE::SpellItem>();

                const bool self = spell->GetDelivery() == RE::MagicSystem::Delivery::kSelf;
                const bool two_handed = spell->IsTwoHanded();
                if (spell->GetCastingType() == RE::MagicSystem::CastingType::kConcentration) {
                    ret = anim_id(concentration_family(two_handed, self, is_ward_spell(spell)), slot);
                }
                else {
                    ret = anim_id(fire_and_forget_family(two_handed, self), slot);
                }

            }
        }
        else {
            ret = static_cast<uint16_t>(anim);
        }
        return ret;
    }

    float Spell_cast_data::get_ritual_conc_anim_prerelease_time(int anim) {
        float pre_release_anim{ 0.0f };
        if (anim == kRitualConc.primary) {
            auto cam = RE::PlayerCamera::GetSingleton();
            if (cam && cam->IsInFirstPerson()) {
                pre_release_anim = 1.5f;
            }
            else
            {
                pre_release_anim = 1.0f;
            }
        }
        return pre_release_anim;
    }
}