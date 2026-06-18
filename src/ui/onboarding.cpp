#include "onboarding.h"
#include "http_client.h"
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

// ---- Fetch models from /v1/models ----
Onboarding::ModelsResponse Onboarding::fetch_models(const std::string& api_endpoint, const std::string& api_key) {
    ModelsResponse resp_data;
    if (api_endpoint.empty()) return resp_data;

    std::string url = api_endpoint;
    size_t pos = url.find("/chat/completions");
    if (pos != std::string::npos) {
        url.replace(pos, 17, "/models");
    } else {
        pos = url.rfind('/');
        if (pos != std::string::npos && pos > 8) url = url.substr(0, pos);
        url += "/models";
    }

    HttpClient client;
    client.set_url(url);
    client.set_method("GET");
    if (!api_key.empty()) {
        client.set_header("Authorization", "Bearer " + api_key);
    }
    auto resp = client.perform();
    if (resp.status_code != 200) return resp_data;

    try {
        json j = json::parse(resp.body);
        if (j.contains("data") && j["data"].is_array() && !j["data"].empty()) {
            for (auto& m : j["data"]) {
                if (m.contains("id")) {
                    std::string mid = m["id"].get<std::string>();
                    resp_data.ids.push_back(mid);
                }
            }
            if (!resp_data.ids.empty()) {
                resp_data.first_id = resp_data.ids[0];
                auto& first = j["data"][0];
                if (first.contains("meta") && first["meta"].is_object()) {
                    auto& meta = first["meta"];
                    if (meta.contains("n_params")) resp_data.first_n_params = meta["n_params"].get<int64_t>();
                    if (meta.contains("n_ctx_train")) resp_data.first_n_ctx_train = meta["n_ctx_train"].get<int64_t>();
                }
            }
        }
    } catch (...) {}

    return resp_data;
}

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
            y = (double)(term_h - 8) + (rand() % 4) - 2;
        }
        instances.push_back({
            si, x, y,
            (double)(rand() % 3 + 1) * 0.15 * (rand() % 2 == 0 ? 1.0 : -1.0),
            (double)(rand() % 3) * 0.05 - 0.05,
            rand() % 20, rand() % 100
        });
    }

    // Seed stars
    std::vector<Star> stars;
    int num_stars = (term_w * term_h) / 120;
    if (num_stars > 40) num_stars = 40;
    for (int i = 0; i < num_stars; i++) {
        stars.push_back({rand() % term_w, rand() % term_h, rand() % 100});
    }

    auto trunc = [](const std::string& s, int n) {
        return s.size() <= (size_t)n ? s : s.substr(0, n - 1) + "\xe2\x80\xa6";
    };

    // Determine if we have a model picker
    bool has_picker = !info.available_models.empty() && info.on_model_selected;
    auto& models = info.available_models;
    int sel_idx = 0;
    // Try to find current model in list
    for (int i = 0; i < (int)models.size(); i++) {
        if (models[i] == info.model_name) { sel_idx = i; break; }
    }

    int box_w = 52;
    int box_x = (term_w - box_w) / 2;
    if (box_x < 2) { box_x = 2; box_w = term_w - 4; }
    int box_y = BANNER_LINES + 3;

    curs_set(0);
    nodelay(stdscr, TRUE);

    int frame = 0;
    int model_list_top = box_y + 10;
    int max_visible_models = term_h - model_list_top - 4;
    if (max_visible_models < 3) max_visible_models = 3;

    while (true) {
        int ch = wgetch(stdscr);
        if (ch != ERR) {
            if (has_picker && (ch == KEY_UP || ch == KEY_DOWN)) {
                if (ch == KEY_UP && sel_idx > 0) sel_idx--;
                if (ch == KEY_DOWN && sel_idx < (int)models.size() - 1) sel_idx++;
            } else if (has_picker && (ch == '\n' || ch == '\r' || ch == KEY_ENTER)) {
                if (info.on_model_selected && sel_idx < (int)models.size()) {
                    info.on_model_selected(models[sel_idx]);
                }
                break;
            } else if (ch == 'q' || ch == 'Q' || ch == 27) {
                break;
            } else {
                break;
            }
        }

        // Background
        for (int y = 0; y < term_h; y++) mvhline(y, 0, ' ', term_w);

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
            inst.x += inst.vx; inst.y += inst.vy; inst.phase++;
            const auto& spr = animals[inst.sprite_idx];
            if (inst.x > term_w + 10)  inst.x = -(double)spr.width - 5;
            if (inst.x < -(double)spr.width - 10) inst.x = (double)term_w + 5;
            if (inst.y > term_h) inst.y = -3;
            if (inst.y < -5)     inst.y = (double)(term_h / 2);
            int ix = (int)inst.x, iy = (int)inst.y;
            for (int li = 0; li < spr.height; li++) {
                int ly = iy + li;
                if (ly < 0 || ly >= term_h) continue;
                draw_str(ly, ix, spr.chars[li], CP_ANIMAL, A_DIM);
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
                draw_str(sy, sx, (std::string("\xe2\x95\x94") + sep + "\xe2\x95\x97").c_str(), sc);
            }
        }

        // Current selection info
        std::string cur_model_s = (sel_idx >= 0 && sel_idx < (int)models.size()) ? trunc(models[sel_idx], 20) : info.model_name;
        std::string ep_s = info.api_endpoint.empty() ? "none" : trunc(info.api_endpoint, 26);

        // Format n_params for selected model
        std::string params_s;
        auto np = (sel_idx == 0 || models.empty()) ? info.model_stats.n_params : 0;
        if (np > 0) {
            if (np >= 1000000000) {
                char buf[16]; snprintf(buf, sizeof(buf), "%.1fB", (double)np / 1000000000.0); params_s = buf;
            } else if (np >= 1000000) {
                char buf[16]; snprintf(buf, sizeof(buf), "%.0fM", (double)np / 1000000.0); params_s = buf;
            } else { params_s = std::to_string(np); }
        }

        // Info box
        {
            int num_rows = 8;
            if (box_y + num_rows + 2 < term_h) {
                auto row = [&](int yy, const char* label, const std::string& val) {
                    draw_str(box_y + yy, box_x, (std::string("\xe2\x95\x91 ") + label).c_str(), CP_ACCENT, A_BOLD);
                    int lw = (int)strlen(label);
                    int vx = box_x + 2 + lw + 1;
                    draw_str(box_y + yy, vx, val.c_str(), CP_VALUE);
                    draw_str(box_y + yy, box_x + box_w - 1, "\xe2\x95\x91", CP_BOX_BORDER);
                };

                std::string top;
                for (int i = 0; i < box_w - 2; i++) top += "\xe2\x95\x90";
                draw_str(box_y - 1, box_x, (std::string("\xe2\x95\x94") + top + "\xe2\x95\x97").c_str(), CP_BOX_BORDER);

                row(0, "MODEL",    cur_model_s);
                row(1, "PARAMS",   params_s.empty() ? "-" : params_s);
                row(2, "CTX",      "30976");
                row(3, "ENDPOINT", ep_s);
                row(4, "TOOLS",    std::to_string(info.tool_count));
                row(5, "SKILLS",   std::to_string(info.skill_count));
                row(6, "MEMORY",   std::to_string(info.memory_count) + " items");
                row(7, "FILE",     info.model_stats.model_id.empty() ? "-" : trunc(info.model_stats.model_id, 20));

                std::string bot;
                for (int i = 0; i < box_w - 2; i++) bot += "\xe2\x95\x90";
                draw_str(box_y + num_rows - 1, box_x, (std::string("\xe2\x95\x9a") + bot + "\xe2\x95\x9d").c_str(), CP_BOX_BORDER);
            }
        }

        // Streak + welcome
        {
            int sy = box_y + 9;
            if (info.streak_days > 0 && sy < term_h)
                draw_centered(sy, term_w, ("\xe2\x9a\xa1  " + std::to_string(info.streak_days) + " day streak").c_str(), CP_ACCENT, A_BOLD);
        }

        // ---- Model picker list ----
        if (has_picker) {
            int list_h = (int)models.size();
            int scroll = 0;
            if (sel_idx >= max_visible_models) scroll = sel_idx - max_visible_models + 1;
            int visible = std::min(list_h - scroll, max_visible_models);
            if (visible < 1) visible = 1;

            int py = model_list_top;
            int list_w = std::min(term_w - 4, 56);
            int lx = (term_w - list_w) / 2;
            if (lx < 2) lx = 2;

            // Box top
            std::string t;
            for (int i = 0; i < list_w - 2; i++) t += "\xe2\x95\x90";
            draw_str(py - 1, lx, (std::string("\xe2\x95\x94") + " MODELS " + t.substr(7)).c_str(), CP_BOX_BORDER);

            for (int i = 0; i < visible; i++) {
                int mi = scroll + i;
                std::string line = "\xe2\x95\x91";
                if (mi == sel_idx) {
                    line += " \xe2\x96\xb6 ";
                    int label_w = list_w - 6;
                    std::string label = trunc(models[mi], label_w);
                    line += label;
                    int pad = list_w - 4 - (int)label.size();
                    if (pad > 0) line += std::string(pad, ' ');
                    line += "\xe2\x95\x91";
                    draw_str(py + i, lx, line.c_str(), CP_ACCENT, A_REVERSE | A_BOLD);
                } else {
                    line += "   ";
                    int label_w = list_w - 5;
                    std::string label = trunc(models[mi], label_w);
                    line += label;
                    int pad = list_w - 5 - (int)label.size();
                    if (pad > 0) line += std::string(pad, ' ');
                    line += "\xe2\x95\x91";
                    draw_str(py + i, lx, line.c_str(), CP_VALUE);
                }
            }

            // Fill remaining visible rows with empty lines
            for (int i = visible; i < max_visible_models; i++) {
                std::string empty = "\xe2\x95\x91";
                empty += std::string(list_w - 2, ' ');
                empty += "\xe2\x95\x91";
                draw_str(py + i, lx, empty.c_str(), CP_BOX_BORDER);
            }

            // Box bottom
            std::string b;
            for (int i = 0; i < list_w - 2; i++) b += "\xe2\x95\x90";
            draw_str(py + max_visible_models, lx, (std::string("\xe2\x95\x9a") + b + "\xe2\x95\x9d").c_str(), CP_BOX_BORDER);

            // Hint
            int hy = py + max_visible_models + 1;
            if (hy < term_h)
                draw_centered(hy, term_w, "\xe2\x86\x91\xe2\x86\x93 select  \xe2\x86\xb5 confirm", CP_LABEL, A_DIM);

            // Override prompt
            int py2 = term_h - 2;
            if (py2 > hy + 1) {
                int pulse = (frame / 6) % 20;
                draw_centered(py2, term_w, "\xe2\x96\xb6  PRESS ANY KEY TO SKIP  \xe2\x97\x80",
                    CP_PROMPT, pulse < 12 ? A_BOLD : A_NORMAL);
            }
        } else {
            // Prompt
            int py = term_h - 2;
            if (py > 0) {
                int pulse = (frame / 6) % 20;
                draw_centered(py, term_w, "\xe2\x96\xb6  PRESS ANY KEY TO BEGIN  \xe2\x97\x80",
                    CP_PROMPT, pulse < 12 ? A_BOLD : A_NORMAL);
            }
        }

        // Scan line
        mvchgat((frame / 3) % term_h, 0, term_w, A_UNDERLINE, CP_ACCENT, NULL);

        wnoutrefresh(stdscr);
        doupdate();

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        frame++;
    }

    curs_set(1);
    nodelay(stdscr, TRUE);
    erase();
    refresh();

    return true;
}
