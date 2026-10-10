#include "msco_cast_driver.h"
#include "art_driver.h"
#include "clip_translation_driver.h"
#include "combo_cache.h"
#include "seam_dispatch.h"
#include "unpaused_clock.h"
#include <array>
#include <atomic>
#include <cstdlib>
#include <string>
#include <Windows.h>
#include "../logger/logger.h"

using namespace std::literals;

namespace SpellHotbar::casts::MscoCastDriver {

	namespace {
		std::atomic<bool> state_active{ false };
		// True while the live cast is on vanilla's shout graph (first person). Cleared with the
		// other per-cast flags so it can never outlive the cast that set it.
		std::atomic<bool> voice_path{ false };
		std::atomic<bool> combo_window{ false };
		std::atomic<CastShape> cast_shape{ CastShape::fire_and_forget };
		std::atomic<bool> clip_committed{ false };
		std::atomic<int> trace_budget{ 0 };
		// When the live channel entered its state, so the hold can be discounted from the combo
		// sample's age at release. Zero means no channel is holding.
		std::atomic<double> channel_started_ms{ 0.0 };
		// When the entry notify was accepted, so a state the graph never leaves can be timed out.
		// Zero means no entry is recorded and there is nothing to time.
		std::atomic<double> state_entered_ms{ 0.0 };
		constexpr int post_cut_trace_events{ 24 };

		MscoChargeCurve g_curve{};
		RollingMcoCombo g_rolling;
		CastComboIndex g_castIndex;
		// `g_swing` says which swing is up, so a live sample equal to the playing index
		// can be recognised as pre-advance instead of restored as a replay; `g_successors` holds
		// what each swing taught, so the successor can be substituted without ever deriving it.
		McoSwingTracker g_swing;
		McoSuccessorTable g_successors;
		// A cast cut by an attack sends its own SH2_CastExit, and the state's exit raises an
		// `attackStop` of its own about 10 ms later -- which would close the swing the seam just
		// opened. Armed at the cut, spent by the first stop inside `kTeardownStopCapMs`. Two
		// fields rather than a mutex because this is read on the animation thread: the timestamp
		// is published before the flag and read after it, so a consumer that sees the flag always
		// sees the timestamp that went with it.
		std::atomic<bool> teardown_stop_pending{ false };
		std::atomic<double> teardown_stop_armed_ms{ 0.0 };

		constexpr std::array<std::string_view, CastComboIndex::kLength> kCastEvents{
			"SH2_CastRight"sv,
			"SH2_Cast2"sv,
			"SH2_Cast3"sv,
			"SH2_Cast4"sv,
		};

		// One clock for the whole file, and it is the unpaused one: the sample ages
		// and the channel hold have to agree with the state watchdog, and all three are measured
		// in gameplay time. A menu visit neither ages a combo sample out of range nor spends the
		// watchdog's cap.
		double now_ms()
		{
			return UnpausedClock::now_ms();
		}

		// One-shot: the first `attackStop` inside the cap spends the flag and leaves the swing
		// open; anything later closes it as usual. An expired flag is spent here too, so a
		// teardown that raised no stop at all cannot reach forward and eat a real one.
		bool consume_teardown_stop()
		{
			if (!teardown_stop_pending.load(std::memory_order_acquire)) {
				return false;
			}
			const double armed_at = teardown_stop_armed_ms.load(std::memory_order_relaxed);
			teardown_stop_pending.store(false, std::memory_order_release);
			return teardown_stop_absorbs_close(true, now_ms(), armed_at, kTeardownStopCapMs);
		}

		std::string_view event_for(int index)
		{
			if (index < 1 || index > CastComboIndex::kLength) {
				return kCastEvents.front();
			}
			return kCastEvents[static_cast<size_t>(index - 1)];
		}

		// Ready-enter and MCO initiate still consume-and-rewrite: they beat a ready
		// stomp before a swing. BFCO never raises the initiate tags, so the send-side
		// seam is the consume for that graph -- `observe_attack_sent` disarms without
		// rewriting. Leaving this list as the rewrite edges is deliberate; adding
		// BFCO_PlayerAttackStart here is too late, the clip's DIY_EndLoop advance has
		// already been treated as a stomp.
		bool is_restore_edge(std::string_view tag)
		{
			return tag == "SBF_ReadyStart"sv || tag == "MSCO_MagicReady"sv ||
				   tag == "MCO_AttackInitiate"sv || tag == "MCO_PowerAttackInitiate"sv;
		}

		constexpr std::string_view kNextAttackVar{ "MCO_nextattack"sv };
		constexpr std::string_view kNextPowerAttackVar{ "MCO_nextpowerattack"sv };

		constexpr std::string_view var_name(McoSgviVariable kind)
		{
			return kind == McoSgviVariable::next_attack ? kNextAttackVar : kNextPowerAttackVar;
		}

		// Which moveset the successor table is being taught about. Movesets differ per weapon type
		// -- lengths, wraps, and stance reorderings all differ -- so a greatsword's teaching must
		// never answer for a dagger's. Anything that is not a weapon in the right hand (fists, a
		// spell, an empty hand) keys to 0, which is also `kHandToHandMelee`: they share a moveset,
		// so sharing a row is correct rather than a fallback.
		int weapon_key(RE::Actor* actor)
		{
			if (!actor) {
				return 0;
			}
			auto* right = actor->GetEquippedObject(false);
			if (!right) {
				return 0;
			}
			auto* weapon = right->As<RE::TESObjectWEAP>();
			if (!weapon) {
				return 0;
			}
			const int key = static_cast<int>(weapon->GetWeaponType());
			return (key >= 0 && key < McoSuccessorTable::kWeaponKeys) ? key : 0;
		}

