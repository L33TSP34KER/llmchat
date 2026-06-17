#pragma once
#include "feature.h"
#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class MemoryFeature : public IFeature {
public:
    std::string name() const override { return "memory"; }
    bool is_tool_provider() const override { return true; }
    bool handles_tool(const std::string& tool_name) const override;
    std::string execute_tool(const std::string& tool_name, const std::string& args_json) override;

private:
    static std::string get_memory_path();
    static json load_memory();
    static void store_memory(const json& data);

    std::string execute_save(const std::string& args_json);
    std::string execute_get(const std::string& args_json);
    std::string execute_list();
    std::string execute_delete(const std::string& args_json);
};
