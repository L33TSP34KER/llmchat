#include "chat_ui.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <chrono>
#include <thread>
#include <ctime>
#include <cstdlib>

ChatUI::ChatUI(Conversation* conv, Config* config)
    : conv_(conv), config_(config) {
    theme_ = config->get_theme();
    use_casino_status_ = theme_.casino_status_bar;
}

ChatUI::~ChatUI() {
    cleanup_ncurses();
}

void ChatUI::set_send_callback(SendCallback cb) {
    send_cb_ = std::move(cb);
}

void ChatUI::set_deep_search_callback(DeepSearchCallback cb) {
    deep_search_cb_ = std::move(cb);
}

void ChatUI::set_clear_callback(ClearCallback cb) {
    clear_cb_ = std::move(cb);
}

void ChatUI::set_cancel_callback(CancelCallback cb) {
    cancel_cb_ = std::move(cb);
}

void ChatUI::set_copy_callback(CopyCallback cb) {
    copy_cb_ = std::move(cb);
}

void ChatUI::stop() {
    should_exit_ = true;
}

void ChatUI::set_state(const UIState& state) {
    state_ = state;
    needs_redraw_ = true;
}

void ChatUI::notify_update() {
    needs_redraw_ = true;
}

void ChatUI::init_ncurses() {
    initscr();
    raw();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(1);
    start_color();
    use_default_colors();

    mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
    mouseinterval(0);
    nodelay(stdscr, TRUE);
    printf("\033[?1003h\033[?1006h");

    getmaxyx(stdscr, term_height_, term_width_);
    create_windows();

    renderer_.set_windows(chat_win_, input_win_, status_win_);
    renderer_.init_pairs(theme_.colors);
    renderer_.init_animation_colors(can_change_color());
    animation_.init(theme_.colors);
}

void ChatUI::cleanup_ncurses() {
    printf("\033[?1003l\033[?1006l");
    destroy_windows();
    endwin();
}

void ChatUI::create_windows() {
    getmaxyx(stdscr, term_height_, term_width_);

    chat_height_ = term_height_ - input_height_ - status_height_;
    if (chat_height_ < 1) chat_height_ = 1;

    chat_win_ = newwin(chat_height_, term_width_, 0, 0);
    input_win_ = newwin(input_height_, term_width_, chat_height_, 0);
    status_win_ = newwin(status_height_, term_width_, chat_height_ + input_height_, 0);

    scrollok(chat_win_, TRUE);
    keypad(chat_win_, TRUE);
    nodelay(input_win_, TRUE);
}

void ChatUI::destroy_windows() {
    if (chat_win_) delwin(chat_win_);
    if (input_win_) delwin(input_win_);
    if (status_win_) delwin(status_win_);
    chat_win_ = input_win_ = status_win_ = nullptr;
}

void ChatUI::handle_resize() {
    destroy_windows();
    endwin();
    refresh();
    clear();
    getmaxyx(stdscr, term_height_, term_width_);
    create_windows();
    renderer_.set_windows(chat_win_, input_win_, status_win_);
    needs_redraw_ = true;
}

void ChatUI::draw_all() {
    if (!chat_win_) return;

    if (state_.processing) {
        animation_.advance();
    }
    if (use_casino_status_ && state_.processing) {
        update_casino_status();
    }

    int total = renderer_.compute_total_box_lines(conv_, term_width_);
    if (scroll_offset_ >= total - chat_height_) {
        scroll_offset_ = std::max(0, total - chat_height_);
    }

    renderer_.draw_chat(conv_, scroll_offset_, state_.processing, animation_.get_color_index());
    renderer_.draw_input(input_buf_, cursor_pos_, state_.processing, animation_.get_color_index());
    renderer_.draw_status(state_.processing, use_casino_status_, casino_frame_,
                          state_.model_name, state_.status_text);
    doupdate();
}

void ChatUI::handle_input(int ch) {
    switch (ch) {
        case KEY_RESIZE:
            handle_resize();
            return;

        case '\n':
        case '\r':
        case KEY_ENTER:
            send_message();
            return;

        case KEY_BACKSPACE:
        case 127:
        case '\b':
            delete_char();
            return;

        case KEY_DC:
            if (cursor_pos_ < (int)input_buf_.size())
                input_buf_.erase(cursor_pos_, 1);
            return;

        case KEY_LEFT:
            if (cursor_pos_ > 0) cursor_pos_--;
            return;

        case KEY_RIGHT:
            if (cursor_pos_ < (int)input_buf_.size()) cursor_pos_++;
            return;

        case KEY_HOME:
            cursor_pos_ = 0;
            return;

        case KEY_END:
            cursor_pos_ = input_buf_.size();
            return;

        case KEY_PPAGE:
            scroll_up(chat_height_);
            return;

        case KEY_NPAGE:
            scroll_down(chat_height_);
            return;

        case KEY_UP:
            if (!input_history_.empty()) {
                if (history_pos_ < 0) history_pos_ = input_history_.size() - 1;
                else if (history_pos_ > 0) history_pos_--;
                if (history_pos_ >= 0 && history_pos_ < (int)input_history_.size()) {
                    input_buf_ = input_history_[history_pos_];
                    cursor_pos_ = input_buf_.size();
                }
            }
            return;

        case KEY_DOWN:
            if (history_pos_ >= 0) {
                history_pos_++;
                if (history_pos_ >= (int)input_history_.size()) {
                    history_pos_ = -1;
                    input_buf_.clear();
                } else {
                    input_buf_ = input_history_[history_pos_];
                }
                cursor_pos_ = input_buf_.size();
            }
            return;

        case KEY_SR:
            scroll_up(3);
            return;

        case KEY_SF:
            scroll_down(3);
            return;

        case KEY_MOUSE: {
            MEVENT event;
            if (getmouse(&event) == OK) {
                if (event.bstate & BUTTON4_PRESSED) {
                    scroll_up(3);
                } else if (event.bstate & BUTTON5_PRESSED) {
                    scroll_down(3);
                }
            }
            return;
        }

        case 3:
            state_.status_text = "Cancelling...";
            if (cancel_cb_) cancel_cb_();
            return;

        case 12:
            conv_->clear();
            scroll_offset_ = 0;
            if (clear_cb_) clear_cb_();
            return;

        case 25:
            if (copy_cb_) copy_cb_();
            return;

        case 17:
            should_exit_ = true;
            return;
    }

    if (ch >= 32 && ch <= 126) {
        insert_char((char)ch);
    }
}

