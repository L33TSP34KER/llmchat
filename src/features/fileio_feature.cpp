#include "fileio_feature.h"
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

bool FileIOFeature::handles_tool(const std::string& tool_name) const {
    return tool_name == "write_file" || tool_name == "edit_file" || tool_name == "read_file";
}

std::string FileIOFeature::execute_tool(const std::string& tool_name, const std::string& args_json) {
    if (tool_name == "write_file") return execute_write(args_json);
    if (tool_name == "edit_file") return execute_edit(args_json);
    if (tool_name == "read_file") return execute_read(args_json);
    return "Error: unknown file tool '" + tool_name + "'";
}

std::string FileIOFeature::execute_write(const std::string& args_json) {
    try {
        json args = json::parse(args_json);
        std::string path = args.value("path", "");
        std::string content = args.value("content", "");
        if (path.empty()) return "Error: no path provided";
        std::ofstream f(path);
        if (!f.is_open()) return "Error: could not write to " + path;
        f << content;
        f.close();
        return "Wrote " + std::to_string(content.size()) + " bytes to " + path;
    } catch (...) { return "Error: invalid arguments"; }
}

std::string FileIOFeature::execute_edit(const std::string& args_json) {
    try {
        json args = json::parse(args_json);
        std::string path = args.value("path", "");
        std::string old_text = args.value("old_text", "");
        std::string new_text = args.value("new_text", "");
        if (path.empty()) return "Error: no path provided";
        if (old_text.empty()) return "Error: no old_text provided";
        std::ifstream f(path);
        if (!f.is_open()) return "Error: could not read " + path;
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        f.close();
        size_t pos = content.find(old_text);
        if (pos == std::string::npos) return "Error: old_text not found in " + path;
        content.replace(pos, old_text.size(), new_text);
        std::ofstream of(path);
        if (!of.is_open()) return "Error: could not write to " + path;
        of << content;
        of.close();
        return "Replaced text in " + path + " (" + std::to_string(old_text.size()) + " chars)";
    } catch (...) { return "Error: invalid arguments"; }
}

std::string FileIOFeature::execute_read(const std::string& args_json) {
    try {
        json args = json::parse(args_json);
        std::string path = args.value("path", "");
        if (path.empty()) return "Error: no path provided";
        std::ifstream f(path);
        if (!f.is_open()) return "Error: could not read " + path;
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (content.empty()) return "(empty file)";
        std::string result = "File: " + path + " (" + std::to_string(content.size()) + " bytes)\n" + content;
        if (result.size() > 10000) {
            result = result.substr(0, 10000);
            result += "\n... [truncated]";
        }
        return result;
    } catch (...) { return "Error: invalid arguments"; }
}
