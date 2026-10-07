#include "flick/flick_watch.h"
#include "flick/flick_windows.h"
#include "flick/ui/config_tool.h"
#include "logger/logger.h"
#include "papyrus_extensions/papyrus_functions.h"
#include "rendering/render_manager.h"
#include "storage/storage.h"
#include "game_data/game_data.h"
#include "game_data/art_pack_gen.h"
#include "bar/hotbars.h"
#include "input/input.h"
#include "input/input_hook.h"
#include "events/eventlistener.h"
#include "events/animationeventhook.h"
#include "events/gameloop_hook.h"
#include "casts/ability_precision.h"
#include "casts/ability_magnetism.h"
#include "casts/cast_intent.h"
#include "casts/casting_controller.h"
#include "casts/clip_translation_driver.h"
#include "casts/msco_cast_driver.h"
#include "lifecycle/lifecycle.h"



constexpr uint32_t serializazion_id = 0xB8498471; //random generated 4byte


namespace SpellHotbar {
    // Reads [Diagnostics] <key> from diagnostics.ini beside the CSV data. Returns true only when
    // the file exists and the key is 1, so a normal install never sees this path. Logged once
    // per key so a diagnostic launch is visible in SpellHotbar2.log.
    bool diagnostic_off(const char* key)
    {
        static const std::string ini = R"(.\data\SKSE\Plugins\SpellHotbar\diagnostics.ini)";
        const int v = GetPrivateProfileIntA("Diagnostics", key, 0, ini.c_str());
        if (v == 1) {
            logger::warn("SH2 diagnostics: {} = 1 -- this launch is a discriminator, not a normal run", key);
        }
        return v == 1;
    }

