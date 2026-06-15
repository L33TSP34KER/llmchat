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
    last_input_time_ = std::chrono::steady_clock::now();
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

void ChatUI::set_status_text(const std::string& text) {
    state_.status_text = text;
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

        update_tamagotchi_mood();

    anim_frame_++;

    int total = renderer_.compute_total_box_lines(conv_, term_width_);
    if (scroll_offset_ >= total - chat_height_) {
        scroll_offset_ = std::max(0, total - chat_height_);
    }

    renderer_.draw_chat(conv_, scroll_offset_, state_.processing, animation_.get_color_index(), tamagotchi_mood_, anim_frame_);
    {
        auto entries = conv_->get_entries();
        int non_system = 0;
        for (auto& e : entries) {
            if (e.type == ConversationEntry::USER || e.type == ConversationEntry::ASSISTANT)
                non_system++;
        }
        renderer_.draw_input(input_buf_, cursor_pos_, state_.processing, animation_.get_color_index(), anim_frame_, non_system);
    }
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
                if (event.bstate & BUTTON1_CLICKED) {
                    // Check if click is on tamagotchi (top-right corner)
                    int face_w = 5; // approximate face width
                    if (event.y == 0 && event.x >= term_width_ - face_w - 4 &&
                        event.x < term_width_ - 2) {
                        tamagotchi_mood_ = TAMAGOTCHI_HAPPY;
                        state_.status_text = "*pet*";
                        last_input_time_ = std::chrono::steady_clock::now();
                    }
                } else if (event.bstate & BUTTON4_PRESSED) {
                    scroll_up(1);
                } else if (event.bstate & BUTTON5_PRESSED) {
                    scroll_down(1);
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

        case '\t': {
            if (input_buf_.empty()) return;
            std::vector<std::string> completions = {
                "/clear", "/cls", "/help", "/skill",
                "/deepsearch", "/stats", "/model", "/export"
            };
            for (auto& s : config_->skills) {
                completions.push_back("/skill " + s.name);
            }
            std::vector<std::string> matches;
            for (auto& c : completions) {
                if (c.size() >= input_buf_.size() &&
                    c.substr(0, input_buf_.size()) == input_buf_) {
                    matches.push_back(c);
                }
            }
            if (matches.size() == 1) {
                input_buf_ = matches[0];
                cursor_pos_ = input_buf_.size();
            } else if (matches.size() > 1) {
                std::string hint;
                for (auto& m : matches) {
                    if (!hint.empty()) hint += "  ";
                    hint += m;
                }
                state_.status_text = hint;
            }
            return;
        }
    }

    if (ch >= 32 && ch <= 126) {
        insert_char((char)ch);
    }
}

void ChatUI::insert_char(char c) {
    input_buf_.insert(cursor_pos_, 1, c);
    cursor_pos_++;
    update_input_height();
}

void ChatUI::delete_char() {
    if (cursor_pos_ > 0 && !input_buf_.empty()) {
        input_buf_.erase(cursor_pos_ - 1, 1);
        cursor_pos_--;
    }
    update_input_height();
}

void ChatUI::update_input_height() {
    // Calculate lines needed: content length / terminal width
    int max_x = term_width_ > 3 ? term_width_ - 2 : 40;
    int needed = 1;
    if (!input_buf_.empty()) {
        needed = (str_width(input_buf_) / max_x) + 1;
    }
    needed = std::max(1, std::min(needed, 5));

    if (needed != input_height_) {
        input_height_ = needed;
        handle_resize();
    }
}

void ChatUI::update_tamagotchi_mood() {
    auto now = std::chrono::steady_clock::now();
    auto idle = std::chrono::duration_cast<std::chrono::seconds>(now - last_input_time_).count();

    if (state_.processing) {
        tamagotchi_mood_ = TAMAGOTCHI_HAPPY;
    } else if (idle < 30) {
        tamagotchi_mood_ = TAMAGOTCHI_HAPPY;
    } else if (idle < 120) {
        tamagotchi_mood_ = TAMAGOTCHI_NEUTRAL;
    } else {
        tamagotchi_mood_ = TAMAGOTCHI_SAD;
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
            last_input_time_ = std::chrono::steady_clock::now();
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
            last_input_time_ = std::chrono::steady_clock::now();
            if (deep_search_cb_) deep_search_cb_(query);
            return;
        }
        // Unknown command: pass through to send callback
        ConversationEntry ue;
        ue.type = ConversationEntry::USER;
        ue.content = msg;
        ue.timestamp = std::time(nullptr);
        conv_->add_entry(ue);
        input_buf_.clear();
        cursor_pos_ = 0;
        last_input_time_ = std::chrono::steady_clock::now();
        if (send_cb_) send_cb_(msg);
        return;
    }

    ConversationEntry ue;
    ue.type = ConversationEntry::USER;
    ue.content = msg;
    ue.timestamp = std::time(nullptr);
    conv_->add_entry(ue);

    input_buf_.clear();
    cursor_pos_ = 0;
    last_input_time_ = std::chrono::steady_clock::now();

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
