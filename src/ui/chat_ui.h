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
    std::string thinking_phrase;
    std::string conversation_title;
    int context_used = 0;
    int context_max = 80000;
};

class ChatUI {
public:
    using SendCallback = std::function<void(const std::string&)>;
    using DeepSearchCallback = std::function<void(const std::string&)>;
    using ClearCallback = std::function<void()>;
    using CancelCallback = std::function<void()>;
    using CopyCallback = std::function<void()>;
    using ClipboardCallback = std::function<bool(const std::string&)>;

    ChatUI(Conversation* conv, Config* config);
    ~ChatUI();

    struct OnboardingInfo {
        std::string model_name;
        std::string api_endpoint;
        std::string api_key;
        int max_context_chars = 80000;
        int tool_count = 0;
        int skill_count = 0;
        int memory_count = 0;
        int streak_days = 0;
        bool is_first_run = false;
        std::string model_id;
        int64_t n_params = 0;
        int64_t n_ctx_train = 0;
        std::vector<std::string> available_models;
        std::function<void(const std::string&)> on_model_selected;
    };

    void run();
    void set_onboarding(const OnboardingInfo& info);
    void clear_onboarding();
    void stop();
    void set_state(const UIState& state);
    void set_processing(bool p);
    void set_status_text(const std::string& text);
    void set_send_callback(SendCallback cb);
    void set_deep_search_callback(DeepSearchCallback cb);
    void set_clear_callback(ClearCallback cb);
    void set_cancel_callback(CancelCallback cb);
    void set_copy_callback(CopyCallback cb);
    void set_clipboard_callback(ClipboardCallback cb);
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
    ClipboardCallback clipboard_cb_;

    Theme theme_;
    Renderer renderer_;
    ColorAnimation animation_;

    WINDOW* top_win_ = nullptr;
    WINDOW* chat_win_ = nullptr;
    WINDOW* input_win_ = nullptr;
    WINDOW* status_win_ = nullptr;

    // Thinking popup state
    int thinking_button_x_ = 0;
    bool thinking_popup_open_ = false;
    WINDOW* thinking_popup_win_ = nullptr;

    int chat_height_ = 0;
    int input_height_ = 2;
    int status_height_ = 1;
    int term_width_ = 0;
    int term_height_ = 0;

    std::string input_buf_;
    int cursor_pos_ = 0;
    int scroll_offset_ = 0;
    bool was_at_bottom_ = true;
    int history_pos_ = -1;
    std::vector<std::string> input_history_;

    std::atomic<bool> should_exit_{false};
    std::atomic<bool> needs_redraw_{true};

    // Bracketed paste mode
    bool ncurses_initialized_ = false;
    bool paste_mode_ = false;
    std::string paste_buf_;
    int esc_state_ = 0;
    std::string esc_buf_;

    int anim_frame_ = 0;

    OnboardingInfo onboarding_info_;
    bool has_onboarding_ = false;

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
    void update_input_height();
};
