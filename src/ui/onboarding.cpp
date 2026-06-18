#include "onboarding.h"
#include <ncurses.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>
#include <vector>
#include <wchar.h>
#include <clocale>

// Banner: "LLMCHAT" in big double-line block letters (6 lines)
static const char* banner[] = {
    "  \xe2\x96\x88\xe2\x96\x88\xe2\x95\x97     \xe2\x96\x88\xe2\x96\x88\xe2\x95\x97    \xe2\x96\x88\xe2\x96\x88\xe2\x95\x97 \xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x95\x97  \xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x95\x97 \xe2\x96\x88\xe2\x96\x88\xe2\x95\x97  \xe2\x96\x88\xe2\x96\x88\xe2\x95\x97 \xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x95\x97 \xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x95\x97",
    "  \xe2\x96\x88\xe2\x96\x88\xe2\x95\x91     \xe2\x96\x88\xe2\x96\x88\xe2\x95\x91    \xe2\x96\x88\xe2\x96\x88\xe2\x95\x91\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x95\x97 \xe2\x96\x88\xe2\x96\x88\xe2\x95\x94\xe2\x95\x90\xe2\x95\x90\xe2\x95\x9d \xe2\x96\x88\xe2\x96\x88\xe2\x95\x91  \xe2\x96\x88\xe2\x96\x88\xe2\x95\x91\xe2\x96\x88\xe2\x96\x88\xe2\x95\x94\xe2\x95\x90\xe2\x96\x88\xe2\x96\x88\xe2\x95\x97\xe2\x95\x9a\xe2\x95\x90\xe2\x96\x88\xe2\x96\x88\xe2\x95\x94\xe2\x95\x90\xe2\x95\x9d",
    "  \xe2\x96\x88\xe2\x96\x88\xe2\x95\x91     \xe2\x96\x88\xe2\x96\x88\xe2\x95\x91    \xe2\x96\x88\xe2\x96\x88\xe2\x95\x91\xe2\x96\x88\xe2\x95\x94\xe2\x96\x88\xe2\x96\x88\xe2\x95\x97\xe2\x96\x88\xe2\x96\x88\xe2\x95\x91     \xe2\x96\x88\xe2\x96\x88\xe2\x95\x91\xe2\x96\x88\xe2\x96\x88\xe2\x95\x91  \xe2\x96\x88\xe2\x96\x88\xe2\x95\x91\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x95\x91   \xe2\x96\x88\xe2\x96\x88\xe2\x95\x91  ",
    "  \xe2\x96\x88\xe2\x96\x88\xe2\x95\x91     \xe2\x96\x88\xe2\x96\x88\xe2\x95\x91    \xe2\x96\x88\xe2\x96\x88\xe2\x95\x91\xe2\x96\x88\xe2\x95\x91\xe2\x95\x9a\xe2\x96\x88\xe2\x96\x88\xe2\x95\x97\xe2\x96\x88\xe2\x96\x88\xe2\x95\x91     \xe2\x96\x88\xe2\x96\x88\xe2\x95\x91\xe2\x96\x88\xe2\x96\x88\xe2\x95\x91  \xe2\x96\x88\xe2\x96\x88\xe2\x95\x91\xe2\x96\x88\xe2\x96\x88\xe2\x95\x94\xe2\x95\x90\xe2\x96\x88\xe2\x96\x88\xe2\x95\x97   \xe2\x96\x88\xe2\x96\x88\xe2\x95\x91  ",
    "  \xe2\x96\x88\xe2\x96\x88\xe2\x95\x91\xe2\x95\x88\xe2\x95\x88\xe2\x95\x88\xe2\x95\x88\xe2\x95\x88\xe2\x96\x88\xe2\x96\x88\xe2\x95\x91\xe2\x95\x88\xe2\x95\x88\xe2\x95\x88\xe2\x95\x88\xe2\x95\x88\xe2\x96\x88\xe2\x96\x88\xe2\x95\x91\xe2\x95\x9a\xe2\x95\x90\xe2\x95\x9d \xe2\x96\x88\xe2\x96\x88\xe2\x95\x97\xe2\x95\x9a\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x95\x97\xe2\x96\x88\xe2\x96\x88\xe2\x95\x91  \xe2\x96\x88\xe2\x96\x88\xe2\x95\x91\xe2\x96\x88\xe2\x96\x88\xe2\x95\x91  \xe2\x96\x88\xe2\x96\x88\xe2\x95\x91\xe2\x95\x9a\xe2\x96\x88\xe2\x96\x88\xe2\x95\x94\xe2\x95\x90\xe2\x95\x9d \xe2\x96\x88\xe2\x96\x88\xe2\x95\x91  ",
    "  \xe2\x95\x9a\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x9d\xe2\x95\x9a\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x9d\xe2\x95\x9a\xe2\x95\x90\xe2\x95\x9d \xe2\x95\x9a\xe2\x95\x90\xe2\x95\x9d\xe2\x95\x9a\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x9d\xe2\x95\x9a\xe2\x95\x90\xe2\x95\x9d  \xe2\x95\x9a\xe2\x95\x90\xe2\x95\x9d\xe2\x95\x9a\xe2\x95\x90\xe2\x95\x9d  \xe2\x95\x9a\xe2\x95\x90\xe2\x95\x9d \xe2\x95\x9a\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x9d \xe2\x95\x9a\xe2\x95\x90\xe2\x95\x9d  ",
};
static constexpr int BANNER_LINES = 6;

