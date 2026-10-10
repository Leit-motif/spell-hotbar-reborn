#include "clip_translation_driver.h"
#include "clip_translation.h"
#include "art_driver.h"
#include "ability_magnetism.h"
#include "msco_cast_driver.h"
#include "combo_cache.h"
#include "../logger/logger.h"
#include "../runtime_hooks.h"
#include "../game_data/game_data.h"
#include "../game_data/custom_ability_runtime.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

using namespace std::literals;

namespace SpellHotbar::casts::ClipTranslationDriver {

	namespace {
		std::mutex mutex;
		// A cast clip exists under the same name in BOTH full-body graphs (1hm_behavior
		// and magicbehavior) and both activate on a driver cast; only one advances its
		// localTime. Track every live activation and let apply() read time from the one
		// that moves — binding only the last activation froze motion whenever the inert
		// graph's clip activated second (SH2_Art_Clip is single-graph and never saw this).
		std::vector<RE::hkbClipGenerator*> bound_clips;
		std::string bound_name;
		std::vector<ClipTranslationKey> keys;
		ClipTranslation last_applied{};
		bool primed = false;

		// The clip's motion for this frame, handed to the player's character controller as velocity
		// (Havok units per second) so walls and other actors stop it. Placing the actor with
		// SetPosition instead walks Abilities straight through bodies. `motion_taken` goes true
		// when a controller update adds it.
		std::atomic<float> motion_vx{ 0.0f };
		std::atomic<float> motion_vy{ 0.0f };
		std::atomic<bool> motion_taken{ true };
		ClipTranslation pending_world_delta{};
		// Set when a frame's motion went untaken: the rest of that clip is placed with SetPosition,
		// so an Ability never loses its motion to a controller that is not updating.
		bool placement_mode = false;

		void clear_motion() noexcept
		{
			motion_vx.store(0.0f, std::memory_order_relaxed);
			motion_vy.store(0.0f, std::memory_order_relaxed);
			motion_taken.store(true, std::memory_order_relaxed);
			pending_world_delta = {};
		}

		void place(RE::PlayerCharacter* pc, ClipTranslation worldDelta)
		{
			auto pos = pc->GetPosition();
			pos.x += worldDelta.x;
			pos.y += worldDelta.y;
			pc->SetPosition(pos, true);
		}

		void add_motion(RE::bhkCharacterController* controller, RE::hkVector4& velocity)
		{
			const float vx = motion_vx.load(std::memory_order_relaxed);
			const float vy = motion_vy.load(std::memory_order_relaxed);
			if (vx == 0.0f && vy == 0.0f) {
				return;
			}
			auto* pc = RE::PlayerCharacter::GetSingleton();
			if (pc == nullptr || controller != pc->GetCharController()) {
				return;
			}
			velocity = velocity + RE::hkVector4(vx, vy, 0.0f, 0.0f);
			motion_taken.store(true, std::memory_order_relaxed);
		}

		struct SubobjectVtable
		{
			std::uintptr_t address{ 0 };
			std::size_t index{ 0 };
			std::uint32_t offset{ 0 };
		};

		// Which of a class's vtables belongs to its `bhkCharacterController` subobject, read from
		// the game's own RTTI rather than assumed from the index. `bhkCharProxyController`
		// inherits `hkpCharacterProxyListener` first, so its [0] is the listener's table: slot 7
		// there is a proxy callback, and hooking it as the velocity setter crashes the game 5 s
		// after a save load. `bhkCharRigidBodyController` has no CommonLib header to read the
		// order from. A table matches when its locator's offset equals the base's
		// displacement in the class hierarchy. Returns a zero address when none does.
		template <std::size_t N>
		[[nodiscard]] SubobjectVtable character_controller_vtable(const std::array<REL::VariantID, N>& vtables)
		{
			constexpr auto kBase = ".?AVbhkCharacterController@@"sv;
			const auto rva = [](std::uint32_t a_rva) { return REL::Relocation<const std::uint32_t*>{ REL::Offset(a_rva) }.get(); };
			for (std::size_t i = 0; i < N; ++i) {
				const auto vtbl = vtables[i].address();
				const auto* col = reinterpret_cast<const RE::RTTI::CompleteObjectLocator* const*>(vtbl)[-1];
				const auto* hierarchy = col ? col->classDescriptor.get() : nullptr;
				if (!hierarchy || !hierarchy->baseClassArray) {
					continue;
				}
				// The hierarchy's base array is an RVA to an array of RVAs, one per base class.
				const auto* bases = rva(hierarchy->baseClassArray.offset());
				for (std::uint32_t b = 0; b < hierarchy->numBaseClasses; ++b) {
					const auto* base = RE::RTTI::RVA<RE::RTTI::BaseClassDescriptor>{ bases[b] }.get();
					const auto* type = base ? base->typeDescriptor.get() : nullptr;
					if (type && type->mangled_name() == kBase &&
						base->pmd.mDisp == static_cast<std::int32_t>(col->offset)) {
						return SubobjectVtable{ vtbl, i, col->offset };
					}
				}
			}
			return {};
		}

