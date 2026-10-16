#include "binding_glyph_layout.h"
#include "../game_data/controller_labels.h"
#include <cstdlib>
#include <iostream>
#include <cmath>

int main()
{
    int failures = 0;
    const auto expect = [&](bool value, const char* message) {
        if (!value) { std::cerr << message << '\n'; ++failures; }
    };
    using SpellHotbar::GameData::controller_binding_keys;
    expect(controller_binding_keys(-1, 0).empty(), "unbound slots have no glyph prompt");
    const auto triangle = SpellHotbar::Rendering::layout_binding_glyphs({1, 1, 1}, 38, 34, 20);
    expect(triangle[4].index == 2 && triangle[4].y < triangle[0].y && triangle[4].y < triangle[2].y,
        "third glyph is above both modifier glyphs");
    expect(std::abs((triangle[4].x + triangle[4].width * .5f) -
        ((triangle[0].x + triangle[0].width * .5f) + (triangle[2].x + triangle[2].width * .5f)) * .5f) < .001f,
        "third glyph is centered over the base, never an L");
    expect(controller_binding_keys(276, 0xC000) == std::vector<int>{280, 281, 276},
        "both trigger glyphs precede the action glyph");
    expect(controller_binding_keys(277, 0x0100) == std::vector<int>{274, 277},
        "shoulder glyph precedes the east-face glyph");
    for (float icon_size : {16.0f, 40.0f, 80.0f}) {
        const auto pair = SpellHotbar::Rendering::layout_binding_glyphs(
            {1, 1}, icon_size * .94f, icon_size * .85f, icon_size * .5f, icon_size * .75f);
        const auto caption = SpellHotbar::Rendering::layout_binding_glyphs(
            {1, 1, 1}, icon_size * .94f, icon_size * .85f, icon_size * .5f, icon_size * .75f);
        expect(std::abs(caption[0].y + caption[0].height - pair[0].y - pair[0].height) < .001f &&
            std::abs(caption[2].y + caption[2].height - pair[2].y - pair[2].height) < .001f,
            "triangle bottom aligns with neighboring two-glyph labels");
        const float action_center = icon_size * .80f + caption[4].y + caption[4].height * .5f;
        expect(std::abs(action_center - icon_size * .05f) < .001f,
            "third glyph center sits at the marked top edge of its icon");
    }
    for (int count : {1, 2, 3, 17}) {
        for (float width : {18.0f, 38.0f, 72.0f}) {
            const auto placed = SpellHotbar::Rendering::layout_binding_glyphs(
                std::vector<float>(count, 1.0f), width, 34.0f, 20.0f);
            expect(placed.size() == static_cast<std::size_t>(count * 2 - 1), "no glyph or separator omitted");
            int next = 0;
            for (const auto& item : placed) {
                if (item.index >= 0) { expect(item.index == next++, "glyph order preserved across wraps"); }
                expect(item.width > 0 && item.height > 0, "glyph has a visible extent");
                expect(item.x >= 0 && item.x + item.width <= width + .001f &&
                    item.y >= 0 && item.y + item.height <= 34.001f, "glyph stays within its slot");
            }
        }
    }
    expect(controller_binding_keys(276, 0xFFFF).size() == 17, "all modifier bits retained");
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
