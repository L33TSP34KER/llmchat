#pragma once
#include <ncurses.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <functional>
#include <chrono>
#include "conversation.h"
#include "config.h"
#include "renderer.h"
#include "animation.h"
#include "text_util.h"

struct UIState {
    bool running = true;
    bool processing = false;
    std::string model_name;
    std::string status_text;
};

class ChatUI {
public:
    using SendCallback = std::function<void(const std::string&)>;
    using DeepSearchCallback = std::function<void(const std::string&)>;
    using ClearCallback = std::function<void()>;
    using CancelCallback = std::function<void()>;
    using CopyCallback = std::function<void()>;

    ChatUI(Conversation* conv, Config* config);
    ~ChatUI();

    void run();
    void stop();
    void set_state(const UIState& state);
    void set_send_callback(SendCallback cb);
    void set_deep_search_callback(DeepSearchCallback cb);
    void set_clear_callback(ClearCallback cb);
    void set_cancel_callback(CancelCallback cb);
    void set_copy_callback(CopyCallback cb);
    void notify_update();

private:
    Conversation* conv_;
    Config* config_;
    UIState state_;
    SendCallback send_cb_;
    DeepSearchCallback deep_search_cb_;
    ClearCallback clear_cb_;
    CancelCallback cancel_cb_;
    CopyCallback copy_cb_;

    Theme theme_;
    Renderer renderer_;
    ColorAnimation animation_;

    WINDOW* chat_win_ = nullptr;
    WINDOW* input_win_ = nullptr;
    WINDOW* status_win_ = nullptr;

    int chat_height_ = 0;
    int input_height_ = 2;
    int status_height_ = 1;
    int term_width_ = 0;
    int term_height_ = 0;

    std::string input_buf_;
    int cursor_pos_ = 0;
    int scroll_offset_ = 0;
    int history_pos_ = -1;
    std::vector<std::string> input_history_;

    std::atomic<bool> should_exit_{false};
    std::atomic<bool> needs_redraw_{true};

    // Casino status bar state
    std::string casino_frame_;
    int casino_ticker_ = 0;
    std::vector<std::string> casino_frames_ = {
        " [ * ] SPINNING...  ",
        " [-*-] ROLLING...   ",
        " [/ \\] LUCKY DAY!  ",
        " [ $ ] JACKPOT!     ",
        " [@-@] DEALING...   ",
        " [%~%] CRAPS!       ",
        " [^_^] WILD CARD!   ",
        " [###] WINNINGS!    ",
    };
    std::string status_content_;
    bool use_casino_status_ = false;

    // Tamagotchi state
    std::chrono::steady_clock::time_point last_input_time_;
    int tamagotchi_mood_ = TAMAGOTCHI_HAPPY;
    int anim_frame_ = 0;

    void update_tamagotchi_mood();
    void init_ncurses();
    void cleanup_ncurses();
    void handle_resize();
    void create_windows();
    void destroy_windows();
    void draw_all();
    void handle_input(int ch);
    void insert_char(char c);
    void delete_char();
    void send_message();
    void scroll_up(int lines);
    void scroll_down(int lines);
    bool is_at_bottom();

    // Casino status
    void update_casino_status();
    std::string get_casino_frame();
};