		// -1 says the graph bool itself could not be read, which is neither state: it must not be
		// mistaken for "not attacking", because the gates below only ever act on a definite true.
		int read_is_attacking(RE::Actor* actor)
		{
			bool attacking{ false };
			if (!actor || !actor->GetGraphVariableBool(RE::BSFixedString("IsAttacking"sv), attacking)) {
				return -1;
			}
			return attacking ? 1 : 0;
		}

		bool sample_mco(RE::Actor* actor, McoCombo& out)
		{
			if (!actor) {
				return false;
			}
			std::int32_t next = 0;
			std::int32_t power = 0;
			if (!actor->GetGraphVariableInt("MCO_nextattack", next) ||
				!actor->GetGraphVariableInt("MCO_nextpowerattack", power)) {
				return false;
			}
			out.nextAttack = next;
			out.nextPowerAttack = power;
			return true;
		}

		void write_mco(RE::Actor* actor, const McoCombo& combo)
		{
			if (!actor) {
				return;
			}
			// This is the write that matters, and it lands in the ROOT graph's storage --
			// which is precisely where the nested MCO_Attack.hkb selector reads it from when
			// it reactivates. Actor-level is not a compromise here; it is the only storage
			// the nested graph's variable link resolves against.
			actor->SetGraphVariableInt("MCO_nextattack", combo.nextAttack);
			actor->SetGraphVariableInt("MCO_nextpowerattack", combo.nextPowerAttack);
			logger::debug("SH2 cast: restored MCO_nextattack={} MCO_nextpowerattack={}",
				combo.nextAttack, combo.nextPowerAttack);
		}

		void arm_restore()
		{
			if (const auto combo = g_rolling.arm(now_ms())) {
				logger::debug("SH2 cast: combo restore armed next={} power={}", combo->nextAttack,
					combo->nextPowerAttack);
				// Write now so a same-frame recovery attack (Ability latch cut) sees the
				// sampled index. The payload edge re-asserts it against each stomp and the
				// initiate/ready edges consume it, so it survives #0006's reset.
				write_mco(RE::PlayerCharacter::GetSingleton(), *combo);
			}
		}

		// The state is gone and nothing is chainable through it. Shared by every way a cast
		// leaves the state, so a new piece of per-cast state is cleared in one place.
		void clear_state_flags()
		{
			voice_path.store(false, std::memory_order_relaxed);
			channel_started_ms.store(0.0, std::memory_order_relaxed);
			state_entered_ms.store(0.0, std::memory_order_relaxed);
			state_active.store(false, std::memory_order_relaxed);
			combo_window.store(false, std::memory_order_relaxed);
			cast_shape.store(CastShape::fire_and_forget, std::memory_order_relaxed);
		}

		bool send_exit(RE::PlayerCharacter* pc)
		{
			if (!pc) {
				return false;
			}
			if (voice_path.load(std::memory_order_relaxed)) {
				// The vanilla shout state ends itself; ShoutStop is the cancel, and a refused one
				// means the state was already gone. Nothing waits on it.
				const bool consumed_stop = pc->NotifyAnimationGraph("ShoutStop"sv);
				logger::debug("SH2 cast: first person, notified ShoutStop -> {}", consumed_stop);
				return consumed_stop;
			}
			const bool consumed = pc->NotifyAnimationGraph("SH2_CastExit"sv);
			logger::debug("SH2 cast: notified SH2_CastExit -> {}", consumed);
			return consumed;
		}

		constexpr std::string_view kChannelEvent{ "SH2_CastChannel"sv };

		bool camera_is_first_person()
		{
			auto* camera = RE::PlayerCamera::GetSingleton();
			return camera && camera->IsInFirstPerson();
		}

		bool send_entry(RE::PlayerCharacter* pc, CastShape shape)
		{
			if (voice_path.load(std::memory_order_relaxed)) {
				// Upstream's entry, on whichever graph is active -- in first person that is the
				// first-person graph, which has vanilla's shout states.
				const bool sent = pc->NotifyAnimationGraph("ShoutStart"sv);
				state_active.store(sent, std::memory_order_relaxed);
				state_entered_ms.store(sent ? now_ms() : 0.0, std::memory_order_relaxed);
				combo_window.store(false, std::memory_order_relaxed);
				clip_committed.store(false, std::memory_order_relaxed);
				logger::debug("SH2 cast: first person, notified ShoutStart ({}) -> {}",
					shape == CastShape::channel ? "channel"sv : "fire-and-forget"sv, sent);
				return sent;
			}
			const int index = g_castIndex.current();
			const auto event = cast_entry_walks_clip_set(shape) ? event_for(index) : kChannelEvent;
			const bool sent = pc->NotifyAnimationGraph(event);
			state_active.store(sent, std::memory_order_relaxed);
			state_entered_ms.store(sent ? now_ms() : 0.0, std::memory_order_relaxed);
			combo_window.store(false, std::memory_order_relaxed);
			clip_committed.store(false, std::memory_order_relaxed);
			if (cast_entry_walks_clip_set(shape)) {
				logger::debug("SH2 cast: notified {} (clip {}) -> {}", event, index, sent);
			} else {
				logger::debug("SH2 cast: notified {} (held channel) -> {}", event, sent);
			}
			return sent;
		}

