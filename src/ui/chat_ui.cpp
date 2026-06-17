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

void ChatUI::set_clipboard_callback(ClipboardCallback cb) {
    clipboard_cb_ = std::move(cb);
}

void ChatUI::stop() {
    should_exit_ = true;
}

void ChatUI::set_state(const UIState& state) {
    state_ = state;
    needs_redraw_ = true;
}

void ChatUI::set_processing(bool p) {
    state_.processing = p;
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

    nodelay(stdscr, TRUE);
    printf("\033[?2004h");  // enable bracketed paste

    // Enable mouse tracking (SGR mode for wider coordinates)
    printf("\033[?1000h\033[?1002h\033[?1006h");
    mousemask(BUTTON1_CLICKED | BUTTON1_PRESSED | REPORT_MOUSE_POSITION, NULL);

    getmaxyx(stdscr, term_height_, term_width_);
    create_windows();

    renderer_.set_windows(chat_win_, input_win_, status_win_);
    renderer_.init_pairs(theme_.colors);
    renderer_.init_animation_colors(can_change_color());
    animation_.init(theme_.colors);
    ncurses_initialized_ = true;
}

void ChatUI::cleanup_ncurses() {
    if (!ncurses_initialized_) return;
    ncurses_initialized_ = false;
    destroy_windows();
    endwin();
    printf("\033[?1000l\033[?1002l\033[?1003l\033[?1006l\033[?2004l");
}

void ChatUI::create_windows() {
    getmaxyx(stdscr, term_height_, term_width_);

    chat_height_ = term_height_ - input_height_ - status_height_ - 1;
    if (chat_height_ < 1) chat_height_ = 1;

    top_win_ = newwin(1, term_width_, 0, 0);
    chat_win_ = newwin(chat_height_, term_width_, 1, 0);
    input_win_ = newwin(input_height_, term_width_, 1 + chat_height_, 0);
    status_win_ = newwin(status_height_, term_width_, 1 + chat_height_ + input_height_, 0);

    scrollok(chat_win_, TRUE);
    keypad(chat_win_, TRUE);
    nodelay(input_win_, TRUE);
}

void ChatUI::destroy_windows() {
    if (thinking_popup_win_) { delwin(thinking_popup_win_); thinking_popup_win_ = nullptr; }
    if (top_win_) delwin(top_win_);
    if (chat_win_) delwin(chat_win_);
    if (input_win_) delwin(input_win_);
    if (status_win_) delwin(status_win_);
    top_win_ = chat_win_ = input_win_ = status_win_ = nullptr;
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

    anim_frame_++;

    int total = renderer_.compute_total_box_lines(conv_, term_width_);
    if (was_at_bottom_) {
        scroll_offset_ = std::max(0, total - chat_height_);
    }

    renderer_.draw_chat(conv_, scroll_offset_, state_.processing);
    {
        auto entries = conv_->get_entries();
        int non_system = 0;
        for (auto& e : entries) {
            if (e.type == ConversationEntry::USER || e.type == ConversationEntry::ASSISTANT)
                non_system++;
        }
        renderer_.draw_input(input_buf_, cursor_pos_, non_system);
    }

    // Calculate thinking button position for popup + mouse tracking
    {
        std::string btn_label;
        switch (config_->max_thinking_tokens) {
            case 0:    btn_label = "\xf0\x9f\xa7\xa0 None"; break;
            case 128:  btn_label = "\xf0\x9f\xa7\xa0 Minimal"; break;
            case 250:  btn_label = "\xf0\x9f\xa7\xa0 Medium"; break;
            case 512:  btn_label = "\xf0\x9f\xa7\xa0 Normal"; break;
            case 1024: btn_label = "\xf0\x9f\xa7\xa0 High"; break;
            default:   btn_label = "\xf0\x9f\xa7\xa0 GODMOD"; break;
        }
        thinking_button_x_ = term_width_ - (int)str_width(btn_label) - 5;
    }

    // Draw top bar
    renderer_.draw_topbar(top_win_, state_.conversation_title, config_->max_thinking_tokens, anim_frame_);

    // Create/destroy thinking popup window
    if (thinking_popup_open_ && !thinking_popup_win_) {
        static const char* popup_labels[] = {
            "None", "Minimal (128)", "Medium (250)",
            "Normal (512)", "High (1024)", "GODMOD (\xe2\x88\x9e)"
        };
        int num_options = 6;
        int max_w = 0;
        for (int i = 0; i < num_options; i++) {
            int w = str_width(std::string("  ") + popup_labels[i]);
            if (w > max_w) max_w = w;
        }
        int popup_w = max_w + 6;
        int popup_h = num_options + 2;
        int btn_right = term_width_ - 3;
        int popup_x = btn_right - popup_w;
        if (popup_x < 2) popup_x = 2;
        if (popup_x + popup_w > term_width_) popup_x = term_width_ - popup_w - 2;
        thinking_popup_win_ = newwin(popup_h, popup_w, 1, popup_x);
    } else if (!thinking_popup_open_ && thinking_popup_win_) {
        delwin(thinking_popup_win_);
        thinking_popup_win_ = nullptr;
    }

    // Draw thinking popup if open
    if (thinking_popup_open_ && thinking_popup_win_) {
        static const char* popup_labels[] = {
            "None",
            "Minimal (128)",
            "Medium (250)",
            "Normal (512)",
            "High (1024)",
            "GODMOD (\xe2\x88\x9e)"
        };
        static const int popup_values[] = {0, 128, 250, 512, 1024, 99999};
        int num_options = 6;

        // Find current selection index
        int cur = 0;
        for (int i = 0; i < num_options; i++) {
            if (config_->max_thinking_tokens == popup_values[i]) {
                cur = i;
                break;
            }
        }

        werase(thinking_popup_win_);
        box(thinking_popup_win_, 0, 0);
        int content_w = getmaxx(thinking_popup_win_) - 2;
        for (int i = 0; i < num_options; i++) {
            int wy = i + 1;
            if (i == cur) wattron(thinking_popup_win_, A_REVERSE);
            wmove(thinking_popup_win_, wy, 1);
            wclrtoeol(thinking_popup_win_);
            std::string line = std::string("  ") + popup_labels[i];
            int cells = str_width(line);
            if (cells < content_w) line.append(content_w - cells, ' ');
            waddstr(thinking_popup_win_, line.c_str());
            if (i == cur) wattroff(thinking_popup_win_, A_REVERSE);
        }
        wnoutrefresh(thinking_popup_win_);
    }

    renderer_.draw_status(state_.processing, state_.model_name, state_.status_text, anim_frame_, state_.thinking_phrase);
    doupdate();
}

