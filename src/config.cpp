#include "config.h"

std::unordered_map<std::string, Theme> Theme::cache_;
bool Theme::cache_loaded_ = false;

static std::string find_themes_json() {
    // Check next to executable (build/ or installed bin/)
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len != -1) {
        buf[len] = '\0';
        std::string exe_path(buf);
        std::string dir = exe_path.substr(0, exe_path.find_last_of('/'));
        // Try exe dir
        std::string path = dir + "/themes.json";
        std::ifstream f(path);
        if (f.is_open()) return path;
        // Try ../src (for build/llmchat -> src/themes.json)
        path = dir + "/../src/themes.json";
        f.open(path);
        if (f.is_open()) return path;
    }
    // Try CWD
    {
        std::ifstream f("themes.json");
        if (f.is_open()) return "themes.json";
    }
    // Try config dir
    {
        std::string path = Config::get_config_dir() + "/themes.json";
        std::ifstream f(path);
        if (f.is_open()) return path;
    }
    return "";
}

Theme Theme::from_json(const std::string& name, const json& j) {
    Theme t;
    t.name = name;
    auto& c = t.colors;
    c.user_fg = j.value("user_fg", c.user_fg);
    c.user_bg = j.value("user_bg", c.user_bg);
    c.assistant_fg = j.value("assistant_fg", c.assistant_fg);
    c.assistant_bg = j.value("assistant_bg", c.assistant_bg);
    c.system_fg = j.value("system_fg", c.system_fg);
    c.system_bg = j.value("system_bg", c.system_bg);
    c.tool_fg = j.value("tool_fg", c.tool_fg);
    c.tool_bg = j.value("tool_bg", c.tool_bg);
    c.error_fg = j.value("error_fg", c.error_fg);
    c.error_bg = j.value("error_bg", c.error_bg);
    c.status_fg = j.value("status_fg", c.status_fg);
    c.status_bg = j.value("status_bg", c.status_bg);
    c.input_fg = j.value("input_fg", c.input_fg);
    c.input_bg = j.value("input_bg", c.input_bg);
    c.separator_fg = j.value("separator_fg", c.separator_fg);
    c.separator_bg = j.value("separator_bg", c.separator_bg);
    t.slot_machine_animation = j.value("slot_machine_animation", false);
    t.casino_status_bar = j.value("casino_status_bar", false);
    if (j.contains("animation_speed")) t.animation_speed = j["animation_speed"];
    return t;
}

void Theme::load_cache() {
    if (cache_loaded_) return;
    cache_loaded_ = true;

    std::string path = find_themes_json();
    if (path.empty()) {
        std::cerr << "Warning: themes.json not found, using default theme only" << std::endl;
        return;
    }

    std::ifstream f(path);
    if (!f.is_open()) return;

    try {
        json j;
        f >> j;
        for (auto it = j.begin(); it != j.end(); ++it) {
            cache_[it.key()] = from_json(it.key(), it.value());
        }
    } catch (std::exception& e) {
        std::cerr << "Themes load error: " << e.what() << std::endl;
    }
}

Theme Theme::get(const std::string& name) {
    load_cache();
    auto it = cache_.find(name);
    if (it != cache_.end()) return it->second;
    Theme t;
    t.name = name;
    return t;
}

static void write_sample_config(const std::string& path) {
    json j;
    j["api_endpoint"] = "http://localhost:8080/v1/chat/completions";
    j["api_key"] = "";
    j["model"] = "llama3.2";
    j["system_prompt"] = "You are a helpful assistant.";
    j["theme"] = "default";
    j["current_skill"] = "";
    j["include_tools_in_context"] = true;
    j["slot_machine_animation"] = false;
    j["animation_speed"] = 50;
    j["casino_status_bar"] = false;
    j["context_compression"] = false;
    j["max_context_chars"] = 80000;
    j["tools"] = json::array();
    j["mcp_servers"] = json::array();
    j["skills"] = json::array();
    std::ofstream f(path);
    if (f.is_open()) f << j.dump(2) << std::endl;
}