		void write_clip_speed(RE::PlayerCharacter* pc, float charge_time)
		{
			const float speed = charge_time_to_anim_speed(charge_time, g_curve);
			const bool ok = pc->SetGraphVariableFloat("MSCO_attackspeed", speed);
			logger::debug("SH2 cast: MSCO_attackspeed={} charge={} wrote={}", speed, charge_time, ok);
		}

		const char* msco_ini()
		{
			return "Data\\SKSE\\Plugins\\MSCO.ini";
		}

		float ini_float(const char* section, const char* key, float fallback)
		{
			char buf[64]{};
			GetPrivateProfileStringA(section, key, "", buf, static_cast<DWORD>(sizeof(buf)), msco_ini());
			if (buf[0] == '\0') {
				return fallback;
			}
			char* end = nullptr;
			const float v = std::strtof(buf, &end);
			return end != buf ? v : fallback;
		}
	}

	void load_charge_curve()
	{
		MscoChargeCurve curve{};
		const char* ini = msco_ini();
		WritePrivateProfileStringA(nullptr, nullptr, nullptr, nullptr);
		curve.mechanic_on = GetPrivateProfileIntA("General", "ChargeMechanicOn", 1, ini) != 0;
		curve.exp_mode = GetPrivateProfileIntA("General", "ExpMode", 1, ini) != 0;
		curve.shortest = ini_float("ChargeTime", "Shortest", curve.shortest);
		curve.longest = ini_float("ChargeTime", "Longest", curve.longest);
		curve.base_time = ini_float("ChargeTime", "BaseTime", curve.base_time);
		curve.min_speed = ini_float("SpeedClamp", "MinSpeed", curve.min_speed);
		curve.max_speed = ini_float("SpeedClamp", "MaxSpeed", curve.max_speed);
		curve.exp_factor = ini_float("Exp", "ExpFactor", curve.exp_factor);
		const bool changed = curve.mechanic_on != g_curve.mechanic_on || curve.exp_mode != g_curve.exp_mode
			|| curve.shortest != g_curve.shortest || curve.longest != g_curve.longest
			|| curve.base_time != g_curve.base_time || curve.min_speed != g_curve.min_speed
			|| curve.max_speed != g_curve.max_speed || curve.exp_factor != g_curve.exp_factor;
		static bool logged_once = false;
		g_curve = curve;
		if (!logged_once || changed) {
			logged_once = true;
			logger::info(
				"SH2 cast: MSCO charge curve mechanic={} exp={} base={} short={} long={} min={} max={} p={}",
				curve.mechanic_on, curve.exp_mode, curve.base_time, curve.shortest, curve.longest,
				curve.min_speed, curve.max_speed, curve.exp_factor);
		}
	}

	bool combo_window_open()
	{
		return combo_window.load(std::memory_order_relaxed);
	}

	// Called only once the graph has ACCEPTED our entry: this cast has taken the swing away from
	// whoever was swinging, and nothing else on this path closes the tracker. Vanilla's
	// `BeginCast` escape routes through the ready state and picks up `attackStop` from its enter
	// events; ours goes straight to `shtb`, whose exit events do not include it. An `attackStop`
	// did follow 209 ms later in one measured run, so the tracker is not always left open -- but
	// "not always" is not a close, and a swing left up would make the next press wait on a cancel
	// window that is never coming (the wait `should_hold_cast_for_foreign_swing` imposes BEFORE
	// the attempt).
	//
	// Do not close before the notify: an entry the graph turns down has taken nothing, and
	// `poll_local_release`'s put-back needs `swing_open()` to still be true to return the press
	// to the swing's end.
	void close_swing_taken_over()
	{
		if (const auto open = g_swing.open_swing()) {
			g_swing.close();
			logger::debug("SH2 cast: closed the swing this cast took over (playing {})",
				open->playing);
		}
	}

