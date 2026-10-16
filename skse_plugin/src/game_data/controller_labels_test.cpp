#include "controller_labels.h"
#include "../input/gamepad_binding.h"
#include <cstdlib>
#include <iostream>
#include <unordered_map>

using namespace SpellHotbar::GameData;
using SpellHotbar::Input::gamepad_bit;
namespace {
    int failures = 0;
    void expect(bool condition, const char* message)
    {
        if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
    }
}

int main()
{
    using Style = ControllerLabelStyle;
    std::unordered_map<int, std::string> original{{35, "H"}}, ps, steam;
    for (int code = 266; code <= 281; ++code) {
        const auto& label = controller_labels[code - 266];
        const auto path = "Interface\\ImGuiIcons\\Icons\\" + std::string(label.icon) + ".png";
        expect(stock_controller_label(code, label.xbox, label.short_xbox, path) != nullptr, "stock row gets family labels");
        expect(stock_controller_label(code, "Custom", label.short_xbox, path) == nullptr, "custom long name preserved");
        expect(stock_controller_label(code, label.xbox, "Custom", path) == nullptr, "custom short name preserved");
        expect(stock_controller_label(code, label.xbox, label.short_xbox, "custom.png") == nullptr, "custom glyph preserved");
        original.emplace(code, label.xbox);
        ps.emplace(code, label.playstation);
        steam.emplace(code, label.steam);
    }
    expect(stock_controller_label(274, "LS", "LS", "interface/imguiicons/icons/360_lb.PNG") != nullptr, "legacy stock shoulder spelling recognized");
    expect(stock_controller_label(274, "L1", "L1", "Interface/ImGuiIcons/Icons/360_LB.png") == nullptr, "custom L1 is not converted back to Xbox");
    expect(stock_controller_label(35, "H", "H", "35.png") == nullptr, "keyboard is outside presentation mapping");
    for (const auto style : {Style::xbox, Style::playstation, Style::xbox}) {
        const auto name = [&](int code) { return select_controller_key_name(original, ps, steam, style, code); };
        const bool sony = style == Style::playstation;
        expect(format_controller_binding(277, gamepad_bit(274), name) == (sony ? "L1+Circle" : "LB+B"), "shoulder combination switches both parts and switches back");
        expect(format_controller_binding(276, gamepad_bit(280), name) == (sony ? "L2+Cross" : "LT+A"), "left trigger combination complete");
        expect(format_controller_binding(276, gamepad_bit(281), name) == (sony ? "R2+Cross" : "RT+A"), "right trigger combination complete");
        expect(format_controller_binding(276, gamepad_bit(280) | gamepad_bit(281), name) == (sony ? "L2+R2+Cross" : "LT+RT+A"), "two modifiers retained");
        expect(name(35) == "H", "keyboard preserved under either family");
        expect(name(272) == (sony ? "L3" : "LTh"), "stick click stays distinct from shoulder");
    }
    const auto steam_name = [&](int code) { return select_controller_key_name(original, ps, steam, Style::steam, code); };
    expect(format_controller_binding(277, gamepad_bit(274), steam_name) == "L1+B", "Steam shoulder plus face matches 2026 labels");
    expect(format_controller_binding(276, gamepad_bit(280) | gamepad_bit(281), steam_name) == "L2+R2+A", "Steam trigger combination complete");
    expect(steam_name(270) == "Menu" && steam_name(271) == "View", "Steam menu buttons use Valve names");
    original[274] = "Custom L1";
    ps.erase(274);
    steam.erase(274); // loader omits a family row when any original field is custom
    expect(select_controller_key_name(original, ps, steam, Style::playstation, 274) == "Custom L1", "custom row survives family selection");

    expect(select_controller_key_name(original, ps, steam, Style::steam, 274) == "Custom L1", "custom row survives Steam selection");

    for (const char* json : {"{}", "{\"settings.controller_labels\":null}", "{\"settings.controller_labels\":true}",
            "{\"settings.controller_labels\":\"1\"}", "{\"settings.controller_labels\":1.5}",
            "{\"settings.controller_labels\":-1}", "{\"settings.controller_labels\":3}",
            "{\"settings.controller_labels\":4294967295}"}) {
        rapidjson::Document doc;
        doc.Parse(json);
        expect(read_controller_labels_json(doc) == Style::xbox, "legacy or malformed preset safely defaults to Xbox");
    }
    for (int value = 0; value <= 2; ++value) {
        rapidjson::Document doc(rapidjson::kObjectType);
        doc.AddMember(rapidjson::StringRef(controller_label_json_key), value, doc.GetAllocator());
        expect(read_controller_labels_json(doc) == controller_label_style(value), "preset round trip");
        expect(read_controller_labels_record(1, 1, static_cast<std::uint8_t>(value)) == controller_label_style(value), "co-save byte round trip");
    }
    expect(read_controller_labels_record(2, 1, 1) == Style::xbox, "future record version rejected");
    expect(read_controller_labels_record(1, 0, 1) == Style::xbox, "truncated record rejected");
    expect(read_controller_labels_record(1, 2, 1) == Style::xbox, "extra bytes rejected");
    expect(read_controller_labels_record(1, 1, 255) == Style::xbox, "unknown style rejected");
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
