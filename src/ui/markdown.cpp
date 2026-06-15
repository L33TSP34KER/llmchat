#include "markdown.h"
#include "text_util.h"
#include <algorithm>
#include <cctype>
#include <sstream>

// -------------------------------------------------------------------
// Inline parser — breaks a single line into segments
// -------------------------------------------------------------------
static std::vector<MdSeg> parse_inline(const std::string& line, bool in_list = false) {
    std::vector<MdSeg> out;
    std::string buf;
    size_t i = 0;

    auto flush = [&](MdSeg::Type t = MdSeg::NORMAL, int lvl = 0) {
        if (!buf.empty()) {
            out.push_back({t, buf, lvl});
            buf.clear();
        }
    };

    auto peek = [&](size_t off) -> char {
        size_t p = i + off;
        return p < line.size() ? line[p] : '\0';
    };

    while (i < line.size()) {
        // ~~strikethrough~~
        if (line[i] == '~' && peek(1) == '~') {
            flush();
            size_t end = line.find("~~", i + 2);
            if (end != std::string::npos) {
                buf = line.substr(i + 2, end - i - 2);
                flush(MdSeg::STRIKETHROUGH);
                i = end + 2;
                continue;
            }
        }
        // **bold**
        if (line[i] == '*' && peek(1) == '*') {
            flush();
            size_t end = line.find("**", i + 2);
            if (end != std::string::npos) {
                buf = line.substr(i + 2, end - i - 2);
                flush(MdSeg::BOLD);
                i = end + 2;
                continue;
            }
        }
        // *italic* (must not be **)
        if (line[i] == '*' && peek(1) != '*') {
            flush();
            size_t end = line.find("*", i + 1);
            if (end != std::string::npos && (end + 1 >= line.size() || line[end + 1] != '*')) {
                buf = line.substr(i + 1, end - i - 1);
                flush(MdSeg::ITALIC);
                i = end + 1;
                continue;
            }
        }
        // `code`
        if (line[i] == '`') {
            flush();
            size_t end = line.find("`", i + 1);
            if (end != std::string::npos) {
                buf = line.substr(i + 1, end - i - 1);
                flush(MdSeg::CODE);
                i = end + 1;
                continue;
            }
        }
        buf += line[i];
        i++;
    }
    flush();
    return out;
}

// Check if a line is a list item (-, *, +, or 1. style)
static int detect_list_item(const std::string& line, bool& is_task, bool& task_done) {
    is_task = false;
    task_done = false;
    size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos) return 0;

    // Bullet: - * +
    if (line[start] == '-' || line[start] == '*' || line[start] == '+') {
        if (start + 1 < line.size() && line[start + 1] == ' ') {
            // Check for task list: - [ ] or - [x]
            size_t t = start + 2;
            if (t + 4 < line.size() && line[t] == '[' && line[t + 2] == ']' && line[t + 3] == ' ') {
                is_task = true;
                task_done = (line[t + 1] == 'x' || line[t + 1] == 'X');
                return 1;
            }
            return 1;
        }
    }

    // Numbered: 1. 2. etc.
    size_t p = start;
    while (p < line.size() && std::isdigit((unsigned char)line[p])) p++;
    if (p > start && p < line.size() && line[p] == '.' && p + 1 < line.size() && line[p + 1] == ' ') {
        return 2;
    }

    return 0;
}

// -------------------------------------------------------------------
// Public inline parser wrapper
// -------------------------------------------------------------------
std::vector<MdSeg> md_parse_inline(const std::string& line) {
    return parse_inline(line);
}

