#pragma once

#include <optional>
#include <utility>

namespace SpellHotbar::TextureRegistry {

template <class Registry, class Loader>
[[nodiscard]] auto load(Registry& registry, Loader&& loader) -> std::optional<typename Registry::size_type>
{
	typename Registry::value_type texture;
	if (!std::forward<Loader>(loader)(texture)) {
		return std::nullopt;
	}

	registry.push_back(std::move(texture));
	return registry.size() - 1;
}

}  // namespace SpellHotbar::TextureRegistry