void ChatUI::insert_char(char c) {
    input_buf_.insert(cursor_pos_, 1, c);
    cursor_pos_++;
}

void ChatUI::delete_char() {
    if (cursor_pos_ > 0 && !input_buf_.empty()) {
        input_buf_.erase(cursor_pos_ - 1, 1);
        cursor_pos_--;
    }
}

void ChatUI::send_message() {
    std::string msg = input_buf_;
    if (msg.empty()) return;

    size_t start = msg.find_first_not_of(" \t\n\r");
    size_t end = msg.find_last_not_of(" \t\n\r");
    if (start == std::string::npos) return;
    msg = msg.substr(start, end - start + 1);

    input_history_.push_back(msg);
    if (input_history_.size() > 100) input_history_.erase(input_history_.begin());
    history_pos_ = -1;

    if (msg[0] == '/') {
        std::string cmd = msg.substr(1);
        if (cmd == "clear" || cmd == "cls") {
            conv_->clear();
            scroll_offset_ = 0;
            input_buf_.clear();
            cursor_pos_ = 0;
            if (clear_cb_) clear_cb_();
            return;
        }
        if (cmd.substr(0, 6) == "skill ") {
            state_.status_text = "Skill set";
            input_buf_.clear();
            cursor_pos_ = 0;
            return;
        }
        if (cmd.substr(0, 11) == "deepsearch ") {
            std::string query = cmd.substr(11);
            query.erase(0, query.find_first_not_of(" \t"));
            query.erase(query.find_last_not_of(" \t") + 1);
            if (query.empty()) {
                state_.status_text = "Usage: /deepsearch <query>";
                input_buf_.clear();
                cursor_pos_ = 0;
                return;
            }
            ConversationEntry ue;
            ue.type = ConversationEntry::USER;
            ue.content = "/deepsearch " + query;
            ue.timestamp = std::time(nullptr);
            conv_->add_entry(ue);
            state_.status_text = "Deep search started...";
            input_buf_.clear();
            cursor_pos_ = 0;
            if (deep_search_cb_) deep_search_cb_(query);
            return;
        }
    }

    ConversationEntry ue;
    ue.type = ConversationEntry::USER;
    ue.content = msg;
    ue.timestamp = std::time(nullptr);
    conv_->add_entry(ue);

    input_buf_.clear();
    cursor_pos_ = 0;

    if (send_cb_) send_cb_(msg);
}

bool ChatUI::is_at_bottom() {
    int total = renderer_.compute_total_box_lines(conv_, term_width_);
    return scroll_offset_ >= total - chat_height_;
}

void ChatUI::scroll_up(int lines) {
    scroll_offset_ = std::max(0, scroll_offset_ - lines);
    needs_redraw_ = true;
}

void ChatUI::scroll_down(int lines) {
    int total = renderer_.compute_total_box_lines(conv_, term_width_);
    scroll_offset_ += lines;
    if (scroll_offset_ > total - chat_height_)
        scroll_offset_ = std::max(0, total - chat_height_);
    needs_redraw_ = true;
}

std::string ChatUI::get_casino_frame() {
    int idx = (casino_ticker_ / 8) % casino_frames_.size();
    return casino_frames_[idx];
}

void ChatUI::update_casino_status() {
    casino_ticker_++;
    casino_frame_ = get_casino_frame();
}

void ChatUI::run() {
    init_ncurses();

    auto last_frame = std::chrono::steady_clock::now();
    const auto frame_duration = std::chrono::milliseconds(theme_.animation_speed);

    while (!should_exit_) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = now - last_frame;

        if (needs_redraw_ || elapsed >= frame_duration) {
            draw_all();
            needs_redraw_ = false;
            last_frame = now;
        }

        int ch = wgetch(stdscr);
        if (ch == ERR) {
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
            continue;
        }

        handle_input(ch);
    }

    cleanup_ncurses();
}
