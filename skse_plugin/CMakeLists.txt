cmake_minimum_required(VERSION 3.21)

project(SpellHotbar2 VERSION 0.9.5 LANGUAGES CXX)

include(CTest)

# Optional MO2 mod folder for local development. A normal configure never deploys.
set(OUTPUT_FOLDER "" CACHE PATH "Optional mod folder that receives the built SKSE plugin")

# The producer owns this ABI contract. Keep the vendored snapshot byte-equivalent after newline
# normalization; refreshing it requires an explicit producer commit and hash change here.
set(SHOUTMCO_CAST_INTENT_CANONICAL_COMMIT "e952394ba0392cccfcb6dc2ba0cb651e2b048024")
set(SHOUTMCO_CAST_INTENT_CANONICAL_SHA256 "30f6e3a4698f666a4695dd78ea4ab43b17b31709da3dc5deb27981f7795cc9cc")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/extern/ShoutMCO_CastIntent.h" SHOUTMCO_CAST_INTENT_HEADER)
string(REPLACE "\r\n" "\n" SHOUTMCO_CAST_INTENT_HEADER "${SHOUTMCO_CAST_INTENT_HEADER}")
string(REPLACE "\r" "\n" SHOUTMCO_CAST_INTENT_HEADER "${SHOUTMCO_CAST_INTENT_HEADER}")
string(SHA256 SHOUTMCO_CAST_INTENT_SHA256 "${SHOUTMCO_CAST_INTENT_HEADER}")
if(NOT SHOUTMCO_CAST_INTENT_SHA256 STREQUAL SHOUTMCO_CAST_INTENT_CANONICAL_SHA256)
    message(FATAL_ERROR
        "src/extern/ShoutMCO_CastIntent.h differs from the canonical ShoutMCO contract at "
        "${SHOUTMCO_CAST_INTENT_CANONICAL_COMMIT}. Refresh it from the producer and update the pinned hash.")
endif()


# FLICK's guest header, vendored from github.com/Fuzzlesz/FUCK. It is still moving upstream --
# API 4 landed days before this pin -- so track a commit, not HEAD, and let FUCK::Connect()'s own
# version check fail closed against an older installed host.
set(FLICK_API_CANONICAL_COMMIT "a123c11629da3b86ffd1c8e15081d5d641001ed3")
set(FLICK_API_CANONICAL_SHA256 "9dc5f80490c9067d2cff98c9104b5e2df15b3af6df421546556ed726166ccefa")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/third_party/flick/FUCK_API.h" FLICK_API_HEADER)
# Normalize line endings before hashing. Written as CMake escapes rather than literal
# newlines, so no editor or checkout can rewrite them.
string(REPLACE "\r\n" "\n" FLICK_API_HEADER "${FLICK_API_HEADER}")
string(REPLACE "\r" "\n" FLICK_API_HEADER "${FLICK_API_HEADER}")
string(SHA256 FLICK_API_SHA256 "${FLICK_API_HEADER}")
if(NOT FLICK_API_SHA256 STREQUAL FLICK_API_CANONICAL_SHA256)
    message(FATAL_ERROR
        "third_party/flick/FUCK_API.h differs from the pinned FLICK header at "
        "${FLICK_API_CANONICAL_COMMIT}. Refresh it from upstream and update the pinned hash.")
endif()

