#pragma once
#include <ncurses.h>
#include <string>
#include <vector>
#include "config.h"
#include "conversation.h"

enum TamagotchiMood {
    TAMAGOTCHI_HAPPY = 0,
    TAMAGOTCHI_NEUTRAL,
    TAMAGOTCHI_SAD
};

class Renderer {
public:
    Renderer() = default;

    void set_windows(WINDOW* chat_win, WINDOW* input_win, WINDOW* status_win);

    void draw_chat(Conversation* conv, int scroll_offset, bool is_streaming, int anim_color_idx, int tamagotchi_mood = TAMAGOTCHI_HAPPY, int anim_frame = 0);
    void draw_input(const std::string& input_buf, int cursor_pos, bool is_processing, int anim_color_idx, int anim_frame = 0, int msg_count = 0);
    void draw_status(bool is_processing, bool use_casino_status, const std::string& casino_frame,
                     const std::string& model_name, const std::string& status_text,
                     int anim_frame = 0, const std::string& thinking_phrase = "");
    int compute_total_box_lines(Conversation* conv, int max_x);

    static void ncurses_color_to_rgb(int c, float& r, float& g, float& b);
    void init_pairs(const ThemeColors& colors);
    void init_animation_colors(bool can_change);
    static void init_pair_safe(int pair, int fg, int bg);

    static void draw_tamagotchi(WINDOW* win, int mood, int anim_frame = 0, int max_x = 80);

    // Get the conversation entry index at a given screen y (after last draw_chat)
    int entry_at_y(int screen_y, int scroll_offset) const;

private:
    WINDOW* chat_win_ = nullptr;
    WINDOW* input_win_ = nullptr;
    WINDOW* status_win_ = nullptr;
    std::vector<int> line_to_entry_;
};
