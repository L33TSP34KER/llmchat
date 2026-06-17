#include "terminal_feature.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

bool TerminalFeature::handles_tool(const std::string& tool_name) const {
    return tool_name == "terminal";
}

std::string TerminalFeature::execute_tool(const std::string& tool_name, const std::string& args_json) {
    try {
        json args = json::parse(args_json);
        std::string cmd = args.value("command", "");
        if (cmd.empty()) return "Error: no command provided";
        return shell_.execute(cmd);
    } catch (...) {
        return "Error: invalid arguments";
    }
}
