#pragma once
#include <string>
#include <vector>

struct MdSeg {
    enum Type { NORMAL, BOLD, ITALIC, STRIKETHROUGH, CODE, HEADING, LINK, TABLE_ROW, TABLE_SEP, BLOCKQUOTE, HR, LIST_ITEM, TASK_DONE, TASK_PENDING };
    Type type;
    std::string text;
    int level = 0;
};

struct MdLine {
    std::vector<MdSeg> segs;
    bool is_table = false;
    bool is_code_block = false;
    bool is_blockquote = false;
};

std::vector<MdSeg> md_parse_inline(const std::string& line);
std::vector<MdLine> md_parse(const std::string& text, int width);