		// vfunc 0x7 on both controller kinds the player can have. Knockback SKSE hooks the same
		// slot for its shoves; each hook calls the one it replaced, so both apply.
		void SetLinearVelocityProxy(RE::bhkCharacterController* a_this, const RE::hkVector4& a_velocity);
		void SetLinearVelocityRigid(RE::bhkCharacterController* a_this, const RE::hkVector4& a_velocity);
		REL::Relocation<decltype(SetLinearVelocityProxy)> _SetLinearVelocityProxy;
		REL::Relocation<decltype(SetLinearVelocityRigid)> _SetLinearVelocityRigid;

		void SetLinearVelocityProxy(RE::bhkCharacterController* a_this, const RE::hkVector4& a_velocity)
		{
			RE::hkVector4 velocity{ a_velocity };
			add_motion(a_this, velocity);
			_SetLinearVelocityProxy(a_this, velocity);
		}

		void SetLinearVelocityRigid(RE::bhkCharacterController* a_this, const RE::hkVector4& a_velocity)
		{
			RE::hkVector4 velocity{ a_velocity };
			add_motion(a_this, velocity);
			_SetLinearVelocityRigid(a_this, velocity);
		}

		void Activate(RE::hkbClipGenerator* a_this, const RE::hkbContext& a_context);
		void Deactivate(RE::hkbClipGenerator* a_this, const RE::hkbContext& a_context);
		REL::Relocation<decltype(Activate)> _Activate;
		REL::Relocation<decltype(Deactivate)> _Deactivate;

		// Cast clips bind too: the cast states carry no bAnimationDriven plant, and MSCO's
		// slight forward traverse is meant to play — animmotion consumed, legs authored.
		// The channel clip stays out: a channel must not translate.
		[[nodiscard]] bool is_shtb_clip(std::string_view name) noexcept
		{
			return name == "SH2_Art_Clip"sv || name == "SH2_CastRight_Clip"sv ||
			       name == "SH2_Cast2_Clip"sv || name == "SH2_Cast3_Clip"sv ||
			       name == "SH2_Cast4_Clip"sv;
		}

		[[nodiscard]] RE::hkaAnimation* bound_animation(RE::hkbClipGenerator* clip)
		{
			if (!clip || !clip->binding || !clip->binding->animation) {
				return nullptr;
			}
			return clip->binding->animation.get();
		}

		std::vector<ClipTranslationKey> parse_keys(const RE::hkaAnimation* animation)
		{
			std::vector<ClipTranslationKey> out;
			if (!animation) {
				return out;
			}
			for (const auto& track : animation->annotationTracks) {
				bool found = false;
				for (std::int32_t i = 0; i < track.annotations.size(); ++i) {
					const auto& annotation = track.annotations[i];
					const char* text = annotation.text.c_str();
					if (!text) {
						continue;
					}
					const auto parsed = parse_animmotion_text(text);
					if (!parsed) {
						continue;
					}
					out.push_back(ClipTranslationKey{
						.time = annotation.time,
						.x = parsed->x,
						.y = parsed->y,
						.z = parsed->z,
					});
					found = true;
				}
				if (found) {
					break;
				}
			}
			std::sort(out.begin(), out.end(),
				[](const ClipTranslationKey& a, const ClipTranslationKey& b) { return a.time < b.time; });
			return out;
		}

