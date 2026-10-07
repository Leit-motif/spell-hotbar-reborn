#include "atomic_file.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		++g_failures;
	}
}

std::string read_text(const std::filesystem::path& path)
{
	std::ifstream input(path, std::ios::binary);
	return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
}

void atomic_overlay_save_preserves_the_previous_file_until_commit()
{
	const auto root = std::filesystem::temp_directory_path() / "spellhotbar-action-overlay-test";
	std::error_code ec;
	std::filesystem::remove_all(root, ec);
	const auto path = root / "action_overlays.json";
	std::filesystem::create_directories(root, ec);
	{
		std::ofstream existing(path, std::ios::binary);
		existing << "{\"actions\":[{\"name\":\"old\"}]}\n";
	}

	const bool committed = SpellHotbar::Storage::write_file_atomically(path,
		[](std::ostream& output) {
			output << "{\"actions\":[{\"name\":\"new\"}]}\n";
			return true;
		});
	expect(committed, "a complete overlay serialization commits");
	expect(read_text(path).find("new") != std::string::npos,
		"a committed overlay can be read back after replacement");
	expect(!std::filesystem::exists(path.string() + ".tmp"),
		"the temporary overlay is removed after replacement");

	const bool failed = SpellHotbar::Storage::write_file_atomically(path,
		[](std::ostream& output) {
			output << "partial";
			return false;
		});
	expect(!failed, "a failed overlay serialization reports failure");
	expect(read_text(path).find("new") != std::string::npos,
		"a failed overlay serialization leaves the last committed file intact");
	expect(!std::filesystem::exists(path.string() + ".tmp"),
		"a failed overlay serialization removes its temporary file");

	std::filesystem::remove_all(root, ec);
}

}  // namespace

int main()
{
	atomic_overlay_save_preserves_the_previous_file_until_commit();
	return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