	bool begin(RE::PlayerCharacter* pc, hand_mode hand, float charge_time, CastShape shape)
	{
		if (!pc) {
			return false;
		}
		(void)hand;
		// The interruption seam. The value of the swing this cast is cutting is taken here, live,
		// while the swing is still up: at that instant the graph still holds its index and MCO has
		// not stomped back to 1 yet. `IsAttacking` is the whole gate -- a begin() outside a swing
		// (the ShoutMCO deferred re-begin, an idle cast) would learn the stomp, so it records
		// nothing and the cache keeps its last WinClose/SGVI teaching.
		//
		// What that live read MEANS depends on where in the clip the interrupt landed.
		// Before the clip's own advance it is the PLAYING index, and handing it on replays the
		// swing the player just interrupted; after the advance it is the successor. The open-swing
		// tracker tells the two apart, and the successor table -- filled only from what the clips
		// themselves taught -- supplies the successor when the sample is pre-advance. With nothing
		// learned yet the sampled value is kept.
		{
			const int attacking_state = read_is_attacking(pc);
			McoCombo sample{};
			bool recorded = false;
			std::string light_note{ "not sampled" };
			std::string power_note{ "not sampled" };
			const auto open = g_swing.open_swing();
			if (should_sample_live_at_cast_begin(attacking_state == 1) && sample_mco(pc, sample)) {
				const int key = weapon_key(pc);
				const auto resolve = [&](McoSgviVariable kind, int& field) {
					const bool matching = open && open->kind == kind;
					if (!live_sample_is_pre_advance(matching, open ? open->playing : 0, field)) {
						return std::string{ "post-advance, keeping " } + std::to_string(field);
					}
					if (const auto successor = g_successors.lookup(key, kind, open->playing)) {
						const int was = field;
						field = *successor;
						return std::string{ "pre-advance, substituted=" } + std::to_string(*successor) +
							   " (was " + std::to_string(was) + ")";
					}
					return std::string{ "pre-advance, successor unknown, keeping " } +
						   std::to_string(field) + " (replay)";
				};
				light_note = resolve(McoSgviVariable::next_attack, sample.nextAttack);
				power_note = resolve(McoSgviVariable::next_power_attack, sample.nextPowerAttack);
				// Full record(): a real swing being interrupted also invalidates any restore
				// we were still holding, exactly as its own advance would have.
				g_rolling.record(sample, now_ms());
				recorded = true;
			}
			// One line per cast, not per event: this is the record that explains a wrong
			// combo restore after the fact, and it is the only place the pre/post-advance
			// call is made. Cheap enough to keep now that the per-event probes are gone.
			logger::debug(
				"SH2 cast: combo sample next={} power={} attacking={} recorded={} swing={} playing={} | next: {} | power: {}",
				sample.nextAttack, sample.nextPowerAttack, attacking_state, recorded,
				open ? "open"sv : "closed"sv, open ? open->playing : 0, light_note, power_note);
		}
		cast_shape.store(shape, std::memory_order_relaxed);
		channel_started_ms.store(
			shape == CastShape::channel ? now_ms() : 0.0, std::memory_order_relaxed);
		trace_budget.store(0, std::memory_order_relaxed);
		interrupt_equipped_casters_if_spell(pc);
		// The camera decides the path, read once here at the only moment a cast can start. A
		// switch mid-cast keeps the path the cast began on; the watchdog and the mod-driven exits
		// still bound it.
		const bool first_person = camera_is_first_person();
		voice_path.store(first_person, std::memory_order_relaxed);
		if (first_person) {
			logger::info("SH2 cast: first person -- casting through vanilla's shout graph, shtb state not entered");
			return send_entry(pc, shape);
		}
		load_charge_curve();
		write_clip_speed(pc, charge_time);
		if (!send_entry(pc, shape)) {
			return false;
		}
		close_swing_taken_over();
		return true;
	}

	bool vanilla_voice_path()
	{
		return voice_path.load(std::memory_order_relaxed);
	}

	bool camera_left_clip_graph()
	{
		return clip_annotations_unreachable(
			voice_path.load(std::memory_order_relaxed), camera_is_first_person());
	}

	bool voice_path_live(RE::PlayerCharacter* pc)
	{
		if (!voice_path.load(std::memory_order_relaxed)) {
			return true;
		}
		bool shouting{ false };
		return pc && pc->GetGraphVariableBool(RE::BSFixedString("IsShouting"sv), shouting) && shouting;
	}

	void voice_release(RE::PlayerCharacter* pc)
	{
		if (!pc || !voice_path.load(std::memory_order_relaxed)) {
			return;
		}
		const bool sent = pc->NotifyAnimationGraph("MT_BreathExhaleShort"sv);
		logger::debug("SH2 cast: first person, notified MT_BreathExhaleShort -> {}", sent);
	}

	void voice_reloop(RE::PlayerCharacter* pc)
	{
		if (!pc || !voice_path.load(std::memory_order_relaxed)) {
			return;
		}
		const bool sent = pc->NotifyAnimationGraph("ShoutStart"sv);
		logger::debug("SH2 cast: first person, channel reloop ShoutStart -> {}", sent);
	}