// ---- Animals ----
struct AnimalSprite {
    const char* chars[3]; // up to 3 lines, empty string = end
    int width;            // visual width of widest line
    int height;           // number of non-empty lines
};

static const AnimalSprite animals[] = {
    // Bird
    {{
        "   ___   ",
        "  (o v)  ",
        "   `~'   ",
    }, 9, 3},
    // Eagle
    {{
        "  _.-\"\"\"\"-._  ",
        "  '\\____/`  ",
    }, 14, 2},
    // Cow
    {{
        "  (___c___)  ",
        "    \\   \\    ",
        "     `-'     ",
    }, 12, 3},
    // Fish
    {{
        "><((((\">",
    }, 8, 1},
    // Cat
    {{
        "  |\\---/|  ",
        "  | o_o |  ",
        "   \\_^_/   ",
    }, 10, 3},
    // Dog
    {{
        "   __     ",
        "  / _)    ",
        " ( / \\__/ ",
    }, 9, 3},
    // Butterfly
    {{
        "  /\\ /\\  ",
        "  \\/ \\/  ",
    }, 8, 2},
    // Duck
    {{
        "  >(.)__ ",
        "  (____/ ",
    }, 8, 2},
    // Penguin
    {{
        "  .--.  ",
        "  |O_O| ",
        "  |_|_| ",
    }, 7, 3},
    // Owl
    {{
        "  ,___,",
        "  [O,O] ",
        "  /)_)  ",
    }, 7, 3},
    // Bunny
    {{
        "  /\\ /\\  ",
        "  (\"v\")  ",
    }, 8, 2},
    // Snake
    {{
        "  ~._.~  ",
    }, 7, 1},
    // Pig
    {{
        "  ( three )  ",
        "    /_ _\\    ",
    }, 12, 2},
};
static constexpr int NUM_ANIMALS = sizeof(animals) / sizeof(animals[0]);

struct AnimalInstance {
    int sprite_idx;
    double x, y;     // float position for smooth movement
    double vx, vy;   // velocity (pixels per frame)
    int counter;      // delay counter
    int phase;        // for animation effects
};

// ---- Background stars ----
struct Star {
    int x, y, phase;
};

// ---- Helpers ----
static int char_width(const char* s) {
    wchar_t wc;
    mbstate_t st = {};
    size_t len = mbrtowc(&wc, s, 4, &st);
    if (len == (size_t)-1 || len == (size_t)-2) return 1;
    int cw = wcwidth(wc);
    return cw > 0 ? cw : 1;
}

static int str_width_mb(const char* s) {
    int w = 0;
    while (*s) {
        int cw = char_width(s);
        int len = 1;
        unsigned char c = (unsigned char)*s;
        if (c >= 0xF0) len = 4;
        else if (c >= 0xE0) len = 3;
        else if (c >= 0xC0) len = 2;
        w += cw;
        s += len;
    }
    return w;
}

// Color pairs (30+ to avoid renderer's 1-15)
static constexpr int CP_ANIMAL     = 30;
static constexpr int CP_BANNER     = 31;
static constexpr int CP_BOX_BORDER = 32;
static constexpr int CP_LABEL      = 33;
static constexpr int CP_VALUE      = 34;
static constexpr int CP_ACCENT     = 35;
static constexpr int CP_PROMPT     = 36;
static constexpr int CP_STAR       = 37;
static constexpr int CP_BG         = 38;
static constexpr int CP_RAINBOW_BASE = 50;

