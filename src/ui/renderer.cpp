#include "renderer.h"
#include "text_util.h"
#include "markdown.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <ctime>

// -------------------------------------------------------------------
// Segment-based rendering structures
// -------------------------------------------------------------------
struct RenderSeg {
    int color;
    int attr;
    std::string text;
};

struct SegLine {
    std::vector<RenderSeg> segs;
    bool is_sep;
};

// -------------------------------------------------------------------
// Table helpers
// -------------------------------------------------------------------
struct TableRow {
    std::vector<std::string> cells;
    bool is_sep;
};

static std::vector<std::string> split_table_cells(const std::string& raw) {
    std::string inner = raw;
    if (inner.size() >= 2 && inner.front() == '|') inner = inner.substr(1);
    if (!inner.empty() && inner.back() == '|') inner.pop_back();

    std::vector<std::string> cells;
    std::string cell;
    for (char c : inner) {
        if (c == '|') { cells.push_back(cell); cell.clear(); }
        else cell += c;
    }
    cells.push_back(cell);
    return cells;
}

static int detect_alignment(const std::string& cell) {
    std::string t = cell;
    t.erase(0, t.find_first_not_of(" \t"));
    t.erase(t.find_last_not_of(" \t") + 1);
    if (t.empty()) return 0;
    bool left = (t.front() == ':');
    bool right = (t.size() > 1 && t.back() == ':');
    if (left && right) return 2; // center
    if (left) return -1;         // left-aligned
    if (right) return 1;        // right-aligned
    return 0;
}

static std::string trim_cell(const std::string& s) {
    std::string t = s;
    t.erase(0, t.find_first_not_of(" \t"));
    t.erase(t.find_last_not_of(" \t") + 1);
    return t;
}

static void render_table_group(const std::vector<MdLine>& group, int color, int content_w,
                                std::vector<SegLine>& out) {
    if (group.empty()) return;

    // Parse all rows
    std::vector<TableRow> rows;
    for (auto& ml : group) {
        TableRow row;
        row.is_sep = (!ml.segs.empty() && ml.segs[0].type == MdSeg::TABLE_SEP);
        auto raw_cells = split_table_cells(ml.segs[0].text);
        for (auto& c : raw_cells)
            row.cells.push_back(c);
        rows.push_back(row);
    }

    // Find column widths
    std::vector<int> col_widths;
    for (auto& row : rows) {
        for (size_t i = 0; i < row.cells.size(); i++) {
            int w = str_width(trim_cell(row.cells[i]));
            if (i >= col_widths.size()) col_widths.push_back(w);
            else col_widths[i] = std::max(col_widths[i], w);
        }
    }
    if (col_widths.empty()) return;

    // Clamp total width
    int total_w = (col_widths.size() - 1) + 1; // | and separators
    for (int w : col_widths) total_w += w + 2;  // padding

    // Build border line
    auto make_border = [&](bool is_top) -> std::string {
        std::string line;
        line += is_top ? "\xe2\x94\x8c" : "\xe2\x94\x94"; // "┌" or "└"
        for (size_t i = 0; i < col_widths.size(); i++) {
            if (i > 0) line += is_top ? "\xe2\x94\xac" : "\xe2\x94\xb4"; // "┬" or "┴"
            for (int j = 0; j < col_widths[i] + 2; j++)
                line += "\xe2\x94\x80"; // "─"
        }
        line += is_top ? "\xe2\x94\x90" : "\xe2\x94\x98"; // "┐" or "┘"
        return line;
    };

    // Add top border
    {
        std::string border = make_border(true);
        if (str_width(border) > content_w) {
            border = border.substr(0, content_w + 2);
        }
        border = "  " + border;
        SegLine sl;
        sl.is_sep = true;
        sl.segs.push_back({color, A_NORMAL, border});
        out.push_back(sl);
    }

    // Extract column alignments from separator row
    std::vector<int> col_align;
    for (auto& row : rows) {
        if (row.is_sep) {
            for (size_t ci = 0; ci < row.cells.size(); ci++)
                col_align.push_back(detect_alignment(row.cells[ci]));
            break;
        }
    }
    if (col_align.empty())
        col_align.resize(col_widths.size(), 0);

    // Render each row
    for (size_t ri = 0; ri < rows.size(); ri++) {
        auto& row = rows[ri];
        if (row.is_sep) continue;

        std::string line = "\xe2\x94\x82"; // "│"
        for (size_t ci = 0; ci < col_widths.size(); ci++) {
            std::string cell_val = (ci < row.cells.size()) ? trim_cell(row.cells[ci]) : "";
            int pad = col_widths[ci] - str_width(cell_val);
            if (pad < 0) pad = 0;
            int align = (ci < col_align.size()) ? col_align[ci] : 0;
            if (align == 1) { // right
                cell_val = std::string(pad, ' ') + cell_val;
            } else if (align == 2) { // center
                int l = pad / 2;
                int r = pad - l;
                cell_val = std::string(l, ' ') + cell_val + std::string(r, ' ');
            } else { // left
                cell_val = cell_val + std::string(pad, ' ');
            }
            line += " " + cell_val + " \xe2\x94\x82"; // " │"
        }

        if (str_width(line) > content_w) {
            line = line.substr(0, content_w + 2);
        }
        line = "  " + line;
        SegLine sl;
        sl.segs.push_back({color, A_NORMAL, line});
        out.push_back(sl);
    }

    // Bottom border
    {
        std::string border = make_border(false);
        if (str_width(border) > content_w)
            border = border.substr(0, content_w + 2);
        border = "  " + border;
        SegLine sl;
        sl.segs.push_back({color, A_NORMAL, border});
        out.push_back(sl);
    }
}