// -------------------------------------------------------------------
// Main parser
// -------------------------------------------------------------------
std::vector<MdLine> md_parse(const std::string& text, int width) {
    std::vector<MdLine> out;
    if (width < 4) width = 4;

    std::istringstream stream(text);
    std::string raw_line;
    bool in_code_block = false;
    std::string code_buf;

    while (std::getline(stream, raw_line)) {
        if (!raw_line.empty() && raw_line.back() == '\r')
            raw_line.pop_back();

        // -------------------------------------------------------------------
        // Code fences ``` or ~~~
        // -------------------------------------------------------------------
        if (raw_line.size() >= 3 && raw_line.substr(0, 3) == "```") {
            if (!in_code_block) {
                in_code_block = true;
                code_buf.clear();
                continue;
            } else {
                in_code_block = false;
                MdLine ml;
                ml.is_code_block = true;
                ml.segs.push_back({MdSeg::CODE, code_buf, 0});
                out.push_back(ml);
                continue;
            }
        }
        if (in_code_block) {
            if (!code_buf.empty()) code_buf += "\n";
            code_buf += raw_line;
            continue;
        }

        // -------------------------------------------------------------------
        // Empty line
        // -------------------------------------------------------------------
        if (raw_line.empty()) {
            out.push_back({});
            continue;
        }

        // -------------------------------------------------------------------
        // Horizontal rule --- or ***
        // -------------------------------------------------------------------
        {
            std::string trimmed = raw_line;
            trimmed.erase(0, trimmed.find_first_not_of(" \t"));
            trimmed.erase(trimmed.find_last_not_of(" \t") + 1);
            bool is_hr = true;
            int dash_or_star = 0;
            for (char c : trimmed) {
                if (c == '-') dash_or_star++;
                else if (c == '*') dash_or_star++;
                else if (c == ' ') continue;
                else { is_hr = false; break; }
            }
            if (is_hr && dash_or_star >= 3) {
                { MdLine ml; ml.segs.push_back({MdSeg::HR, "", 0}); out.push_back(ml); }
                continue;
            }
        }

        // -------------------------------------------------------------------
        // Table detection: line starts and ends with |
        // -------------------------------------------------------------------
        std::string trimmed = raw_line;
        trimmed.erase(0, trimmed.find_first_not_of(" \t"));
        trimmed.erase(trimmed.find_last_not_of(" \t") + 1);
        if (trimmed.size() >= 2 && trimmed.front() == '|' && trimmed.back() == '|') {
            MdLine ml;
            ml.is_table = true;
            bool is_sep = true;
            std::string inner = trimmed.substr(1, trimmed.size() - 2);
            std::string cell;
            std::vector<std::string> cells;
            for (char c : inner) {
                if (c == '|') { cells.push_back(cell); cell.clear(); }
                else cell += c;
            }
            cells.push_back(cell);
            for (auto& c : cells) {
                std::string t = c;
                t.erase(0, t.find_first_not_of(" \t"));
                t.erase(t.find_last_not_of(" \t") + 1);
                for (char cc : t)
                    if (cc != '-' && cc != ':' && cc != ' ') is_sep = false;
                if (!t.empty() && t.find_first_not_of("-: ") != std::string::npos)
                    is_sep = false;
            }
            ml.segs.push_back({is_sep ? MdSeg::TABLE_SEP : MdSeg::TABLE_ROW, trimmed, 0});
            out.push_back(ml);
            continue;
        }

        // -------------------------------------------------------------------
        // Blockquote >
        // -------------------------------------------------------------------
        size_t start = raw_line.find_first_not_of(" \t");
        if (start != std::string::npos && raw_line[start] == '>') {
            std::string content = raw_line.substr(start + 1);
            content.erase(0, content.find_first_not_of(" \t"));
            MdLine ml;
            ml.is_blockquote = true;
            ml.segs = parse_inline(content);
            out.push_back(ml);
            continue;
        }

        // -------------------------------------------------------------------
        // Heading # ## ### #### ##### ######
        // -------------------------------------------------------------------
        start = raw_line.find_first_not_of(" \t");
        if (start != std::string::npos) {
            int level = 0;
            size_t p = start;
            while (p < raw_line.size() && raw_line[p] == '#') { level++; p++; }
            if (level >= 1 && level <= 6 && p < raw_line.size() && raw_line[p] == ' ') {
                std::string content = raw_line.substr(p + 1);
                auto segs = parse_inline(content);
                MdLine ml;
                for (auto& s : segs) {
                    s.type = MdSeg::HEADING;
                    s.level = level;
                    ml.segs.push_back(s);
                }
                out.push_back(ml);
                continue;
            }
        }

        // -------------------------------------------------------------------
        // List item / Task list
        // -------------------------------------------------------------------
        {
            bool is_task = false;
            bool task_done = false;
            int list_type = detect_list_item(raw_line, is_task, task_done);
            if (list_type > 0) {
                // Determine content start
                size_t content_start = raw_line.find_first_not_of(" \t");
                if (is_task) {
                    content_start += 6; // skip "- [ ] " or "- [x] "
                } else {
                    // skip bullet or number marker
                    if (list_type == 1) content_start += 2; // "- " or "* "
                    else {
                        content_start = raw_line.find('.', content_start) + 2; // "1. "
                    }
                }
                std::string content = raw_line.substr(content_start);
                auto segs = parse_inline(content);
                MdLine ml;
                if (is_task) {
                    MdSeg marker;
                    marker.type = task_done ? MdSeg::TASK_DONE : MdSeg::TASK_PENDING;
                    marker.text = task_done ? "[x] " : "[ ] ";
                    ml.segs.push_back(marker);
                } else {
                    MdSeg marker;
                    marker.type = MdSeg::LIST_ITEM;
                    marker.text = "  ";
                    ml.segs.push_back(marker);
                }
                for (auto& s : segs) ml.segs.push_back(s);
                out.push_back(ml);
                continue;
            }
        }

        // -------------------------------------------------------------------
        // Regular line with inline parsing + word wrapping
        // -------------------------------------------------------------------
        auto segs = parse_inline(raw_line);
        int col = 0;
        MdLine current;
        for (auto& seg : segs) {
            std::string word;
            for (size_t ci = 0; ci < seg.text.size();) {
                size_t len = utf8_char_len((unsigned char)seg.text[ci]);
                word = seg.text.substr(ci, len);
                ci += len;

                if (col + str_width(word) > width && col > 0) {
                    out.push_back(current);
                    current = MdLine();
                    col = 0;
                }
                if (col == 0 && word == " ") continue;
                current.segs.push_back({seg.type, word, seg.level});
                col += str_width(word);
            }
        }
        if (!current.segs.empty())
            out.push_back(current);
        else
            out.push_back({});
    }

    if (in_code_block) {
        { MdLine ml; ml.segs.push_back({MdSeg::CODE, code_buf, 0}); out.push_back(ml); }
    }

    return out;
}
