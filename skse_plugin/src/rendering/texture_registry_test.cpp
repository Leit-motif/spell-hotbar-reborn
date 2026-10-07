#include "texture_registry.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using SpellHotbar::TextureRegistry::load;

namespace {

int g_failures = 0;

void expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		++g_failures;
	}
}

struct FakeTexture {
	std::string name;
};

void a_failed_load_returns_no_index_and_never_reuses_a_prior_texture()
{
	std::vector<FakeTexture> textures{ FakeTexture{ "existing" } };
	const auto result = load(textures, [](FakeTexture&) { return false; });

	expect(!result.has_value(), "a decode failure has no texture index");
	expect(textures.size() == 1, "a decode failure does not append a registry entry");
	expect(textures.front().name == "existing", "a decode failure cannot masquerade as the prior texture");
}

void a_successful_load_returns_the_new_entry()
{
	std::vector<FakeTexture> textures{ FakeTexture{ "existing" } };
	const auto result = load(textures, [](FakeTexture& texture) {
		texture.name = "new";
		return true;
	});

	expect(result.has_value() && *result == 1, "the new texture index is returned");
	expect(textures.size() == 2, "a successful load appends exactly one entry");
	expect(textures.back().name == "new", "the returned index names the loaded texture");
}

}  // namespace

int main()
{
	a_failed_load_returns_no_index_and_never_reuses_a_prior_texture();
	a_successful_load_returns_the_new_entry();

	if (g_failures != 0) {
		std::cerr << g_failures << " failure(s)\n";
		return EXIT_FAILURE;
	}
	std::cout << "ok\n";
	return EXIT_SUCCESS;
}
