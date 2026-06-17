#pragma once
#include <ncurses.h>
#include <string>
#include <vector>
#include "config.h"
#include "conversation.h"

class Renderer {
public:
    Renderer() = default;

    void set_windows(WINDOW* chat_win, WINDOW* input_win, WINDOW* status_win);

    void draw_chat(Conversation* conv, int scroll_offset, bool is_streaming);
    void draw_input(const std::string& input_buf, int cursor_pos, int msg_count = 0);
    void draw_status(bool is_processing,
                     const std::string& model_name, const std::string& status_text,
                     int anim_frame = 0, const std::string& thinking_phrase = "");

    void draw_topbar(WINDOW* top_win, const std::string& title, int thinking_tokens, int anim_frame);
    int compute_total_box_lines(Conversation* conv, int max_x);

    static void ncurses_color_to_rgb(int c, float& r, float& g, float& b);
    void init_pairs(const ThemeColors& colors);
    void init_animation_colors(bool can_change);
    static void init_pair_safe(int pair, int fg, int bg);

    // Get the conversation entry index at a given screen y (after last draw_chat)
    int entry_at_y(int screen_y, int scroll_offset) const;

private:
    WINDOW* chat_win_ = nullptr;
    WINDOW* input_win_ = nullptr;
    WINDOW* status_win_ = nullptr;
    std::vector<int> line_to_entry_;
};