// -------------------------------------------------------------------
// Relative timestamp helper
// -------------------------------------------------------------------
static std::string format_timestamp(int64_t ts) {
    if (ts <= 0) return "";
    auto now = std::time(nullptr);
    auto diff = std::difftime(now, ts);
    char tbuf[32];
    if (diff < 60) {
        return "just now";
    } else if (diff < 3600) {
        int m = (int)(diff / 60);
        snprintf(tbuf, sizeof(tbuf), "%dm ago", m);
        return tbuf;
    } else if (diff < 86400) {
        int h = (int)(diff / 3600);
        snprintf(tbuf, sizeof(tbuf), "%dh ago", h);
        return tbuf;
    } else if (diff < 604800) {
        int d = (int)(diff / 86400);
        snprintf(tbuf, sizeof(tbuf), "%dd ago", d);
        return tbuf;
    } else {
        std::tm* tm = std::localtime(&ts);
        if (tm) {
            std::strftime(tbuf, sizeof(tbuf), "%b %d", tm);
            return tbuf;
        }
        return "";
    }
}

// -------------------------------------------------------------------
// Build seg_lines from conversation (shared by draw & compute)
// -------------------------------------------------------------------
static std::vector<SegLine> build_seg_lines(Conversation* conv, int max_x,
                                             bool is_streaming, int anim_color_idx) {
    int content_w = max_x - 4;
    if (content_w < 4) content_w = 4;

    auto entries = conv->get_entries();
    std::vector<SegLine> seg_lines;

    if (entries.empty()) {
        {
            SegLine sl;
            sl.segs.push_back({6, A_BOLD, ""});
            seg_lines.push_back(sl);
        }
        {
            SegLine sl;
            sl.segs.push_back({6, A_BOLD, "  \xe2\x96\x8c llmchat"});
            seg_lines.push_back(sl);
        }
        {
            SegLine sl;
            sl.segs.push_back({6, A_NORMAL, "  \xe2\x96\x8c  terminal AI chat client"});
            seg_lines.push_back(sl);
        }
        {
            SegLine sl;
            sl.segs.push_back({8, A_NORMAL, ""});
            seg_lines.push_back(sl);
        }
        {
            SegLine sl;
            sl.segs.push_back({8, A_NORMAL, "  \xe2\x96\x8c  Type a message to begin"});
            seg_lines.push_back(sl);
        }
        {
            SegLine sl;
            sl.segs.push_back({8, A_NORMAL, "  \xe2\x96\x8c  /help for commands  "});
            seg_lines.push_back(sl);
        }
        {
            SegLine sl;
            sl.segs.push_back({8, A_NORMAL, "  \xe2\x96\x8c  TAB for completion  "});
            seg_lines.push_back(sl);
        }
    }

    int prev_type = -1;

    for (size_t ei = 0; ei < entries.size(); ei++) {
        auto& entry = entries[ei];
        std::string label;
        int color = -1;

        switch (entry.type) {
            case ConversationEntry::USER:
                label = "You";
                color = 1;
                break;
            case ConversationEntry::ASSISTANT:
                label = "Assistant";
                color = 2;
                break;
            case ConversationEntry::SYSTEM:
                label = "System";
                color = 3;
                break;
            case ConversationEntry::TOOL_CALL:
                label = "Tool: " + entry.tool_name;
                color = 4;
                break;
            case ConversationEntry::TOOL_RESULT:
                label = "Result";
                color = 1;
                break;
            case ConversationEntry::ERROR:
                label = "Error";
                color = 5;
                break;
        }

        bool same_role = (prev_type == (int)entry.type);
        prev_type = (int)entry.type;

        std::string content = entry.content;

        if (is_streaming &&
            ei == entries.size() - 1 && entry.type == ConversationEntry::ASSISTANT) {
            color = anim_color_idx;
        }

        // Role label header (skip if same consecutive role)
        if (!same_role) {
            std::string ts = format_timestamp(entry.timestamp);
            std::string header = " \xe2\x96\x8c "; // " ▌ "
            header += label;
            if (!ts.empty()) {
                header += "  \xe2\x80\xa2  " + ts;
            }
            SegLine sl;
            sl.segs.push_back({color, A_BOLD, header});
            seg_lines.push_back(sl);
        }

        // Parse content with md_parse
        auto md_lines = md_parse(content, content_w);

        // Collect table groups
        std::vector<MdLine> table_group;
        auto flush_table = [&]() {
            if (table_group.empty()) return;
            render_table_group(table_group, color, content_w, seg_lines);
            table_group.clear();
        };

        for (auto& md_line : md_lines) {
            if (md_line.is_table) {
                table_group.push_back(md_line);
                continue;
            }
            flush_table();

            // Empty line
            if (md_line.segs.empty() && !md_line.is_code_block && !md_line.is_blockquote) {
                SegLine sl;
                sl.segs.push_back({-1, A_NORMAL, ""});
                seg_lines.push_back(sl);
                continue;
            }

            // Code block
            if (md_line.is_code_block) {
                auto code_lines = wrap_text(md_line.segs[0].text, content_w);
                if (code_lines.empty()) code_lines = {" "};
                for (size_t ci = 0; ci < code_lines.size(); ci++) {
                    SegLine sl;
                    sl.segs.push_back({color, A_DIM, " \xe2\x96\x8c " + code_lines[ci]}); // " ▌ "
                    seg_lines.push_back(sl);
                }
                continue;
            }

            // HR
            if (!md_line.segs.empty() && md_line.segs[0].type == MdSeg::HR) {
                std::string hr = "  ";
                for (int i = 0; i < content_w; i++) hr += '-';
                SegLine sl;
                sl.is_sep = true;
                sl.segs.push_back({color, A_BOLD, hr});
                seg_lines.push_back(sl);
                continue;
            }

            // Regular line with segments
            SegLine sl;
            int x_pos = 0;
            for (auto& seg : md_line.segs) {
                RenderSeg rs;
                rs.color = color;
                rs.attr = A_NORMAL;

                if (seg.type == MdSeg::BOLD || seg.type == MdSeg::HEADING) {
                    rs.attr = A_BOLD;
                } else if (seg.type == MdSeg::ITALIC) {
                    rs.attr = A_ITALIC;
                } else if (seg.type == MdSeg::STRIKETHROUGH) {
                    rs.attr = A_DIM;
                } else if (seg.type == MdSeg::CODE) {
                    rs.attr = A_DIM;
                } else if (seg.type == MdSeg::MATH) {
                    rs.attr = A_DIM;
                } else if (seg.type == MdSeg::DISPLAY_MATH) {
                    rs.attr = A_DIM | A_BOLD;
                } else if (seg.type == MdSeg::BLOCKQUOTE) {
                    rs.attr = A_DIM;
                    seg.text = "\xe2\x96\x8d " + seg.text; // "▍ "
                } else if (seg.type == MdSeg::LINK) {
                    rs.attr = A_UNDERLINE;
                } else if (seg.type == MdSeg::LIST_ITEM) {
                    seg.text = "\xe2\x80\xa2 "; // bullet • 
                } else if (seg.type == MdSeg::TASK_DONE) {
                    rs.attr = A_BOLD;
                    seg.text = "\xe2\x9c\x93 "; // checkmark ✓
                } else if (seg.type == MdSeg::TASK_PENDING) {
                    seg.text = "\xe2\x97\x8b "; // circle ○
                }

                // If it's a heading, prefix with #
                std::string prefix;
                if (seg.type == MdSeg::HEADING) {
                    prefix = std::string(seg.level, '#') + " ";
                }

                rs.text = prefix + seg.text;

                // Ensure total line doesn't exceed content_w
                int seg_w = str_width(rs.text);
                if (x_pos + seg_w > content_w) {
                    rs.text = rs.text.substr(0, content_w - x_pos);
                }
                x_pos += str_width(rs.text);

                if (!rs.text.empty())
                    sl.segs.push_back(rs);
            }
            if (sl.segs.empty()) {
                sl.segs.push_back({-1, A_NORMAL, ""});
            } else {
                // Prefix first segment with bar
                auto& first = sl.segs.front();
                first.text = " \xe2\x96\x8c " + first.text;
            }
            seg_lines.push_back(sl);
        }
        flush_table();

        // Trailing blank line
        SegLine blank;
        blank.segs.push_back({-1, A_NORMAL, ""});
        seg_lines.push_back(blank);
    }

    return seg_lines;
}

