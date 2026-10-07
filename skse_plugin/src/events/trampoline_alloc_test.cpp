// Guards an invariant whose violation crashed every load with a real detour installed: the SKSE
// trampoline is allocated EXACTLY ONCE, in plugin.cpp, before any hook installs.
//
// `SKSE::AllocTrampoline` does not extend the buffer. It replaces it, and when the old buffer came
// from CommonLib's fallback allocation it is freed, so a second call can silently invalidate every
// stub already written into the first. That is
// invisible to the compiler, invisible to a linking build, and invisible to every other test in
// this suite -- the DLL built and linked clean with two calls in it. What it produced was
// `EXCEPTION_ACCESS_VIOLATION` executing a freed page, on the first non-player special idle after
// a load, roughly one second after the loading screen closed.
//
// A second call in `gameloop_hook.h` is harmless only while every hook in this build is a vtable
// write and nothing lives in the trampoline. A real detour can make it fatal; one on
// `AIProcess::SetupSpecialIdle` did. So the hazard is not "someone wrote bad
// code" -- it is that adding a detour retroactively arms an old line somewhere else in the tree.
// A reviewer cannot be expected to make that connection; this test can.
//
// It reads the source tree rather than the binary, because the thing being asserted is a property
// of the source. SH2_SOURCE_DIR is baked in by CMake.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void expect(bool cond, const std::string& msg)
{
	if (!cond) {
		std::cerr << "FAIL: " << msg << '\n';
		++g_failures;
	}
}

// Strip line comments and block comments so the explanatory prose in `gameloop_hook.h` -- which
// names `AllocTrampoline` on purpose, to stop the line being re-added -- is not counted as a call.
std::string strip_comments(const std::string& src)
{
	std::string out;
	out.reserve(src.size());
	enum class State { code, line_comment, block_comment, string_lit };
	State state = State::code;
	for (std::size_t i = 0; i < src.size(); ++i) {
		const char c = src[i];
		const char n = (i + 1 < src.size()) ? src[i + 1] : '\0';
		switch (state) {
		case State::code:
			if (c == '/' && n == '/') { state = State::line_comment; ++i; }
			else if (c == '/' && n == '*') { state = State::block_comment; ++i; }
			else if (c == '"') { state = State::string_lit; out.push_back(c); }
			else { out.push_back(c); }
			break;
		case State::line_comment:
			if (c == '\n') { state = State::code; out.push_back(c); }
			break;
		case State::block_comment:
			if (c == '*' && n == '/') { state = State::code; ++i; }
			break;
		case State::string_lit:
			out.push_back(c);
			if (c == '\\') { if (i + 1 < src.size()) { out.push_back(n); ++i; } }
			else if (c == '"') { state = State::code; }
			break;
		}
	}
	return out;
}

std::size_t count_occurrences(const std::string& hay, const std::string& needle)
{
	std::size_t n = 0;
	for (std::size_t pos = hay.find(needle); pos != std::string::npos;
		 pos = hay.find(needle, pos + needle.size())) {
		++n;
	}
	return n;
}

}

int main()
{
	namespace fs = std::filesystem;

	const fs::path root{ SH2_SOURCE_DIR };
	if (!fs::exists(root)) {
		std::cerr << "FAIL: source dir not found: " << root.string() << '\n';
		return EXIT_FAILURE;
	}

	std::vector<std::string> call_sites;
	std::size_t total = 0;

	for (const auto& entry : fs::recursive_directory_iterator(root)) {
		if (!entry.is_regular_file()) {
			continue;
		}
		const auto ext = entry.path().extension().string();
		if (ext != ".cpp" && ext != ".h" && ext != ".hpp") {
			continue;
		}
		// This file talks about the function by name; it must not count itself.
		if (entry.path().filename() == "trampoline_alloc_test.cpp") {
			continue;
		}
		std::ifstream in{ entry.path(), std::ios::binary };
		if (!in) {
			continue;
		}
		std::ostringstream buf;
		buf << in.rdbuf();
		const auto code = strip_comments(buf.str());
		const auto hits = count_occurrences(code, "AllocTrampoline");
		if (hits > 0) {
			total += hits;
			call_sites.push_back(
				fs::relative(entry.path(), root).string() + " x" + std::to_string(hits));
		}
	}

	std::string where;
	for (const auto& s : call_sites) {
		where += "\n    " + s;
	}

	expect(total == 1,
		"SKSE::AllocTrampoline must be called exactly once in the whole source tree, and it "
		"must stay in plugin.cpp ahead of every hook install. A second call REPLACES the buffer "
		"and releases the old one, dangling every stub already written into it -- which crashes "
		"on the next detoured call, not at install. Found " +
			std::to_string(total) + " call(s):" + where);

	const bool in_plugin_cpp =
		call_sites.size() == 1 && call_sites.front().rfind("plugin.cpp", 0) == 0;
	expect(in_plugin_cpp,
		"the single AllocTrampoline call must be the one in plugin.cpp; found it elsewhere:" +
			where);

	if (g_failures != 0) {
		std::cerr << g_failures << " failure(s)\n";
		return EXIT_FAILURE;
	}
	std::cout << "trampoline_alloc_test: exactly one AllocTrampoline, in plugin.cpp\n";
	return EXIT_SUCCESS;
}
