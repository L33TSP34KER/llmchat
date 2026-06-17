#pragma once
#include "feature.h"

class FileIOFeature : public IFeature {
public:
    std::string name() const override { return "fileio"; }
    bool is_tool_provider() const override { return true; }
    bool handles_tool(const std::string& tool_name) const override;
    std::string execute_tool(const std::string& tool_name, const std::string& args_json) override;

private:
    std::string execute_write(const std::string& args_json);
    std::string execute_edit(const std::string& args_json);
    std::string execute_read(const std::string& args_json);
};
