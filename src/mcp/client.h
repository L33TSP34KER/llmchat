#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "config.h"

using json = nlohmann::json;

struct MCPTool {
    std::string name;
    std::string description;
    json input_schema;
};

class MCPClient {
public:
    MCPClient(const MCPServerConfig& cfg);
    ~MCPClient();

    bool start();
    void stop();
    bool is_running() const { return running_; }

    std::vector<MCPTool> list_tools();
    std::string call_tool(const std::string& name, const json& args);

private:
    MCPServerConfig config_;
    pid_t pid_ = -1;
    int stdin_fd_ = -1;
    int stdout_fd_ = -1;
    bool running_ = false;

    std::string send_request(const json& request);
};