static void setup_colors() {
    init_pair(CP_ANIMAL,     8,    -1);
    init_pair(CP_BANNER,     COLOR_CYAN, -1);
    init_pair(CP_BOX_BORDER, 7,    -1);
    init_pair(CP_LABEL,      8,    -1);
    init_pair(CP_VALUE,      7,    -1);
    init_pair(CP_ACCENT,     COLOR_YELLOW, -1);
    init_pair(CP_PROMPT,     COLOR_GREEN,  -1);
    init_pair(CP_STAR,       8,    -1);
    init_pair(CP_BG,         8,    -1);

    if (can_change_color()) {
        init_color(100, 1000, 0,    0);
        init_color(101, 1000, 500,  0);
        init_color(102, 1000, 1000, 0);
        init_color(103, 0,    1000, 0);
        init_color(104, 0,    1000, 1000);
        init_color(105, 0,    0,    1000);
        init_color(106, 1000, 0,    1000);
        for (int i = 0; i < 7; i++)
            init_pair(CP_RAINBOW_BASE + i, 100 + i, -1);
    } else {
        int colors[] = {COLOR_RED, COLOR_YELLOW, COLOR_GREEN, COLOR_CYAN, COLOR_BLUE, COLOR_MAGENTA};
        for (int i = 0; i < 6; i++)
            init_pair(CP_RAINBOW_BASE + i, colors[i], -1);
    }
}

static void draw_str(int y, int x, const char* s, int pair, int attr = A_NORMAL) {
    if (pair > 0) wattron(stdscr, COLOR_PAIR(pair));
    if (attr != A_NORMAL) wattron(stdscr, attr);
    mvaddstr(y, x, s);
    if (attr != A_NORMAL) wattroff(stdscr, attr);
    if (pair > 0) wattroff(stdscr, COLOR_PAIR(pair));
}

static void draw_centered(int y, int term_w, const char* s, int pair, int attr = A_NORMAL) {
    int w = str_width_mb(s);
    int x = (term_w - w) / 2;
    if (x < 0) x = 0;
    draw_str(y, x, s, pair, attr);
}