		void bind_clip(RE::hkbClipGenerator* clip)
		{
			const char* raw = clip && clip->name.c_str() ? clip->name.c_str() : "";
			if (!is_shtb_clip(raw)) {
				return;
			}
			auto* animation = bound_animation(clip);
			auto parsed = parse_keys(animation);
			if (std::string_view{ raw } == "SH2_Art_Clip"sv) {
				bool has_win_open{ false };
				int hit_frame_count{ 0 };
				float last_hit_time{ -1.0f };
				std::vector<BowStringCue> bow_cues;
				std::vector<ArrowHitCue> arrow_hit_cues;
				if (animation) {
					for (const auto& track : animation->annotationTracks) {
						for (std::int32_t i = 0; i < track.annotations.size(); ++i) {
							const char* text = track.annotations[i].text.c_str();
							if (!text) {
								continue;
							}
							const std::string_view tag{ text };
							if (is_ability_win_open_event(tag)) {
								has_win_open = true;
							}
							if (is_hit_frame_annotation(tag)) {
								++hit_frame_count;
								last_hit_time = std::max(last_hit_time, track.annotations[i].time);
							}
							if (const auto cue = bow_string_cue_for(tag); cue != BowStringEvent::none) {
								bow_cues.push_back(BowStringCue{ .time = track.annotations[i].time, .event = cue });
							}
							if (const auto effect = arrow_hit_effect_for(tag); effect != ArrowHitEffect::none) {
								arrow_hit_cues.push_back(ArrowHitCue{ .time = track.annotations[i].time, .effect = effect });
							}
						}
					}
					if (const int selector = GameData::get_art_selector(); selector > 0) {
						const auto art_id = static_cast<std::uint32_t>(selector);
						if (const ArtDefinition* art = GameData::get_art(art_id)) {
							inject_custom_ability_pie(animation, *art);
						}
					}
				}
				ArtDriver::bind_latch(has_win_open, hit_frame_count, last_hit_time, animation, std::move(bow_cues),
					std::move(arrow_hit_cues));
			}
			std::lock_guard lock{ mutex };
			if (raw != bound_name) {
				// A different clip took over (combo chain, or a fresh cast): the new
				// name owns the key set and the candidate list from here.
				bound_clips.clear();
				bound_name = raw;
				keys = std::move(parsed);
				last_applied = {};
				primed = false;
				placement_mode = false;
				if (keys.empty()) {
					logger::warn("SH2 motion: {} activated with no animmotion keys", raw);
				} else {
					logger::info("SH2 motion: bound {} ({} animmotion keys)", raw, keys.size());
				}
			}
			if (std::find(bound_clips.begin(), bound_clips.end(), clip) == bound_clips.end()) {
				bound_clips.push_back(clip);
			}
		}

		void unbind_clip(RE::hkbClipGenerator* clip)
		{
			std::lock_guard lock{ mutex };
			const auto it = std::find(bound_clips.begin(), bound_clips.end(), clip);
			if (it != bound_clips.end()) {
				bound_clips.erase(it);
				if (bound_clips.empty()) {
					bound_name.clear();
					keys.clear();
					last_applied = {};
					primed = false;
				}
			}
		}

		[[nodiscard]] bool is_art_clip(RE::hkbClipGenerator* clip) noexcept
		{
			const char* raw = clip && clip->name.c_str() ? clip->name.c_str() : "";
			return std::string_view{ raw } == "SH2_Art_Clip"sv;
		}

		void Activate(RE::hkbClipGenerator* a_this, const RE::hkbContext& a_context)
		{
			_Activate(a_this, a_context);
			bind_clip(a_this);
			if (is_art_clip(a_this)) {
				ArtDriver::observe_clip_activate();
			}
		}

		void Deactivate(RE::hkbClipGenerator* a_this, const RE::hkbContext& a_context)
		{
			if (is_art_clip(a_this)) {
				ArtDriver::observe_clip_deactivate();
			}
			unbind_clip(a_this);
			_Deactivate(a_this, a_context);
		}
	}

	void install()
	{
		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_hkbClipGenerator[0] };
		_Activate = RuntimeHooks::write_verified_vfunc(vtbl, 0x4, Activate, "hkbClipGenerator::Activate");
		_Deactivate = RuntimeHooks::write_verified_vfunc(vtbl, 0x7, Deactivate, "hkbClipGenerator::Deactivate");
		logger::info("SH2 motion: clip translation hooks installed");

