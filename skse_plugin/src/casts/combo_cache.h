#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string_view>

#include "attack_seam.h"

namespace SpellHotbar::casts {

struct McoCombo {
	int nextAttack = 1;
	int nextPowerAttack = 1;
};

// Last MCO combo position taken at an attack-time event. The value is preserved, never
// derived: a read of 3 is written back as 3. Age is the discriminator that a live read
// at cast start cannot be, because the ready-state payload has already stomped the
// variable to 1 by then.
//
// Capture (animation thread) and arm (main thread, on cancel/finish) share this object,
// so every mutation takes the mutex. consume() is one-shot: the first ready pass after
// a Driver Cast writes the sampled index, and a later ready/PIE cannot replay it.
class RollingMcoCombo {
public:
	static constexpr double kMaxAgeMs = 5000.0;

	void record(McoCombo sample, double nowMs)
	{
		std::lock_guard lock{ mutex_ };
		valid_ = true;
		takenAtMs_ = nowMs;
		sample_ = sample;
		pending_ = false;
	}

	// The clip teaches its two counters in SEPARATE `@SGVI` events, so the SGVI sampling path
	// can only ever update one field at a time. The other field keeps its last known value
	// rather than being reset: an attack clip that writes only `MCO_nextattack` must not blank
	// the power chain's position. Otherwise these are `record()` -- same timestamp refresh,
	// same valid_ latch, same pending_ clear, under the same mutex, because a real swing
	// teaching its advance invalidates a restore we were holding just as a full sample does.
	void record_next_attack(int nextAttack, double nowMs)
	{
		std::lock_guard lock{ mutex_ };
		valid_ = true;
		takenAtMs_ = nowMs;
		sample_.nextAttack = nextAttack;
		pending_ = false;
	}

	void record_next_power_attack(int nextPowerAttack, double nowMs)
	{
		std::lock_guard lock{ mutex_ };
		valid_ = true;
		takenAtMs_ = nowMs;
		sample_.nextPowerAttack = nextPowerAttack;
		pending_ = false;
	}

	[[nodiscard]] std::optional<McoCombo> usable(double nowMs) const
	{
		std::lock_guard lock{ mutex_ };
		return usable_locked(nowMs);
	}

	std::optional<McoCombo> arm(double nowMs)
	{
		std::lock_guard lock{ mutex_ };
		const auto combo = usable_locked(nowMs);
		if (!combo) {
			pending_ = false;
			return std::nullopt;
		}
		restore_ = *combo;
		pending_ = true;
		return restore_;
	}

	[[nodiscard]] bool restore_pending() const
	{
		std::lock_guard lock{ mutex_ };
		return pending_;
	}

	[[nodiscard]] std::optional<McoCombo> peek() const
	{
		std::lock_guard lock{ mutex_ };
		if (!pending_) {
			return std::nullopt;
		}
		return restore_;
	}

	[[nodiscard]] std::optional<McoCombo> consume()
	{
		std::lock_guard lock{ mutex_ };
		if (!pending_) {
			return std::nullopt;
		}
		pending_ = false;
		return restore_;
	}

	// A held channel is one continuous action, not a gap between actions. kMaxAgeMs measures
	// how long the player has been OUT of the chain, so the hold itself must not count against
	// it: a concentration channel is unbounded, so any hold past five seconds would age the
	// sample out and the chain could never continue. Crediting the held time keeps the cap
	// doing its real job -- refusing a combo sampled in an earlier fight -- while a hold of any
	// length hands its position on.
	void credit_held_time(double heldMs)
	{
		std::lock_guard lock{ mutex_ };
		if (!valid_ || heldMs <= 0.0) {
			return;
		}
		takenAtMs_ += heldMs;
	}

	void disarm()
	{
		std::lock_guard lock{ mutex_ };
		pending_ = false;
	}

	void clear()
	{
		std::lock_guard lock{ mutex_ };
		valid_ = false;
		pending_ = false;
	}

private:
	[[nodiscard]] std::optional<McoCombo> usable_locked(double nowMs) const
	{
		if (!valid_) {
			return std::nullopt;
		}
		if (nowMs - takenAtMs_ > kMaxAgeMs) {
			return std::nullopt;
		}
		return sample_;
	}

	mutable std::mutex mutex_;
	bool valid_ = false;
	bool pending_ = false;
	double takenAtMs_ = 0.0;
	McoCombo sample_{};
	McoCombo restore_{};
};

// SH2's own cast combo. Independent of MCO_nextattack: advances on a cast, wraps at the
// clip-set length, and is not reset by an attack. Atomic because the graph-side commitment
// point advances it on the animation thread while the start path reads it on the game loop;
// a plain int here would be a C++ data race.
class CastComboIndex {
public:
	static constexpr int kLength = 4;

	[[nodiscard]] int current() const { return index_.load(std::memory_order_relaxed); }

	void advance()
	{
		int expected = index_.load(std::memory_order_relaxed);
		while (!index_.compare_exchange_weak(expected, expected % kLength + 1,
			std::memory_order_relaxed)) {
		}
	}

