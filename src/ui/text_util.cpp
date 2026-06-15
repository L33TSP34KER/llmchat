#include "text_util.h"

bool is_utf8_cont(char c) {
    return (c & 0xC0) == 0x80;
}

size_t utf8_char_len(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

size_t utf8_prev_start(const std::string& s, size_t pos) {
    while (pos > 0 && is_utf8_cont(s[pos])) pos--;
    return pos;
}

size_t utf8_advance(const std::string& s, size_t pos, int n) {
    for (int i = 0; i < n && pos < s.size(); i++) {
        pos += utf8_char_len((unsigned char)s[pos]);
    }
    return pos;
}

int str_width(const std::string& s) {
    int w = 0;
    for (size_t i = 0; i < s.size(); w++)
        i += utf8_char_len((unsigned char)s[i]);
    return w;
}

std::vector<std::string> wrap_text(const std::string& text, int width) {
    std::vector<std::string> lines;
    if (width < 4) width = 4;

    size_t start = 0;
    while (start < text.size()) {
        if (text[start] == '\n') {
            lines.push_back("");
            start++;
            continue;
        }

        size_t end = utf8_advance(text, start, width);
        if (end >= text.size()) {
            lines.push_back(text.substr(start));
            break;
        }

        size_t break_newline = text.find('\n', start);
        if (break_newline != std::string::npos && break_newline < end) {
            lines.push_back(text.substr(start, break_newline - start));
            start = break_newline + 1;
            continue;
        }

        size_t break_at = std::string::npos;
        for (size_t i = end; i > start; i--) {
            if (text[i] == ' ' && !is_utf8_cont(text[i])) {
                break_at = i;
                break;
            }
        }

        if (break_at != std::string::npos && break_at > start) {
            lines.push_back(text.substr(start, break_at - start));
            start = break_at + 1;
        } else {
            end = utf8_prev_start(text, end);
            if (end <= start) end = utf8_advance(text, start, 1);
            lines.push_back(text.substr(start, end - start));
            start = end;
        }
    }

    if (lines.empty()) lines.push_back("");
    return lines;
}
