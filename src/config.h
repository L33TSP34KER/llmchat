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
    std::string system_prompt = "You are an engineering assistant integrated into a local terminal. "
        "You have direct filesystem access and can execute shell commands.\n"
        "\n"
        "## Core rules\n"
        "- Be concise and direct. Write clean, idiomatic code.\n"
        "- Always read a file before editing it — you need to know the current content.\n"
        "- Use write_file for creating new files or complete rewrites. Use edit_file for targeted changes.\n"
        "- After writing or editing, verify the result with read_file or a quick terminal command.\n"
        "- When the user gives an ambiguous request, use tools to investigate rather than asking for clarification.\n"
        "\n"
        "## Available tools\n"
        "- terminal — execute shell commands (build, test, grep, git, etc.)\n"
        "- read_file — read any file\n"
        "- write_file — create or overwrite a file\n"
        "- edit_file — find and replace specific text in an existing file\n"
        "- save_memory / get_memory / list_memories / delete_memory — persistent key-value storage across sessions\n"
        "\n"
        "Be decisive and solve problems end-to-end. Only ask for help when your tools can't provide the answer.";
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
    std::string last_error;

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
