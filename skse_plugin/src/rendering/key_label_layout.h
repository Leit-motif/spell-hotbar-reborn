#pragma once
#include <algorithm>
#include <string>
#include <string_view>

namespace SpellHotbar::Rendering {
    struct KeyLabelLayout {
        std::string text;
        float scale{1.0f};
    };

    template <class Measure>
    KeyLabelLayout fit_key_label(std::string_view text, float width, float height, Measure measure)
    {
        KeyLabelLayout result{std::string(text), 1.0f};
        if (text.empty() || width <= 0 || height <= 0) { return result; }
        if (measure(text).x > width && text.find('+') != std::string_view::npos) {
            result.text.clear();
            std::string line;
            for (std::size_t start = 0; start < text.size();) {
                const auto plus = text.find('+', start);
                const auto end = plus == std::string_view::npos ? text.size() : plus + 1;
                const std::string token(text.substr(start, end - start));
                if (!line.empty() && measure(line + token).x > width) {
                    result.text += line + '\n';
                    line.clear();
                }
                line += token;
                start = end;
            }
            result.text += line;
        }
        const auto size = measure(result.text);
        if (size.x > width) { result.scale = std::min(result.scale, width / size.x); }
        if (size.y > height) { result.scale = std::min(result.scale, height / size.y); }
        return result;
    }
}
