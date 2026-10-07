#include "bind_drop.h"

#include <cstdlib>
#include <iostream>

using SpellHotbar::BindMenu::apply_bind_drop;
using SpellHotbar::BindMenu::action_bind;
using SpellHotbar::BindMenu::art_bind;
using SpellHotbar::BindMenu::empty_bind;
using SpellHotbar::BindMenu::form_bind;
using SpellHotbar::BindMenu::SlotBind;

namespace {

int g_failures = 0;

void expect(bool cond, const char* msg)
{
	if (!cond) {
		std::cerr << "FAIL: " << msg << '\n';
		++g_failures;
	}
}

void dropping_test_art_onto_empty_binds_the_art_id()
{
	const SlotBind next = apply_bind_drop(SlotBind{}, art_bind(1));
	expect(next.art_id == 1, "Test Ability id 1 is bound");
	expect(next.form_id == 0, "an art bind has no FormID");
}

void dropping_a_form_onto_an_art_replaces_the_kind()
{
	SlotBind slot{ .form_id = 0, .art_id = 1 };
	const SlotBind next = apply_bind_drop(slot, form_bind(0x00012FCD));
	expect(next.form_id == 0x00012FCD, "the spell FormID is bound");
	expect(next.art_id == 0, "the previous art id is cleared");
}

void dropping_an_art_onto_a_form_replaces_the_kind()
{
	SlotBind slot{ .form_id = 0x00012FCD, .art_id = 0 };
	const SlotBind next = apply_bind_drop(slot, art_bind(1));
	expect(next.art_id == 1, "Test Ability replaces the spell");
	expect(next.form_id == 0, "the previous FormID is cleared");
}

void dropping_an_action_onto_empty_binds_the_action_id()
{
	const SlotBind next = apply_bind_drop(SlotBind{}, action_bind(42));
	expect(next.action_id == 42, "Test Action id 42 is bound");
	expect(next.form_id == 0, "an action bind has no FormID");
	expect(next.art_id == 0, "an action bind has no Ability id");
}

void dropping_an_action_onto_an_art_replaces_the_kind()
{
	SlotBind slot{ .form_id = 0, .art_id = 1, .action_id = 0 };
	const SlotBind next = apply_bind_drop(slot, action_bind(42));
	expect(next.action_id == 42, "Test Action replaces the Ability");
	expect(next.form_id == 0, "the previous FormID remains clear");
	expect(next.art_id == 0, "the previous Ability id is cleared");
}

void dropping_an_action_onto_a_form_replaces_the_kind()
{
	SlotBind slot{ .form_id = 0x00012FCD, .art_id = 0, .action_id = 0 };
	const SlotBind next = apply_bind_drop(slot, action_bind(42));
	expect(next.action_id == 42, "Test Action replaces the spell");
	expect(next.form_id == 0, "the previous FormID is cleared");
	expect(next.art_id == 0, "the previous Ability id remains clear");
}

void dropping_a_form_onto_an_action_replaces_the_kind()
{
	SlotBind slot{ .form_id = 0, .art_id = 0, .action_id = 42 };
	const SlotBind next = apply_bind_drop(slot, form_bind(0x00012FCD));
	expect(next.form_id == 0x00012FCD, "the spell FormID is bound");
	expect(next.action_id == 0, "the previous Action id is cleared");
	expect(next.art_id == 0, "the previous Ability id remains clear");
}

void dropping_an_art_onto_an_action_replaces_the_kind()
{
	SlotBind slot{ .form_id = 0, .art_id = 0, .action_id = 42 };
	const SlotBind next = apply_bind_drop(slot, art_bind(1));
	expect(next.art_id == 1, "Test Ability replaces the Action");
	expect(next.action_id == 0, "the previous Action id is cleared");
	expect(next.form_id == 0, "the previous FormID remains clear");
}

void unbind_clears_an_art_slot()
{
	SlotBind slot{ .form_id = 0, .art_id = 1, .action_id = 0 };
	const SlotBind next = apply_bind_drop(slot, empty_bind());
	expect(next.form_id == 0, "unbind leaves no FormID");
	expect(next.art_id == 0, "unbind leaves no art id");
	expect(next.action_id == 0, "unbind leaves no action id");
}

}  // namespace

int main()
{
	dropping_test_art_onto_empty_binds_the_art_id();
	dropping_a_form_onto_an_art_replaces_the_kind();
	dropping_an_art_onto_a_form_replaces_the_kind();
	dropping_an_action_onto_empty_binds_the_action_id();
	dropping_an_action_onto_an_art_replaces_the_kind();
	dropping_an_action_onto_a_form_replaces_the_kind();
	dropping_a_form_onto_an_action_replaces_the_kind();
	dropping_an_art_onto_an_action_replaces_the_kind();
	unbind_clears_an_art_slot();

	if (g_failures != 0) {
		std::cerr << g_failures << " failure(s)\n";
		return EXIT_FAILURE;
	}
	std::cout << "ok\n";
	return EXIT_SUCCESS;
}