Config Config::load() {
    Config cfg;
    std::string dir = get_config_dir();
    std::string path = dir + "/config.json";

    std::ifstream f(path);
    if (!f.is_open()) {
        mkdir(dir.c_str(), 0755);
        write_sample_config(path);
        cfg.last_error = "Created sample config at " + path;
        return cfg;
    }

    f.seekg(0, std::ios::end);
    bool empty = f.tellg() == 0;
    f.seekg(0, std::ios::beg);

    if (empty) {
        write_sample_config(path);
        cfg.last_error = "Config was empty, wrote sample";
        return cfg;
    }

    try {
        json j;
        f >> j;

        if (!j.is_object()) {
            cfg.last_error = "Config is not a JSON object — check the file";
            return cfg;
        }

        if (j.contains("api_endpoint")) cfg.api_endpoint = j["api_endpoint"];
        if (j.contains("api_key")) cfg.api_key = j["api_key"];
        if (j.contains("model")) cfg.model = j["model"];
        if (j.contains("system_prompt")) cfg.system_prompt = j["system_prompt"];
        if (j.contains("current_skill")) cfg.current_skill = j["current_skill"];
        if (j.contains("include_tools_in_context")) cfg.include_tools_in_context = j["include_tools_in_context"];
        if (j.contains("theme")) cfg.theme_name = j["theme"];
        if (j.contains("slot_machine_animation")) cfg.slot_machine_animation = j["slot_machine_animation"];
        if (j.contains("animation_speed")) cfg.animation_speed = j["animation_speed"];
        if (j.contains("casino_status_bar")) cfg.casino_status_bar = j["casino_status_bar"];
        if (j.contains("context_compression")) cfg.context_compression = j["context_compression"];
        if (j.contains("max_context_chars")) cfg.max_context_chars = j["max_context_chars"];

        if (j.contains("tools")) {
            for (auto& t : j["tools"]) {
                ToolDefinition td;
                td.name = t.value("name", "");
                td.description = t.value("description", "");
                td.input_schema = t.value("input_schema", json::object());
                td.command = t.value("command", "");
                cfg.tools.push_back(td);
            }
        }

        if (j.contains("mcp_servers")) {
            for (auto& m : j["mcp_servers"]) {
                MCPServerConfig mc;
                mc.name = m.value("name", "");
                mc.command = m.value("command", "");
                if (m.contains("args")) mc.args = m["args"].get<std::vector<std::string>>();
                cfg.mcp_servers.push_back(mc);
            }
        }

        if (j.contains("skills")) {
            for (auto& s : j["skills"]) {
                SkillDefinition sd;
                sd.name = s.value("name", "");
                sd.system_prompt = s.value("system_prompt", "");
                cfg.skills.push_back(sd);
            }
        }
    } catch (std::exception& e) {
        std::string msg = e.what();
        cfg.last_error = "Config parse error: " + msg + " — check " + path;
    }
    return cfg;
}

void Config::save() {
    std::string dir = get_config_dir();
    std::string path = dir + "/config.json";
    mkdir(dir.c_str(), 0755);

    json j;
    j["api_endpoint"] = api_endpoint;
    j["api_key"] = api_key;
    j["model"] = model;
    j["system_prompt"] = system_prompt;
    j["current_skill"] = current_skill;
    j["include_tools_in_context"] = include_tools_in_context;
    j["theme"] = theme_name;
    j["slot_machine_animation"] = slot_machine_animation;
    j["animation_speed"] = animation_speed;
    j["casino_status_bar"] = casino_status_bar;
    j["context_compression"] = context_compression;
    j["max_context_chars"] = max_context_chars;

    json tools_arr = json::array();
    for (auto& t : tools) {
        json tj;
        tj["name"] = t.name;
        tj["description"] = t.description;
        tj["input_schema"] = t.input_schema;
        tj["command"] = t.command;
        tools_arr.push_back(tj);
    }
    j["tools"] = tools_arr;

    json mcp_arr = json::array();
    for (auto& m : mcp_servers) {
        json mj;
        mj["name"] = m.name;
        mj["command"] = m.command;
        mj["args"] = m.args;
        mcp_arr.push_back(mj);
    }
    j["mcp_servers"] = mcp_arr;

    json skills_arr = json::array();
    for (auto& s : skills) {
        json sj;
        sj["name"] = s.name;
        sj["system_prompt"] = s.system_prompt;
        skills_arr.push_back(sj);
    }
    j["skills"] = skills_arr;

    std::ofstream f(path);
    if (f.is_open()) f << j.dump(2) << std::endl;
}
