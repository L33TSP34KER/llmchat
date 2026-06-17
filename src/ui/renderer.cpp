#include "renderer.h"
#include "text_util.h"
#include "markdown.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <ctime>
#include <unistd.h>

// -------------------------------------------------------------------
// Inline image rendering
// -------------------------------------------------------------------
#include <sys/wait.h>
#include <cstdio>
#include <map>
#include <mutex>

static std::vector<std::string> render_inline_image(const std::string& url, int max_w) {
    static std::map<std::string, std::vector<std::string>> cache;
    static std::mutex cache_mtx;

    {
        std::lock_guard<std::mutex> lock(cache_mtx);
        auto it = cache.find(url);
        if (it != cache.end()) return it->second;
    }

    // Download to temp file
    char tmp_path[] = "/tmp/llmchat_img_XXXXXX";
    int fd = mkstemp(tmp_path);
    if (fd < 0) return {};

    std::string cmd = "curl -s -L -o " + std::string(tmp_path) + " '" + url + "' 2>/dev/null";
    int rc = system(cmd.c_str());
    if (rc != 0) { close(fd); unlink(tmp_path); return {}; }
    close(fd);

    // Render with chafa
    std::string chafa_cmd = "chafa --symbols block --color-space hsi -s " +
        std::to_string(std::min(max_w, 60)) + "x20 " + std::string(tmp_path) + " 2>/dev/null";

    std::vector<std::string> lines;
    FILE* fp = popen(chafa_cmd.c_str(), "r");
    if (fp) {
        char line[1024];
        while (fgets(line, sizeof(line), fp)) {
            std::string s = line;
            if (!s.empty() && s.back() == '\n') s.pop_back();
            if (!s.empty()) lines.push_back(s);
        }
        pclose(fp);
    }

    unlink(tmp_path);

    if (lines.empty()) {
        lines.push_back("[image: " + url + "]");
    }

    {
        std::lock_guard<std::mutex> lock(cache_mtx);
        cache[url] = lines;
    }

    return lines;
}

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