// -------------------------------------------------------------------
// Renderer implementation
// -------------------------------------------------------------------

void Renderer::set_windows(WINDOW* chat_win, WINDOW* input_win, WINDOW* status_win) {
    chat_win_ = chat_win;
    input_win_ = input_win;
    status_win_ = status_win;
}

void Renderer::init_pair_safe(int pair, int fg, int bg) {
    if (fg < 0 && bg < 0) init_pair(pair, -1, -1);
    else if (bg < 0) init_pair(pair, fg, -1);
    else if (fg < 0) init_pair(pair, -1, bg);
    else init_pair(pair, fg, bg);
}

void Renderer::ncurses_color_to_rgb(int c, float& r, float& g, float& b) {
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

void Renderer::init_pairs(const ThemeColors& colors) {
    init_pair_safe(1, colors.user_fg, colors.user_bg);
    init_pair_safe(2, colors.assistant_fg, colors.assistant_bg);
    init_pair_safe(3, colors.system_fg, colors.system_bg);
    init_pair_safe(4, colors.tool_fg, colors.tool_bg);
    init_pair_safe(5, colors.error_fg, colors.error_bg);
    init_pair_safe(6, colors.status_fg, colors.status_bg);
    init_pair_safe(7, colors.separator_fg, colors.separator_bg);
    init_pair_safe(8, colors.input_fg, colors.input_bg);
}

void Renderer::init_animation_colors(bool can_change) {
    if (can_change) {
        init_color(16, 1000, 0, 0);
        init_pair(9, 16, -1);
    } else {
        init_pair_safe(9, COLOR_YELLOW, -1);
    }

    if (can_change) {
        init_color(17, 500, 0, 0);
        init_pair(10, 17, -1);
        init_color(18, 0, 500, 0);
        init_pair(11, 18, -1);
        init_color(19, 0, 0, 500);
        init_pair(12, 19, -1);
        init_color(20, 500, 500, 0);
        init_pair(13, 20, -1);
        init_color(21, 500, 0, 500);
        init_pair(14, 21, -1);
        init_color(22, 0, 500, 500);
        init_pair(15, 22, -1);
    } else {
        init_pair_safe(10, COLOR_GREEN, -1);
        init_pair_safe(11, COLOR_RED, -1);
        init_pair_safe(12, COLOR_CYAN, -1);
        init_pair_safe(13, COLOR_MAGENTA, -1);
        init_pair_safe(14, COLOR_BLUE, -1);
        init_pair_safe(15, COLOR_WHITE, -1);
    }
}

int Renderer::compute_total_box_lines(Conversation* conv, int max_x) {
    auto sl = build_seg_lines(conv, max_x, false, 0);
    return sl.size();
}

void Renderer::draw_tamagotchi(WINDOW* win, int mood, int anim_frame, int max_x) {
    std::string face;
    switch (mood) {
        case TAMAGOTCHI_HAPPY:
            face = (anim_frame % 16 < 8)
                ? "\xe2\x97\x95\xe2\x80\xbf\xe2\x97\x95"   // ◕‿◕
                : "\xe2\x97\x95\xcb\x98\xe2\x97\x95";       // ◕˘◕ (breathing)
            break;
        case TAMAGOTCHI_NEUTRAL:
            face = "\xe2\x97\x95_\xe2\x97\x95"; break;      // ◕_◕
        case TAMAGOTCHI_SAD:
        default:
            face = "\xe2\x97\x95\xef\xb9\x8f\xe2\x97\x95"; break; // ◕﹏◕
    }

    int face_w = str_width(face);
    int x = max_x - face_w - 2;
    if (x < 0) x = 0;
    wattron(win, COLOR_PAIR(6) | A_BOLD);
    mvwaddstr(win, 0, x, face.c_str());
    wattroff(win, COLOR_PAIR(6) | A_BOLD);
}

void Renderer::draw_chat(Conversation* conv, int scroll_offset, bool is_streaming, int anim_color_idx, int tamagotchi_mood, int anim_frame) {
    werase(chat_win_);
    int max_x = getmaxx(chat_win_);
    int max_y = getmaxy(chat_win_);
    if (max_x < 1 || max_y < 1) return;

    draw_tamagotchi(chat_win_, tamagotchi_mood, anim_frame, max_x);

    auto seg_lines = build_seg_lines(conv, max_x, is_streaming, anim_color_idx);

    int total_lines = seg_lines.size();
    int start_line = scroll_offset;

    for (int i = start_line; i < total_lines && (i - start_line) < max_y; i++) {
        int y = i - start_line;
        auto& sl = seg_lines[i];
        int x = 0;

        for (auto& seg : sl.segs) {
            if (seg.color > 0) {
                wattron(chat_win_, COLOR_PAIR(seg.color));
            }
            if (seg.attr != A_NORMAL) {
                wattron(chat_win_, seg.attr);
            }

            int max_w = max_x - x - 1;
            if (max_w < 0) max_w = 0;
            if (!seg.text.empty()) {
                mvwaddnstr(chat_win_, y, x, seg.text.c_str(), max_w);
                x += str_width(seg.text);
                if (x > max_x - 1) x = max_x - 1;
            }

            if (seg.attr != A_NORMAL) {
                wattroff(chat_win_, seg.attr);
            }
            if (seg.color > 0) {
                wattroff(chat_win_, COLOR_PAIR(seg.color));
            }
        }
    }

    // Scroll indicators
    if (scroll_offset > 0) {
        mvwaddch(chat_win_, 0, max_x - 2, ACS_UARROW);
    }
    if (total_lines > max_y && scroll_offset < total_lines - max_y) {
        mvwaddch(chat_win_, max_y - 1, max_x - 2, ACS_DARROW);
    }
    // Scrollbar track and thumb
    if (total_lines > max_y && max_y > 2) {
        // Draw track
        for (int ty = 0; ty < max_y; ty++) {
            mvwaddch(chat_win_, ty, max_x - 1, ACS_VLINE | A_DIM);
        }
        // Draw thumb
        float pct = (float)scroll_offset / (float)(total_lines - max_y);
        int thumb_h = std::max(1, max_y * max_y / total_lines);
        int thumb_y = (int)(pct * (max_y - thumb_h));
        if (thumb_y + thumb_h > max_y) thumb_y = max_y - thumb_h;
        for (int ty = 0; ty < thumb_h; ty++) {
            wattron(chat_win_, COLOR_PAIR(6) | A_BOLD);
            mvwaddch(chat_win_, thumb_y + ty, max_x - 1, ' ');
            wattroff(chat_win_, COLOR_PAIR(6) | A_BOLD);
        }
    }

    wnoutrefresh(chat_win_);
}

void Renderer::draw_input(const std::string& input_buf, int cursor_pos, bool is_processing, int anim_color_idx, int anim_frame, int msg_count) {
    werase(input_win_);
    int max_x = getmaxx(input_win_);

    wattron(input_win_, COLOR_PAIR(8) | A_BOLD);
    mvwaddstr(input_win_, 0, 0, "> ");
    wattroff(input_win_, COLOR_PAIR(8) | A_BOLD);

    if (!input_buf.empty()) {
        wattron(input_win_, COLOR_PAIR(8));
        mvwaddnstr(input_win_, 0, 2, input_buf.c_str(), max_x - 3);
        wattroff(input_win_, COLOR_PAIR(8));
    }

    // Message counter on the right
    if (msg_count > 0) {
        std::string cnt = "[msg #" + std::to_string(msg_count) + "]";
        int cnt_w = str_width(cnt);
        if (cnt_w < max_x - 5) {
            wattron(input_win_, COLOR_PAIR(7) | A_DIM);
            mvwaddstr(input_win_, 0, max_x - cnt_w, cnt.c_str());
            wattroff(input_win_, COLOR_PAIR(7) | A_DIM);
        }
    }

    int cursor_display = std::min(cursor_pos, max_x - 3);
    wmove(input_win_, 0, 2 + cursor_display);

    if (is_processing) {
        static const char* dots[] = { "...", ".. ", ".  ", "   " };
        int idx = (anim_frame / 3) % 4;
        std::string gen = "generating";
        gen += dots[idx];
        wattron(input_win_, COLOR_PAIR(anim_color_idx) | A_BOLD);
        mvwaddstr(input_win_, 1, 0, gen.c_str());
        wattroff(input_win_, COLOR_PAIR(anim_color_idx) | A_BOLD);
    }

    wnoutrefresh(input_win_);
}

void Renderer::draw_status(bool is_processing, bool use_casino_status, const std::string& casino_frame,
                           const std::string& model_name, const std::string& status_text) {
    werase(status_win_);
    int max_x = getmaxx(status_win_);

    std::string left;
    if (is_processing) {
        if (use_casino_status) {
            left = casino_frame;
        } else {
            left = "\xe2\x97\x8f"; // ●
            if (!model_name.empty()) left += " " + model_name;
            left += " generating...";
        }
    } else {
        if (!model_name.empty()) {
            left = "\xe2\x97\x8f " + model_name; // ● model
        }
    }

    if (!status_text.empty()) {
        if (!left.empty()) left += "  " + status_text;
        else left = status_text;
    }

    std::string hints = "^C cancel  ^L clear  ^Y copy";

    wattron(status_win_, COLOR_PAIR(6) | A_BOLD);
    mvwaddnstr(status_win_, 0, 0, left.c_str(), max_x);
    wattroff(status_win_, COLOR_PAIR(6) | A_BOLD);

    int hints_len = str_width(hints);
    if (hints_len < max_x) {
        wattron(status_win_, COLOR_PAIR(6));
        mvwaddnstr(status_win_, 0, max_x - hints_len, hints.c_str(), hints_len);
        wattroff(status_win_, COLOR_PAIR(6));
    }

    wnoutrefresh(status_win_);
}