	void observe_graph_event(RE::Actor* a_player, const RE::BSFixedString& a_tag,
		const RE::BSFixedString& a_payload, SpellFireHand a_spellfire_hand,
		std::uint8_t a_armed_mask)
	{
		const std::string_view tag{ a_tag.c_str() ? a_tag.c_str() : "" };
		const std::string_view payload{ a_payload.c_str() ? a_payload.c_str() : "" };

		// Which swing is open. This runs FIRST, before the restore-consume branch at
		// the bottom of this function, because the answer to "did this swing's initiate consume a
		// restore" is only true until that branch runs -- read after it, every swing would look
		// like an ordinary one and a restore-taught (unverified) playing index would be taught to
		// the successor table as fact.
		//
		// A real swing opens the tracker at its own index; our own cast and art clips raise the
		// same initiates, and those CLOSE it instead -- there is no MCO swing behind them, and a
		// stale open swing would mislabel the next payload.
		if (tag == "MCO_AttackInitiate"sv || tag == "MCO_PowerAttackInitiate"sv) {
			const McoSgviVariable kind = tag == "MCO_AttackInitiate"sv
				? McoSgviVariable::next_attack
				: McoSgviVariable::next_power_attack;
			std::int32_t playing = 0;
			if (should_record_mco_combo_sample(is_active() || ArtDriver::is_active()) && a_player &&
				a_player->GetGraphVariableInt(RE::BSFixedString(var_name(kind)), playing)) {
				// Measured live: at the initiate the variable still reads as the swing that is
				// PLAYING, not its successor. That is precisely the index the clip's advance is
				// about to move on from, so it is the "from" side of every pair learned below.
				const auto already = g_swing.open_swing();
				const bool already_same = already && already->kind == kind;
				const bool taught = reopen_keeps_restore_taught(
					g_rolling.restore_pending(), already_same,
					already_same && already->taught_by_restore);
				g_swing.open(kind, static_cast<int>(playing), taught);
			} else {
				g_swing.close();
			}
		} else if (is_mco_swing_window_open_event(tag)) {
			// The swing's cancel window, on the swing that is open -- the edge a press held on
			// the local latch is released at. The two predicates match BFCO's own window events
			// (`BFCO_NextWinStart`, `BFCO_NextPowerWinStart`, `BFCO_DIY_EndLoop`) as well as
			// MCO's names, so the same two branches cover clips annotated either way.
			//
			// Gated the same way the initiate branch is: our own cast clip raises `MSCO_WinOpen`
			// at ~1.2 s, and a cast that started inside someone else's swing would otherwise
			// mark THAT swing's window open from our clip's annotation. The close is deliberately
			// ungated -- closing early only returns the press to the attackStop fallback.
			if (should_record_mco_combo_sample(is_active() || ArtDriver::is_active())) {
				g_swing.set_window_open(true);
			}
		} else if (is_mco_swing_window_close_event(tag)) {
			g_swing.set_window_open(false);
		} else if (tag == "attackStop"sv || tag == "MCO_EndAnimation"sv) {
			// The swing is over. Anything SGVI arriving after this is the ready-state or
			// AttackState-exit reset -- which is also `@SGVI|MCO_nextattack|1` -- and recording
			// that is the exact stomp the rolling cache exists to outlive.
			//
			// Unless this stop is the cut cast's teardown rather than the swing's end. One stop,
			// once, inside the cap.
			if (consume_teardown_stop()) {
				// Two different facts, and saying the wrong one costs a reader an hour. With a
				// swing open this stop really is being held off that swing. On the deferred-forward
				// path the attack has not been re-sent yet, so there is no swing to keep open and
				// the pass is simply being spent by the stop it was armed for.
				if (g_swing.open_swing()) {
					logger::debug("SH2 cast: teardown attackStop ignored, swing stays open ({})",
						tag);
				} else {
					logger::debug(
						"SH2 cast: teardown attackStop absorbed, no swing open yet ({})", tag);
				}
			} else {
				g_swing.close();
			}
		}

		// The clip's own advance, taken at the moment it is written. This is the primary edge:
		// a cast that interrupts a swing lands after the WinOpen-time advance but before
		// WinClose, so WinClose alone never learned the interrupted swing's teaching. The value
		// comes from the TAG, never from a graph read here -- the Payload Interpreter's write
		// may race this dispatch.
		if (const auto sgvi = parse_mco_sgvi_sample(tag)) {
			const bool pending = g_rolling.restore_pending();
			const bool record = !pending && should_record_mco_combo_sample(
				is_active() || ArtDriver::is_active());
			// Measurement: does an @SGVI payload reach this sink at all? One line
			// per tag, so the log answers that without a second run.
			logger::info("SH2 probe: sgvi {}={} pending={} record={}",
				sgvi->variable == McoSgviVariable::next_attack ? "MCO_nextattack"sv
															  : "MCO_nextpowerattack"sv,
				sgvi->value, pending, record);
			if (pending) {
				// Same rule as the window-close stomp-to-undo branch: while a restore is
				// pending the payload we are seeing is the ready reset's, not a swing's, so
				// put ours back rather than learning from it. Parity note: like that branch,
				// this does not distinguish a genuine real swing arriving while a restore is
				// still pending -- the same accepted gap, kept identical on purpose.
				if (const auto combo = g_rolling.peek()) {
					write_mco(a_player, *combo);
				}
			} else if (record) {
				if (sgvi->variable == McoSgviVariable::next_attack) {
					g_rolling.record_next_attack(sgvi->value, now_ms());
				} else {
					g_rolling.record_next_power_attack(sgvi->value, now_ms());
				}
			}
		}

		// The same advance, arriving where it actually lands. The annotation
		// `PIE.@SGVI|...` splits into an event name (raised as `Pie`) and this payload, so
		// this -- not the tag branch above -- is the edge that fires under real packs.
		//
		// The gate is stricter than the tag branch's, and deliberately so. The ready-state reset
		// and the AttackState exit fire the SAME payload, `@SGVI|MCO_nextattack|1`. Most arrive
		// with no swing open and `IsAttacking` false -- but NOT all: when a swing is cut, the
		// AttackState-exit notify lands BEFORE attackStop (measured: tracker still open,
		// IsAttacking still 1), so the value itself is the last line of defence:
		// `payload_advance_is_recordable` quarantines 1, the only value a reset can carry.
		if (const auto sgvi = parse_mco_sgvi_sample(payload)) {
			const bool pending = g_rolling.restore_pending();
			const int attacking_state = read_is_attacking(a_player);
			const auto open = g_swing.open_swing();
			const bool matching_kind = open && open->kind == sgvi->variable;
			std::string_view decision{ "ignored"sv };

			if (pending) {
				// Same rule as the tag branch and the window-close stomp-to-undo branch: while a
				// restore is pending, what we are seeing is the reset's payload, not a swing's.
				decision = "restore pending -> put ours back"sv;
				if (const auto combo = g_rolling.peek()) {
					write_mco(a_player, *combo);
				}
			} else if (matching_kind && attacking_state == 1 &&
					   !payload_advance_is_recordable(sgvi->value)) {
				// A cut swing's exit notify beats attackStop to this sink, so an open swing and a
				// live IsAttacking do NOT prove a clip advance. A reset always teaches 1 and a
				// clip can never teach itself, so 1 goes to the WinClose edge instead -- see
				// payload_advance_is_recordable.
				decision = "reset-valued (1) -> left to the WinClose edge"sv;
			} else if (matching_kind && attacking_state == 1) {
				if (sgvi->variable == McoSgviVariable::next_attack) {
					g_rolling.record_next_attack(sgvi->value, now_ms());
				} else {
					g_rolling.record_next_power_attack(sgvi->value, now_ms());
				}
				if (!open->taught_by_restore) {
					// The clip just said what follows the swing it is playing. That pair is the
					// only source of successor knowledge -- never arithmetic.
					g_successors.learn(weapon_key(a_player), sgvi->variable, open->playing,
						sgvi->value);
					decision = "recorded + learned successor"sv;
				} else {
					// This swing's initiate consumed a restore, so its playing index is what we
					// wrote rather than what the engine ran (2H currently ignores the write). The
					// advance is still the clip's own and worth recording; the PAIR is not
					// trustworthy and must not enter the table.
					decision = "recorded; restore-taught swing, no pair learned"sv;
				}
			} else if (!open) {
				decision = "no open swing (ready/AttackState reset) -> ignored"sv;
			} else if (!matching_kind) {
				decision = "open swing is the other kind -> ignored"sv;
			} else {
				decision = "IsAttacking not true -> ignored"sv;
			}

			logger::debug("SH2 cast: advance {}={} ({})",
				sgvi->variable == McoSgviVariable::next_attack ? kNextAttackVar
															  : kNextPowerAttackVar,
				sgvi->value, decision);
		}

		if (is_mco_combo_index_edge(tag)) {
			if (window_close_is_a_stomp_to_undo(g_rolling.restore_pending())) {
				// This window-close is usually our own cast clip's (MSCO_left2.hkx fires
				// MCO_winclose at 1.2s) carrying the ready reset's stomped value. Put ours
				// back instead; the next real swing consumes it.
				if (const auto combo = g_rolling.peek()) {
					write_mco(a_player, *combo);
				}
			} else if (should_record_mco_combo_sample(is_active() || ArtDriver::is_active())) {
				McoCombo sample{};
				if (sample_mco(a_player, sample)) {
					g_rolling.record(sample, now_ms());
					// Fallback teaching. A window-close arrives after the advance, so
					// the value it carries is the successor of the swing still open -- the same
					// pair the payload edge learns, one edge later. Only the open swing's OWN
					// kind is paired: a light swing's WinClose says nothing about what follows a
					// power attack, and pairing both would invent a power chain from a light one.
					if (const auto open = g_swing.open_swing(); open && !open->taught_by_restore) {
						const int taught = open->kind == McoSgviVariable::next_attack
							? sample.nextAttack
							: sample.nextPowerAttack;
						g_successors.learn(weapon_key(a_player), open->kind, open->playing, taught);
					}
					logger::debug("SH2 cast: sampled MCO next={} power={} at {}", sample.nextAttack,
						sample.nextPowerAttack, tag);
				} else {
					g_rolling.disarm();
				}
			}
		}

		if (is_msco_combo_window_open_event(a_spellfire_hand, a_armed_mask) && is_active()) {
			const CastShape shape = cast_shape.load(std::memory_order_relaxed);
			combo_window.store(spellfire_opens_combo_window(shape), std::memory_order_relaxed);
			bool expected = false;
			if (clip_committed.compare_exchange_strong(expected, true, std::memory_order_relaxed) &&
				spellfire_advances_cast_index(shape)) {
				g_castIndex.advance();
			}
			logger::debug("SH2 cast: commitment point ({}), shape={}, window={}", tag,
				shape == CastShape::channel ? "channel" : "fnf", spellfire_opens_combo_window(shape));
		}

		if (is_msco_combo_window_close_event(tag)) {
			combo_window.store(false, std::memory_order_relaxed);
			logger::debug("SH2 cast: combo window closed ({})", tag);
		}

		if (tag == "SH2_CastExit"sv) {
			if (is_active()) {
				arm_restore();
			}
			if (!clip_committed.load(std::memory_order_relaxed) &&
				exit_without_spellfire_is_a_dropped_press(cast_shape.load(std::memory_order_relaxed))) {
				logger::warn("SH2 cast: graph raised SH2_CastExit before SpellFire (clip {}); press produced no payload",
					g_castIndex.current());
				g_castIndex.reset();
			}
			state_active.store(false, std::memory_order_relaxed);
			combo_window.store(false, std::memory_order_relaxed);
			clip_committed.store(false, std::memory_order_relaxed);
			logger::debug("SH2 cast: state exiting (clip end or cancel)");
		}

		if (is_restore_edge(tag)) {
			const auto combo = ready_edge_consumes_restore(deferred_attack_armed())
				? g_rolling.consume()
				: g_rolling.peek();
			if (combo) {
				write_mco(a_player, *combo);
			}
		}

		// The graph is back in magic-ready, so a cut's exit transition has finished and an attack
		// held for it can go. Last in this function on purpose: the send it releases re-enters
		// the seam, and everything above is this event's own business.
		if (is_cast_exit_landed_event(tag)) {
			note_cast_exit_landed();
		}
	}

