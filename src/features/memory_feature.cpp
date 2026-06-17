#include "memory_feature.h"
#include "config.h"
#include <fstream>

std::string MemoryFeature::get_memory_path() {
    return Config::get_config_dir() + "/memory.json";
}

json MemoryFeature::load_memory() {
    std::ifstream f(get_memory_path());
    if (!f.is_open()) return json::object();
    try {
        json j;
        f >> j;
        return j;
    } catch (...) { return json::object(); }
}

void MemoryFeature::store_memory(const json& data) {
    std::ofstream f(get_memory_path());
    if (f.is_open()) f << data.dump(2) << std::endl;
}

bool MemoryFeature::handles_tool(const std::string& tool_name) const {
    return tool_name == "save_memory" || tool_name == "get_memory"
        || tool_name == "list_memories" || tool_name == "delete_memory";
}

std::string MemoryFeature::execute_tool(const std::string& tool_name, const std::string& args_json) {
    if (tool_name == "save_memory") return execute_save(args_json);
    if (tool_name == "get_memory") return execute_get(args_json);
    if (tool_name == "list_memories") return execute_list();
    if (tool_name == "delete_memory") return execute_delete(args_json);
    return "Error: unknown memory tool '" + tool_name + "'";
}

std::string MemoryFeature::execute_save(const std::string& args_json) {
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

std::string MemoryFeature::execute_get(const std::string& args_json) {
    try {
        json args = json::parse(args_json);
        std::string key = args.value("key", "");
        if (key.empty()) return "Error: no key provided";
        json mem = load_memory();
        if (mem.contains(key)) return mem[key].get<std::string>();
        return "No memory found for key: " + key;
    } catch (...) { return "Error: invalid arguments"; }
}

std::string MemoryFeature::execute_list() {
    json mem = load_memory();
    if (mem.empty()) return "No memories stored.";
    std::string result;
    for (auto it = mem.begin(); it != mem.end(); ++it)
        result += it.key() + ": " + it.value().get<std::string>().substr(0, 80) + "\n";
    while (!result.empty() && result.back() == '\n') result.pop_back();
    return result;
}

std::string MemoryFeature::execute_delete(const std::string& args_json) {
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