	void reset() { index_.store(1, std::memory_order_relaxed); }

private:
	std::atomic<int> index_{ 1 };
};

// Public hotbar path: a second press during a committed Driver Cast's SpellFire-to-WinClose
// window is a combo step, not a refusal. Concentration and pre-spellfire are excluded by the
// holding flag the caller already computed (is_committed_cast_holding_graph); the separate
// window bit refuses late presses after WinClose until CastExit ends the instance.
enum class HotbarCastPress {
	start,
	chain,
	refuse,
};

[[nodiscard]] constexpr HotbarCastPress classify_hotbar_cast_press(
	bool has_live_cast, bool committed_cuttable_holding_graph, bool combo_window_open) noexcept
{
	if (!has_live_cast) {
		return HotbarCastPress::start;
	}
	if (committed_cuttable_holding_graph && combo_window_open) {
		return HotbarCastPress::chain;
	}
	return HotbarCastPress::refuse;
}

// Which hand's MagicCaster a graph SpellFire event belongs to. `none` is both "this
// event is not a SpellFire" and "nothing to isolate", so one return type answers the
// question and names the caster in the same value.
enum class SpellFireHand {
	none,
	left,
	right,
};

// The one place a SpellFire tag becomes a hand. The hook needs the hand twice — once for
// isolation, once for the notify's bool — and two decoders of one wire format drift.
[[nodiscard]] constexpr SpellFireHand spellfire_hand_for_tag(std::string_view tag) noexcept
{
	if (tag == "MLh_SpellFire_Event") {
		return SpellFireHand::left;
	}
	if (tag == "MRh_SpellFire_Event") {
		return SpellFireHand::right;
	}
	return SpellFireHand::none;
}

// The arming mask's bit for one hand, in `spellfire_arm_mask`'s encoding (1 = left,
// 2 = right). `none` owns no bit, so it never matches an armed hand.
[[nodiscard]] constexpr std::uint8_t spellfire_hand_bit(SpellFireHand hand) noexcept
{
	switch (hand) {
	case SpellFireHand::left:
		return 1U;
	case SpellFireHand::right:
		return 2U;
	default:
		return 0U;
	}
}

[[nodiscard]] constexpr bool spellfire_hand_is_armed(SpellFireHand hand, std::uint8_t armed_mask) noexcept
{
	const std::uint8_t bit = spellfire_hand_bit(hand);
	return bit != 0U && (armed_mask & bit) != 0U;
}

// The graph-side commitment point is any ARMED hand's SpellFire, not the left one alone.
// Keying this to MLh alone leaves an MRh clip unable to open the combo window, mark the clip
// committed, or advance the cast index — every right cast re-enters clip 1 (observed live).
//
// It must be an armed hand, not any hand: each cell plays its own hand's clip, so an unrelated
// vanilla cast's SpellFire must not open the window, mark the clip committed, or advance the
// index.
[[nodiscard]] constexpr bool is_msco_combo_window_open_event(
	SpellFireHand event_hand, std::uint8_t armed_mask) noexcept
{
	return spellfire_hand_is_armed(event_hand, armed_mask);
}

[[nodiscard]] constexpr bool is_msco_combo_window_close_event(std::string_view tag) noexcept
{
	return tag == "MSCO_WinClose" || tag == "MCO_WinClose" || tag == "MSCO_winclose" ||
		   tag == "MCO_winclose";
}

// The SGVI payload is the primary sampling edge; window-close is the retained fallback.
//
// The advance itself is a clip annotation, `@SGVI|MCO_nextattack|<N>`, and its TIME is pack data
// rather than an MCO guarantee. Thuum's reference clips annotate at t=0.06 (before HitFrame);
// the stance packs measured here (Elder Creed - Blade, Mercenary Greatsword) annotate AT
// `MCO_WinOpen`. Measured live over a four-hit chain, every `MCO_WinClose` carries the index the
// NEXT swing will use (1, 2, 3, 4), and each following `MCO_AttackInitiate` reads that value back
// as the swing it is playing -- so WinClose really is always after the advance, and attack-time
// edges are one step behind.
//
// What WinClose alone cannot survive is an INTERRUPTION (measured: sword attack2 advances at
// 0.633s and closes at 1.467s). A hotbar cast during a swing lands between those two moments: the
// advance has already happened but WinClose never fires, so the cache still holds the PREVIOUS
// swing's teaching and the restore repeats the attack that was interrupted, observed on both 1H
// and greatsword. Sampling the `@SGVI` payload itself takes the value at the moment the clip
// writes it, so an interrupted swing has already taught its advance.
//
// The advance's VALUE is parsed out of the annotation text, never read back off the graph at that
// moment: the Payload Interpreter's own write may race the event dispatch. That keeps the cache's
// "preserve, never derive" rule intact -- the number is the clip's own, and moveset length still
// lives only in the annotations.
//
// WHERE that text arrives is easy to get wrong. The clip annotation is
// `PIE.@SGVI|MCO_nextattack|3`, and the engine splits it at the FIRST `.`: everything before is
// the event NAME (raised as `Pie` -- the engine's own casing, NOT the annotation's `PIE`; a tag
// compare against `PIE` never matches), everything after is the event PAYLOAD
// (`@SGVI|MCO_nextattack|3`). `RE::BSAnimationGraphEvent` carries the two in separate members, so
// a sink hook that forwards only `tag` feeds this parser event names alone: it measures zero hits,
// which looks like "these packs emit no SGVI" while the advance arrives every swing in a field
// nobody passed on. The hook therefore forwards `payload` too, and the parser runs over both: a
// pack that puts the annotation in the event name (no `.`) and a pack that puts it in the payload
// both reach the same sampler.
//
// WinClose stays an edge as a fallback: if `@SGVI` never parses out of either field under some
// pack, behaviour degrades to WinClose-only sampling. A WinClose arriving after an SGVI simply
// re-records the same value.
//
// `attackStop` is deliberately NOT an edge, though it carries a value. It is MCO's end-of-swing
// reset to 1 -- the stomp this whole rolling cache exists to outlive. Sampling it overwrites the
// good index with the reset and restores 1 (measured live).
[[nodiscard]] constexpr bool is_mco_combo_index_edge(std::string_view tag) noexcept
{
	return is_msco_combo_window_close_event(tag);
}

// `@SGVI|<variable>|<integer>` -- the Payload Interpreter's set-graph-variable-int annotation, as
// it arrives at the animation-event sink. The argument is deliberately named for neither field:
// depending on how the pack wrote the annotation this text is the event's TAG (no `.` in the
// annotation) or its PAYLOAD (everything after the first `.`), and the caller feeds it both.
// The variable name is matched IN FULL: `msco_nextright`
// and other variables sharing a prefix must be rejected, or an unrelated clip counter would be
// learned as a combo position. Allocation-free so it can run on the animation thread.
[[nodiscard]] constexpr std::optional<int> parse_sgvi_int(
	std::string_view tag, std::string_view variable) noexcept
{
	constexpr std::string_view kPrefix{ "@SGVI|" };
	if (variable.empty() || tag.size() <= kPrefix.size() ||
		tag.substr(0, kPrefix.size()) != kPrefix) {
		return std::nullopt;
	}
	const std::string_view rest = tag.substr(kPrefix.size());
	if (rest.size() <= variable.size() || rest.substr(0, variable.size()) != variable ||
		rest[variable.size()] != '|') {
		return std::nullopt;
	}
	const std::string_view digits = rest.substr(variable.size() + 1);
	if (digits.empty()) {
		return std::nullopt;
	}
	std::size_t i = 0;
	bool negative = false;
	if (digits[0] == '-' || digits[0] == '+') {
		negative = digits[0] == '-';
		i = 1;
		if (digits.size() == 1) {
			return std::nullopt;
		}
	}
	int value = 0;
	for (; i < digits.size(); ++i) {
		const char c = digits[i];
		if (c < '0' || c > '9') {
			return std::nullopt;
		}
		value = value * 10 + (c - '0');
	}
	return negative ? -value : value;
}

// Which of the two counters a clip just taught. They arrive as separate events, so the cache
// takes them as separate partial updates.
enum class McoSgviVariable {
	next_attack,
	next_power_attack,
};

struct McoSgviSample {
	McoSgviVariable variable = McoSgviVariable::next_attack;
	int value = 1;
};

// Which swing an outgoing attack event opens.
//
// Opening the tracker below only at `MCO_AttackInitiate` / `MCO_PowerAttackInitiate` is not
// enough: BFCO's graph never raises them, so every BFCO swing would leave the tracker closed,
// every clip advance would be logged as `no open swing -> ignored`, and a press during a swing
// would have no swing to wait for. The send-side seam sees the START of every swing, whoever
// sends it, so the swing is opened there from the seam's own classification. Only the two attack
// kinds are swings; a bash, a block, or a dodge is not a combo position and opens nothing.
[[nodiscard]] constexpr std::optional<McoSgviVariable> swing_kind_for_seam(
	AttackSeamKind kind) noexcept
{
	switch (kind) {
	case AttackSeamKind::attack:
		return McoSgviVariable::next_attack;
	case AttackSeamKind::power_attack:
		return McoSgviVariable::next_power_attack;
	default:
		return std::nullopt;
	}
}

[[nodiscard]] constexpr std::optional<McoSgviSample> parse_mco_sgvi_sample(
	std::string_view tag) noexcept
{
	if (const auto next = parse_sgvi_int(tag, "MCO_nextattack")) {
		return McoSgviSample{ McoSgviVariable::next_attack, *next };
	}
	if (const auto power = parse_sgvi_int(tag, "MCO_nextpowerattack")) {
		return McoSgviSample{ McoSgviVariable::next_power_attack, *power };
	}
	return std::nullopt;
}

// Which swing is currently up, and why anything needs to know.
//
// Measured live: at `MCO_AttackInitiate` the graph's `MCO_nextattack` reads back as the index of
// the swing that is PLAYING, not its successor. The clip's own `@SGVI` advance overwrites it
// later, mid-clip. So one live read at cast begin() means two opposite things depending only on
// WHEN in the clip the interrupt landed:
//
//   * before the advance -- the value IS the playing index, and handing it on replays the very
//     swing the player just interrupted. That replay was observed on both 1H and 2H.
//   * after the advance -- the value is the successor, which is exactly what should be handed on.
//
// Nothing in the number distinguishes the two. Knowing which swing is open does: equal to the
// playing index means pre-advance, different means post-advance.
//
// `taught_by_restore` marks a swing whose open consumed a pending restore — the
// send-side seam, or MCO's initiate when that is the first open. On 2H the
// engine currently ignores the restored value, so that swing's playing index is
// UNVERIFIED -- it may not be the index we wrote. Its advance is still real and
// worth recording, but the pair (playing -> advance) must not be taught to the
// successor table, or one bad restore poisons the table for every later swing
// at that index.
//
// Opened on the animation thread, read on the input/main thread at begin(), so every access takes
// the mutex.
class McoSwingTracker {
public:
	static constexpr int kMinIndex = 1;
	static constexpr int kMaxIndex = 10;

