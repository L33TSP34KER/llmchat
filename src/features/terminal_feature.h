#pragma once
#include "feature.h"
#include "llm/persistent_shell.h"

class TerminalFeature : public IFeature {
public:
    std::string name() const override { return "terminal"; }
    bool is_tool_provider() const override { return true; }
    bool handles_tool(const std::string& tool_name) const override;
    std::string execute_tool(const std::string& tool_name, const std::string& args_json) override;

private:
    PersistentShell shell_;
};