# Precision's modder API types and V1 layout come from github.com/ersh1/Precision @ e094209.
# Verify the upstream bytes separately from our two-line Win32 lookup adaptation. The exact
# replacement below is the reviewable delta. CommonLibSSE-NG has no SKSE::WinAPI helper, and this
# build is UNICODE, so the adapted helper uses GetModuleHandleW/GetProcAddress directly.
set(PRECISION_API_UPSTREAM_COMMIT "e094209bff698a0739b3a881dfdfa3b1697267d8")
set(PRECISION_API_UPSTREAM_RAW_SHA256 "263dc1721fc151f68fe83936d01a5af3879266b5d50cf9121211f0bb9072153d")
set(PRECISION_API_ADAPTED_SHA256 "e51294cca62d1f03430315b15f6bfbba6ab4b1b244dd856271cd0c6cdc0f1c36")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/third_party/precision/PrecisionAPI.h" PRECISION_API_HEADER)
string(REPLACE "\r\n" "\n" PRECISION_API_HEADER "${PRECISION_API_HEADER}")
string(REPLACE "\r" "\n" PRECISION_API_HEADER "${PRECISION_API_HEADER}")
string(SHA256 PRECISION_API_SHA256 "${PRECISION_API_HEADER}")
set(PRECISION_API_ADAPTED_LOOKUP [=[		const auto pluginHandle = GetModuleHandleW(L"Precision.dll");
		const auto requestAPIFunction = reinterpret_cast<_RequestPluginAPI>(GetProcAddress(pluginHandle, "RequestPluginAPI"));]=])
set(PRECISION_API_UPSTREAM_LOOKUP [=[		auto pluginHandle = GetModuleHandle("Precision.dll");
		_RequestPluginAPI requestAPIFunction = (_RequestPluginAPI)SKSE::WinAPI::GetProcAddress(pluginHandle, "RequestPluginAPI");]=])
set(PRECISION_API_RECONSTRUCTED_UPSTREAM "${PRECISION_API_HEADER}")
string(REPLACE "${PRECISION_API_ADAPTED_LOOKUP}" "${PRECISION_API_UPSTREAM_LOOKUP}"
    PRECISION_API_RECONSTRUCTED_UPSTREAM "${PRECISION_API_RECONSTRUCTED_UPSTREAM}")
string(SHA256 PRECISION_API_RECONSTRUCTED_SHA256 "${PRECISION_API_RECONSTRUCTED_UPSTREAM}")
if(NOT PRECISION_API_SHA256 STREQUAL PRECISION_API_ADAPTED_SHA256 OR
   NOT PRECISION_API_RECONSTRUCTED_SHA256 STREQUAL PRECISION_API_UPSTREAM_RAW_SHA256)
    message(FATAL_ERROR
        "third_party/precision API or its documented Win32 adaptation changed. Refresh from "
        "upstream ${PRECISION_API_UPSTREAM_COMMIT}, reapply the documented lookup replacement, "
        "and update both hashes.")
endif()

# True Directional Movement's modder API comes from github.com/ersh1/TrueDirectionalMovement
# @ bfb0cde, the last commit to touch it, with the same two-line UNICODE lookup adaptation as
# Precision's above.
set(TDM_API_UPSTREAM_COMMIT "bfb0cdecd03485ebc35a2aa76b56ef0e337d43d5")
set(TDM_API_UPSTREAM_RAW_SHA256 "9bb0b29613e437bb1364d362d1cd8d1bf10afe82bac92dcdde299ac4bbba2f55")
set(TDM_API_ADAPTED_SHA256 "8e11718443102a565180ea7380139d5c1a6f06645b004a9819c8ea605ce0879b")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/third_party/tdm/TrueDirectionalMovementAPI.h" TDM_API_HEADER)
string(REPLACE "\r\n" "\n" TDM_API_HEADER "${TDM_API_HEADER}")
string(REPLACE "\r" "\n" TDM_API_HEADER "${TDM_API_HEADER}")
string(SHA256 TDM_API_SHA256 "${TDM_API_HEADER}")
set(TDM_API_ADAPTED_LOOKUP [=[		const auto pluginHandle = GetModuleHandleW(L"TrueDirectionalMovement.dll");
		const auto requestAPIFunction = reinterpret_cast<_RequestPluginAPI>(GetProcAddress(pluginHandle, "RequestPluginAPI"));]=])