	struct OpenSwing {
		McoSgviVariable kind = McoSgviVariable::next_attack;
		int playing = 1;
		bool taught_by_restore = false;
		// True between this swing's window-open and window-close annotations -- the span MCO's
		// own graph let a cast in at. A fresh swing opens with it false: the window belongs to
		// the clip, and the clip has not reached it yet.
		bool window_open = false;
	};

	// An index outside the plausible moveset range is not a combo position -- the read failed, or
	// the graph holds something that is not a swing index. Rather than remember a value we cannot
	// reason about, forget the swing entirely: a closed tracker degrades to today's behaviour,
	// while a wrong playing index would mislabel a post-advance sample as a replay.
	void open(McoSgviVariable kind, int playing, bool taught_by_restore)
	{
		std::lock_guard lock{ mutex_ };
		if (playing < kMinIndex || playing > kMaxIndex) {
			open_ = false;
			return;
		}
		open_ = true;
		swing_ = OpenSwing{ kind, playing, taught_by_restore };
	}

	void close()
	{
		std::lock_guard lock{ mutex_ };
		open_ = false;
	}

	// The window is a property of the swing that is open, so a window annotation arriving with no
	// open swing is nothing to record -- our own cast clips and the Ability's raise the same
	// names, and a tracker that is closed is closed.
	void set_window_open(bool value)
	{
		std::lock_guard lock{ mutex_ };
		if (open_) {
			swing_.window_open = value;
		}
	}

	[[nodiscard]] std::optional<OpenSwing> open_swing() const
	{
		std::lock_guard lock{ mutex_ };
		if (!open_) {
			return std::nullopt;
		}
		return swing_;
	}

private:
	mutable std::mutex mutex_;
	bool open_ = false;
	OpenSwing swing_{};
};

// What follows what, learned from the clips themselves.
//
// The successor of attack2 is NOT "3". Movesets wrap at their own length, stance packs reorder
// steps, and power chains are shorter than light ones -- deriving the next index arithmetically
// is exactly what the "preserve, never derive" rule forbids. So the table is filled only from a
// clip's own teaching: swing N was playing when the clip wrote M, therefore on this weapon, for
// this kind, N is followed by M. Both the payload edge and the WinClose fallback teach the same
// way.
//
// Keyed by weapon type because the movesets differ per weapon, and a greatsword's wrap must not
// answer for a dagger's. `weaponKey` is `RE::WEAPON_TYPE` cast to int (0..9, hand-to-hand through
// crossbow); anything else is refused rather than folded onto a neighbour's row.
//
// Plain arrays, zero-initialised, no allocation: this is written from the animation thread.
// Zero means "not taught yet", which is why index 0 of each row is never a valid entry.
class McoSuccessorTable {
public:
	static constexpr int kWeaponKeys = 10;
	static constexpr int kMinIndex = McoSwingTracker::kMinIndex;
	static constexpr int kMaxIndex = McoSwingTracker::kMaxIndex;

	void learn(int weaponKey, McoSgviVariable kind, int from, int to)
	{
		if (!in_range(weaponKey, from) || to < kMinIndex || to > kMaxIndex) {
			return;
		}
		std::lock_guard lock{ mutex_ };
		table_[static_cast<std::size_t>(weaponKey)][kind_index(kind)]
			  [static_cast<std::size_t>(from)] = to;
	}

	[[nodiscard]] std::optional<int> lookup(int weaponKey, McoSgviVariable kind, int from) const
	{
		if (!in_range(weaponKey, from)) {
			return std::nullopt;
		}
		std::lock_guard lock{ mutex_ };
		const int to = table_[static_cast<std::size_t>(weaponKey)][kind_index(kind)]
							 [static_cast<std::size_t>(from)];
		if (to == 0) {
			return std::nullopt;
		}
		return to;
	}

private:
	[[nodiscard]] static constexpr bool in_range(int weaponKey, int from) noexcept
	{
		return weaponKey >= 0 && weaponKey < kWeaponKeys && from >= kMinIndex && from <= kMaxIndex;
	}

	[[nodiscard]] static constexpr std::size_t kind_index(McoSgviVariable kind) noexcept
	{
		return kind == McoSgviVariable::next_attack ? 0u : 1u;
	}

