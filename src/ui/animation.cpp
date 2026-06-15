#include "animation.h"
#include <ncurses.h>
#include <algorithm>

static void color_to_rgb(int c, float& r, float& g, float& b) {
    switch (c) {
        case 0:  r=0.0f; g=0.0f; b=0.0f; break;
        case 1:  r=1.0f; g=0.0f; b=0.0f; break;
        case 2:  r=0.0f; g=1.0f; b=0.0f; break;
        case 3:  r=1.0f; g=1.0f; b=0.0f; break;
        case 4:  r=0.0f; g=0.0f; b=1.0f; break;
        case 5:  r=1.0f; g=0.0f; b=1.0f; break;
        case 6:  r=0.0f; g=1.0f; b=1.0f; break;
        case 7:  r=1.0f; g=1.0f; b=1.0f; break;
        case 8:  r=0.5f; g=0.5f; b=0.5f; break;
        default: r=0.5f; g=0.5f; b=0.5f; break;
    }
}

void ColorAnimation::init(const ThemeColors& colors) {
    color_stops_.clear();
    auto add = [&](int c) {
        float r, g, b;
        color_to_rgb(c, r, g, b);
        color_stops_.push_back({r, g, b});
    };
    add(colors.user_fg);
    add(colors.assistant_fg);
    add(colors.system_fg);
    add(colors.tool_fg);
    add(colors.error_fg);
    if (color_stops_.size() < 2)
        color_stops_ = {{1,1,1}, {0,1,1}};
}

void ColorAnimation::advance() {
    if (!can_change_color() || color_stops_.size() < 2) return;

    t_ += SPEED;
    if (t_ >= 1.0f) t_ -= 1.0f;

    float total = (float)color_stops_.size() - 1.0f;
    float pos = t_ * total;
    int i = (int)pos;
    float frac = pos - (float)i;
    if (i >= (int)color_stops_.size() - 1) { i = (int)color_stops_.size() - 2; frac = 1.0f; }

    float smooth = frac * frac * (3.0f - 2.0f * frac);
    auto& a = color_stops_[i];
    auto& b = color_stops_[i + 1];
    rgb_r_ = a.r + (b.r - a.r) * smooth;
    rgb_g_ = a.g + (b.g - a.g) * smooth;
    rgb_b_ = a.b + (b.b - a.b) * smooth;

    init_color(16, rgb_to_ncurses(rgb_r_), rgb_to_ncurses(rgb_g_), rgb_to_ncurses(rgb_b_));
}