set(TDM_API_UPSTREAM_LOOKUP [=[		auto pluginHandle = GetModuleHandle("TrueDirectionalMovement.dll");
		_RequestPluginAPI requestAPIFunction = (_RequestPluginAPI)GetProcAddress(pluginHandle, "RequestPluginAPI");]=])
set(TDM_API_RECONSTRUCTED_UPSTREAM "${TDM_API_HEADER}")
string(REPLACE "${TDM_API_ADAPTED_LOOKUP}" "${TDM_API_UPSTREAM_LOOKUP}"
    TDM_API_RECONSTRUCTED_UPSTREAM "${TDM_API_RECONSTRUCTED_UPSTREAM}")
string(SHA256 TDM_API_RECONSTRUCTED_SHA256 "${TDM_API_RECONSTRUCTED_UPSTREAM}")
if(NOT TDM_API_SHA256 STREQUAL TDM_API_ADAPTED_SHA256 OR
   NOT TDM_API_RECONSTRUCTED_SHA256 STREQUAL TDM_API_UPSTREAM_RAW_SHA256)
    message(FATAL_ERROR
        "third_party/tdm API or its documented Win32 adaptation changed. Refresh from "
        "upstream ${TDM_API_UPSTREAM_COMMIT}, reapply the documented lookup replacement, "
        "and update both hashes.")
endif()


