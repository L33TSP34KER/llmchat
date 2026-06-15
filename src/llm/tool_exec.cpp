#include "tool_exec.h"
#include "persistent_shell.h"
#include "config.h"
#include <cstdio>
#include <fstream>
#include <algorithm>

using json = nlohmann::json;

namespace tool_exec {

static PersistentShell g_terminal;

std::string execute_terminal(const std::string& command) {
    return g_terminal.execute(command);
}

static std::string get_memory_path() {
    return Config::get_config_dir() + "/memory.json";
}

static json load_memory() {
    std::ifstream f(get_memory_path());
    if (!f.is_open()) return json::object();
    try {
        json j;
        f >> j;
        return j;
    } catch (...) { return json::object(); }
}

static void store_memory(const json& data) {
    std::ofstream f(get_memory_path());
    if (f.is_open()) f << data.dump(2) << std::endl;
}

std::string execute_memory_save(const std::string& args_json) {
    try {
        json args = json::parse(args_json);
        std::string key = args.value("key", "");
        std::string value = args.value("value", "");
        if (key.empty()) return "Error: no key provided";
        json mem = load_memory();
        mem[key] = value;
        store_memory(mem);
        return "Saved memory '" + key + "'";
    } catch (...) { return "Error: invalid arguments"; }
}

std::string execute_memory_get(const std::string& args_json) {
    try {
        json args = json::parse(args_json);
        std::string key = args.value("key", "");
        if (key.empty()) return "Error: no key provided";
        json mem = load_memory();
        if (mem.contains(key)) return mem[key].get<std::string>();
        return "No memory found for key: " + key;
    } catch (...) { return "Error: invalid arguments"; }
}

std::string execute_memory_list() {
    json mem = load_memory();
    if (mem.empty()) return "No memories stored.";
    std::string result;
    for (auto it = mem.begin(); it != mem.end(); ++it)
        result += it.key() + ": " + it.value().get<std::string>().substr(0, 80) + "\n";
    while (!result.empty() && result.back() == '\n') result.pop_back();
    return result;
}

std::string execute_memory_delete(const std::string& args_json) {
    try {
        json args = json::parse(args_json);
        std::string key = args.value("key", "");
        if (key.empty()) return "Error: no key provided";
        json mem = load_memory();
        if (mem.erase(key) > 0) {
            store_memory(mem);
            return "Deleted memory '" + key + "'";
        }
        return "No memory found for key: " + key;
    } catch (...) { return "Error: invalid arguments"; }
}

std::string execute_shell(const ToolDefinition& tool, const std::string& args_json) {
    // Route special tools
    if (tool.name == "terminal") {
        try {
            json args = json::parse(args_json);
            std::string cmd = args.value("command", "");
            if (cmd.empty()) return "Error: no command provided";
            return execute_terminal(cmd);
        } catch (...) {
            return "Error: invalid arguments";
        }
    }
    if (tool.name == "save_memory") return execute_memory_save(args_json);
    if (tool.name == "get_memory") return execute_memory_get(args_json);
    if (tool.name == "list_memories") return execute_memory_list();
    if (tool.name == "delete_memory") return execute_memory_delete(args_json);
    std::string command = tool.command;
    try {
        json args = json::parse(args_json);
        for (auto it = args.begin(); it != args.end(); ++it) {
            std::string placeholder = "{" + it.key() + "}";
            std::string val = it.value().dump();
            if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
                val = val.substr(1, val.size() - 2);
            size_t pos = 0;
            while ((pos = command.find(placeholder, pos)) != std::string::npos) {
                command.replace(pos, placeholder.size(), val);
                pos += val.size();
            }
        }
    } catch (json::parse_error&) {
        return "Error: Failed to parse tool arguments";
    }

    std::string result;
    FILE* pipe = popen((command + " 2>&1").c_str(), "r");
    if (pipe) {
        char buffer[4096];
        while (fgets(buffer, sizeof(buffer), pipe)) result += buffer;
        int rc = pclose(pipe);
        if (rc != 0 && result.empty()) result = "Command exited with code " + std::to_string(rc);
    } else {
        result = "Error: Failed to execute command";
    }

    if (result.empty()) result = "(no output)";
    return result;
}

bool is_mcp_tool(const std::string& name) {
    return name.size() >= 4 && name.substr(0, 4) == "mcp_";
}

}