		// Without a velocity hook the controller never takes the motion, and apply() falls back
		// to placing the actor after one frame, so a missing table costs collision, not motion.
		if (const auto proxy = character_controller_vtable(RE::VTABLE_bhkCharProxyController); proxy.address) {
			REL::Relocation<std::uintptr_t> table{ proxy.address };
			_SetLinearVelocityProxy = RuntimeHooks::write_verified_vfunc(table, 0x7, SetLinearVelocityProxy,
				"bhkCharProxyController::SetLinearVelocity");
			logger::info("SH2 motion: velocity hook on bhkCharProxyController vtable [{}] (subobject +0x{:X})",
				proxy.index, proxy.offset);
		} else {
			logger::error("SH2 motion: no bhkCharacterController vtable in bhkCharProxyController's RTTI; Ability motion stays placed");
		}
		if (const auto rigid = character_controller_vtable(RE::VTABLE_bhkCharRigidBodyController); rigid.address) {
			REL::Relocation<std::uintptr_t> table{ rigid.address };
			_SetLinearVelocityRigid = RuntimeHooks::write_verified_vfunc(table, 0x7, SetLinearVelocityRigid,
				"bhkCharRigidBodyController::SetLinearVelocity");
			logger::info("SH2 motion: velocity hook on bhkCharRigidBodyController vtable [{}] (subobject +0x{:X})",
				rigid.index, rigid.offset);
		} else {
			logger::error("SH2 motion: no bhkCharacterController vtable in bhkCharRigidBodyController's RTTI; Ability motion stays placed");
		}
	}

	void apply(RE::PlayerCharacter* pc, float dt)
	{
		// Last frame's motion, if no controller update took it, was never applied.
		const bool controller_missed = !motion_taken.load(std::memory_order_relaxed);
		const ClipTranslation missed_delta = pending_world_delta;
		clear_motion();
		if (!pc) {
			return;
		}
		// Before the motion is read, so this frame's step follows the new heading.
		AbilityMagnetism::update(pc, dt, ArtDriver::is_active());
		if (!ArtDriver::is_active() && !MscoCastDriver::is_active()) {
			return;
		}

		ClipTranslation worldDelta{};
		{
			std::lock_guard lock{ mutex };
			if (bound_clips.empty() || keys.empty()) {
				return;
			}
			float clipTime = 0.0f;
			for (const auto* clip : bound_clips) {
				clipTime = std::max(clipTime, clip->localTime);
			}
			const auto cumulative = interpolate_clip_translation(keys, clipTime);
			if (!primed) {
				last_applied = cumulative;
				primed = true;
				return;
			}
			const auto localDelta = translation_delta(last_applied, cumulative);
			last_applied = cumulative;
			if (localDelta.x == 0.0f && localDelta.y == 0.0f) {
				return;
			}
			worldDelta = local_to_world(localDelta, pc->GetAngleZ());
		}

		if (controller_missed && !placement_mode) {
			placement_mode = true;
			worldDelta.x += missed_delta.x;
			worldDelta.y += missed_delta.y;
			logger::warn("SH2 motion: the character controller did not take {}'s motion; placing the actor for the rest of the clip",
				bound_name);
		}
		if (placement_mode) {
			place(pc, worldDelta);
			return;
		}
		const auto velocity = frame_delta_to_havok_velocity(worldDelta, dt, RE::bhkWorld::GetWorldScale());
		pending_world_delta = worldDelta;
		motion_vx.store(velocity.x, std::memory_order_relaxed);
		motion_vy.store(velocity.y, std::memory_order_relaxed);
		motion_taken.store(false, std::memory_order_relaxed);
	}

	void reset()
	{
		std::lock_guard lock{ mutex };
		bound_clips.clear();
		bound_name.clear();
		keys.clear();
		last_applied = {};
		primed = false;
		placement_mode = false;
		clear_motion();
	}

	float art_clip_time()
	{
		std::lock_guard lock{ mutex };
		if (bound_name != "SH2_Art_Clip"sv || bound_clips.empty()) {
			return -1.0f;
		}
		// Both full-body graphs activate the clip and only one advances; see `bound_clips`.
		float time = 0.0f;
		for (const auto* clip : bound_clips) {
			time = std::max(time, clip->localTime);
		}
		return time;
	}
}