	bool is_active()
	{
		return state_active.load(std::memory_order_relaxed);
	}

	void observe_attack_sent(RE::Actor* a_player, AttackSeamKind a_kind)
	{
		const auto kind = swing_kind_for_seam(a_kind);
		if (!kind || !a_player) {
			return;
		}
		// Same read as the initiate branch: before the graph has played the swing the variable
		// holds the index the swing about to start will play, so it is the "from" side of the
		// pair the clip's advance teaches. An unreadable or out-of-range value leaves the tracker
		// as it was rather than opening a swing at an index nobody can reason about.
		std::int32_t playing = 0;
		if (!a_player->GetGraphVariableInt(RE::BSFixedString(var_name(*kind)), playing)) {
			logger::debug("SH2 cast: seam saw {} but {} is unreadable; swing not opened",
				attack_seam_kind_label(a_kind), var_name(*kind));
			return;
		}
		const bool taught_by_restore = g_rolling.restore_pending();
		g_swing.open(*kind, static_cast<int>(playing), taught_by_restore);
		// BFCO never raises MCO_AttackInitiate, so the seam is the consume.
		// Disarm after the open so this swing stays restore-taught; a rewrite here
		// would fight the clip's upcoming advance. An out-of-range open is not a swing.
		if (seam_open_disarms_pending_restore(taught_by_restore, g_swing.open_swing().has_value())) {
			g_rolling.disarm();
		}
		logger::debug("SH2 cast: seam opened {} swing at playing={} restore_pending={} -> {}",
			attack_seam_kind_label(a_kind), playing, taught_by_restore,
			g_swing.open_swing() ? "open"sv : "closed (index out of range)"sv);
	}

