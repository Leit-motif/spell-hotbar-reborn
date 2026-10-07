#include "voice_cast_driver.h"
#include "../game_data/game_data.h"
#include "../input/input.h"
#include "../logger/logger.h"

namespace SpellHotbar::casts::VoiceCastDriver {

	namespace {
		RE::SpellItem* proxy_power{ nullptr };
		RE::TESShout* proxy_shout{ nullptr };
		RE::TESWordOfPower* proxy_word{ nullptr };
		RE::TESForm* previous_power{ nullptr };
		bool active{ false };
		bool proxy_was_known{ false };
		bool shout_added{ false };
		// A physical key produces a down event, per-frame repeats with a growing hold time,
		// and a release. Sending only the down reads as an instant tap (the word fired 52ms
		// after the press) and leaves the engine holding a phantom button that blocks
		// attack/sheathe controls. Track the hold and reproduce all three phases.
		bool button_down{ false };
		float button_held_secs{ 0.0f };

		bool process_voice_button(float value, float held_duration)
		{
			// Direct ShoutHandler::ProcessButton is accepted but never transitions the graph
			// (two canaries). Real hotbar shout casts work because their button event travels
			// the full input dispatch, so the synthetic press must too: push a fully-formed
			// event onto the game's own input queue and let next frame's dispatch deliver it
			// exactly like a physical keypress.
			auto queue = RE::BSInputEventQueue::GetSingleton();
			auto user_events = RE::UserEvents::GetSingleton();
			if (!queue || !user_events) {
				logger::error("Voice cast driver could not resolve BSInputEventQueue/UserEvents");
				return false;
			}

			auto [device, key] = Input::get_shout_key_and_device();
			auto event = RE::ButtonEvent::Create(device, user_events->shout, key, value, held_duration);
			if (!event) {
				logger::error("Voice cast driver could not allocate a shout button event");
				return false;
			}

			// The queue owns the event from here; the dispatcher consumes it next frame.
			queue->PushOntoInputQueue(event);
			logger::debug("Voice cast driver queued shout button (value={}, held={})", value, held_duration);
			return true;
		}
	}

	bool initialize()
	{
		if (proxy_shout) {
			return true;
		}

		if (!GameData::spellhotbar_unbind_slot || !GameData::equip_slot_voice) {
			logger::error("Voice cast driver requires SpellHotbar_unbind and the Voice equip slot");
			return false;
		}

		if (GameData::spellhotbar_unbind_slot->GetSpellType() != RE::MagicSystem::SpellType::kLesserPower ||
			GameData::spellhotbar_unbind_slot->GetEquipSlot() != GameData::equip_slot_voice) {
			logger::error("SpellHotbar_unbind is not a Voice-slot Lesser Power");
			return false;
		}

		// A lesser power selected in the voice slot never enters the shout graph (measured:
		// spellfire fired but IsShouting stayed false and no shout clip played). A genuine
		// TESShout is the form ShoutHandler's animation path is built around, so construct a
		// harmless one at runtime -- the same approach Equip Spells As Shouts ships with.
		auto word_factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::TESWordOfPower>();
		auto shout_factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::TESShout>();
		auto spell_factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::SpellItem>();
		if (!word_factory || !shout_factory || !spell_factory) {
			logger::error("Voice cast driver could not resolve WOOP/SHOU/SPEL form factories");
			return false;
		}

		proxy_word = word_factory->Create();
		proxy_shout = shout_factory->Create();
		proxy_power = spell_factory->Create();
		if (!proxy_word || !proxy_shout || !proxy_power) {
			logger::error("Voice cast driver could not create the proxy shout forms");
			proxy_word = nullptr;
			proxy_shout = nullptr;
			proxy_power = nullptr;
			return false;
		}

		// The word payload must look like a vanilla shout word: a Voice Power with a real
		// charge time. The first proxy reused the shipped do-nothing LESSER power and the
		// engine fired the word 56ms after the press with no charge phase or animation at
		// all -- the spell type/charge profile is what buys the inhale. Clone the shipped
		// dummy's harmless effect list, then reshape the profile.
		auto dummy = GameData::spellhotbar_unbind_slot;
		proxy_power->fullName = "";
		proxy_power->effects = dummy->effects;
		proxy_power->equipSlot = dummy->equipSlot;
		proxy_power->data = dummy->data;
		proxy_power->data.spellType = RE::MagicSystem::SpellType::kVoicePower;
		proxy_power->data.chargeTime = 0.9f;
		proxy_power->data.castingType = RE::MagicSystem::CastingType::kFireAndForget;
		proxy_power->data.delivery = RE::MagicSystem::Delivery::kSelf;

		proxy_word->fullName = "";
		proxy_word->translation = "";
		proxy_word->formFlags |= RE::TESForm::RecordFlags::kKnown;

		proxy_shout->fullName = "";
		for (auto& variation : proxy_shout->variations) {
			variation.word = proxy_word;
			variation.spell = proxy_power;
			variation.recoveryTime = 0.0f;
		}

		logger::info("Voice cast driver initialized with proxy shout {:08X} (payload {:08X})",
			proxy_shout->GetFormID(), proxy_power->GetFormID());
		return true;
	}

