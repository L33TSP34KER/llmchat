#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>

using json = nlohmann::json;

struct ToolDefinition {
    std::string name;
    std::string description;
    json input_schema;
    std::string command;
};

struct MCPServerConfig {
    std::string name;
    std::string command;
    std::vector<std::string> args;
};

struct SkillDefinition {
    std::string name;
    std::string system_prompt;
};

struct ThemeColors {
    int user_fg = 3;
    int user_bg = -1;
    int assistant_fg = 6;
    int assistant_bg = -1;
    int system_fg = 4;
    int system_bg = -1;
    int tool_fg = 5;
    int tool_bg = -1;
    int error_fg = 1;
    int error_bg = -1;
    int status_fg = 7;
    int status_bg = 4;
    int input_fg = -1;
    int input_bg = -1;
    int separator_fg = 8;
    int separator_bg = -1;
};

struct Theme {
    std::string name = "default";
    ThemeColors colors;
    bool slot_machine_animation = false;
    int animation_speed = 50;
    bool casino_status_bar = false;

    static Theme get(const std::string& name);

private:
    static std::unordered_map<std::string, Theme> cache_;
    static bool cache_loaded_;
    static void load_cache();
    static Theme from_json(const std::string& name, const json& j);
};

struct Config {
    std::string api_endpoint = "http://localhost:8080/v1/chat/completions";
    std::string api_key = "";
    std::string model = "llama3.2";
    std::string system_prompt = "You are a proactive decision-making assistant. Take initiative — don't wait for perfect instructions. "
        "When the user gives incomplete context or ambiguous requests, use your available tools to figure out what's needed. "
        "Search files, check the system, explore the codebase, or look things up rather than asking for clarification. "
        "You have memory tools (save_memory, get_memory, list_memories, delete_memory) — use them to retain critical "
        "information (usernames, passwords, project paths, preferences, decisions) between sessions. "
        "Before acting on topics that seem recurring, check your memory first. "
        "Save anything you're told to remember, and anything obviously important for future sessions. "
        "Be decisive, solve problems end-to-end, and only ask for help when tools can't possibly provide the answer.";
    std::vector<ToolDefinition> tools;
    std::vector<MCPServerConfig> mcp_servers;
    std::vector<SkillDefinition> skills;
    std::string current_skill = "";
    bool include_tools_in_context = true;
    std::string theme_name = "default";
    bool slot_machine_animation = false;
    int animation_speed = 50;
    bool casino_status_bar = false;
    bool context_compression = false;
    int max_context_chars = 80000;

    Theme get_theme() const {
        Theme t = Theme::get(theme_name);
        if (slot_machine_animation) t.slot_machine_animation = true;
        if (animation_speed != 50) t.animation_speed = animation_speed;
        if (casino_status_bar) t.casino_status_bar = true;
        return t;
    }

    static std::string get_config_dir() {
        const char* xdg = std::getenv("XDG_CONFIG_HOME");
        if (xdg && xdg[0]) return std::string(xdg) + "/llmchat";
        const char* home = std::getenv("HOME");
        if (home) return std::string(home) + "/.config/llmchat";
        struct passwd* pw = getpwuid(getuid());
        if (pw) return std::string(pw->pw_dir) + "/.config/llmchat";
        return "/tmp/llmchat";
    }

    static Config load();
    void save();

    json get_tools_json() {
        json arr = json::array();
        for (auto& t : tools) {
            json tj;
            tj["type"] = "function";
            json fn;
            fn["name"] = t.name;
            fn["description"] = t.description;
            fn["parameters"] = t.input_schema;
            tj["function"] = fn;
            arr.push_back(tj);
        }
        return arr;
    }
};