    //The FLICK windows register on the first game start -- kPostLoadGame, or kNewGame when the
    //player starts a new game before loading any save -- not during the initial load.
    //Registering at kPostLoad crashed 3/3 and at kDataLoaded 4/5 (0/2 with the registration
    //switched off, 1/1 with it on), always as an access violation on an nvwgf2umx.dll worker
    //thread within the same millisecond -- the driver is busy on the initial load and something
    //registration makes FLICK do lands on it. By the first game start the pipeline is settled,
    //having drawn the main menu.
    //Every register_* call is idempotent, so every later start is a no-op.
    void register_flick_ui()
    {
        if (!diagnostic_off("DisableFlickDock")) {
            Flick::register_windows();
        }
        //The HUD bars: FLICK windows like the dock, one key each.
        if (!diagnostic_off("DisableFlickHud")) {
            Flick::register_hud_windows();
        }
        //The bind menu, the editors and the bar-position editor, one diagnostics key each so a
        //bisect stays one launch per hypothesis.
        Flick::WindowSet windows;
        windows.bind_menu = !diagnostic_off("DisableFlickBindMenu");
        windows.spell_editor = !diagnostic_off("DisableFlickSpellEditor");
        windows.potion_editor = !diagnostic_off("DisableFlickPotionEditor");
        windows.bar_drag = !diagnostic_off("DisableFlickBarDrag");
        Flick::register_ui_windows(windows);
        //The configuration tool in FLICK's sidebar, where the Mod Control Panel pages went.
        if (!diagnostic_off("DisableFlickConfigTool")) {
            FlickUi::ConfigTool::register_tool();
        }
    }
}
SKSEPluginLoad(const SKSE::LoadInterface * skse)
{
    SKSE::Init(skse);
    SpellHotbar::SetupLogger();
    logger::trace("SpellHotbar2 logger setup!");

    SpellHotbar::Bars::init();

    SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* message) {
        if (message->type == SKSE::MessagingInterface::kPostLoad) {
            //Every SKSE plugin DLL is loaded by now, so ShoutMCO's optional cast-intent export
            //can be resolved. Absent or incompatible is normal and costs nothing.
            SpellHotbar::casts::CastIntent::negotiate();
            //Write the pointer-art OAR pack HERE, not at kDataLoaded: OAR parses config.json
            //when it builds its replacer mods, which is after this and before our data load, so
            //generating any later means the arts only appear on the next launch.
            //Pure filesystem work, so it needs nothing the game has not set up yet.
            SpellHotbar::ArtPackGen::generate_and_cache();
            //FLICK is the UI host: the windows, the dock, the HUD bars and the config
            //tool are all its guests. Connect here, once every plugin DLL is loaded; register
            //nothing yet (see register_flick_ui).
            // A data-load crash discriminator. Each switch is a launch, not a rebuild:
            // .\data\SKSE\Plugins\SpellHotbar\diagnostics.ini, [Diagnostics], 0/1, absent = on.
            if (!SpellHotbar::diagnostic_off("DisableFlick")) {
                SpellHotbar::Flick::install();
            }
            //The input hook goes in HERE rather than in SKSEPluginLoad so that it is written
            //after FLICK's on the same call site, which makes SH2's thunk the outer one: it sees
            //every event raw, before the host zeroes the ones it blocks. Bind capture and the
            //action editor's key capture depend on that order (input/input_hook.h).
            SpellHotbar::Input::install_hook();
        } else if (message->type == SKSE::MessagingInterface::kDataLoaded) {
            SpellHotbar::RenderManager::load_fixed_textures();
            SpellHotbar::GameData::onDataLoad();
            SpellHotbar::casts::MscoCastDriver::load_charge_curve();
            SpellHotbar::casts::AbilityPrecision::negotiate();
            SpellHotbar::casts::AbilityMagnetism::negotiate();
            logger::info("SpellHotbar2 GameData loaded!");
        } else if (message->type == SKSE::MessagingInterface::kNewGame) {
            logger::info("SH2 lifecycle: kNewGame");
            SpellHotbar::casts::CastingController::drop_live_cast();
            SpellHotbar::Lifecycle::on_new_game();
            //A New Game straight from the main menu of a fresh launch never sees kPostLoadGame.
            SpellHotbar::register_flick_ui();
        } else if (message->type == SKSE::MessagingInterface::kPostLoadGame) {
            logger::info("SH2 lifecycle: kPostLoadGame");
            SpellHotbar::Lifecycle::on_post_load_game();
            SpellHotbar::register_flick_ui();
        } else if (message->type == SKSE::MessagingInterface::kPreLoadGame) {
            SpellHotbar::casts::CastingController::drop_live_cast();
        }
     });

    // MUST precede every hook install below. Allocating later, next to the Papyrus registration,
    // is harmless only while every hook in this build is a vtable write and nothing touches the
    // trampoline. A real detour installed before the trampoline is allocated finds no room for
    // its stub, and CommonLib stops the game with "Failed to handle allocation request".
    //
    // 16 bytes was enough while it was unused. A `write_branch<5>` needs room for the displaced
    // prologue plus the jump back; 64 leaves headroom for the next one.
    SKSE::AllocTrampoline(1 << 6);

    //Install animationeventhook: the cast's commitment point is the clip's own
    //`MLh/MRh_SpellFire_Event`, and this hook is how we hear it.
    SpellHotbar::events::install();
    SpellHotbar::casts::ClipTranslationDriver::install();
    SpellHotbar::events::GameLoopHook::hook();

    auto event_listener = SpellHotbar::events::EventListener::GetSingleton();
    SKSE::GetActionEventSource()->AddEventSink(event_listener);
    auto eventSourceHolder = RE::ScriptEventSourceHolder::GetSingleton();
    eventSourceHolder->AddEventSink<RE::TESSpellCastEvent>(event_listener);
    eventSourceHolder->AddEventSink<RE::TESHitEvent>(event_listener);
    eventSourceHolder->AddEventSink<RE::TESEquipEvent>(event_listener);
    //eventSourceHolder->AddEventSink<RE::TESPlayerBowShotEvent>(event_listener);

    RE::CriticalHit::GetEventSource()->AddEventSink(event_listener);

    //SKSE::GetActionEventSource()->AddEventSink(event_listener);

    SKSE::GetPapyrusInterface()->Register(SpellHotbar::register_papyrus_functions);

    logger::info("SpellHotbar2 Papyrus DLL functions registered!");

    auto serialization = SKSE::GetSerializationInterface();
    serialization->SetUniqueID(serializazion_id);
    serialization->SetSaveCallback(SpellHotbar::Storage::SaveCallback);
    serialization->SetLoadCallback(SpellHotbar::Storage::LoadCallback);
    serialization->SetRevertCallback(SpellHotbar::Storage::RevertCallback);
    logger::info("SpellHotbar2 serialization registered!");

    return true;
}
