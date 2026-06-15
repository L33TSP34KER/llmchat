#pragma once
#include <string>
#include <nlohmann/json.hpp>

struct ToolDefinition;

namespace tool_exec {
    std::string execute_shell(const ToolDefinition& tool, const std::string& args_json);
    std::string execute_terminal(const std::string& command);
    std::string execute_memory_save(const std::string& args_json);
    std::string execute_memory_get(const std::string& args_json);
    std::string execute_memory_list();
    std::string execute_memory_delete(const std::string& args_json);
    bool is_mcp_tool(const std::string& name);
}
