#include "hotbar_serialization.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

using SpellHotbar::BarSerialization::SlotRecord;
using SpellHotbar::BarSerialization::kActionSlotKind;
using SpellHotbar::BarSerialization::kActionSlotSaveFormat;
using SpellHotbar::BarSerialization::kArtSlotKind;
using SpellHotbar::BarSerialization::kFormSlotKind;
using SpellHotbar::BarSerialization::is_action_record;
using SpellHotbar::BarSerialization::read_slot_record;
using SpellHotbar::BarSerialization::slot_is_in_range;

namespace {

int g_failures = 0;

void expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		++g_failures;
	}
}

template <std::size_t N>
class ByteReader {
public:
	explicit ByteReader(const std::array<std::uint8_t, N>& bytes) : bytes_(bytes) {}

	bool read(void* destination, std::size_t size)
	{
		if (offset_ + size > bytes_.size()) {
			return false;
		}
		std::memcpy(destination, bytes_.data() + offset_, size);
		offset_ += size;
		return true;
	}

	[[nodiscard]] std::size_t offset() const { return offset_; }

private:
	const std::array<std::uint8_t, N>& bytes_;
	std::size_t offset_{ 0 };
};

void format_8_is_the_writer_and_format_7_stays_readable()
{
	// Mirrors Storage::save_format. Format 8 adds Action kind 2 while format 7
	// remains a readable legacy record version. Keep this literal in lockstep
	// with storage.h.
	constexpr std::uint32_t writer_format = 8U;
	expect(writer_format == kActionSlotSaveFormat, "the addon writer is the Action save format");
	expect(kFormSlotKind == 0U, "form slots keep kind 0");
	expect(kArtSlotKind == 1U, "Ability slots keep kind 1");
	expect(kActionSlotKind == 2U, "Action slots use kind 2");
	expect(writer_format != 7U, "the addon writer must bump past format 7 for Action slots");
	expect(writer_format != 5U, "the addon writer must not regress to format 5");
}

void format_7_hotb_trailer_keeps_spell_gcd_before_the_keybind_count()
{
	// After the v5 shout-cooldown bool, format 7 appends one float (spell_gcd)
	// and then the existing uint8 keybind count. A format-5-shaped tail that
	// skipped the float would make the next byte (keybind count) look like
	// the first byte of a float.
	const std::array<std::uint8_t, 6> tail{
		1,
		0x00, 0x00, 0xc0, 0x3f,
		23,
	};
	ByteReader reader(tail);
	auto read = [&reader](void* destination, std::size_t size) {
		return reader.read(destination, size);
	};

	bool shout_cds = false;
	expect(read(&shout_cds, sizeof(shout_cds)), "v5 shout-cd bool is present");
	expect(shout_cds, "sample shout-cd is true");

	float spell_gcd = 0.0f;
	expect(read(&spell_gcd, sizeof(spell_gcd)), "v7 spell_gcd follows the v5 bool");
	expect(spell_gcd > 1.49f && spell_gcd < 1.51f, "sample spell_gcd is 1.5");

	std::uint8_t num_keybinds = 0;
	expect(read(&num_keybinds, sizeof(num_keybinds)), "keybind count follows spell_gcd");
	expect(num_keybinds == 23, "keybind count stays aligned after the v7 float");
	expect(reader.offset() == tail.size(), "v7 trailer consumes exactly bool+float+count");
}

void action_kind_is_only_supported_in_format_8()
{
	// Version 7 can parse the kind byte introduced in v6, but it must not
	// interpret a future kind 2 payload as a FormID. Format 8 owns Action ids.
	const std::array<std::uint8_t, 7> bytes{
		3, 2, 0x2a, 0x00, 0x00, 0x00, 1,
	};
	ByteReader reader(bytes);
	auto read = [&reader](void* destination, std::size_t size) {
		return reader.read(destination, size);
	};

	SlotRecord action;
	expect(read_slot_record(read, 7, action), "format 7 Action-shaped record is readable");
	expect(action.kind == 2, "the Action record keeps kind 2");
	expect(!is_action_record(action, 6), "format 6 does not restore an Action slot");
	expect(!is_action_record(action, 7), "format 7 does not restore an Action slot");
	expect(is_action_record(action, 8), "format 8 restores an Action slot");
	expect(action.payload == 42, "the Action id is the payload");
	expect(action.hand == 1, "the Action record keeps its hand byte");
}

void an_out_of_range_slot_is_rejected_without_desynchronizing_the_next_record()
{
	// Version 6 record layout: slot, kind, 32-bit payload, hand.
	const std::array<std::uint8_t, 14> bytes{
		12, 1, 0x44, 0x33, 0x22, 0x11, 3,
		11, 0, 0xDD, 0xCC, 0xBB, 0xAA, 2,
	};
	ByteReader reader(bytes);
	auto read = [&reader](void* destination, std::size_t size) {
		return reader.read(destination, size);
	};

	SlotRecord invalid;
	expect(read_slot_record(read, 6, invalid), "the malformed record is still fully readable");
	expect(!slot_is_in_range(invalid, 12), "serialized slot 12 is outside a 12-slot bar");
	expect(reader.offset() == 7, "rejecting a slot does not leave its payload or hand unread");

	SlotRecord valid;
	expect(read_slot_record(read, 6, valid), "the record after an invalid slot remains aligned");
	expect(slot_is_in_range(valid, 12), "serialized slot 11 is the final valid slot");
	expect(valid.slot == 11, "the next record keeps its slot index");
	expect(valid.payload == 0xAABBCCDD, "the next record keeps its payload");
	expect(valid.hand == 2, "the next record keeps its hand");
}

}  // namespace

int main()
{
	format_8_is_the_writer_and_format_7_stays_readable();
	format_7_hotb_trailer_keeps_spell_gcd_before_the_keybind_count();
	action_kind_is_only_supported_in_format_8();
	an_out_of_range_slot_is_rejected_without_desynchronizing_the_next_record();

	if (g_failures != 0) {
		std::cerr << g_failures << " failure(s)\n";
		return EXIT_FAILURE;
	}
	std::cout << "ok\n";
	return EXIT_SUCCESS;
}