bool Onboarding::show(const Info& info) {
    setup_colors();

    int term_w = getmaxx(stdscr);
    int term_h = getmaxy(stdscr);
    if (term_w < 40 || term_h < 18) {
        return true;
    }

    // Seed animals
    std::vector<AnimalInstance> instances;
    int num_animals = 6 + rand() % 4;
    for (int i = 0; i < num_animals; i++) {
        int si = rand() % NUM_ANIMALS;
        double x = (double)(rand() % (term_w + 60)) - 30;
        double y = (double)(rand() % (term_h - 10)) + 1;
        if (si == 3 || si == 11) {
            // Fish and snake stay near bottom/middle
            y = (double)(term_h - 8) + (rand() % 4) - 2;
        }
        instances.push_back({
            si,
            x, y,
            (double)(rand() % 3 + 1) * 0.15 * (rand() % 2 == 0 ? 1.0 : -1.0),
            (double)(rand() % 3) * 0.05 - 0.05,
            rand() % 20,
            rand() % 100
        });
    }

    // Seed stars
    std::vector<Star> stars;
    int num_stars = (term_w * term_h) / 120;
    if (num_stars > 40) num_stars = 40;
    for (int i = 0; i < num_stars; i++) {
        stars.push_back({rand() % term_w, rand() % term_h, rand() % 100});
    }

    // Build box content
    auto trunc = [](const std::string& s, int n) {
        return s.size() <= (size_t)n ? s : s.substr(0, n - 1) + "\xe2\x80\xa6";
    };
    std::string model_s = info.model_name.empty() ? "none" : trunc(info.model_name, 20);
    std::string ep_s    = info.api_endpoint.empty() ? "none" : trunc(info.api_endpoint, 26);
    std::string ctx_s = std::to_string(info.max_context_chars) + " tok";

    int box_w = 52;
    int box_x = (term_w - box_w) / 2;
    if (box_x < 2) { box_x = 2; box_w = term_w - 4; }
    int box_y = BANNER_LINES + 3;

    curs_set(0);
    nodelay(stdscr, TRUE);

    int frame = 0;

    while (true) {
        int ch = wgetch(stdscr);
        if (ch != ERR) {
            if (ch == 'q' || ch == 'Q') break;
            break;
        }

        // Background fill
        for (int y = 0; y < term_h; y++) {
            mvhline(y, 0, ' ', term_w);
        }

        // Stars
        for (auto& s : stars) {
            s.phase = (s.phase + 1) % 100;
            if (s.phase < 30)
                draw_str(s.y, s.x, ".", CP_STAR, s.phase < 15 ? A_NORMAL : A_DIM);
        }

        // Animals
        for (auto& inst : instances) {
            inst.counter++;
            if (inst.counter < 3) continue;
            inst.counter = 0;

            inst.x += inst.vx;
            inst.y += inst.vy;
            inst.phase++;

            // Wrap: if too far left, appear on right; if too far right, go left
            const auto& spr = animals[inst.sprite_idx];
            if (inst.x > term_w + 10)  inst.x = -(double)spr.width - 5;
            if (inst.x < -(double)spr.width - 10) inst.x = (double)term_w + 5;
            if (inst.y > term_h) inst.y = -3;
            if (inst.y < -5)     inst.y = (double)(term_h / 2);

            int ix = (int)inst.x;
            int iy = (int)inst.y;

            int anim_pair = CP_ANIMAL;
            int anim_attr = A_DIM;

            for (int li = 0; li < spr.height; li++) {
                int ly = iy + li;
                if (ly < 0 || ly >= term_h) continue;
                draw_str(ly, ix, spr.chars[li], anim_pair, anim_attr);
            }
        }

        // Banner
        int bc = can_change_color() ? (CP_RAINBOW_BASE + (frame / 8) % 7) : (CP_RAINBOW_BASE + (frame / 10) % 6);
        for (int i = 0; i < BANNER_LINES; i++)
            draw_centered(1 + i, term_w, banner[i], bc, A_BOLD);

        // Separator
        {
            int sy = BANNER_LINES + 2;
            int sw = std::min(term_w - 4, 60);
            std::string sep;
            for (int i = 0; i < sw; i++) sep += "\xe2\x95\x90";
            int sx = (term_w - sw) / 2;
            int sc = (frame / 6) % 2 ? CP_ACCENT : CP_BOX_BORDER;
            if (sx >= 0 && sy < term_h) {
                std::string line = std::string("\xe2\x95\x94") + sep + "\xe2\x95\x97";
                draw_str(sy, sx, line.c_str(), sc);
            }
        }

        // Info box
        if (box_y + 8 < term_h) {
            auto row = [&](int yy, const char* label, const std::string& val) {
                draw_str(box_y + yy, box_x, (std::string("\xe2\x95\x91 ") + label).c_str(), CP_ACCENT, A_BOLD);
                int lw = (int)strlen(label);
                int vx = box_x + 2 + lw + 1;
                draw_str(box_y + yy, vx, val.c_str(), CP_VALUE);
                int rbx = box_x + box_w - 1;
                draw_str(box_y + yy, rbx, "\xe2\x95\x91", CP_BOX_BORDER);
            };

            std::string top;
            for (int i = 0; i < box_w - 2; i++) top += "\xe2\x95\x90";
            draw_str(box_y - 1, box_x, (std::string("\xe2\x95\x94") + top + "\xe2\x95\x97").c_str(), CP_BOX_BORDER);

            row(0, "MODEL",    model_s);
            row(1, "ENDPOINT", ep_s);
            row(2, "CONTEXT",  ctx_s);
            row(3, "TOOLS",    std::to_string(info.tool_count));
            row(4, "SKILLS",   std::to_string(info.skill_count));
            row(5, "MEMORY",   std::to_string(info.memory_count) + " items");

            std::string bot;
            for (int i = 0; i < box_w - 2; i++) bot += "\xe2\x95\x90";
            draw_str(box_y + 6, box_x, (std::string("\xe2\x95\x9a") + bot + "\xe2\x95\x9d").c_str(), CP_BOX_BORDER);

            if (info.streak_days > 0) {
                int sy = box_y + 8;
                if (sy < term_h) {
                    std::string streak = "\xe2\x9a\xa1  " + std::to_string(info.streak_days) + " day streak";
                    draw_centered(sy, term_w, streak.c_str(), CP_ACCENT, A_BOLD);
                }
            }
            if (info.is_first_run) {
                int fy = box_y + 9;
                if (fy < term_h)
                    draw_centered(fy, term_w, "welcome to llmchat", CP_LABEL, A_DIM);
            }
        }

        // Prompt
        {
            int py = term_h - 2;
            if (py > 0) {
                int pulse = (frame / 6) % 20;
                draw_centered(py, term_w, "\xe2\x96\xb6  PRESS ANY KEY TO BEGIN  \xe2\x97\x80",
                    CP_PROMPT, pulse < 12 ? A_BOLD : A_NORMAL);
            }
        }

        // Scan line
        int scan_y = (frame / 3) % term_h;
        mvchgat(scan_y, 0, term_w, A_UNDERLINE, CP_ACCENT, NULL);

        wnoutrefresh(stdscr);
        doupdate();

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        frame++;
    }

    curs_set(1);
    nodelay(stdscr, FALSE);
    erase();
    refresh();

    return true;
}
