#pragma once
#include <string>
#include <vector>

bool is_utf8_cont(char c);
size_t utf8_char_len(unsigned char c);
size_t utf8_prev_start(const std::string& s, size_t pos);
size_t utf8_advance(const std::string& s, size_t pos, int n);
int str_width(const std::string& s);
std::vector<std::string> wrap_text(const std::string& text, int width);
