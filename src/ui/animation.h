#pragma once
#include <vector>
#include "config.h"

class ColorAnimation {
public:
    void init(const ThemeColors& colors);
    void advance();
    int get_color_index() const { return 9; }
    float get_r() const { return rgb_r_; }
    float get_g() const { return rgb_g_; }
    float get_b() const { return rgb_b_; }

private:
    struct RGB { float r, g, b; };
    std::vector<RGB> color_stops_;
    float rgb_r_ = 1.0f, rgb_g_ = 1.0f, rgb_b_ = 1.0f;
    float t_ = 0.0f;
    static constexpr float SPEED = 0.008f;
    int rgb_to_ncurses(float v) { return (int)(v * 1000.0f); }
};
