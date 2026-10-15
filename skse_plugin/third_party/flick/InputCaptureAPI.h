#pragma once
#include <cstdint>

// Independent, versioned C ABI; the existing RequestFUCK/API 6 layout is unchanged.
// GetBlockedReasons reads a published snapshot and is safe from any thread.
// excludedPlugin omits only that guest's windows, never the host menu or rebinding.
enum FUCKInputCaptureReason : std::uint32_t {
    FUCK_INPUT_HOST_MENU = 1u,
    FUCK_INPUT_WINDOW = 2u,
    FUCK_INPUT_BINDING = 4u
};
struct FUCKInputCaptureV1 {
    std::uint32_t size;
    std::uint32_t version;
    std::uint32_t (*GetBlockedReasons)(const char* excludedPlugin) noexcept;
};
using RequestFUCKInputCaptureFn = const FUCKInputCaptureV1* (*)(std::uint32_t version) noexcept;
