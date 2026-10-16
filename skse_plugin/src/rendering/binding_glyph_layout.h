#pragma once
#include <algorithm>
#include <vector>

namespace SpellHotbar::Rendering {
    struct BindingGlyphPlacement {
        int index; // -1 is the visible '+' joining the next button.
        float x, y, width, height;
    };

    inline std::vector<BindingGlyphPlacement> layout_binding_glyphs(
        const std::vector<float>& aspects, float width, float height, float preferred_height,
        float triangle_peak_rise = 0.0f)
    {
        if (aspects.empty() || width <= 0 || height <= 0 || preferred_height <= 0) { return {}; }
        if (aspects.size() == 3 && aspects[0] > 0 && aspects[1] > 0 && aspects[2] > 0) {
            const float base_width = aspects[0] + aspects[1] + 0.30f;
            const float extent = std::max(base_width, aspects[2] + 0.60f);
            const bool raised = triangle_peak_rise > 0.0f;
            const float h = std::min({preferred_height, width / extent, raised ? height : height / 2.08f});
            const float left = (extent - base_width) * h * 0.5f;
            const float top = (extent - aspects[2]) * h * 0.5f;
            // Icon captions keep their base at the label origin and place the action center at the requested peak.
            const float base_y = raised ? 0.0f : h * 1.08f;
            const float action_y = raised ? -triangle_peak_rise - h * 0.5f : base_y - h * 1.08f;
            return {{0, left, base_y, aspects[0] * h, h},
                    {-1, left + aspects[0] * h, base_y, h * 0.30f, h},
                    {1, left + (aspects[0] + 0.30f) * h, base_y, aspects[1] * h, h},
                    {-1, top - h * 0.30f, action_y, h * 0.30f, h},
                    {2, top, action_y, aspects[2] * h, h}};
        }
        float upper = std::min(preferred_height, height);
        for (float aspect : aspects) {
            if (aspect <= 0) { return {}; }
            upper = std::min(upper, width / (aspect + 0.30f));
        }
        const auto arrange = [&](float h) {
            std::vector<BindingGlyphPlacement> result;
            float x = 0, y = 0;
            for (int i = 0; i < static_cast<int>(aspects.size()); ++i) {
                const float separator = i ? h * 0.30f : 0;
                const float w = aspects[i] * h;
                if (x > 0 && x + separator + w > width) { x = 0; y += h * 1.08f; }
                if (i) { result.push_back({-1, x, y, separator, h}); x += separator; }
                result.push_back({i, x, y, w, h});
                x += w;
            }
            return result;
        };
        float lower = 0;
        for (int iteration = 0; iteration < 24; ++iteration) {
            const float mid = (lower + upper) * 0.5f;
            const auto placed = arrange(mid);
            if (placed.back().y + mid <= height) { lower = mid; }
            else { upper = mid; }
        }
        return arrange(lower);
    }
}