static void render_table_group(const std::vector<MdLine>& tbl, int color, int content_w,
                                std::vector<SegLine>& out,
                                const std::string& gutter = "  ") {
    if (tbl.empty()) return;

    // Parse all rows
    std::vector<TableRow> rows;
    for (auto& ml : tbl) {
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
        border = gutter + border;
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
        line = gutter + line;
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
                                             bool is_streaming, int anim_color_idx,
                                             std::vector<int>* line_to_entry = nullptr) {
    int content_w = max_x - 4;
    if (content_w < 4) content_w = 4;

    auto entries = conv->get_entries();
    std::vector<SegLine> seg_lines;
    if (line_to_entry) line_to_entry->clear();
    int current_entry = -1;
    auto add_line = [&](SegLine sl) {
        if (line_to_entry) line_to_entry->push_back(current_entry);
        seg_lines.push_back(std::move(sl));
    };

    if (entries.empty()) {
        {
            SegLine sl;
            sl.segs.push_back({8, A_NORMAL, "  \xe2\x94\x80\xe2\x94\x80 llmchat \xe2\x94\x80\xe2\x94\x80"});
            add_line(sl);
        }
        {
            SegLine sl;
            sl.segs.push_back({8, A_NORMAL, "  terminal AI chat client"});
            add_line(sl);
        }
        {
            SegLine sl;
            sl.segs.push_back({8, A_NORMAL, ""});
            add_line(sl);
        }
        {
            SegLine sl;
            sl.segs.push_back({8, A_NORMAL, "  Type a message to begin"});
            add_line(sl);
        }
    }

    int prev_type = -1;

    for (size_t ei = 0; ei < entries.size(); ei++) {
        auto& entry = entries[ei];
        current_entry = (int)ei;
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
                label = "." + entry.tool_name;
                color = 7;
                break;
            case ConversationEntry::TOOL_RESULT:
                continue;
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

        // Role label header (or grouping connector if same consecutive role)
        bool grouped = same_role;
        if (!grouped) {
            std::string header;
            int hdr_color = color;
            int hdr_attr = A_BOLD;
            if (entry.type == ConversationEntry::TOOL_CALL) {
                header = "  " + label;
                hdr_attr = A_DIM;
                hdr_color = 7;
            } else {
                std::string ts = format_timestamp(entry.timestamp);
                header = "  \xe2\x94\x80\xe2\x94\x80 " + label + " \xe2\x94\x80\xe2\x94\x80";
                if (!ts.empty()) {
                    header += " " + ts;
                }
            }
            SegLine sl;
            sl.segs.push_back({hdr_color, hdr_attr, header});
            add_line(sl);
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

        // Gutter prefix for grouped messages
        std::string gutter;
        std::string gutter_blank;
        if (grouped) {
            gutter = "  \xc2\xb7 ";        // "  · "
            gutter_blank = "    ";
        } else {
            gutter = "  ";
            gutter_blank = "  ";
        }

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
                add_line(sl);
                continue;
            }

            // Code block with syntax highlighting
            if (md_line.is_code_block) {
                // Language tag line
                if (!md_line.code_lang.empty()) {
                    SegLine lsl;
                    lsl.segs.push_back({7, A_DIM, gutter + "[" + md_line.code_lang + "]"});
                    add_line(lsl);
                }
                // Syntax-highlighted code
                auto hl_segs = md_syntax_highlight(md_line.segs[0].text, md_line.code_lang);
                struct CodeSeg { int color; int attr; std::string text; };
                std::vector<std::vector<CodeSeg>> code_out_lines;
                auto flush_code_out = [&]() {
                    if (!code_out_lines.empty()) {
                        bool first = true;
                        for (auto& out_line : code_out_lines) {
                            SegLine sl;
                            sl.segs.push_back({7, A_DIM, (first ? gutter : gutter_blank) + "\xe2\x94\x82 "});
                            for (auto& cs : out_line) {
                                RenderSeg rs;
                                rs.color = cs.color;
                                rs.attr = cs.attr;
                                rs.text = cs.text;
                                sl.segs.push_back(rs);
                            }
                            add_line(sl);
                        }
                        code_out_lines.clear();
                    }
                };
                for (auto& seg : hl_segs) {
                    int syntax_color = 7;
                    int syntax_attr = A_NORMAL;
                    switch (seg.type) {
                        case MdSeg::SYNTAX_KEYWORD:   syntax_color = 9;  syntax_attr = A_BOLD; break;
                        case MdSeg::SYNTAX_STRING:    syntax_color = 10; break;
                        case MdSeg::SYNTAX_COMMENT:   syntax_color = 7;  syntax_attr = A_DIM; break;
                        case MdSeg::SYNTAX_NUMBER:    syntax_color = 7;  syntax_attr = A_BOLD; break;
                        case MdSeg::SYNTAX_BUILTIN:   syntax_color = 10; syntax_attr = A_BOLD; break;
                        case MdSeg::SYNTAX_PREPROC:   syntax_color = 9;  syntax_attr = A_DIM; break;
                        case MdSeg::SYNTAX_OPERATOR:  syntax_color = 7;  break;
                        case MdSeg::SYNTAX_FUNCTION:  syntax_color = 11; syntax_attr = A_BOLD; break;
                        case MdSeg::SYNTAX_TYPE:      syntax_color = 10; syntax_attr = A_BOLD; break;
                        case MdSeg::SYNTAX_ATTRIBUTE: syntax_color = 9;  break;
                        default: break;
                    }
                    std::string text = seg.text;
                    size_t start = 0;
                    while (start < text.size()) {
                        size_t nl = text.find('\n', start);
                        std::string piece = (nl == std::string::npos) ? text.substr(start) : text.substr(start, nl - start);
                        if (piece.empty() && nl != std::string::npos) {
                            flush_code_out();
                            code_out_lines.push_back({});
                            flush_code_out();
                        } else if (!piece.empty()) {
                            if (code_out_lines.empty()) code_out_lines.push_back({});
                            code_out_lines.back().push_back({syntax_color, syntax_attr, piece});
                        }
                        if (nl == std::string::npos) break;
                        start = nl + 1;
                    }
                }
                flush_code_out();
                continue;
            }

            // HR
            if (!md_line.segs.empty() && md_line.segs[0].type == MdSeg::HR) {
                std::string hr = "  ";
                for (int i = 0; i < content_w; i++) hr += "\xe2\x94\x80";
                SegLine sl;
                sl.is_sep = true;
                sl.segs.push_back({7, A_DIM, hr});
                add_line(sl);
                continue;
            }

            // Image: render inline if possible
            if (!md_line.segs.empty() && md_line.segs[0].type == MdSeg::IMAGE) {
                std::string text = md_line.segs[0].text;
                size_t nl = text.find('\n');
                std::string url = (nl == std::string::npos) ? text : text.substr(nl + 1);
                auto img_lines = render_inline_image(url, content_w);
                for (auto& il : img_lines) {
                    SegLine sl;
                    sl.segs.push_back({color, A_NORMAL, (grouped ? gutter_blank : "  ") + il});
                    add_line(sl);
                }
                continue;
            }

            // Regular line with segments
            SegLine sl;
            int x_pos = 0;
            for (auto& seg : md_line.segs) {
                RenderSeg rs;
                rs.color = color;
                rs.attr = A_NORMAL;

                if (seg.type == MdSeg::BOLD) {
                    rs.attr = A_BOLD;
                } else if (seg.type == MdSeg::HEADING) {
                    rs.color = 7;
                    rs.attr = A_BOLD;
                } else if (seg.type == MdSeg::ITALIC) {
                    rs.attr = A_ITALIC;
                } else if (seg.type == MdSeg::STRIKETHROUGH) {
                    rs.attr = A_DIM;
                } else if (seg.type == MdSeg::CODE) {
                    rs.color = 4;
                    rs.attr = A_BOLD;
                } else if (seg.type == MdSeg::MATH) {
                    rs.color = 3;
                    rs.attr = A_BOLD;
                } else if (seg.type == MdSeg::DISPLAY_MATH) {
                    rs.color = 3;
                    rs.attr = A_BOLD;
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

                rs.text = seg.text;

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
                auto& first = sl.segs.front();
                first.text = gutter + first.text;
            }
            add_line(sl);

            // For h1, add underline below
            bool all_heading = !md_line.segs.empty();
            int heading_level = 0;
            for (auto& s : md_line.segs) {
                if (s.type == MdSeg::HEADING) {
                    if (heading_level == 0 || s.level < heading_level)
                        heading_level = s.level;
                } else {
                    all_heading = false;
                    break;
                }
            }
            if (all_heading && heading_level == 1) {
                std::string ul;
                for (int i = 0; i < content_w; i++) ul += "\xe2\x94\x80";
                SegLine ul_sl;
                ul_sl.segs.push_back({7, A_DIM, gutter_blank + ul});
                add_line(ul_sl);
            }
        }
        flush_table();

        // Trailing separator
        SegLine sep;
        sep.segs.push_back({7, A_DIM, "  \xc2\xb7 \xc2\xb7 \xc2\xb7"});
        add_line(sep);
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
    auto sl = build_seg_lines(conv, max_x, false, 0, nullptr);
    return sl.size();
}

int Renderer::entry_at_y(int screen_y, int scroll_offset) const {
    int total = (int)line_to_entry_.size();
    int line = screen_y + scroll_offset;
    if (line < 0 || line >= total) return -1;
    return line_to_entry_[line];
}

void Renderer::draw_chat(Conversation* conv, int scroll_offset, bool is_streaming) {
    werase(chat_win_);
    int max_x = getmaxx(chat_win_);
    int max_y = getmaxy(chat_win_);
    if (max_x < 1 || max_y < 1) return;

    int anim_color_idx = 0; // streaming uses regular color (pair 2)
    auto seg_lines = build_seg_lines(conv, max_x, is_streaming, anim_color_idx, &line_to_entry_);

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

    // Scroll indicators only (no scrollbar track/thumb)
    if (scroll_offset > 0 && max_x > 1) {
        mvwaddch(chat_win_, 0, max_x - 1, ACS_UARROW);
    }
    if (total_lines > max_y && scroll_offset < total_lines - max_y && max_x > 1) {
        mvwaddch(chat_win_, max_y - 1, max_x - 1, ACS_DARROW);
    }

    wnoutrefresh(chat_win_);
}

void Renderer::draw_input(const std::string& input_buf, int cursor_pos, int msg_count) {
    werase(input_win_);
    int max_x = getmaxx(input_win_);
    int max_y = getmaxy(input_win_);
    int avail = max_x - 3;
    if (avail < 1) avail = 1;

    // Word-wrap input buffer into visual-width-aware lines
    std::vector<std::string> lines;
    int cursor_line = 0;
    int cursor_col = 0;
    bool cursor_found = false;

    size_t bp = 0;
    while (bp < input_buf.size()) {
        size_t line_start = bp;
        int col = 0;
        size_t last_space = std::string::npos;
        int last_space_col = 0;

        while (bp < input_buf.size()) {
            size_t len = utf8_char_len((unsigned char)input_buf[bp]);
            std::string ch = input_buf.substr(bp, len);
            int w = str_width(ch);

            if (ch == "\n") {
                bp += len;
                break;
            }

            if (col + w > avail) {
                if (last_space != std::string::npos) {
                    bp = last_space;
                    col = last_space_col;
                } else if (col == 0) {
                    col += w;
                    bp += len;
                }
                break;
            }

            if (ch == " ") {
                last_space = bp + len;
                last_space_col = col + w;
            }

            col += w;
            bp += len;
        }

        if (!cursor_found) {
            int cursor_in_line = cursor_pos - (int)line_start;
            if (cursor_in_line >= 0 && cursor_in_line <= (int)(bp - line_start)) {
                cursor_line = (int)lines.size();
                cursor_col = str_width(input_buf.substr(line_start, cursor_in_line));
                cursor_found = true;
            }
        }

        lines.push_back(input_buf.substr(line_start, bp - line_start));
        if (bp >= input_buf.size()) break;
        if (input_buf[bp] == ' ' || input_buf[bp] == '\n') {
            if (!cursor_found && cursor_pos == (int)bp) {
                cursor_line = (int)lines.size();
                cursor_col = 0;
                cursor_found = true;
            }
            bp++;
        }
    }

    if (lines.empty()) lines.push_back("");

    if (!cursor_found) {
        cursor_line = (int)lines.size() - 1;
        cursor_col = str_width(lines.back());
    }

    // Vertical scroll to keep cursor visible
    int scroll_line = 0;
    if (cursor_line >= max_y) {
        scroll_line = cursor_line - max_y + 1;
    }

    // Draw visible wrapped lines
    for (int y = 0; y < max_y && y + scroll_line < (int)lines.size(); y++) {
        int actual_line = y + scroll_line;
        int prompt_col = 0;
        std::string text;
        if (scroll_line == 0 && actual_line == 0) {
            // First visible line: "> prompt + text"
            text = "> ";
            prompt_col = 2;
        }
        text += lines[actual_line];
        wattron(input_win_, COLOR_PAIR(8) | A_BOLD);
        mvwaddnstr(input_win_, y, prompt_col, text.c_str(), max_x - prompt_col);
        wattroff(input_win_, COLOR_PAIR(8) | A_BOLD);
    }

    // Position cursor
    int display_line = cursor_line - scroll_line;
    if (display_line >= 0 && display_line < max_y) {
        int prompt_offset = (scroll_line == 0 && cursor_line == 0) ? 2 : 0;
        wmove(input_win_, display_line, cursor_col + prompt_offset);
    }

    wnoutrefresh(input_win_);
}

void Renderer::draw_status(bool is_processing,
                           const std::string& model_name, const std::string& status_text,
                           int anim_frame, const std::string& thinking_phrase) {
    werase(status_win_);
    int max_x = getmaxx(status_win_);

    static const char* spinner_frames = "\xe2\xa0\x8b\xe2\xa0\x99\xe2\xa0\xb9\xe2\xa0\xb8\xe2\xa0\xbc\xe2\xa0\xb4\xe2\xa0\xa6\xe2\xa0\xa7\xe2\xa0\x87\xe2\xa0\x8f";
    int spin_idx = (anim_frame / 3) % 10;
    char spin_char[4] = {0};
    spin_char[0] = spinner_frames[spin_idx * 3];
    spin_char[1] = spinner_frames[spin_idx * 3 + 1];
    spin_char[2] = spinner_frames[spin_idx * 3 + 2];

    std::string left;
    if (is_processing) {
        left = spin_char;
        if (!model_name.empty()) left += " " + model_name;
        if (!thinking_phrase.empty()) {
            left += " " + thinking_phrase;
        } else {
            left += " working...";
        }
    } else {
        if (!model_name.empty()) {
            left = "\xe2\x97\x8f " + model_name;
        }
    }

    if (!status_text.empty()) {
        if (!left.empty()) left += "  " + status_text;
        else left = status_text;
    }

    wattron(status_win_, COLOR_PAIR(7));
    mvwaddnstr(status_win_, 0, 0, left.c_str(), max_x);
    wattroff(status_win_, COLOR_PAIR(7));

    wnoutrefresh(status_win_);
}

void Renderer::draw_topbar(WINDOW* top_win, const std::string& title, int thinking_tokens, int anim_frame) {
    int max_x = getmaxx(top_win);

    // Fill full bar with background color
    wattron(top_win, COLOR_PAIR(6));
    std::string bg(max_x, ' ');
    mvwaddstr(top_win, 0, 0, bg.c_str());
    wattroff(top_win, COLOR_PAIR(6));

    // Draw title on the left
    std::string display_title = title.empty() ? "llmchat" : title;
    int max_title_w = max_x - 40;
    if ((int)display_title.size() > max_title_w && max_title_w > 0) {
        display_title = display_title.substr(0, max_title_w - 1) + "\xe2\x80\xa6";
    }

    wattron(top_win, COLOR_PAIR(6) | A_BOLD);
    mvwaddnstr(top_win, 0, 2, display_title.c_str(), max_x - 4);
    wattroff(top_win, COLOR_PAIR(6) | A_BOLD);

    // Draw thinking button on the right
    std::string btn_label;
    bool is_godmod = false;
    switch (thinking_tokens) {
        case 0:    btn_label = "\xf0\x9f\xa7\xa0 None"; break;
        case 128:  btn_label = "\xf0\x9f\xa7\xa0 Minimal"; break;
        case 250:  btn_label = "\xf0\x9f\xa7\xa0 Medium"; break;
        case 512:  btn_label = "\xf0\x9f\xa7\xa0 Normal"; break;
        case 1024: btn_label = "\xf0\x9f\xa7\xa0 High"; break;
        default:   btn_label = "\xf0\x9f\xa7\xa0 GODMOD"; is_godmod = true; break;
    }

    int btn_x = max_x - (int)str_width(btn_label) - 5;

    // Rainbow animation for GODMOD
    if (is_godmod) {
        int rainbow_idx = (anim_frame / 6) % 6;
        int color_pair = 10 + rainbow_idx;
        wattron(top_win, A_BOLD);
        wmove(top_win, 0, btn_x);
        waddch(top_win, ' ');
        wattron(top_win, COLOR_PAIR(color_pair));
        waddstr(top_win, btn_label.c_str());
        wattroff(top_win, COLOR_PAIR(color_pair));
        waddch(top_win, ' ');
        wattroff(top_win, A_BOLD);
    } else {
        wmove(top_win, 0, btn_x);
        waddch(top_win, ' ');
        wattron(top_win, COLOR_PAIR(6) | A_BOLD);
        waddstr(top_win, btn_label.c_str());
        wattroff(top_win, COLOR_PAIR(6) | A_BOLD);
        waddch(top_win, ' ');
    }

    wnoutrefresh(top_win);
}
