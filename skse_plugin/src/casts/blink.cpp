#include "blink.h"
#include "ability_magnetism.h"
#include "blink_rules.h"
#include "casting_controller.h"
#include "unpaused_clock.h"
#include "../game_data/art_definition.h"
#include "../game_data/custom_ability_config.h"
#include "../game_data/game_data.h"
#include "../input/input.h"
#include "../logger/logger.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace SpellHotbar::casts::Blink {

	namespace {
		// About six metres. Walls shorten it; nothing lengthens it.
		constexpr float kDistance = 400.0f;
		constexpr float kMaxSlopeDegrees = 50.0f;

		// The constants below are skse-blink's, kept as measured there.
		constexpr float kClearance = 40.0f;   // pull back from a wall we hit
		constexpr float kMinTravel = 10.0f;   // shortest blink worth doing
		// No blink at all when the nearest obstacle is inside this, so a spammed press cannot
		// creep through a wall a few units at a time.
		constexpr float kDeadzone = kClearance + kMinTravel;
		constexpr float kGroundProbeUp = 120.0f;
		constexpr float kGroundProbeDown = 600.0f;
		constexpr float kGroundLift = 4.0f;
		constexpr float kMaxSnapDown = 256.0f;
		constexpr float kMaxSnapUp = 256.0f;
		constexpr float kMaxClimbFollow = 1024.0f;
		constexpr float kGroundedTolerance = 32.0f;
		// Deeper than any drop a ledge Blink should keep: nothing this far down is the void.
		constexpr float kVoidProbeDown = 8192.0f;
		// SkyParkour's landing roll waits for half a second of fall; the timer is pulled back to
		// just past that, never zeroed, so the roll still fires.
		constexpr float kRollFallTimeGate = 0.6f;

		// One obstacle ray per body height, so anything shorter than the chest still blocks. The
		// ankle ray is what tells a slope (walked along) from a cliff (a wall).
		constexpr float kPathHeights[] = { 8.0f, 32.0f, 56.0f, 80.0f, 104.0f, 128.0f };

		// A stair riser reads as a wall to every slope limit; it is a step when the air above
		// the contact stays clear for a tread's depth.
		constexpr float kStepLift = 48.0f;
		constexpr float kStepClearDepth = 32.0f;
		constexpr float kStepProbeLength = 64.0f;

		// The body volume at the landing, probed outward from its centre: a ray starting inside a
		// boulder can miss its back faces, one reaching it from outside cannot.
		constexpr float kBodyProbeHeights[] = { 16.0f, 48.0f, 80.0f, 112.0f };
		constexpr int kBodyProbeDirections = 8;
		constexpr float kBodyProbeLength = 28.0f;  // under the controller radius
		constexpr float kBodyProbeTop = 128.0f;
		constexpr float kFitStep = 25.0f;
		constexpr int kMaxFitPulls = 8;

		constexpr float kRadiansPerDegree = 3.14159265f / 180.0f;
		constexpr float kTwoPi = 6.28318531f;

		// The look, in vanilla's conjuration language (issue #34): vanilla's summon portal, retimed into
		// rifts by python_scripts/blink_vfx/build_blink_nifs.py, opens where the caster leaves and
		// flares where they land; Whirlwind Sprint's radial blur covers the jump, and the model is
		// hidden for the instant of travel. The portal billboards itself round the up axis, so it never
		// shows edge-on. Each rift is a placed activator, whose mesh's own controllers play it from
		// load; a temporary effect never loaded the mesh at all (measured, issue #34).
		constexpr const char* kDepartureModel = R"(SpellHotbar2\blink\departure.nif)";
		constexpr const char* kArrivalModel = R"(SpellHotbar2\blink\arrival.nif)";
		constexpr RE::FormID kTravelBlur = 0x000440D4;  // MAGShoutSprint03
		// Two short vanilla sounds, one per end of the jump. The conjure-portal sound peaks 2.7 s in,
		// long after the jump has landed; the illusion release whoosh peaks at 75 ms and is gone by
		// 175 ms, and the conjure impact lands its thump at 25 ms (measured, issue #34).
		constexpr const char* kVanishSound = "MAGIllusionReleaseSelfSD";
		constexpr const char* kArriveSound = "MAGConjureImpactSD";

		// Vampiric Blink (issue #34): the same jump in the Vampire Lord's Bats power, all vanilla
		// Dawnguard. The swarm mesh bursts at both ends, the body dissolves into inky smoke as it
		// leaves and reforms out of it as it lands, and the power's own two sounds play. No travel
		// blur: Whirlwind Sprint's blue streaks belong to the conjuration look.
		constexpr const char* kBatSwarmModel = R"(DLC01\Magic\DLC1VampLordBatsFX.nif)";
		constexpr const char* kBatVanishSound = "MAGPowersBats";
		constexpr const char* kBatArriveSound = "MAGPowersBatsFinish";
		constexpr const char* kDawnguard = "Dawnguard.esm";
		constexpr RE::FormID kBatDissolveShader = 0x018EF0;  // DLC1VampireBatsFXS
		constexpr RE::FormID kBatReformShader = 0x018EF1;    // DLC1VampireBatsReformFXS
		constexpr float kBatDissolveSeconds = 0.3f;
		constexpr float kBatReformSeconds = 0.8f;
		// The model is hidden for the instant of travel and comes back still glowing, so it reads as
		// materializing out of the light rather than popping in (issue #34).
		constexpr double kHiddenSeconds = 0.15;

		// The body glow: the caster's skin and gear turn white at the press, stay white through the
		// hide, and fade back through conjuration violet after landing (issue #34). It is a membrane
		// fill on the body's own geometry,
		// cloned from vanilla's short frost shader for its additive cloud-tile fill; vanilla's
		// invisibility shader has no fill at all, only sparks, and its sparks read red.
		constexpr RE::FormID kGlowDonor = 0x000435A3;  // FrostShortFXShader
		constexpr float kGlowFullSeconds = 0.25f;  // the hide plus a beat of solid white after it
		constexpr float kGlowFadeSeconds = 0.35f;
		constexpr float kGlowSeconds = kGlowFullSeconds + kGlowFadeSeconds;

		// A short dip in the world's speed at the jump, to give it weight (issue #34): the
		// world drops to kDipScale at the press, holds for kDipHoldSeconds of real time, and eases
		// back over kDipEaseSeconds. It runs on the wall clock, since the game's own delta is the
		// thing being slowed.
		// Half speed could not be felt at all (issue #34).
		constexpr float kDipScale = 0.25f;
		constexpr double kDipHoldSeconds = 0.08;
		constexpr double kDipEaseSeconds = 0.12;

		// Whirlwind Sprint's own strength washes the screen white for a frame; this keeps the
		// streaks and the blue tint without hiding the room (measured, issue #34).
		constexpr float kTravelBlurStrength = 0.6f;

		// The rifts play out well inside this; then each reference is deleted so a spammed Blink
		// leaves nothing in the cell.
		constexpr double kRiftLifeSeconds = 4.0;

		// Untouchable for this long from the jump, like a dodge roll's invulnerability (issue #34).
		// The engine's cached ghost flag is the one TK Dodge RE and IFrame Generator RE
		// set: melee swings and projectiles pass through a ghost.
		constexpr double kIframeSeconds = 1.0;

		// What has to happen a beat after the jump: the model shown again, the rifts removed, the
		// ghost flag cleared.
		struct Pending {
			double reveal_at{ 0.0 };
			bool hidden{ false };
			double iframe_until{ 0.0 };
			bool ghosted{ false };
			struct Rift {
				double placed_at{ 0.0 };
				RE::ObjectRefHandle ref;
				bool shown{ false };
			};
			std::vector<Rift> rifts;
		};
		Pending pending;

		// The time dip in flight: when it began on the wall clock, the multiplier it found and
		// gives back, and the last one it set. If something else has changed the multiplier since,
		// the dip lets go without restoring, so it never undoes another mod's slow motion.
		struct TimeDip {
			bool active{ false };
			std::chrono::steady_clock::time_point started;
			float base{ 1.0f };
			float last_set{ 1.0f };
		};
		TimeDip dip;

		void set_time_scale(float scale)
		{
			if (auto* timer = RE::BSTimer::GetSingleton()) {
				timer->SetGlobalTimeMultiplier(scale, true);
			}
			dip.last_set = scale;
		}

		void start_dip()
		{
			if (!dip.active) {
				dip.base = RE::BSTimer::QGlobalTimeMultiplier();
			}
			dip.active = true;
			dip.started = std::chrono::steady_clock::now();
			set_time_scale(dip.base * kDipScale);
			logger::info("SH2 blink: time dip to {:.2f} (from {:.2f})", RE::BSTimer::QGlobalTimeMultiplier(), dip.base);
		}

		void update_dip()
		{
			if (!dip.active) {
				return;
			}
			if (std::abs(RE::BSTimer::QGlobalTimeMultiplier() - dip.last_set) > 1e-3f) {
				dip.active = false;
				logger::info("SH2 blink: time dip let go, the multiplier was changed elsewhere");
				return;
			}
			const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - dip.started).count();
			if (t >= kDipHoldSeconds + kDipEaseSeconds) {
				set_time_scale(dip.base);
				dip.active = false;
				logger::info("SH2 blink: time dip over after {:.0f} ms, speed back to {:.2f}", t * 1000.0,
					RE::BSTimer::QGlobalTimeMultiplier());
				return;
			}
			// Smoothstep from the dipped speed back to the base once the hold is over.
			const double e = std::clamp((t - kDipHoldSeconds) / kDipEaseSeconds, 0.0, 1.0);
			const double s = e * e * (3.0 - 2.0 * e);
			set_time_scale(dip.base * static_cast<float>(kDipScale + (1.0 - kDipScale) * s));
		}

		BlinkGcd gcd;
		std::atomic<float> stick_x{ 0.0f };
		std::atomic<float> stick_y{ 0.0f };
		std::optional<RE::NiPoint2> test_input;

		// Gameplay time, so a GCD does not run out behind a menu (casts/unpaused_clock.h).
		double now_seconds()
		{
			return UnpausedClock::now_ms() / 1000.0;
		}

		struct MoveInput {
			float x{ 0.0f };
			float y{ 0.0f };
			const char* source{ "nothing held" };
		};

		// The movement keys as the keyboard device holds them, through the control map so a
		// rebind still reads right. The root stops the character, not the keys.
		bool read_keyboard(MoveInput& out)
		{
			auto* map = RE::ControlMap::GetSingleton();
			auto* events = RE::UserEvents::GetSingleton();
			auto* devices = RE::BSInputDeviceManager::GetSingleton();
			auto* keyboard = devices ? devices->GetKeyboard() : nullptr;
			if (!map || !events || !keyboard) {
				return false;
			}
			const auto held = [&](const RE::BSFixedString& event) {
				const auto key = map->GetMappedKey(event, RE::INPUT_DEVICE::kKeyboard);
				// DirectInput's key state, read raw: `IsPressed` would link the device's vtable,
				// which CommonLib does not export.
				return key < 0x100 && (keyboard->GetRuntimeData().curState[key] & 0x80) != 0;
			};
			float x = 0.0f;
			float y = 0.0f;
			if (held(events->forward)) { y += 1.0f; }
			if (held(events->back)) { y -= 1.0f; }
			if (held(events->strafeRight)) { x += 1.0f; }
			if (held(events->strafeLeft)) { x -= 1.0f; }
			if (x == 0.0f && y == 0.0f) {
				return false;
			}
			out = MoveInput{ x, y, "movement keys" };
			return true;
		}

		MoveInput read_move_input()
		{
			if (test_input) {
				const MoveInput seam{ test_input->x, test_input->y, "test input" };
				test_input.reset();
				return seam;
			}
			const float sx = stick_x.load(std::memory_order_relaxed);
			const float sy = stick_y.load(std::memory_order_relaxed);
			if (std::sqrt(sx * sx + sy * sy) >= kBlinkInputDeadzone) {
				return MoveInput{ sx, sy, "left stick" };
			}
			MoveInput keys;
			if (read_keyboard(keys)) {
				return keys;
			}
			return {};
		}

		RE::NiPoint3 heading_point(const BlinkHeading& h)
		{
			return RE::NiPoint3{ h.x, h.y, 0.0f };
		}

		// The player's own group with the character-controller layer, so no ray hits the player.
		std::uint32_t ray_filter(RE::PlayerCharacter* pc)
		{
			RE::CFilter filter;
			pc->GetCollisionFilterInfo(filter);
			return ((filter.filter >> 16) << 16) | static_cast<std::uint32_t>(RE::COL_LAYER::kCharController);
		}

		struct Ray {
			RE::NiPoint3 point{};
			bool hit{ false };
			float fraction{ 1.0f };
			float slope_degrees{ 90.0f };  // 90 is a wall
		};

		// A missing or unusable normal reads as a wall, so a runtime that does not fill it in
		// blocks rather than lets a Blink through geometry.
		float slope_from_normal(const RE::hkVector4& normal)
		{
			alignas(16) float raw[4]{};
			_mm_storeu_ps(raw, normal.quad);
			const float length = std::sqrt(raw[0] * raw[0] + raw[1] * raw[1] + raw[2] * raw[2]);
			if (!std::isfinite(length) || length < 1e-4f) {
				return 90.0f;
			}
			return std::acos(std::clamp(raw[2] / length, -1.0f, 1.0f)) / kRadiansPerDegree;
		}

		Ray raycast(RE::bhkWorld* world, const RE::NiPoint3& from, const RE::NiPoint3& to, std::uint32_t filter)
		{
			const float scale = RE::bhkWorld::GetWorldScale();
			RE::bhkPickData pick{};
			pick.rayInput.from = from * scale;
			pick.rayInput.to = to * scale;
			pick.rayInput.filterInfo.filter = filter;
			world->PickObject(pick);

			Ray ray;
			ray.hit = pick.rayOutput.HasHit();
			ray.fraction = ray.hit ? pick.rayOutput.hitFraction : 1.0f;
			ray.point = ray.hit ? from + (to - from) * ray.fraction : to;
			ray.slope_degrees = ray.hit ? slope_from_normal(pick.rayOutput.normal) : 90.0f;
			return ray;
		}

		bool walkable(float slope_degrees)
		{
			return slope_degrees <= kMaxSlopeDegrees;
		}

		struct Probe {
			RE::bhkWorld* world;
			std::uint32_t filter;

			bool is_step_over(const Ray& contact, const RE::NiPoint3& dir, float distance) const
			{
				const RE::NiPoint3 from = contact.point + RE::NiPoint3{ 0.0f, 0.0f, kStepLift };
				const float remaining = (std::max)(distance * (1.0f - contact.fraction), kStepClearDepth);
				const float length = (std::min)(remaining, kStepProbeLength);
				const auto air = raycast(world, from, from + dir * length, filter);
				return !air.hit || walkable(air.slope_degrees) || air.fraction * length >= kStepClearDepth;
			}

			// The nearest solid fraction along the path, or 2 when it is clear. Walkable ground
			// is gone over, and the landing follows it.
			float path_fraction(const RE::NiPoint3& feet, const RE::NiPoint3& dir, float distance) const
			{
				float nearest = 2.0f;
				for (const float height : kPathHeights) {
					const RE::NiPoint3 from = feet + RE::NiPoint3{ 0.0f, 0.0f, height };
					const auto ray = raycast(world, from, from + dir * distance, filter);
					if (!ray.hit || walkable(ray.slope_degrees) || is_step_over(ray, dir, distance)) {
						continue;
					}
					nearest = (std::min)(nearest, ray.fraction);
				}
				return nearest;
			}

			bool body_fits(const RE::NiPoint3& centre, const RE::NiPoint3& dir) const
			{
				for (const float height : kBodyProbeHeights) {
					const RE::NiPoint3 from = centre + RE::NiPoint3{ 0.0f, 0.0f, height };
					for (int i = 0; i < kBodyProbeDirections; ++i) {
						const float angle = kTwoPi * static_cast<float>(i) / static_cast<float>(kBodyProbeDirections);
						const float c = std::cos(angle);
						const float s = std::sin(angle);
						const RE::NiPoint3 outward{ dir.x * c - dir.y * s, dir.x * s + dir.y * c, 0.0f };
						const auto ray = raycast(world, from, from + outward * kBodyProbeLength, filter);
						if (ray.hit && !walkable(ray.slope_degrees)) {
							return false;
						}
					}
				}
				const RE::NiPoint3 low = centre + RE::NiPoint3{ 0.0f, 0.0f, kBodyProbeHeights[0] };
				const RE::NiPoint3 top = centre + RE::NiPoint3{ 0.0f, 0.0f, kBodyProbeTop };
				return !raycast(world, low, top, filter).hit;
			}

			// Straight down from `up` above `at`, `down` below it.
			Ray ground(const RE::NiPoint3& at, float up, float down = kGroundProbeDown) const
			{
				const RE::NiPoint3 start{ at.x, at.y, at.z + up };
				return raycast(world, start, start - RE::NiPoint3{ 0.0f, 0.0f, up + down }, filter);
			}
		};

		struct Landing {
			bool ok{ false };
			RE::NiPoint3 point{};
			float travel{ 0.0f };
			bool blocked{ false };
			bool grounded{ false };
			const char* height{ "kept height (airborne)" };
			const char* refusal{ nullptr };
		};

		Landing plan_landing(const Probe& probe, const RE::NiPoint3& feet, const RE::NiPoint3& dir)
		{
			Landing plan;
			const float nearest = probe.path_fraction(feet, dir, kDistance);
			plan.blocked = nearest <= 1.0f;
			float travel = kDistance;
			if (plan.blocked) {
				const float clear = kDistance * nearest;
				if (clear < kDeadzone) {
					plan.refusal = "a wall is inside the deadzone";
					return plan;
				}
				travel = clear - kClearance;
			}

			// On the ground the landing takes its height from the ground there; in the air the
			// height is kept, so a Blink mid-jump keeps the jump.
			const auto under = probe.ground(feet, kGroundProbeUp);
			plan.grounded = under.hit && std::abs(under.point.z + kGroundLift - feet.z) <= kGroundedTolerance;

			const float slope_tan = std::tan(kMaxSlopeDegrees * kRadiansPerDegree);
			RE::NiPoint3 landing{};
			for (int pulls = 0;; ++pulls) {
				landing = feet + dir * travel;
				landing.z = feet.z;
				plan.height = plan.grounded ? "kept height (no ground there)" : "kept height (airborne)";
				if (plan.grounded) {
					// The band a walkable slope can climb grows with the distance, so a hill is
					// not read as a ledge.
					const float climb_band = std::clamp(travel * slope_tan + kGroundLift, kMaxSnapUp, kMaxClimbFollow);
					auto ground = probe.ground(landing, kGroundProbeUp);
					if (!ground.hit) {
						// Only look higher when nothing is within reach, so a bridge over flat
						// ground is never read as a hill to climb onto.
						ground = probe.ground(landing, kGroundProbeUp + climb_band);
					}
					const float delta = ground.point.z + kGroundLift - feet.z;
					if (ground.hit && walkable(ground.slope_degrees) && delta >= -kMaxSnapDown && delta <= climb_band) {
						landing.z = ground.point.z + kGroundLift;
						plan.height = std::abs(delta) < 2.0f ? "ground (level)" : "ground (followed the slope)";
					}
				}
				// A ledge keeps its drop, but a landing with nothing under it at all is outside the
				// world: the player falls through the void and the engine resets them to the cell's
				// entry (measured, QASmoke's edge and past the Sleeping Giant's walls).
				const bool over_world = probe.ground(landing, 0.0f, kVoidProbeDown).hit;
				if (over_world && probe.body_fits(landing, dir)) {
					break;
				}
				if (pulls >= kMaxFitPulls || travel <= kMinTravel) {
					plan.refusal = over_world ? "no landing spot the body fits" : "nothing under the landing";
					return plan;
				}
				travel = (std::max)(travel - kFitStep, kMinTravel);
			}

			if ((landing - feet).Length() < kMinTravel) {
				plan.refusal = "wedged against geometry";
				return plan;
			}
			plan.ok = true;
			plan.point = landing;
			plan.travel = travel;
			return plan;
		}

		// Moving the player resets the controller's velocity, so a Blink in mid-air would stop
		// dead; it is read before the move and put back after.
		struct Momentum {
			RE::bhkCharacterController* controller{ nullptr };
			RE::hkVector4 velocity{};
		};

		Momentum capture_momentum(RE::PlayerCharacter* pc)
		{
			Momentum m;
			m.controller = pc->GetCharController();
			if (m.controller) {
				m.controller->GetLinearVelocityImpl(m.velocity);
			}
			return m;
		}

		void restore_momentum(const Momentum& m)
		{
			if (m.controller && m.velocity.Dot3(m.velocity) > 1e-6f) {
				m.controller->SetLinearVelocityImpl(m.velocity);
			}
		}

		// The engine charges fall damage at the next landing from the height the fall began at.
		// A Blink is a discontinuity it knows nothing about, so a Blink off a ledge would be
		// charged for the drop it skipped; re-base the tracker on the landing.
		void rebase_fall(RE::bhkCharacterController* controller, const RE::NiPoint3& landing)
		{
			controller->fallStartHeight = landing.z;
			controller->fallTime = (std::min)(controller->fallTime, kRollFallTimeGate);
		}

		// The model's root is shrunk to nothing rather than culled: True Directional Movement drives
		// the player's own visibility every frame, and with it installed a cull never hid the caster
		// (filmed with and without TDM, issue #34).
		constexpr float kHiddenScale = 0.0001f;
		float shown_scale = 1.0f;

		void set_model_hidden(RE::PlayerCharacter* pc, bool hidden)
		{
			auto* root = pc->Get3D(false);
			if (!root) {
				return;
			}
			const bool shrunk = root->local.scale == kHiddenScale;
			if (hidden && !shrunk) {
				shown_scale = root->local.scale;
				root->local.scale = kHiddenScale;
			} else if (!hidden && shrunk) {
				root->local.scale = shown_scale;
			}
		}

		// The process caches the ghost flag and the engine can recompute it, so it is set again on
		// every frame of the window. `bIframeActive` and `bInIframe` are the graph variables the
		// dodge mods read; a graph that does not declare them ignores the set.
		void set_ghost(RE::PlayerCharacter* pc, bool ghost)
		{
			auto* process = pc->GetActorRuntimeData().currentProcess;
			if (process && process->cachedValues) {
				auto& flags = process->cachedValues->flags;
				if (ghost) {
					flags.set(RE::CachedValues::Flags::kActorIsGhost);
				} else {
					flags.reset(RE::CachedValues::Flags::kActorIsGhost);
				}
			}
			pc->SetGraphVariableBool("bIframeActive", ghost);
			pc->SetGraphVariableBool("bInIframe", ghost);
		}

		// An activator base created in memory for each rift mesh, so the meshes need no plugin.
		RE::TESObjectACTI* rift_base(const char* model)
		{
			static std::unordered_map<std::string, RE::TESObjectACTI*> bases;
			auto& base = bases[model];
			if (!base) {
				if (auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::TESObjectACTI>()) {
					base = factory->Create();
					if (base) {
						base->SetModel(model);
					}
				}
			}
			return base;
		}

		// The glow shader, cloned once from the donor: its fill texture and blend kept, its frost
		// colours, timing and ice-wisp particles replaced.
		RE::TESEffectShader* glow_shader()
		{
			static RE::TESEffectShader* glow = [] {
				auto* donor = RE::TESForm::LookupByID<RE::TESEffectShader>(kGlowDonor);
				auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::TESEffectShader>();
				auto* shader = donor && factory ? factory->Create() : nullptr;
				if (!shader) {
					logger::warn("SH2 blink: body glow unavailable");
					return static_cast<RE::TESEffectShader*>(nullptr);
				}
				shader->data = donor->data;
				shader->fillTexture.textureName = donor->fillTexture.textureName;
				auto& d = shader->data;
				// The colour keys are the colour, not a palette lookup, and no particles.
				d.flags.reset(RE::EffectShaderData::Flags::kGreyscaleToColor, RE::EffectShaderData::Flags::kGreyscaleToAlpha);
				d.flags.set(RE::EffectShaderData::Flags::kDisableParticleShader);
				d.particleShaderFullParticleBirthRatio = 0.0f;
				d.particleShaderPersistantParticleCount = 0.0f;
				// White at the press, still white-hot as the body reappears, conjuration violet as it fades.
				d.fillTextureEffectColorKey1 = RE::Color(255, 255, 255, 0);
				d.fillTextureEffectColorKey2 = RE::Color(232, 218, 255, 0);
				d.fillTextureEffectColorKey3 = RE::Color(165, 125, 255, 0);
				d.fillTextureEffectColorKeyScaleTimeColorKey1Scale = 2.0f;
				d.fillTextureEffectColorKeyScaleTimeColorKey2Scale = 1.5f;
				d.fillTextureEffectColorKeyScaleTimeColorKey3Scale = 1.0f;
				d.fillTextureEffectColorKeyScaleTimeColorKey1Time = 0.0f;
				d.fillTextureEffectColorKeyScaleTimeColorKey2Time = kGlowFullSeconds;
				d.fillTextureEffectColorKeyScaleTimeColorKey3Time = kGlowSeconds;
				d.fillTextureEffectAlphaFadeInTime = 0.0f;
				d.fillTextureEffectFullAlphaTime = kGlowFullSeconds;
				d.fillTextureEffectAlphaFadeOutTime = kGlowFadeSeconds;
				d.fillTextureEffectFullAlphaRatio = 1.0f;
				d.fillTextureEffectPersistentAlphaRatio = 0.0f;
				d.fillTextureEffectAlphaPulseAmplitude = 0.0f;
				// A violet rim on the silhouette, on the same clock as the fill.
				d.edgeEffectColor = RE::Color(190, 160, 255, 0);
				d.edgeEffectAlphaFadeInTime = 0.0f;
				d.edgeEffectFullAlphaTime = kGlowFullSeconds;
				d.edgeEffectAlphaFadeOutTime = kGlowFadeSeconds;
				d.edgeEffectFullAlphaRatio = 1.0f;
				d.edgeEffectPersistentAlphaRatio = 0.0f;
				d.edgeEffectAlphaPulseAmplitude = 0.0f;
				logger::info("SH2 blink: body glow cloned from FrostShortFXShader, fill '{}'",
					shader->fillTexture.textureName.c_str());
				return shader;
			}();
			return glow;
		}

		// Both rift meshes held resident from the first Blink-capable frame. A rift's controllers run
		// from when its mesh is created, and on the Nolvus list a mesh read from disk at placement
		// arrived after its 0.6 s had run out, so no rift was ever seen (filmed, issue #34).
		void keep_rifts_loaded()
		{
			static RE::NiPointer<RE::NiNode> resident[3];
			if (resident[0] && resident[1] && resident[2]) {
				return;
			}
			const RE::BSModelDB::DBTraits::ArgsType args{};
			const char* models[3] = { kDepartureModel, kArrivalModel, kBatSwarmModel };
			for (int i = 0; i < 3; ++i) {
				if (!resident[i]) {
					RE::BSModelDB::Demand(models[i], resident[i], args);
				}
			}
			logger::info("SH2 blink: rift meshes resident: {} {}, bat swarm: {}", resident[0] != nullptr,
				resident[1] != nullptr, resident[2] != nullptr);
		}

		RE::TESEffectShader* dawnguard_shader(RE::FormID local)
		{
			auto* data = RE::TESDataHandler::GetSingleton();
			return data ? data->LookupForm<RE::TESEffectShader>(local, kDawnguard) : nullptr;
		}

		RE::TESObjectREFR* place_rift(RE::PlayerCharacter* pc, const char* model, double now)
		{
			auto* base = rift_base(model);
			const auto placed = base ? pc->PlaceObjectAtMe(base, false) : nullptr;
			if (!placed) {
				logger::warn("SH2 blink: rift '{}' was not placed", model);
				return nullptr;
			}
			pending.rifts.push_back({ now, placed->CreateRefHandle() });
			return placed.get();
		}

		// Departure, at the spot the caster leaves: the rift, the blur, the sound, and the model
		// hidden. Runs before the move.
		void play_departure(RE::PlayerCharacter* pc, double now, BlinkLook look)
		{
			if (look == BlinkLook::Bats) {
				place_rift(pc, kBatSwarmModel, now);
				RE::PlaySound(kBatVanishSound);
				if (auto* dissolve = dawnguard_shader(kBatDissolveShader)) {
					pc->ApplyEffectShader(dissolve, kBatDissolveSeconds);
				}
			} else {
				place_rift(pc, kDepartureModel, now);
				if (auto* blur = RE::TESForm::LookupByID<RE::TESImageSpaceModifier>(kTravelBlur)) {
					RE::ImageSpaceModifierInstanceForm::Trigger(blur, kTravelBlurStrength, nullptr);
				}
				RE::PlaySound(kVanishSound);
				if (auto* glow = glow_shader()) {
					pc->ApplyEffectShader(glow, kGlowSeconds);
				}
			}
			start_dip();
			set_model_hidden(pc, true);
			pending.hidden = true;
			pending.reveal_at = now + kHiddenSeconds;
			set_ghost(pc, true);
			pending.ghosted = true;
			pending.iframe_until = now + kIframeSeconds;
		}

		// Arrival, the moment the caster lands: the rift flares around the landing with a thump.
		void play_arrival(RE::PlayerCharacter* pc, double now, BlinkLook look)
		{
			if (look == BlinkLook::Bats) {
				place_rift(pc, kBatSwarmModel, now);
				RE::PlaySound(kBatArriveSound);
				if (auto* reform = dawnguard_shader(kBatReformShader)) {
					pc->ApplyEffectShader(reform, kBatReformSeconds);
				}
				return;
			}
			place_rift(pc, kArrivalModel, now);
			RE::PlaySound(kArriveSound);
		}

		void refuse(const char* why)
		{
			logger::info("SH2 blink: refused, {}", why);
			RE::PlaySound(Input::sound_MagFail);
		}
	}

	bool try_blink(std::uint32_t art_id, std::size_t slot)
	{
		auto* pc = RE::PlayerCharacter::GetSingleton();
		const ArtDefinition* art = GameData::get_art(art_id);
		if (!pc || !art || art->kind != ArtKind::Blink) {
			return false;
		}
		if (pc->IsDead()) {
			return false;
		}
		if (!blink_look_is_live(art->blink_look, GameData::isVampire())) {
			refuse("Vampiric Blink needs a vampire");
			return false;
		}
		const double now = now_seconds();
		if (gcd.active(now)) {
			logger::info("SH2 blink: refused, GCD");
			return false;
		}
		if (GameData::is_art_on_cd(art_id)) {
			logger::info("SH2 blink: Ability {} on cooldown", art_id);
			return false;
		}
		auto* av = pc->AsActorValueOwner();
		if (!av) {
			return false;
		}
		const auto short_meter = unaffordable_art_meter(art->stamina_cost, art->magicka_cost, art->health_cost,
			av->GetActorValue(RE::ActorValue::kStamina), av->GetActorValue(RE::ActorValue::kMagicka),
			av->GetActorValue(RE::ActorValue::kHealth));
		if (short_meter != ArtMeter::None) {
			RE::HUDMenu::FlashMeter(short_meter == ArtMeter::Magicka ? RE::ActorValue::kMagicka :
									short_meter == ArtMeter::Health  ? RE::ActorValue::kHealth :
																	   RE::ActorValue::kStamina);
			refuse("unaffordable");
			return false;
		}
		auto* cell = pc->GetParentCell();
		auto* world = cell ? cell->GetbhkWorld() : nullptr;
		if (!world) {
			// Every ray needs the cell's Havok world; without one a clear path is a guess.
			refuse("no Havok world in this cell");
			return false;
		}

		const auto input = read_move_input();
		const float input_yaw = [&] {
			if (auto* camera = RE::PlayerCamera::GetSingleton()) {
				return camera->GetRuntimeData2().yaw;
			}
			return pc->data.angle.z;
		}();
		static std::mt19937 rng{ std::random_device{}() };
		const float random01 = std::uniform_real_distribution<float>{ 0.0f, 1.0f }(rng);
		const auto heading = resolve_blink_heading(input.x, input.y, input_yaw, pc->data.angle.z, random01);
		const auto dir = heading_point(heading);

		const RE::NiPoint3 feet = pc->GetPosition();
		const Probe probe{ world, ray_filter(pc) };
		const auto landing = plan_landing(probe, feet, dir);
		if (!landing.ok) {
			refuse(landing.refusal);
			return false;
		}

		play_departure(pc, now, art->blink_look);
		const auto momentum = capture_momentum(pc);
		pc->SetPosition(landing.point, true);
		restore_momentum(momentum);
		if (momentum.controller) {
			rebase_fall(momentum.controller, landing.point);
		}
		// Under a lock-on the caster lands facing the target, while still hidden, so a sideways Blink
		// circles the target the way a strafe does.
		const bool faced = AbilityMagnetism::face_locked_target(pc);
		play_arrival(pc, now, art->blink_look);

		if (art->stamina_cost > 0.0f) {
			av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kStamina, -art->stamina_cost);
		}
		if (art->magicka_cost > 0.0f) {
			av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kMagicka, -art->magicka_cost);
		}
		if (art->health_cost > 0.0f) {
			av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, -art->health_cost);
		}
		if (art->cooldown_days > 0.0f) {
			GameData::add_art_cooldown(art_id, art->cooldown_days);
		}
		gcd.start(now, art->gcd);

		const auto after = pc->GetPosition();
		logger::info(
			"SH2 blink: slot {} Ability {} dir=({:.2f}, {:.2f}) from {} travel={:.0f} blocked={} height='{}' "
			"({:.0f}, {:.0f}, {:.0f}) -> ({:.0f}, {:.0f}, {:.0f}) cast live={} faced lock={}",
			slot, art_id, heading.x, heading.y, heading.from_input ? input.source : "backward cone",
			landing.travel, landing.blocked, landing.height, feet.x, feet.y, feet.z, after.x, after.y, after.z,
			!CastingController::can_start_new_cast(), faced);
		return true;
	}

	void update()
	{
		keep_rifts_loaded();
		update_dip();
		if (!pending.hidden && !pending.ghosted && pending.rifts.empty()) {
			return;
		}
		const double now = now_seconds();
		if (pending.ghosted) {
			if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
				const bool still = now < pending.iframe_until;
				set_ghost(pc, still);
				if (!still) {
					pending.ghosted = false;
					logger::info("SH2 blink: iframes over");
				}
			}
		}
		if (pending.hidden) {
			// Set on every frame of the window: on the Nolvus list something shows the player's model
			// again within a frame, and a cull set once at the jump never hid it (filmed, issue #34).
			const bool still = now < pending.reveal_at;
			pending.hidden = still;
			if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
				set_model_hidden(pc, still);
			}
		}
		for (auto& rift : pending.rifts) {
			// A rift's root is a BSFadeNode, the only root the Nolvus list draws at all; the engine
			// fades a new reference's fade node in over about a second, longer than the whole rift,
			// so it is put at full the moment its 3D arrives (filmed, issue #34).
			const auto ref = rift.ref.get();
			auto* root = ref ? ref->Get3D() : nullptr;
			auto* fade = root ? root->AsFadeNode() : nullptr;
			if (!fade) {
				continue;
			}
			fade->GetRuntimeData().currentFade = 1.0f;
			if (!rift.shown) {
				rift.shown = true;
				logger::info("SH2 blink: rift 3D attached {:.0f} ms after placement", (now - rift.placed_at) * 1000.0);
			}
		}
		std::erase_if(pending.rifts, [now](auto& rift) {
			if (now < rift.placed_at + kRiftLifeSeconds) {
				return false;
			}
			if (const auto ref = rift.ref.get()) {
				ref->Disable();
				ref->SetDelete(true);
			}
			return true;
		});
	}

	void reset()
	{
		// A load drops the old cell's references with it; only the model needs putting back.
		if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
			if (pending.hidden) {
				set_model_hidden(pc, false);
			}
			if (pending.ghosted) {
				set_ghost(pc, false);
			}
		}
		pending = {};
		if (dip.active && std::abs(RE::BSTimer::QGlobalTimeMultiplier() - dip.last_set) <= 1e-3f) {
			set_time_scale(dip.base);
		}
		dip = {};
	}

	bool gcd_active()
	{
		return gcd.active(now_seconds());
	}

	float gcd_progress()
	{
		return gcd.progress(now_seconds());
	}

	float gcd_duration()
	{
		return gcd_active() ? gcd.duration : 0.0f;
	}

	void note_left_stick(float x, float y)
	{
		stick_x.store(x, std::memory_order_relaxed);
		stick_y.store(y, std::memory_order_relaxed);
	}

	void set_test_input(float x, float y)
	{
		test_input = RE::NiPoint2{ x, y };
	}

	bool is_ghost()
	{
		auto* pc = RE::PlayerCharacter::GetSingleton();
		auto* process = pc ? pc->GetActorRuntimeData().currentProcess : nullptr;
		return process && process->cachedValues &&
		       process->cachedValues->flags.all(RE::CachedValues::Flags::kActorIsGhost);
	}

	void place_test_model(const std::string& model, float forward)
	{
		auto* pc = RE::PlayerCharacter::GetSingleton();
		auto* camera = RE::PlayerCamera::GetSingleton();
		auto* placed = pc ? place_rift(pc, model.c_str(), now_seconds()) : nullptr;
		if (!placed || !camera || !camera->cameraRoot) {
			return;
		}
		// Ahead of the camera along its heading, at eye height.
		const float yaw = camera->GetRuntimeData2().yaw;
		const auto eye = camera->cameraRoot->world.translate;
		placed->SetPosition(eye + RE::NiPoint3{ std::sin(yaw) * forward, std::cos(yaw) * forward, 0.0f });
		logger::info("SH2 blink: test model '{}' placed {:.0f} ahead of the camera", model, forward);
	}
}
