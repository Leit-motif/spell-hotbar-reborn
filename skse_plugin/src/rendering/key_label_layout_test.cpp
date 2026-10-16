#include "key_label_layout.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace {
    struct Size { float x, y; };
    Size measure(std::string_view text)
    {
        float width = 0, line = 0, height = 10;
        for (const auto c : text) {
            if (c == '\n') { width = std::max(width, line); line = 0; height += 10; }
            else { line += c == '+' ? 4.0f : 6.0f; }
        }
        return {std::max(width, line), height};
    }
    int failures = 0;
    void expect(bool condition, const char* message)
    {
        if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
    }
}
int main()
{
    for (const std::string text : {"LB+B", "L2+Cross", "L2+R2+Cross", "L1+Circle",
            "A very long custom name", "L1+R1+L2+R2+Triangle"}) {
        const auto label = SpellHotbar::Rendering::fit_key_label(text, 36, 26, measure);
        const auto size = measure(label.text);
        expect(size.x * label.scale <= 36.01f, "label stays inside its slot width");
        expect(size.y * label.scale <= 26.01f, "label stays inside its slot height");
        auto flat = label.text;
        std::erase(flat, '\n');
        expect(flat == text, "complete label and all plus signs survive wrapping");
        expect(label.scale > 0 && label.scale <= 1, "font only scales down");
    }
    const auto short_label = SpellHotbar::Rendering::fit_key_label("LB+B", 36, 26, measure);
    expect(short_label.text == "LB+B" && short_label.scale == 1, "labels that fit retain their layout");
    const auto combo = SpellHotbar::Rendering::fit_key_label("L2+R2+Cross", 36, 26, measure);
    expect(combo.text.find('\n') != std::string::npos && combo.scale == 1, "wrap a long combination before reducing readable font size");
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
