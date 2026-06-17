#pragma once
#include <string>
#include <vector>

struct ToolDefinition;

class IFeature {
public:
    virtual ~IFeature() = default;
    virtual std::string name() const = 0;

    // Tool-based features
    virtual bool is_tool_provider() const { return false; }
    virtual bool handles_tool(const std::string& tool_name) const { return false; }
    virtual std::string execute_tool(const std::string& tool_name, const std::string& args_json) {
        return "Error: feature '" + name() + "' does not handle tools";
    }
};
