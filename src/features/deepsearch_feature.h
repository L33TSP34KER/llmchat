#pragma once
#include "feature.h"
#include <functional>
#include <atomic>
#include <nlohmann/json.hpp>

struct StreamEvent;
class LLMProvider;
class Config;
class MCPManager;
class FeatureRegistry;

struct Message {
    std::string role;
    std::string content;
    std::string tool_call_id;
    std::string tool_name;
    nlohmann::json tool_calls = nlohmann::json::array();

    nlohmann::json to_json() const {
        using json = nlohmann::json;
        json j;
        j["role"] = role;
        if (!content.empty()) j["content"] = content;
        if (!tool_call_id.empty()) j["tool_call_id"] = tool_call_id;
        if (!tool_name.empty()) j["name"] = tool_name;
        if (!tool_calls.empty()) j["tool_calls"] = tool_calls;
        return j;
    }
};

struct ToolCall {
    std::string name;
    std::string arguments;
};

class DeepSearchFeature : public IFeature {
public:
    using StreamCallback = std::function<void(const StreamEvent&)>;

    struct ResearchContext {
        LLMProvider* provider = nullptr;
        Config* config = nullptr;
        MCPManager* mcp_manager = nullptr;
        FeatureRegistry* features = nullptr;
        std::vector<Message>* messages = nullptr;
        StreamCallback stream_cb;
        std::function<void()> compress_fn;
        std::atomic<bool>* cancel = nullptr;
    };

    std::string name() const override { return "deepsearch"; }

    void execute(const std::string& query, ResearchContext& ctx);

private:
    std::string build_payload(const std::vector<Message>& history, Config* config, bool include_tools);
    std::string execute_generic_command(const std::string& command_template, const std::string& args_json);
};
