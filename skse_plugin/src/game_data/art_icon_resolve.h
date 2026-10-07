#pragma once

#include <cstdint>
#include <string_view>

namespace SpellHotbar {

enum class ArtIconDrawKind {
	Form,
	ExtraAtlas,
	DefaultIcon,
	Unknown
};

[[nodiscard]] ArtIconDrawKind resolve_art_icon_draw_kind(uint32_t icon_form, std::string_view icon_key,
	bool extra_atlas_contains_key, bool default_icons_contains_key) noexcept;

}  // namespace SpellHotbar
