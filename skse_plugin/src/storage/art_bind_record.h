#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace SpellHotbar::Storage {

constexpr uint32_t wart_record = 'WART';

// One encoded bind: bar id (4), slot (1), modifier (1), art id (4).
constexpr std::size_t art_bind_encoded_size = 10;
// The co-save's record length is untrusted: a damaged save can declare anything. Every bar times
// every modifier times every slot is a few hundred binds, far under this.
constexpr uint32_t max_art_bind_record_bytes = 64 * 1024;

struct ArtBind {
	uint32_t bar_id{ 0 };
	uint8_t slot{ 0 };
	uint8_t modifier{ 0 };
	uint32_t art_id{ 0 };
};

inline void write_le_u32(std::vector<uint8_t>& out, uint32_t value)
{
	out.push_back(static_cast<uint8_t>(value));
	out.push_back(static_cast<uint8_t>(value >> 8));
	out.push_back(static_cast<uint8_t>(value >> 16));
	out.push_back(static_cast<uint8_t>(value >> 24));
}

inline bool read_le_u32(std::span<const uint8_t>& in, uint32_t& value)
{
	if (in.size() < 4) {
		return false;
	}
	value = static_cast<uint32_t>(in[0]) | (static_cast<uint32_t>(in[1]) << 8) |
		(static_cast<uint32_t>(in[2]) << 16) | (static_cast<uint32_t>(in[3]) << 24);
	in = in.subspan(4);
	return true;
}

inline std::vector<uint8_t> encode_art_binds(const std::vector<ArtBind>& binds)
{
	std::vector<uint8_t> out;
	write_le_u32(out, static_cast<uint32_t>(binds.size()));
	for (const auto& bind : binds) {
		write_le_u32(out, bind.bar_id);
		out.push_back(bind.slot);
		out.push_back(bind.modifier);
		write_le_u32(out, bind.art_id);
	}
	return out;
}

inline bool decode_art_binds(std::span<const uint8_t> bytes, std::vector<ArtBind>& out)
{
	out.clear();
	uint32_t count{ 0 };
	if (!read_le_u32(bytes, count)) {
		return false;
	}
	// Checked before reserving, so a damaged count cannot allocate past the bytes it came with.
	if (count > bytes.size() / art_bind_encoded_size) {
		return false;
	}
	out.reserve(count);
	for (uint32_t i = 0; i < count; ++i) {
		ArtBind bind;
		if (!read_le_u32(bytes, bind.bar_id) || bytes.size() < 2) {
			return false;
		}
		bind.slot = bytes[0];
		bind.modifier = bytes[1];
		bytes = bytes.subspan(2);
		if (!read_le_u32(bytes, bind.art_id)) {
			return false;
		}
		out.push_back(bind);
	}
	return bytes.empty();
}

}