	mutable std::mutex mutex_;
	std::array<std::array<std::array<int, kMaxIndex + 1>, 2>, kWeaponKeys> table_{};
};

// What the payload edge is allowed to take from a parsed advance.
//
// The ready-state and AttackState-exit notifies teach the SAME payload shape as a clip advance,
// always with value 1. When a swing is CUT, the exit notify arrives BEFORE attackStop -- measured
// live: a cast cutting attack1 delivered the exit payload with the tracker still open and
// IsAttacking still 1, which recorded 1 over the good post-advance sample and taught
// successor[1]=1. Nothing in the event tells that reset from a genuine wrap teaching (a wrap also
// teaches 1), so 1 is quarantined from the payload edge entirely -- recording and learning both.
// The wrap pair (e.g. Mercenary Greatsword's 4->1) still gets learned, one edge later, by the
// WinClose sampler: a WinClose only follows a swing that actually advanced, which is exactly what
// makes it reset-safe. Values outside 1..10 are not combo indices at all.
[[nodiscard]] constexpr bool payload_advance_is_recordable(int value) noexcept
{
	return value >= 2 && value <= McoSwingTracker::kMaxIndex;
}

// The whole pre/post-advance discriminator, in one line, so it can be tested without a graph.
//
// A live sample equal to the open swing's playing index is PRE-advance: the clip has not written
// its successor yet, and handing this value on replays the interrupted swing. Different means the
// advance already landed and the value is the successor we want. With no open swing of that kind
// there is nothing to compare against, so the sample is taken as-is -- today's behaviour.
[[nodiscard]] constexpr bool live_sample_is_pre_advance(
	bool swing_open_matching_kind, int playing, int sampled) noexcept
{
	return swing_open_matching_kind && sampled == playing;
}

// The cast-begin live read, and why it is gated on IsAttacking.
//
// The `@SGVI` path above is the seam that survives an interruption, but it sees nothing when the
// annotation's text never reaches the parser. This predicate covers that blind spot without it.
// Both run: the payload edge takes the advance the moment the clip writes it, and this live read
// still covers a pack whose annotation the parser cannot read at all.
//
// Live reads are not interchangeable with the payload edge: a live read taken BEFORE the clip's
// advance returns the playing index, so restoring it replays the interrupted swing.
// `live_sample_is_pre_advance` above is what tells the two apart, and the successor table is what
// fixes it.
//
// The seam that always fires is `MscoCastDriver::begin()`, the moment a hotbar cast starts.
// Measured live: a cast begun mid-swing logs `IsAttacking=1` and the graph still holds the
// interrupted swing's ADVANCED value (n=3 while attack2 was playing) -- MCO's stomp back to 1
// happens only after the swing is cut. The same cast re-begun through the ShoutMCO deferral 272ms
// later logs `IsAttacking=0` and reads the post-stomp 1.
//
// So a live read at begin() is trustworthy exactly when the player is attacking at that instant.
// A begin() with IsAttacking=0 -- the deferred re-begin, a cast from idle, any post-stomp tail --
// must record nothing, so the cache keeps whatever the WinClose or SGVI edges last taught it.
[[nodiscard]] constexpr bool should_sample_live_at_cast_begin(bool is_attacking) noexcept
{
	return is_attacking;
}

// What a window-close means depends on whether we are holding a restore.
//
// Measured live: after a Driver Cast writes the sampled index back, a further `MCO_WinClose`
// arrives carrying 1. The EVENT is our own borrowed cast clip's -- the ready reset fires @SGVI
// payloads, never a WinClose, and MSCO_left2.hkx carries `MCO_winclose` at 1.2s in its
// annotation dump. The VALUE 1 it carried is the ready reset's stomp read at that moment.
// Sampling it both learned the wrong value AND dropped the pending restore (record() clears
// it, by design, because a real SWING invalidates one). A reset is not a swing. So while a
// restore is pending, a window-close is a stomp to put back, not a value to learn from; the
// restore stays authoritative until an actual attack consumes it.
[[nodiscard]] constexpr bool window_close_is_a_stomp_to_undo(bool restore_pending) noexcept
{
	return restore_pending;
}

// BFCO never raises `MCO_AttackInitiate`, which is the consume edge `is_restore_edge`
// was built on. The swing already opens on the send-side seam; that open is also the
// consume. Arm already wrote the sampled index, and this swing's playing index is that
// value. Leaving pending treats the clip's own `@SGVI` advance as a ready-reset stomp
// and pins the combo until sheathe (observed with Night Flurry: restore next=2, every
// later swing `advance 3 (restore pending -> put ours back)`). Disarm without rewriting.
// An out-of-range open is not a swing; do not consume.
[[nodiscard]] constexpr bool seam_open_disarms_pending_restore(
	bool restore_pending, bool swing_opened) noexcept
{
	return restore_pending && swing_opened;
}

// MCO still raises initiate after the seam. If the seam already opened this swing as
// restore-taught and disarmed, a reopen that only reads the now-false pending flag
// would drop the mark and teach an unverified playing index to the successor table,
// poisoning it for every later swing at that index.
[[nodiscard]] constexpr bool reopen_keeps_restore_taught(
	bool restore_pending, bool already_open_same_kind, bool already_taught) noexcept
{
	return restore_pending || (already_open_same_kind && already_taught);
}

// MSCO v2 charge time → clip playback speed. Defaults match the shipped MSCO.ini
// exponential curve (Nexus 168499 / Desmos px706ivga2).
struct MscoChargeCurve {
	bool mechanic_on = true;
	bool exp_mode = true;
	float shortest = 0.0f;
	float longest = 2.0f;
	float base_time = 0.15f;
	float min_speed = 0.6f;
	float max_speed = 1.25f;
	float exp_factor = 0.17f;
};

[[nodiscard]] inline float charge_time_to_anim_speed(float charge, const MscoChargeCurve& curve) noexcept
{
	if (!curve.mechanic_on) {
		return 1.0f;
	}
	float t = charge;
	if (t < curve.shortest) {
		t = curve.shortest;
	}
	if (t > curve.longest) {
		t = curve.longest;
	}
	float speed = 1.0f;
	if (curve.exp_mode) {
		const float x = t < 1.0e-6f ? 1.0e-6f : t;
		speed = std::pow(curve.base_time / x, curve.exp_factor);
	} else {
		const float o1 = (curve.base_time - curve.shortest) > 1.0e-6f
			? (curve.base_time - curve.shortest)
			: 1.0e-6f;
		const float o2 = (curve.longest - curve.base_time) > 1.0e-6f
			? (curve.longest - curve.base_time)
			: 1.0e-6f;
		const float p1 = 1.0f + (curve.max_speed - 1.0f) * (curve.base_time - t) / o1;
		const float p2 = 1.0f - (1.0f - curve.min_speed) * (t - curve.base_time) / o2;
		speed = (t < curve.base_time) ? p1 : p2;
	}
	if (speed < curve.min_speed) {
		return curve.min_speed;
	}
	if (speed > curve.max_speed) {
		return curve.max_speed;
	}
	return speed;
}

// The cut still reads commitment. Clearing spellfire between classify and start_cast
// turns a chain press into a refuse against the live instance.
[[nodiscard]] constexpr bool keep_commitment_until_cut(HotbarCastPress press) noexcept
{
	return press == HotbarCastPress::chain;
}

// A handled spell-slot press must not also reach vanilla when the left hand
// holds a spell: the borrowed clip raises MLh_SpellFire_Event, and an
// uncaptured hotbar key is vanilla Hotkey1.
[[nodiscard]] constexpr bool capture_hotbar_press_to_prevent_dual_fire(
	bool handled_spell_slot, bool left_hand_holds_spell) noexcept
{
	return handled_spell_slot && left_hand_holds_spell;
}

// A Driver Cast or Ability interrupts a hand's MagicCaster when that hand
// holds a spell, so an in-progress vanilla charge cannot stay IsCasting after
// the shtb clip takes over (otherwise sheathe is the only reset). Applied to
// each hand independently. Idle casters are a no-op; clip SpellFire is isolated
// separately.
[[nodiscard]] constexpr bool isolate_caster_for_driver_cast(
	bool hand_holds_spell) noexcept
{
	return hand_holds_spell;
}

// The SpellFire arming mask for a resolved cast hand, as bits (1 = left,
// 2 = right). Dual arms both because either authored event is that cast's own;
// an unresolved value falls back to left — the borrowed clip's own annotation —
// rather than arming nothing and losing the commitment point. Pure so the
// contract is unit-testable. `hand` uses hand_mode's values: 1 left, 2 right,
// 3 dual.
[[nodiscard]] constexpr uint8_t spellfire_arm_mask(int hand) noexcept
{
	switch (hand) {
	case 1:
		return 1U;
	case 2:
		return 2U;
	case 3:
		return 3U;
	default:
		return 1U;
	}
}

// Whose event is this? One rule, asked by both halves of the commitment point.
//
// Two terms, and the second alone is not enough. The arming mask says which hands the LAST cast
// throws with, and it stays armed well past that cast's own clip: through the whole GCD tail
// after delivery, and deliberately through a retired cast's armed window, where the
// still-playing clip's own SpellFire is the normal way its payload leaves. So "this hand is
// armed" answers "a cast armed it at some point", not "a live cast armed it".
//
// The other term is the driver: only our own clip carries the annotation, so no driver cast
// means no event of ours. Isolation and acceptance in `notify_spellfire` must both carry it.
// Without it on the acceptance side, a vanilla release in an armed hand after the shtb clip is
// gone -- a staff's own attack, an equipped spell -- sets the seen latch and logs
// `graph raised a ... SpellFire event` with no isolation line beside it. Nothing is
// mis-delivered, because the latch is cleared at cast start; what is damaged is the commitment
// evidence the trace log is read for.
[[nodiscard]] constexpr bool spellfire_event_commits_the_cast(
	bool driver_cast_active, SpellFireHand event_hand, std::uint8_t armed_mask) noexcept
{
	return driver_cast_active && spellfire_hand_is_armed(event_hand, armed_mask);
}

// InterruptCast at begin() is too early: the caster is idle, and the borrowed clip's
// SpellFire is ~0.5s later. Vanilla also processes that event before this plugin's
// observer. Isolate immediately before vanilla sees SpellFire while a Driver Cast is
// live.
//
// Isolation is not left-only. A right-hand clip raises MRh_SpellFire_Event, which vanilla
// would otherwise use to complete an equipped RIGHT-hand spell alongside SH2's own immediate
// payload. Isolation follows the event's own hand: a dual cast raises both events and each
// isolates its own caster, which is exactly right — both equipped hands must be silenced, and
// the two events still deliver once through the SpellFire latch.
//
// Only an ARMED hand is isolated, and only an armed hand's event is swallowed. Each cell
// plays its own hand's clip, so swallowing every hand would eat an unrelated vanilla cast
// released mid-Driver-Cast. `armed_mask` is `spellfire_arm_mask`'s encoding.
//
// Expressed through `spellfire_event_commits_the_cast` rather than repeating its two terms,
// because isolation and acceptance are one question asked twice, and two copies drift apart.
[[nodiscard]] constexpr SpellFireHand isolate_caster_before_vanilla_spellfire(
	bool driver_cast_active, SpellFireHand event_hand, std::uint8_t armed_mask) noexcept
{
	return spellfire_event_commits_the_cast(driver_cast_active, event_hand, armed_mask)
		? event_hand
		: SpellFireHand::none;
}

// The left control is block when that hand holds a weapon or shield. A
// left-hand spell press during a committed Driver Cast ends the hotbar state
// the same way an attack press does.
[[nodiscard]] constexpr bool cut_committed_cast_for_left_hand_press(
	bool committed_holding_graph, bool left_hand_holds_spell, bool is_left_attack_key) noexcept
{
	return committed_holding_graph && left_hand_holds_spell && is_left_attack_key;
}

// The SpellFire annotation leads, so the payload lands at the frame the
// animation shows it leaving the hand; the authored cast time is the
// floor only as a missing-annotation fallback. Timer expiry while the clip
// is still playing is not that fallback — clip 4's SpellFire is at ~0.92s,
// past a 0.5s authored time, so a live clip waits for the pose.
enum class CastDelivery {
	wait,
	deliver,
	cancel,
};

[[nodiscard]] constexpr CastDelivery classify_cast_delivery(
	bool already_delivered, bool timer_expired, bool spellfire_seen, bool anim_ok) noexcept
{
	if (already_delivered) {
		return CastDelivery::wait;
	}
	if (spellfire_seen) {
		return CastDelivery::deliver;
	}
	if (anim_ok) {
		return CastDelivery::wait;
	}
	return timer_expired ? CastDelivery::deliver : CastDelivery::cancel;
}

// The GCD is the WHOLE lockout. No animation gates the button.
//
// Retiring a cuttable cast on `gcd_expired && spellfire_seen` would make the annotation a floor
// under the clock, which varies the cadence per clip (measured: clips 1-3 free at 1.50s, clip 4
// at 1.80s) and couples the feel to whichever animation set happens to be installed. The action
// class needs one number, so retirement is `gcd_expired` alone and there is nothing left to
// classify.
//
// The one thing such a floor would protect -- our payload IS the animation event, so retiring
// early must never eat a cast -- lives in delivery instead of the lockout. A cast retired before
// its SpellFire stays ARMED, and the rules below say when the armed payload leaves.

// Should the instance being retired be kept alive to deliver later?
//
// Only a cuttable cast can be retired with its clip still running, and only an undelivered one has
// anything left to deliver. Everything else is torn down at retirement as it always was.
[[nodiscard]] constexpr bool retired_cast_stays_armed(bool cuttable, bool already_delivered) noexcept
{
	return cuttable && !already_delivered;
}

// When an armed payload leaves the hand. Whichever comes first wins; the CUT is not here because
// it is not a poll -- the next press calls the delivery directly before starting its own cast.
//
// `already_delivered` is the latch: one delivery per instance, whichever path reaches it first
// (the same bit `CastingInstance::deliver_payload` returns early on).
enum class ArmedDelivery {
	hold,
	on_spellfire,  // the normal case: no press, the clip plays on and raises its own event
	on_clip_end,   // the fallback: the clip ended having raised no SpellFire at all
};

// The spell that fires 8 seconds late. `on_spellfire` above assumes the commitment annotation
// can still arrive. For a cast that began in THIRD person it cannot, once the camera has gone:
// `MLh_SpellFire_Event` is an annotation on the third-person graph, and those were measured to
// stop reaching the event sink the moment the camera leaves it.
//
// Measured: the payload then sat owed through every poll until the 8 s state watchdog cleared
// the driver and `on_clip_end` finally fired. The same spell undisturbed delivers at 0.43 s.
// Nothing was broken -- all three exits simply failed closed, and the watchdog cap WAS the delay.
//
// A cast that STARTS in first person already has the answer: it commits on the authored cast
// timer, because vanilla's shout path has no SpellFire annotation either. This predicate is what
// lets a cast reach that same clock when the camera takes the annotation away mid-flight. It is
// deliberately not the weapon art's annotation-anchored deadline -- an art has no vanilla
// fallback, so cancelling is all it can do, while a cast has one and should take it.
[[nodiscard]] constexpr bool clip_annotations_unreachable(
	bool began_on_voice_path, bool camera_is_first_person) noexcept
{
	return !began_on_voice_path && camera_is_first_person;
}

[[nodiscard]] constexpr ArmedDelivery classify_armed_delivery(
	bool already_delivered, bool spellfire_seen, bool clip_active) noexcept
{
	if (already_delivered) {
		return ArmedDelivery::hold;
	}
	if (spellfire_seen) {
		return ArmedDelivery::on_spellfire;
	}
	return clip_active ? ArmedDelivery::hold : ArmedDelivery::on_clip_end;
}

// The clip outlives the instance, and the cut has to outlive it too.
//
// Because a fire-and-forget cast retires at GCD expiry, `current_cast` is gone for the whole
// follow-through while the borrowed clip keeps playing. `is_committed_cast_holding_graph`
// therefore reads false from retirement to clip end, and the attack chain-out it gates would go
// with it -- the player would have to wait out the entire clip to swing.
//
// What is left standing after retirement is the driver: the shtb state is still active, so there
// is still a state to end and still a tail to turn into the start of a swing. That is the whole
// rule -- no live cast, driver still active.
//
// A CHARGING cast is the case this must not swallow, and it is excluded by the same first term: a
// pre-SpellFire cast still has `current_cast` set, so it reads false here and stays protected by
// the committed gate exactly as before, where a cut would cost the player the spell.
[[nodiscard]] constexpr bool graph_is_in_cuttable_follow_through(
	bool has_live_cast, bool driver_active) noexcept
{
	return !has_live_cast && driver_active;
}

// Ability latch: WinOpen if the bound clip carries it, else HitFrame, else SH2_ArtExit.
enum class AbilityLatch {
	winOpen,
	hitFrame,
	artExit,
};

[[nodiscard]] constexpr AbilityLatch classify_ability_latch(
	bool has_win_open, int hit_frame_count) noexcept
{
	if (has_win_open) {
		return AbilityLatch::winOpen;
	}
	if (hit_frame_count > 0) {
		return AbilityLatch::hitFrame;
	}
	return AbilityLatch::artExit;
}

// The `hitFrame` latch opens on the clip's LAST hit frame, not its first, and this is the count
// that says which one that is.
//
// Opening on the first lets an art be cancelled at almost any point: 16 of the 25 arts on this
// fallback land several hits, so the window would open partway through the art's own combo.
// Measured over the 57-clip pack: first hit at a median 22% of the clip, last hit at 55%. The 21
// arts whose source moveset authored an `MCO_WinOpen` put it at a median 57% -- just past their
// last hit. So the fallback and the authored case want the same frame, and taking the last hit
// derives it instead of asking anyone to stamp it. It is also the ordinary action-game rule:
// commit through the active frames, cancel out of recovery.
[[nodiscard]] constexpr bool ability_latch_opens_on_hit(
	int hit_frame_count, int hits_seen) noexcept
{
	return hits_seen >= hit_frame_count && hit_frame_count > 0;
}

// One frame at 30 fps: a hit frame the clip is this close to counts as reached.
inline constexpr float kLatchClipTimeSlack = 1.0f / 30.0f;

// The same "last hit" rule, read from the clip's own time: the `hitFrame` latch opens on a hit frame
// that arrives once the clip has reached its last hit frame. Counting arrivals breaks wherever two hit
// frames share a game frame and arrive as one. Dual Flurry authors its hits in same-time pairs, one
// per hand, and Head Chopper puts two 0.017 s apart, so a count of four sees three and the latch waits
// for SH2_ArtExit. A negative clip time means none is known, and the caller falls back to the count.
[[nodiscard]] constexpr bool ability_latch_opens_at(float clip_time, float last_hit_time) noexcept
{
	return clip_time >= 0.0f && clip_time + kLatchClipTimeSlack >= last_hit_time;
}

// An Ability clip's authored cancel window, in both annotation vocabularies: `BFCO_NextWinStart`
// is BFCO's own name for `MCO_WinOpen` (an alias BFCO's author documents), and a Custom Ability
// can play a BFCO-native clip. Power windows are deliberately NOT here, unlike
// `is_mco_swing_window_open_event`: six shipped Abilities carry only a power window, and reading
// it would move their latch off the last HitFrame they are tuned to (up to 0.32 s).
[[nodiscard]] constexpr bool is_ability_win_open_event(std::string_view tag) noexcept
{
	return tag == "MSCO_WinOpen" || tag == "MCO_WinOpen" || tag == "MSCO_winopen" ||
		   tag == "MCO_winopen" || tag == "BFCO_NextWinStart";
}

[[nodiscard]] constexpr bool is_ability_hit_frame_event(std::string_view tag) noexcept
{
	return tag == "HitFrame";
}

// The latch counts a clip's hit frames from its annotation TEXT, which is not what the graph sends
// at runtime. `HitFrame.$AoW_Knockback` is the event HitFrame carrying a payload, and the graph
// matches annotation text to its events without case, so `Hitframe` is HitFrame too. Counting only
// the exact text opens Flurry Strike's latch on hit 5 of 6, before its knockback finisher.
[[nodiscard]] constexpr bool is_hit_frame_annotation(std::string_view text) noexcept
{
	constexpr std::string_view hit_frame = "hitframe";
	const auto name = text.substr(0, text.find('.'));
	if (name.size() != hit_frame.size()) {
		return false;
	}
	for (std::size_t i = 0; i < name.size(); ++i) {
		const char c = name[i] >= 'A' && name[i] <= 'Z' ? static_cast<char>(name[i] - 'A' + 'a') : name[i];
		if (c != hit_frame[i]) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] constexpr bool is_ability_latch_event(AbilityLatch latch, std::string_view tag) noexcept
{
	switch (latch) {
	case AbilityLatch::winOpen:
		return is_ability_win_open_event(tag);
	case AbilityLatch::hitFrame:
		return is_ability_hit_frame_event(tag);
	case AbilityLatch::artExit:
		return tag == "SH2_ArtExit";
	}
	return false;
}

// Whether a press should be held BEFORE the attempt rather than after a refusal.
//
// A latch that only backs up a refusal (try the graph, and hold only what it turns down) works
// only while the graph turns down every mid-swing cast, so that the refusal itself is the
// "you are in someone else's swing" signal. `AttackState` has an edge into `shtb`, so the graph
// accepts at any frame -- and a press tried first fires on whatever frame it arrived on.
// Measured: a cast pressed 256 ms into a 1.9 s BFCO power attack started there, cutting the
// swing near its start. Without that edge the same press waits for the swing's end. Neither is
// right: the press belongs in the cancel window.
//
// Our own state is not covered here -- `our_latch_is_closed()` gates that, on a different
// question -- and concentration is excluded for the same reason it is below.
[[nodiscard]] constexpr bool should_hold_cast_for_foreign_swing(
	bool our_shtb_busy, bool concentration_live, bool someone_elses_swing_open,
	bool swing_window_open) noexcept
{
	if (concentration_live || our_shtb_busy) {
		return false;
	}
	return someone_elses_swing_open && !swing_window_open;
}

// Our Driver Cast or Ability owns release until its latch opens. Concentration is excluded.
//
// Someone else's swing cannot be left to ShoutMCO's clock: ShoutMCO only sees a swing through
// MCO's initiate events, which BFCO never raises, so under BFCO it answers PASS_THROUGH to every
// mid-swing press and the press would be refused. The seam opens the swing tracker for every
// attack that reaches the graph, so a press held during a visible swing is held here and
// released when the swing closes -- the same "no longer attacking" edge ShoutMCO's own READY
// cause names. ShoutMCO keeps the shout clock; the swing clock is local.
//
// The window term: an open swing is not a flat "hold until it ends". Inside BFCO's own cancel
// window the graph is willing to take a cast, which is exactly where the press should land.
// Outside it -- before the window opens, or after it closes -- the press is still held to the
// swing's `attackStop`, which is also the fallback the release path returns to when the graph
// refuses a press inside the window after all.
[[nodiscard]] constexpr bool should_retain_local_cast_intent(
	bool our_shtb_busy, bool local_latch_open, bool concentration_live,
	bool someone_elses_swing_open, bool swing_window_open) noexcept
{
	if (concentration_live) {
		return false;
	}
	if (our_shtb_busy) {
		return !local_latch_open;
	}
	// One definition of the foreign-swing term, so a later change to the wait cannot leave the
	// hold and the release disagreeing about when a swing stops mattering.
	return should_hold_cast_for_foreign_swing(
		our_shtb_busy, concentration_live, someone_elses_swing_open, swing_window_open);
}

// A waiting state that cannot time out is a bug, not a feature: ShoutMCO bounds every one of its
// own waits, and the local latch was the only waiter in this stack with no cap. These match its
// pressCapMs and shoutCapMs, so a press and a cast state expire on the same clocks either side of
// the boundary.
inline constexpr double kLocalLatchCapMs = 4000.0;
inline constexpr double kCastStateCapMs = 8000.0;

// A press waiting on SOMEONE ELSE's swing is a different wait, and 4 s is the wrong number for it.
//
// The cap above is ShoutMCO's `pressCapMs`, adopted for parity across that API boundary rather
// than measured against anything here. Behind OUR OWN cast clip it is defensible -- the press is
// queued behind an animation we started and can see the end of. Behind someone else's swing it
// means a press made 1.2 s before the cancel window still fires there, which is exactly the
// failure: the cast goes off at a frame the player did not choose.
//
// BFCO itself does not buffer at all. Its transitions out of `AttackState` carry zero-length
// trigger intervals, so the gating is entirely in conditions reading graph variables that the
// clip's own annotations set at the window and clear at `BFCO_DIY_EndLoop`; a press outside that
// span fails the condition and is gone. Matching that exactly would drop most cast presses --
// measured on the moveset under test, the legal span is 0.417 s of a 2.500 s clip, 17% -- which
// is fine for an attack you are already mashing and wrong for a hotbar key pressed once.
//
// So: a real input buffer instead. A press close to the window still fires; a press made early in
// the swing expires and flashes the slot, and the player presses again.
inline constexpr double kForeignSwingPressBufferMs = 250.0;

[[nodiscard]] constexpr double press_hold_cap_ms(bool holding_for_foreign_swing) noexcept
{
	return holding_for_foreign_swing ? kForeignSwingPressBufferMs : kLocalLatchCapMs;
}

[[nodiscard]] constexpr bool local_latch_hold_expired(
	double now_ms, double retained_at_ms, double cap_ms) noexcept
{
	return (now_ms - retained_at_ms) >= cap_ms;
}

// The cast state ends when the graph raises SH2_CastExit, and the graph is free to refuse the
// notify that asks it to. A held concentration channel is the one case where a long-lived state is
// correct -- it owns the state for the whole hold -- so it is never watched. Zero means no entry
// was recorded, which is not an elapsed time.
[[nodiscard]] constexpr bool cast_state_watchdog_expired(bool state_active, bool is_channel,
	double now_ms, double entered_at_ms, double cap_ms) noexcept
{
	if (!state_active || is_channel || entered_at_ms <= 0.0) {
		return false;
	}
	return (now_ms - entered_at_ms) >= cap_ms;
}

// A cast cut by an attack raises its OWN attackStop about 10 ms after the seam cuts it -- the
// cast state's exit, not the swing's end. Measured: the seam cuts, forwards, opens the swing at
// `note_attack_forwarded`, and that teardown stop then closes the tracker before the BFCO clip's
// own advance arrives. Under MCO the clip's `MCO_PowerAttackInitiate` re-opened it; BFCO raises
// no initiate, so nothing does, and the cut swing's successor is never learned.
//
// So the cut arms a one-shot pass, and the first stop inside the cap is absorbed rather than
// closing the swing. The cap is what stops a teardown that raises no stop at all from eating a
// later real `attackStop`: past it the flag is spent whether or not anything consumed it. 300 ms
// is comfortably longer than the measured 10 ms gap and comfortably shorter than any attack
// clip's own stop.
inline constexpr double kTeardownStopCapMs = 300.0;

[[nodiscard]] constexpr bool teardown_stop_absorbs_close(
	bool armed, double now_ms, double armed_at_ms, double cap_ms) noexcept
{
	return armed && (now_ms - armed_at_ms) < cap_ms;
}

// The cut's own `SH2_CastExit` is accepted synchronously, but the transition it asks for lands a
// frame later -- measured live, in 3 of 3 cut cases, at 14 ms AFTER the seam forwarded the
// attack. `attackStop` + `SBF_ReadyStart` + `MSCO_MagicReady` then land inside a swing that has
// already started and is still ~900 ms from its own `MCO_WinOpen`, and the swing dies ~200 ms
// later having opened no window at all.
//
// The negative control from the same run settles what is to blame: an attack entered 109 ms
// AFTER the identical triple opens both windows and plays through. The triple is harmless; its
// POSITION relative to entry is not. So the cut stops forwarding in the same pass. It captures
// the attack, waits for the exit's transition to land, and re-sends it -- putting the attack on
// the side of the triple the control proves works.
inline constexpr double kDeferredAttackCapMs = 200.0;

// The observable edge for "the exit's transition has landed": the graph raises these on entering
// magic-ready. `attackStop` leads the same triple and is deliberately excluded -- it is also what
// every real swing ends with, and the deferral must never key on an event a swing owns.
[[nodiscard]] constexpr bool is_cast_exit_landed_event(std::string_view tag) noexcept
{
	return tag == "MSCO_MagicReady" || tag == "SBF_ReadyStart";
}

// A cap rather than an indefinite wait: a graph that accepts `SH2_CastExit` and then raises
// neither ready tag exists (measured: a greatsword fired neither after the exit), and an attack
// the player pressed has to reach the graph either way. 200 ms is comfortably past the measured
// 14 ms transition and short of a swing's own length.
[[nodiscard]] constexpr bool deferred_attack_should_send(
	bool armed, bool exit_landed, double now_ms, double armed_at_ms, double cap_ms) noexcept
{
	return armed && (exit_landed || (now_ms - armed_at_ms) >= cap_ms);
}

// The same hold for an attack whose cut ran BEFORE it reached the seam. An SH2 Action cuts the
// cast when it queues its key, so the attack that key produces arrives at the seam a frame later
// with nothing left to cut, and used to forward straight into the exit's transition. Measured
// live: five Action power attacks sent that way stopped 204-226 ms in with no window,
// while the one power attack the seam held played through. So the cut leaves a marker, and an
// attack reaching the seam while it stands is held exactly as the seam's own cut holds one. The
// re-send is exempt (it IS the held attack), the ready triple clears the marker, and it lapses
// on the same cap so a graph that raises no ready tag cannot hold a later press.
[[nodiscard]] constexpr bool seam_holds_for_earlier_cut(
	bool is_resend, bool cut_waiting, double now_ms, double cut_at_ms, double cap_ms) noexcept
{
	return !is_resend && cut_waiting && (now_ms - cut_at_ms) < cap_ms;
}

// Who consumes a pending combo restore at the ready edge. Normally the ready edge does: it is
// the last stomp before the next swing. But a cast cut by an attack HOLDS that attack until the
// exit transition lands, and the ready state's own `@SGVI|MCO_nextattack|1` reset arrives after
// the ready edge and before the held send. Consumed at the edge, nothing undoes that reset and
// the held attack plays attack 1. So while an attack is held, the edge re-writes but keeps the
// restore pending; the stomp-undo branches put it back, and the held attack's seam send is the
// consume -- the same rule BFCO's graph already follows.
[[nodiscard]] constexpr bool ready_edge_consumes_restore(bool attack_held_for_exit) noexcept
{
	return !attack_held_for_exit;
}

// The swing's cancel window, under BOTH annotation vocabularies.
//
// MCO's names alone are not enough, even though a BFCO swing can show `MCO_WinOpen` /
// `MCO_PowerWinOpen`. BFCO documents its own event set and says of MCO's only that "MCO
// annotations can also work with BFCO" -- a compatibility path, not its native vocabulary. A clip
// raising `MCO_WinOpen` proves that CLIP is MCO-annotated and proves nothing about BFCO.
//
// BFCO's own names, from the mod page's EVENT LIST (v3.100.8A):
//
//   BFCO_NextWinStart       "Attack-N win open"
//   BFCO_NextPowerWinStart  "Attack-P win open"
//   BFCO_DIY_EndLoop        "Attack-N&P win close"
//
// Both vocabularies are matched because both occur: a BFCO-native moveset raises the BFCO set, an
// MCO-annotated moveset played under BFCO raises MCO's. Neither is the odd one out.
//
// `BFCO_DIY_recovery` is deliberately NOT a window event. Its author defines it as the point
// "from now on, player can use movement/block keys to end this attack" -- a tail that should also
// be cancellable, but a different question from the combo window, and one that deserves its
// own decision rather than being folded in here for being nearby.
[[nodiscard]] constexpr bool is_mco_swing_window_open_event(std::string_view tag) noexcept
{
	return is_ability_win_open_event(tag) || tag == "MCO_PowerWinOpen" ||
		   tag == "MSCO_PowerWinOpen" || tag == "MCO_powerwinopen" || tag == "MSCO_powerwinopen" ||
		   tag == "BFCO_NextPowerWinStart";
}

[[nodiscard]] constexpr bool is_mco_swing_window_close_event(std::string_view tag) noexcept
{
	return is_msco_combo_window_close_event(tag) || tag == "MCO_PowerWinClose" ||
		   tag == "MSCO_PowerWinClose" || tag == "MCO_powerwinclose" ||
		   tag == "MSCO_powerwinclose" || tag == "BFCO_DIY_EndLoop";
}

[[nodiscard]] constexpr bool should_cut_ability_for_attack(
	bool art_active, bool latch_open, bool is_attack_press) noexcept
{
	return art_active && latch_open && is_attack_press;
}

[[nodiscard]] constexpr bool should_capture_attack_during_ability(
	bool art_active, bool latch_open, bool is_attack_press) noexcept
{
	return art_active && !latch_open && is_attack_press;
}

// The graph can leave SH2_Art_State without raising SH2_ArtExit. Measured:
// Holding Thorns (art 34) landed one hit, then SBF_DefaultStop / SBF_DefaultStart /
// SBF_ReadyStart while the latch stayed shut, and the seam captured every attack until
// the eight-second deadline. ReadyStart and DefaultStart are the graph saying it is back
// in locomotion, so the Ability latch must drop even though the authored exit never arrived.
[[nodiscard]] constexpr bool is_ability_graph_lost_event(std::string_view tag) noexcept
{
	return tag == "SBF_ReadyStart" || tag == "SBF_DefaultStart";
}

// HitFrame / AttackInitiate on our Driver Cast or Ability are not a new MCO
// swing. Recording them would replace the pre-Ability sample with 1 and the
// recovery attack would restart the combo.
[[nodiscard]] constexpr bool should_record_mco_combo_sample(bool our_shtb_busy) noexcept
{
	return !our_shtb_busy;
}

// What the shtb cast state is being used for. A fire-and-forget cast owns the state for the
// whole clip and leaves when the clip ends. A concentration channel owns its own state for the
// whole hold: SH2_Channel_State plays a MODE_LOOPING generator on the shout-inhale path, which
// is the one clip every non-idle conc submod replaces, so OAR still picks the per-family clip
// from SpellHotbar_SpellAnimationType.
enum class CastShape {
	fire_and_forget,
	channel,
};

// Which notify enters the state. A fire-and-forget press walks the MSCO_left1..left4 clip set,
// so its event comes from the cast index; a channel has one entry of its own and never touches
// that set. Kept here rather than in the driver so the split is testable without a graph.
[[nodiscard]] constexpr bool cast_entry_walks_clip_set(CastShape shape) noexcept
{
	return shape == CastShape::fire_and_forget;
}

// SH2_CastExit arriving with no SpellFire behind it means a fire-and-forget press produced no
// payload -- the clip was cut before it committed -- and the cast index must go back to 1. A
// channel reaches its exit that way by design: it commits on the authored cast-time floor
// rather than on a clip annotation, so treating its exit as a dropped press would reset the
// fire-and-forget combo position after every hold.
[[nodiscard]] constexpr bool exit_without_spellfire_is_a_dropped_press(CastShape shape) noexcept
{
	return shape == CastShape::fire_and_forget;
}

// SH2's cast-combo index walks the MSCO_left1..left4 clip set. A channel is one cast held, not
// a chain step, so its SpellFire commits the cast without moving the index.
[[nodiscard]] constexpr bool spellfire_advances_cast_index(CastShape shape) noexcept
{
	return shape == CastShape::fire_and_forget;
}

// Nor does a channel's SpellFire open the follow-up-press window. That window is an envelope
// inside a clip that is still playing; a channel's start clip has already handed off, and a
// second hotbar cast during a hold is a refusal, not a chain.
[[nodiscard]] constexpr bool spellfire_opens_combo_window(CastShape shape) noexcept
{
	return shape == CastShape::fire_and_forget;
}

// A channel's chain-out window is the hold itself. It opens when the channel commits and the
// spell starts streaming, and it closes when the player lets go. There is no clip clock to
// read here, because the start clip ended long before the hold does.
[[nodiscard]] constexpr bool channel_chain_window_open(
	bool channel_live, bool channel_streaming) noexcept
{
	return channel_live && channel_streaming;
}

// An attack inside that window ends the channel and becomes the swing. The press itself is not
// captured: it travels the rest of the dispatch, the same fail-safe the committed-cast cut uses,
// so a graph that refuses the cut leaves the player an ordinary attack, not a swallowed one.
[[nodiscard]] constexpr bool should_cut_channel_for_attack(
	bool channel_chain_open, bool is_attack_press) noexcept
{
	return channel_chain_open && is_attack_press;
}

// Combo position must survive the hold: the swing after a channel continues the chain the
// channel interrupted, through the same combo-index write-back a Driver Cast arms at SH2_CastExit.
// A channel that never started streaming has nothing to hand off.
[[nodiscard]] constexpr bool channel_end_arms_combo_restore(bool channel_streaming) noexcept
{
	return channel_streaming;
}

// Is a sample still usable at the end of a hold? `sampleAgeMs` is wall-clock age; `heldMs` is
// the part of it spent holding the channel, which does not count. Time between the sampled
// swing and the start of the hold still does.
[[nodiscard]] constexpr bool combo_sample_survives_hold(double sampleAgeMs, double heldMs) noexcept
{
	return (sampleAgeMs - heldMs) <= RollingMcoCombo::kMaxAgeMs;
}

// A hotbar shout that is allowed to start must leave SH2_Art_State / the
// Driver Cast before the Shout ButtonEvent, or both clips play together.
[[nodiscard]] constexpr bool should_yield_shtb_before_hotbar_shout(
	bool our_shtb_busy, bool local_latch_open) noexcept
{
	return our_shtb_busy && local_latch_open;
}

}  // namespace SpellHotbar::casts