find_package(CommonLibSSE CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(RapidJSON CONFIG REQUIRED)

set(source_files src/plugin.cpp
                 src/logger/logger.h
                 src/logger/logger.cpp
                 src/rendering/render_manager.h
                 src/rendering/render_manager.cpp
                 src/rendering/texture_registry.h
                 src/mcp/bind_capture.h
                 src/mcp/bind_capture.cpp
                 src/mcp/mcp_preset_name.h
                 src/papyrus_extensions/papyrus_functions.h
                 src/papyrus_extensions/papyrus_functions.cpp
                 src/storage/storage.h
                 src/storage/storage.cpp
                 src/storage/runtime_state_reset.h
                 src/game_data/game_data.h
                 src/game_data/game_data.cpp
                 src/bar/hotbar.h
                 src/bar/hotbar.cpp
                 src/bar/hotbar_serialization.h
                 src/bar/hotbars.h
                 src/bar/hotbars.cpp
                 src/bar/oblivion_bar.h
                 src/bar/oblivion_bar.cpp
                 "src/input/input.h"
                 "src/input/input.cpp"
                 "src/input/input_hook.h"
                 "src/input/input_hook.cpp"
                 "src/input/action_inject.h"
                 "src/input/input_event_adapter.h"
                 "src/input/keybinds.h"
                 "src/input/keybinds.cpp"
                 "src/input/modes.h"
                 "src/input/modes.cpp"
                 src/rendering/texture_csv_loader.h
                 src/rendering/texture_csv_loader.cpp
                 src/events/eventlistener.h
                 src/events/eventlistener.cpp
                 src/events/animationeventhook.h
                 src/events/animationeventhook.cpp
                 src/events/attack_seam_hook.h
                 src/events/attack_seam_hook.cpp
                 src/eventS/gameloop_hook.h
                 src/eventS/gameloop_hook.cpp
                 src/lifecycle/first_run_policy.h
                 src/lifecycle/lifecycle.h
                 src/lifecycle/lifecycle.cpp
                 src/game_data/spell_data_csv_loader.h
                 src/game_data/spell_data_csv_loader.cpp
                 src/game_data/spell_casteffect_csv_loader.h
                 src/game_data/spell_casteffect_csv_loader.cpp
                 src/game_data/csv_loader.h
                 src/game_data/custom_transform_csv_loader.h
                 src/game_data/custom_transform_csv_loader.cpp
                 src/game_data/animation_data_csv_loader.h
                 src/game_data/animation_data_csv_loader.cpp
                 src/game_data/art_definition.h
                 src/game_data/art_definition.cpp
                 src/game_data/action_definition.h
                 src/game_data/action_overlay_json.h
                 src/game_data/action_definition.cpp
                 src/game_data/art_icon_resolve.h
                 src/game_data/art_icon_resolve.cpp
                 src/game_data/custom_ability_config.h
                 src/game_data/custom_ability_config.cpp
                 src/game_data/art_data_csv_loader.h
                 src/game_data/art_data_csv_loader.cpp
                 src/casts/casting_controller.h
                 src/casts/casting_controller.cpp
                 src/casts/voice_cast_driver.h
                 src/casts/voice_cast_driver.cpp
                 src/casts/msco_cast_driver.h
                 src/casts/msco_cast_driver.cpp
                 src/casts/art_driver.h
                 src/casts/art_driver.cpp
                 src/casts/ability_damage.h
                 src/casts/ability_precision.h
                 src/casts/ability_precision.cpp
                 src/casts/ability_magnetism.h
                 src/casts/ability_magnetism.cpp
                 src/casts/hyper_armor.h
                 src/casts/hyper_armor_sync.h
                 src/casts/hyper_armor_sync.cpp
                 src/casts/clip_translation.h
                 src/casts/clip_translation_driver.h
                 src/casts/clip_translation_driver.cpp
                 src/casts/combo_cache.h
                 src/casts/attack_seam.h
                 src/casts/attack_action_events.h
                 src/casts/seam_dispatch.h
                 src/casts/seam_dispatch.cpp
                 src/casts/cast_intent.h
                 src/casts/cast_intent.cpp
                 src/extern/ShoutMCO_CastIntent.h
                 src/game_data/keynames_csv_loader.h
                 src/game_data/keynames_csv_loader.cpp
                 "src/input/keycode_helper.h"
                 src/game_data/user_custom_spelldata.h
                 src/game_data/user_custom_spelldata.cpp
                 src/storage/user_data_io.h
                 src/storage/user_data_io.cpp
                 src/game_data/cast_anim_ids.h
                 src/game_data/spell_cast_data.h
                 src/game_data/spell_cast_data.cpp
                 src/game_data/user_custom_entry.h
                 src/game_data/user_custom_entry.cpp
                 src/game_data/art_pack_gen.h
                 src/game_data/art_pack_gen.cpp
                 src/game_data/custom_ability_runtime.h
                 src/game_data/custom_ability_runtime.cpp
                 src/casts/spell_proc.h
                 src/casts/spell_proc.cpp
                 src/rendering/bind_drop.h
                 src/rendering/ui_bridge.h
                 src/game_data/localization.h
                 src/game_data/localization.cpp
)

if(BUILD_TESTING)
add_executable(combo_cache_test src/casts/combo_cache_test.cpp)
target_compile_features(combo_cache_test PRIVATE cxx_std_23)
target_include_directories(combo_cache_test PRIVATE src/casts)

add_executable(attack_seam_test src/casts/attack_seam_test.cpp)
target_compile_features(attack_seam_test PRIVATE cxx_std_23)
target_include_directories(attack_seam_test PRIVATE src/casts)

add_executable(trampoline_alloc_test src/events/trampoline_alloc_test.cpp)
target_compile_features(trampoline_alloc_test PRIVATE cxx_std_23)
target_compile_definitions(trampoline_alloc_test PRIVATE
    SH2_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src")

add_executable(clip_translation_test src/casts/clip_translation_test.cpp)
target_compile_features(clip_translation_test PRIVATE cxx_std_23)
target_include_directories(clip_translation_test PRIVATE src/casts)

add_executable(art_pack_gen_test src/game_data/art_pack_gen_test.cpp src/game_data/art_definition.cpp src/game_data/art_icon_resolve.cpp src/game_data/custom_ability_config.cpp)
target_compile_features(art_pack_gen_test PRIVATE cxx_std_23)
target_include_directories(art_pack_gen_test PRIVATE src/game_data)

add_executable(art_data_test src/game_data/art_data_test.cpp src/game_data/art_definition.cpp src/game_data/art_icon_resolve.cpp src/game_data/custom_ability_config.cpp)
target_compile_features(art_data_test PRIVATE cxx_std_23)
target_include_directories(art_data_test PRIVATE src/game_data)

add_executable(action_data_test src/game_data/action_data_test.cpp src/game_data/action_definition.cpp)
target_compile_features(action_data_test PRIVATE cxx_std_23)
target_include_directories(action_data_test PRIVATE src/game_data)

add_executable(action_overlay_json_test src/game_data/action_overlay_json_test.cpp)
target_compile_features(action_overlay_json_test PRIVATE cxx_std_23)
target_include_directories(action_overlay_json_test PRIVATE src/game_data)
target_link_libraries(action_overlay_json_test PRIVATE rapidjson)

add_executable(art_overlay_json_test src/game_data/art_overlay_json_test.cpp src/game_data/art_definition.cpp src/game_data/custom_ability_config.cpp)
target_compile_features(art_overlay_json_test PRIVATE cxx_std_23)
target_include_directories(art_overlay_json_test PRIVATE src/game_data)
target_link_libraries(art_overlay_json_test PRIVATE rapidjson)

add_executable(bind_capture_test src/mcp/bind_capture_test.cpp)
target_compile_features(bind_capture_test PRIVATE cxx_std_23)
target_include_directories(bind_capture_test PRIVATE src/mcp src/game_data)

add_executable(cast_anim_ids_test src/game_data/cast_anim_ids_test.cpp)
target_compile_features(cast_anim_ids_test PRIVATE cxx_std_23)
target_include_directories(cast_anim_ids_test PRIVATE src/game_data)

add_executable(art_bind_record_test src/storage/art_bind_record_test.cpp)
target_compile_features(art_bind_record_test PRIVATE cxx_std_23)
target_include_directories(art_bind_record_test PRIVATE src/storage)


add_executable(art_deadline_test src/casts/art_deadline_test.cpp)
target_compile_features(art_deadline_test PRIVATE cxx_std_23)
target_include_directories(art_deadline_test PRIVATE src/casts)

add_executable(ability_start_test src/casts/ability_start_test.cpp)
target_compile_features(ability_start_test PRIVATE cxx_std_23)
target_include_directories(ability_start_test PRIVATE src/casts)

add_executable(ability_damage_test src/casts/ability_damage_test.cpp)
target_compile_features(ability_damage_test PRIVATE cxx_std_23)
target_include_directories(ability_damage_test PRIVATE src/casts)

add_executable(hyper_armor_test src/casts/hyper_armor_test.cpp)
target_compile_features(hyper_armor_test PRIVATE cxx_std_23)
target_include_directories(hyper_armor_test PRIVATE src/casts)

add_executable(bow_string_relay_test src/casts/bow_string_relay_test.cpp)
target_compile_features(bow_string_relay_test PRIVATE cxx_std_23)
target_include_directories(bow_string_relay_test PRIVATE src/casts)

add_executable(arrow_hit_arm_test src/casts/arrow_hit_arm_test.cpp)
target_compile_features(arrow_hit_arm_test PRIVATE cxx_std_23)
target_include_directories(arrow_hit_arm_test PRIVATE src/casts)

add_executable(bind_drop_test src/rendering/bind_drop_test.cpp)
target_compile_features(bind_drop_test PRIVATE cxx_std_23)
target_include_directories(bind_drop_test PRIVATE src/rendering)

add_executable(hotbar_serialization_test src/bar/hotbar_serialization_test.cpp)
target_compile_features(hotbar_serialization_test PRIVATE cxx_std_23)
target_include_directories(hotbar_serialization_test PRIVATE src/bar)

add_executable(texture_registry_test src/rendering/texture_registry_test.cpp)
target_compile_features(texture_registry_test PRIVATE cxx_std_23)
target_include_directories(texture_registry_test PRIVATE src/rendering)

add_executable(action_overlay_persistence_test src/storage/action_overlay_persistence_test.cpp)
target_compile_features(action_overlay_persistence_test PRIVATE cxx_std_23)
target_include_directories(action_overlay_persistence_test PRIVATE src/storage)

add_test(NAME combo_cache_test COMMAND combo_cache_test)
add_test(NAME clip_translation_test COMMAND clip_translation_test)
add_test(NAME attack_seam_test COMMAND attack_seam_test)
add_test(NAME trampoline_alloc_test COMMAND trampoline_alloc_test)
add_test(NAME art_pack_gen_test COMMAND art_pack_gen_test)
add_test(NAME art_data_test COMMAND art_data_test)
add_test(NAME cast_anim_ids_test COMMAND cast_anim_ids_test)
add_test(NAME art_bind_record_test COMMAND art_bind_record_test)
add_test(NAME bind_drop_test COMMAND bind_drop_test)
add_test(NAME art_deadline_test COMMAND art_deadline_test)
add_test(NAME ability_start_test COMMAND ability_start_test)
add_test(NAME ability_damage_test COMMAND ability_damage_test)
add_test(NAME hyper_armor_test COMMAND hyper_armor_test)
add_test(NAME bow_string_relay_test COMMAND bow_string_relay_test)
add_test(NAME arrow_hit_arm_test COMMAND arrow_hit_arm_test)
add_test(NAME hotbar_serialization_test COMMAND hotbar_serialization_test)
add_test(NAME texture_registry_test COMMAND texture_registry_test)
add_test(NAME action_data_test COMMAND action_data_test)
add_test(NAME action_overlay_persistence_test COMMAND action_overlay_persistence_test)
add_test(NAME art_overlay_json_test COMMAND art_overlay_json_test)
add_test(NAME bind_capture_test COMMAND bind_capture_test)
add_test(NAME action_overlay_json_test COMMAND action_overlay_json_test)
endif()

add_commonlibsse_plugin(${PROJECT_NAME} SOURCES ${source_files})
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

# Symbols in Release. `/Zi` emits the PDB, `/DEBUG` writes it out, and `/OPT:REF /OPT:ICF` put
# back the two optimizations `/DEBUG` silently turns off -- without them a "release" DLL keeps
# unreferenced code and duplicate COMDATs, which is a real size and layout change, not a free one.
# Optimization itself is untouched: this is an optimized build that can be symbolicated, which is
# what CrashLoggerSSE needs to print a call stack instead of a blank heading.
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE "$<$<CONFIG:Release>:/Zi>")
    target_link_options(${PROJECT_NAME} PRIVATE "$<$<CONFIG:Release>:/DEBUG>" "$<$<CONFIG:Release>:/OPT:REF>" "$<$<CONFIG:Release>:/OPT:ICF>")
endif()
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

target_include_directories(${PROJECT_NAME} PRIVATE "src")
# imgui for its headers only: ImVec2, ImU32 and IM_COL32 in the texture registry and the display
# lists. FUCK.dll does every draw call. The plugin still links the static copy for those symbols;
# its context is never created, which is why no ImGui:: call may appear anywhere in src (below).
target_link_libraries(${PROJECT_NAME} PRIVATE imgui::imgui)

# The FLICK side is compiled apart from the rest of the plugin. The split began as a necessity
# (SMF's ImGuiMCP::ImVec2 and imgui.h's ImVec2 could not share a translation unit) and stays as a
# boundary: these TUs are the only ones that include FUCK_API.h and the only ones that draw, and
# they reach the plugin's state through headers that carry no drawing at all
# (rendering/ui_bridge.h, flick/flick_watch.h, flick/flick_windows.h).
add_library(sh2_flick OBJECT
    src/flick/flick_pch.h
    src/flick/flick_watch.h
    src/flick/flick_watch.cpp
    src/flick/flick_images.h
    src/flick/flick_images.cpp
    src/flick/flick_windows.h
    src/flick/flick_windows.cpp
    src/flick/flick_hud.cpp
    src/flick/ui/widgets.h
    src/flick/ui/widgets.cpp
    src/flick/ui/bind_menu.h
    src/flick/ui/bind_menu.cpp
    src/flick/ui/spell_editor.h
    src/flick/ui/spell_edit_dialog.h
    src/flick/ui/spell_editor.cpp
    src/flick/ui/spell_edit_dialog.cpp
    src/flick/ui/potion_editor.h
    src/flick/ui/icon_edit_dialog.h
    src/flick/ui/potion_editor.cpp
    src/flick/ui/ability_editor.h
    src/flick/ui/ability_editor.cpp
    src/flick/ui/action_editor.h
    src/flick/ui/action_editor.cpp
    src/flick/ui/art_icon_edit_dialog.h
    src/flick/ui/art_icon_edit_dialog.cpp
    src/flick/ui/bar_drag.h
    src/flick/ui/bar_drag.cpp
    src/flick/ui/config_tool.h
    src/flick/ui/config_tool.cpp
)
target_precompile_headers(sh2_flick PRIVATE src/flick/flick_pch.h)
target_compile_features(sh2_flick PRIVATE cxx_std_23)
target_compile_definitions(sh2_flick PRIVATE IMGUI_DEFINE_MATH_OPERATORS)
target_link_libraries(sh2_flick PRIVATE CommonLibSSE::CommonLibSSE imgui::imgui)
target_include_directories(sh2_flick PRIVATE "src")
if(MSVC)
    target_compile_options(sh2_flick PRIVATE "$<$<CONFIG:Release>:/Zi>")
endif()
target_link_libraries(${PROJECT_NAME} PRIVATE sh2_flick)

# Mechanical form of the rule flick_pch.h states: nothing in the plugin may call ImGui::
# directly. Inside a FUCK::IWindow::Draw() SH2's own GImGui is uninitialised, so a stray call
# compiles, links, and crashes the game -- and there is no other ImGui context in the plugin's
# life at all. The real imgui.h is on every include path for its types, so the compiler cannot
# catch this; this does.
file(GLOB_RECURSE SH2_FLICK_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")
foreach(flick_source IN LISTS SH2_FLICK_SOURCES)
    # A line with any `/` before ImGui:: passes, so a `//` comment about the rule is not a breach
    # of it; a block-comment continuation line (` * ImGui::...`) has no `/` and is rejected.
    file(STRINGS "${flick_source}" flick_imgui_calls REGEX "^[^/]*ImGui::")
    if(flick_imgui_calls)
        message(FATAL_ERROR "${flick_source} calls ImGui:: directly; FLICK translation units draw through FUCK:: only:
${flick_imgui_calls}")
    endif()
endforeach()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# Automatically copy to the explicitly requested output folder.
if(OUTPUT_FOLDER)
    set(DLL_FOLDER "${OUTPUT_FOLDER}/SKSE/Plugins")

    message(STATUS "SKSE plugin output folder: ${DLL_FOLDER}")

    add_custom_command(
        TARGET "${PROJECT_NAME}"
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${DLL_FOLDER}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
        VERBATIM
    )

    # The PDB ships in EVERY configuration, Release included.
    #
    # Without symbols a load crash has to be diagnosed by hand out of raw registers and a stack of
    # bare `SpellHotbar2.dll+0xNNNNN` offsets, because CrashLoggerSSE prints "PROBABLE CALL STACK:"
    # and then nothing at all. With this file beside the DLL it names our frames directly. It
    # costs deployment size and leaks nothing a disassembler would not: the released archive is
    # built from `DLL_FOLDER` by hand, so drop it there if it is unwanted.
    add_custom_command(
        TARGET "${PROJECT_NAME}"
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_PDB_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_PDB_FILE_NAME:${PROJECT_NAME}>"
        VERBATIM
    )
endif()