	bool swing_open()
	{
		return g_swing.open_swing().has_value();
	}

	bool swing_window_open()
	{
		const auto open = g_swing.open_swing();
		return open && open->window_open;
	}

	void arm_teardown_stop()
	{
		teardown_stop_armed_ms.store(now_ms(), std::memory_order_relaxed);
		teardown_stop_pending.store(true, std::memory_order_release);
		logger::debug("SH2 cast: armed teardown attackStop pass ({:.0f} ms cap)",
			kTeardownStopCapMs);
	}

	void clear_teardown_stop()
	{
		teardown_stop_pending.store(false, std::memory_order_release);
	}

	bool should_trace_graph_events()
	{
		if (is_active()) {
			return true;
		}
		// Someone else's swing is exactly the span whose cancel-window annotations the latch
		// release depends on, and the post-cut budget below cannot cover it: 24 events is spent
		// long before a 1.8 s swing ends, and a trace that goes silent there reads as "BFCO
		// raises no window events". An open swing is self-limiting -- it closes at
		// `attackStop` -- so this cannot become an unbounded trace.
		if (swing_open()) {
			return true;
		}
		int remaining = trace_budget.load(std::memory_order_relaxed);
		while (remaining > 0) {
			if (trace_budget.compare_exchange_weak(remaining, remaining - 1, std::memory_order_relaxed)) {
				return true;
			}
		}
		return false;
	}

	void end_channel(RE::PlayerCharacter* pc)
	{
		// The state is live for the whole hold -- SH2_Channel_State plays a MODE_LOOPING clip and
		// has no end-of-clip trigger -- so this exit is what ends it. It also hands the combo
		// position on, so the swing after a hold continues the chain the hold interrupted rather
		// than starting at attack1. Sent unconditionally either way: the event reaches nothing when
		// the state is already gone.
		//
		// Discount the hold from the sample's age first. No sample is taken while our state is
		// live, so the position comes from the swing before the channel, and a hold longer than
		// kMaxAgeMs would otherwise age it out and reset the chain to attack1.
		if (const double started = channel_started_ms.exchange(0.0, std::memory_order_relaxed);
			started > 0.0) {
			const double held = now_ms() - started;
			g_rolling.credit_held_time(held);
			logger::debug("SH2 cast: channel held {:.0f}ms; discounted from the combo sample age", held);
		}
		arm_restore();
		send_exit(pc);
		clear_state_flags();
	}

	void arm_combo_restore()
	{
		arm_restore();
	}

	bool notify_attack_cut(RE::PlayerCharacter* pc)
	{
		if (!pc || voice_path.load(std::memory_order_relaxed)) {
			return false;
		}
		// Ahead of SH2_CastExit, never instead of it: the magic graph and all bookkeeping still
		// key on the exit. In 1hm_behavior this takes the cast state to state 0 with no blend, so
		// the exit that follows finds nothing to do there. Its 0.2 s blend was what restarted the
		// attack clip when it completed under the swing (BFCO_PlayerAttackStart twice, ~180 ms
		// apart). A graph generated before this event existed refuses it and blends as before.
		const bool consumed = pc->NotifyAnimationGraph("SH2_CastCut"sv);
		logger::debug("SH2 cast: notified SH2_CastCut -> {}", consumed);
		return consumed;
	}