	bool begin(RE::PlayerCharacter* pc)
	{
		if (!pc || active || (!proxy_shout && !initialize())) {
			return false;
		}
		auto equip_manager = RE::ActorEquipManager::GetSingleton();
		if (!equip_manager) {
			logger::error("Voice cast driver could not resolve ActorEquipManager");
			return false;
		}

		// ShoutHandler only acts on a voice form the actor knows, so the runtime shout is
		// taught once per session; its word is unlocked the same way Game.UnlockWord would.
		if (!shout_added) {
			pc->AddShout(proxy_shout);
			pc->UnlockWord(proxy_word);
			shout_added = true;
		}

		auto& actor_data = pc->GetActorRuntimeData();
		previous_power = actor_data.selectedPower;
		equip_manager->EquipShout(pc, proxy_shout);
		if (actor_data.selectedPower != proxy_shout) {
			logger::error("Voice cast driver could not equip proxy shout {:08X}", proxy_shout->GetFormID());
			previous_power = nullptr;
			return false;
		}
		active = true;
		logger::debug("Voice cast driver selected proxy shout {:08X}", proxy_shout->GetFormID());
		return true;
	}

	bool press()
	{
		if (!active || button_down) {
			return active && button_down;
		}
		if (!process_voice_button(1.0f, 0.0f)) {
			return false;
		}
		button_down = true;
		button_held_secs = 0.0f;
		return true;
	}

	void tick(float delta)
	{
		if (active && button_down) {
			button_held_secs += delta;
			process_voice_button(1.0f, button_held_secs);
		}
	}

	bool release(float)
	{
		if (!active || !button_down) {
			return false;
		}
		if (!process_voice_button(0.0f, std::max(button_held_secs, 0.001f))) {
			return false;
		}
		button_down = false;
		return true;
	}

	bool replay(float held_duration)
	{
		if (!active || button_down) {
			return false;
		}
		if (!process_voice_button(1.0f, 0.0f)) {
			return false;
		}
		return process_voice_button(0.0f, std::max(held_duration, 0.001f));
	}

	void restore(RE::PlayerCharacter* pc)
	{
		if (!active) {
			return;
		}

		// Never leave the engine holding a phantom voice button -- that locks the player out
		// of attacking and sheathing until something else releases it.
		if (button_down) {
			process_voice_button(0.0f, std::max(button_held_secs, 0.001f));
			button_down = false;
		}

		if (!pc) {
			pc = RE::PlayerCharacter::GetSingleton();
		}
		if (pc) {
			auto& actor_data = pc->GetActorRuntimeData();
			if (actor_data.selectedPower == proxy_shout) {
				auto equip_manager = RE::ActorEquipManager::GetSingleton();
				if (equip_manager && previous_power && previous_power->GetFormType() == RE::FormType::Shout) {
					equip_manager->EquipShout(pc, previous_power->As<RE::TESShout>());
				}
				else if (equip_manager && previous_power && previous_power->GetFormType() == RE::FormType::Spell) {
					equip_manager->EquipSpell(pc, previous_power->As<RE::SpellItem>(), GameData::equip_slot_voice);
				}
				else {
					actor_data.selectedPower = previous_power;
				}
			}
		}

		previous_power = nullptr;
		proxy_was_known = false;
		active = false;
	}

	void cancel(RE::PlayerCharacter* pc)
	{
		if (active && pc) {
			pc->NotifyAnimationGraph("ShoutStop"sv);
		}
		restore(pc);
	}

	bool is_active()
	{
		return active;
	}
}
