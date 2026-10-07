#pragma once

#include <cstddef>
#include <cstdint>

namespace SpellHotbar::BarSerialization {

inline constexpr std::uint32_t kActionSlotSaveFormat = 8U;
inline constexpr std::uint8_t kFormSlotKind = 0U;
inline constexpr std::uint8_t kArtSlotKind = 1U;
inline constexpr std::uint8_t kActionSlotKind = 2U;

struct SlotRecord {
	std::uint8_t slot{ 0 };
	std::uint8_t kind{ 0 };
	std::uint32_t payload{ 0 };
	std::uint8_t hand{ 0 };
};

template <class Read>
bool read_slot_record(Read&& read, std::uint32_t version, SlotRecord& record)
{
	record = {};
	if (!read(&record.slot, sizeof(record.slot))) {
		return false;
	}
	if (version >= 6 && !read(&record.kind, sizeof(record.kind))) {
		return false;
	}
	if (!read(&record.payload, sizeof(record.payload))) {
		return false;
	}
	return read(&record.hand, sizeof(record.hand));
}

[[nodiscard]] constexpr bool is_action_record(const SlotRecord& record, std::uint32_t version) noexcept
{
	return version >= kActionSlotSaveFormat && record.kind == kActionSlotKind;
}

[[nodiscard]] constexpr bool slot_is_in_range(const SlotRecord& record, std::size_t slot_count) noexcept
{
	return record.slot < slot_count;
}

}  // namespace SpellHotbar::BarSerialization
