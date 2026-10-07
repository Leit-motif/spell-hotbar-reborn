#pragma once

#include <filesystem>
#include <fstream>
#include <ostream>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace SpellHotbar::Storage {

/**
 * Serialize one file to a sibling temporary path, flush and close it, then replace the target
 * with one atomic rename. The writer returns false for serialization failure; in either failure
 * case the previous target remains intact and the temporary file is removed.
 */
template <typename Writer>
bool write_file_atomically(const std::filesystem::path& path, Writer&& writer)
{
	const auto parent_dir = path.parent_path();
	if (!parent_dir.empty()) {
		std::error_code ec;
		std::filesystem::create_directories(parent_dir, ec);
		if (ec) {
			return false;
		}
	}

	auto temporary_path = path;
	temporary_path += ".tmp";
	std::ofstream output(temporary_path, std::ios::binary | std::ios::out | std::ios::trunc);
	if (!output.is_open()) {
		return false;
	}

	bool serialized = false;
	try {
		serialized = std::forward<Writer>(writer)(output);
	} catch (...) {
		serialized = false;
	}
	if (serialized) {
		output.flush();
		serialized = output.good();
	}
	output.close();
	serialized = serialized && !output.fail();
	if (!serialized) {
		std::error_code ec;
		std::filesystem::remove(temporary_path, ec);
		return false;
	}

#if defined(_WIN32)
	if (!MoveFileExW(temporary_path.c_str(), path.c_str(),
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		std::error_code ec;
		std::filesystem::remove(temporary_path, ec);
		return false;
	}
#else
	std::error_code ec;
	std::filesystem::rename(temporary_path, path, ec);
	if (ec) {
		std::filesystem::remove(temporary_path, ec);
		return false;
	}
#endif
	return true;
}

}  // namespace SpellHotbar::Storage
