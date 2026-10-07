#pragma once

namespace SpellHotbar::GameData {

enum class EquippedType {
	FIST,
	ONEHAND_EMPTY,
	ONEHAND_SHIELD,
	ONEHAND_SPELL,
	DUAL_WIELD,
	TWOHAND,
	BOW,
	SPELL,
	CROSSBOW,
	STAFF_SHIELD,
	// A staff held in the LEFT hand while the right hand establishes no stance of its own.
	// Its own value rather than STAFF_SHIELD's, because the two resolve Auto to opposite
	// hands: the cast comes from the hand that holds the staff.
	STAFF_LEFT
};

}  // namespace SpellHotbar::GameData
