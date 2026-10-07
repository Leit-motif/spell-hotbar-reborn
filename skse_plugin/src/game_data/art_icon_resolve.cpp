#include "art_icon_resolve.h"

namespace SpellHotbar {

ArtIconDrawKind resolve_art_icon_draw_kind(uint32_t icon_form, std::string_view icon_key,
	bool extra_atlas_contains_key, bool default_icons_contains_key) noexcept
{
	if (icon_form != 0) {
		return ArtIconDrawKind::Form;
	}
	if (extra_atlas_contains_key) {
		return ArtIconDrawKind::ExtraAtlas;
	}
	if (default_icons_contains_key) {
		return ArtIconDrawKind::DefaultIcon;
	}
	(void)icon_key;
	return ArtIconDrawKind::Unknown;
}

}  // namespace SpellHotbar