	bool cancel(RE::PlayerCharacter* pc)
	{
		if (is_active()) {
			trace_budget.store(post_cut_trace_events, std::memory_order_relaxed);
			arm_restore();
		}
		const bool consumed = send_exit(pc);
		clear_state_flags();
		return consumed;
	}

	void tick_combo_age(RE::PlayerCharacter* pc, bool a_is_shouting)
	{
		// Per unpaused frame. A shout or our own cast is one continuous action, not time out of
		// the chain, so it is credited off the sample's age and the pending restore's clock, the
		// same as a concentration hold. The step is capped: the first frame, and a frame after a
		// hitch, must not credit a gap the player really spent out of combat.
		static double last_ms = 0.0;
		const double now = now_ms();
		const double step = last_ms > 0.0 ? now - last_ms : 0.0;
		last_ms = now;
		if ((a_is_shouting || is_active()) && step > 0.0 && step <= 100.0) {
			g_rolling.credit_held_time(step);
		}
		if (g_rolling.expire_pending(now)) {
			// The last write to the variables was ours: the stomp-undo put the restore back over
			// the ready reset, and nothing resets them again until the next swing. So put back
			// what the ready state writes, 1 and 1 (McoCombo's defaults), or the chain continues
			// however long the player waited. Measured live: a spell, 7 s idle, then attack 3.
			// Not over an open swing, which owns its own advance.
			if (pc && !g_swing.open_swing()) {
				write_mco(pc, McoCombo{});
			}
			logger::debug("SH2 cast: combo restore expired; no attack came for it");
		}
	}

	void poll_watchdog(RE::PlayerCharacter* pc)
	{
		// On vanilla's shout graph the state ends ITSELF after the exhale -- there is no
		// SH2_CastExit to clear the flag, so the graph's own IsShouting is read for it here.
		// Measured: without this the flag stays up until the 8 s watchdog on every first-person
		// cast. The 50 ms floor covers the entry frame, where IsShouting may not have turned yet.
		if (pc && voice_path.load(std::memory_order_relaxed) && is_active()) {
			const double elapsed = now_ms() - state_entered_ms.load(std::memory_order_relaxed);
			bool shouting{ false };
			if (elapsed >= 50.0 &&
				pc->GetGraphVariableBool(RE::BSFixedString("IsShouting"sv), shouting) && !shouting) {
				logger::debug("SH2 cast: first person, vanilla shout state ended on its own after {:.0f}ms",
					elapsed);
				clear_state_flags();
				return;
			}
		}
		// `notified SH2_CastExit -> false` is a measured, repeating event, and a graph that
		// refuses the exit never raises it back -- which, without this watchdog, leaves the state
		// live with nothing able to clear it, and the input latch behind it retaining every press.
		if (!cast_state_watchdog_expired(is_active(),
				cast_shape.load(std::memory_order_relaxed) == CastShape::channel, now_ms(),
				state_entered_ms.load(std::memory_order_relaxed), kCastStateCapMs)) {
			return;
		}
		const double elapsed = now_ms() - state_entered_ms.load(std::memory_order_relaxed);
		logger::warn("SH2 cast: state watchdog expired after {:.0f}ms; clearing wedged cast state",
			elapsed);
		cancel(pc);
	}

	void finish(RE::PlayerCharacter* pc)
	{
		if (is_active()) {
			arm_restore();
		}
		send_exit(pc);
		clear_state_flags();
	}

	void reset_session()
	{
		clear_state_flags();
		clip_committed.store(false, std::memory_order_relaxed);
		trace_budget.store(0, std::memory_order_relaxed);
		g_castIndex.reset();
		// No swing survives a load. A stale open swing would label the first sample of the new
		// session against an index from the old one. The successor table is NOT cleared: it is
		// learned moveset shape keyed by weapon type, and that outlives a save the same way the
		// clips do.
		g_swing.close();
		teardown_stop_pending.store(false, std::memory_order_release);
		// No held attack survives a load either: the swing it was going to start belongs to the
		// session that just ended.
		clear_deferred_attack();
		ClipTranslationDriver::reset();
	}

	void interrupt_equipped_casters_if_spell(RE::PlayerCharacter* pc)
	{
		// Both hands, not just the left. The job is that no vanilla charge survives into a
		// driver action — a right-hand charge left running keeps `pc->IsCasting` true and
		// silently refuses every later press until a sheathe/draw cycle. Idle casters are a
		// no-op.
		if (!pc) {
			return;
		}
		const auto interrupt_hand = [pc](bool left_hand) {
			auto* held = pc->GetEquippedObject(left_hand);
			const bool holds_spell =
				held && (held->Is(RE::FormType::Spell) || held->Is(RE::FormType::Scroll));
			if (!isolate_caster_for_driver_cast(holds_spell)) {
				return;
			}
			const auto source = left_hand ? RE::MagicSystem::CastingSource::kLeftHand
										  : RE::MagicSystem::CastingSource::kRightHand;
			if (auto* caster = pc->GetMagicCaster(source)) {
				caster->InterruptCast(true);
			}
			logger::debug("SH2: isolated {}-hand caster (spell in {} hand)",
				left_hand ? "left" : "right", left_hand ? "left" : "right");
		};
		interrupt_hand(true);
		interrupt_hand(false);
	}
}