void ChatUI::handle_input(int ch) {
    // Bracketed paste escape sequence handling
    if (esc_state_ > 0) {
        esc_buf_ += (char)ch;
        if (ch == '~') {
            esc_state_ = 0;
            if (esc_buf_ == "[200~") {
                paste_mode_ = true;
                paste_buf_.clear();
            } else if (esc_buf_ == "[201~") {
                paste_mode_ = false;
                for (char c : paste_buf_)
                    insert_char(c);
                update_input_height();
            }
        } else if (esc_buf_.size() > 6) {
            esc_state_ = 0;
        }
        return;
    }
    if (ch == 27) {
        esc_state_ = 1;
        esc_buf_.clear();
        return;
    }
    if (paste_mode_) {
        paste_buf_ += (char)ch;
        return;
    }

    if (ch == KEY_MOUSE) {
        MEVENT event;
        if (getmouse(&event) == OK) {
            // Calculate thinking button position (right side of top bar)
            std::string btn_label;
            switch (config_->max_thinking_tokens) {
                case 0:    btn_label = "\xf0\x9f\xa7\xa0 None"; break;
                case 128:  btn_label = "\xf0\x9f\xa7\xa0 Minimal"; break;
                case 250:  btn_label = "\xf0\x9f\xa7\xa0 Medium"; break;
                case 512:  btn_label = "\xf0\x9f\xa7\xa0 Normal"; break;
                case 1024: btn_label = "\xf0\x9f\xa7\xa0 High"; break;
                default:   btn_label = "\xf0\x9f\xa7\xa0 GODMOD"; break;
            }
            int btn_x = term_width_ - (int)str_width(btn_label) - 5;
            int btn_w = (int)str_width(btn_label) + 3;

            if (event.bstate & (BUTTON1_CLICKED | BUTTON1_PRESSED)) {
                // Check if click on thinking button (row 0)
                if (event.y == 0 && event.x >= btn_x && event.x < btn_x + btn_w) {
                    thinking_popup_open_ = !thinking_popup_open_;
                    needs_redraw_ = true;
                    return;
                }

                // Check if click on popup option
                if (thinking_popup_open_) {
                    static const char* popup_labels_calc[] = {
                        "None", "Minimal (128)", "Medium (250)",
                        "Normal (512)", "High (1024)", "GODMOD (\xe2\x88\x9e)"
                    };
                    static const int popup_values[] = {0, 128, 250, 512, 1024, 99999};
                    int num_options = 6;
                    int max_w = 0;
                    for (int i = 0; i < num_options; i++) {
                        int w = str_width(std::string("  ") + popup_labels_calc[i]);
                        if (w > max_w) max_w = w;
                    }
                    int popup_w = max_w + 6;
                    int btn_right = term_width_ - 3;
                    int popup_x = btn_right - popup_w;
                    if (popup_x < 2) popup_x = 2;
                    if (popup_x + popup_w > term_width_) popup_x = term_width_ - popup_w - 2;

                    // options start at screen row 2 (row 1 = top border, row 2+ = options)
                    // content area is from column 1 to popup_w - 2
                    int option_y = event.y - 2;
                    int option_x = event.x - popup_x;

                    if (option_y >= 0 && option_y < num_options && option_x >= 1 && option_x < popup_w - 1) {
                        config_->max_thinking_tokens = popup_values[option_y];
                        thinking_popup_open_ = false;
                        config_->save();
                        needs_redraw_ = true;
                        return;
                    }

                    // Click outside popup (anywhere not on the popup window area) → close it
                    int popup_h = num_options + 2;
                    if (event.y >= 1 && event.y < 1 + popup_h &&
                        event.x >= popup_x && event.x < popup_x + popup_w) {
                        // Clicked on popup border or outside content area → close
                        thinking_popup_open_ = false;
                        needs_redraw_ = true;
                        return;
                    }
                    // Click is outside popup entirely → close
                    thinking_popup_open_ = false;
                    needs_redraw_ = true;
                    return;
                }
            }
        }
        return;
    }

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
            if (input_buf_.empty()) {
                // Input empty → scroll wheel via alternate scroll mode
                scroll_up(1);
            } else if (!input_history_.empty()) {
                if (history_pos_ < 0) history_pos_ = input_history_.size() - 1;
                else if (history_pos_ > 0) history_pos_--;
                if (history_pos_ >= 0 && history_pos_ < (int)input_history_.size()) {
                    input_buf_ = input_history_[history_pos_];
                    cursor_pos_ = input_buf_.size();
                }
            }
            return;

        case KEY_DOWN:
            if (input_buf_.empty()) {
                scroll_down(1);
            } else if (history_pos_ >= 0) {
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
            if (state_.processing && cancel_cb_) cancel_cb_();
            should_exit_ = true;
            return;

        case '\t': {
            if (input_buf_.empty()) return;
            std::vector<std::string> completions = {
                "/clear", "/cls", "/help", "/skill",
                "/deepsearch", "/stats", "/model", "/export", "/copy"
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
        if (cmd.substr(0, 5) == "copy ") {
            std::string num = cmd.substr(5);
            num.erase(0, num.find_first_not_of(" \t"));
            num.erase(num.find_last_not_of(" \t") + 1);
            int idx = atoi(num.c_str());
            if (idx <= 0) {
                state_.status_text = "Usage: /copy <message number>";
            } else {
                auto entries = conv_->get_entries();
                if (idx > (int)entries.size()) {
                    state_.status_text = "Only " + std::to_string(entries.size()) + " messages";
                } else if (clipboard_cb_(entries[idx-1].content)) {
                    state_.status_text = "Copied message " + num;
                } else {
                    state_.status_text = "Copy failed";
                }
            }
            input_buf_.clear();
            cursor_pos_ = 0;
            return;
        }
        if (cmd == "deepsearch" || cmd.substr(0, 11) == "deepsearch ") {
            std::string query;
            if (cmd == "deepsearch") {
                state_.status_text = "Usage: /deepsearch <query>";
                input_buf_.clear();
                cursor_pos_ = 0;
                return;
            }
            query = cmd.substr(11);
            query.erase(0, query.find_first_not_of(" \t"));
            query.erase(query.find_last_not_of(" \t") + 1);
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
        // Unknown command: pass through to send callback
        ConversationEntry ue;
        ue.type = ConversationEntry::USER;
        ue.content = msg;
        ue.timestamp = std::time(nullptr);
        conv_->add_entry(ue);
        input_buf_.clear();
        cursor_pos_ = 0;
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

    if (send_cb_) send_cb_(msg);
}

bool ChatUI::is_at_bottom() {
    return was_at_bottom_;
}

void ChatUI::scroll_up(int lines) {
    scroll_offset_ = std::max(0, scroll_offset_ - lines);
    was_at_bottom_ = false;
    needs_redraw_ = true;
}

void ChatUI::scroll_down(int lines) {
    int total = renderer_.compute_total_box_lines(conv_, term_width_);
    scroll_offset_ += lines;
    if (scroll_offset_ > total - chat_height_)
        scroll_offset_ = std::max(0, total - chat_height_);
    int max_scroll = std::max(0, total - chat_height_);
    was_at_bottom_ = (scroll_offset_ >= max_scroll);
    needs_redraw_ = true;
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
