#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <thread>
#include <atomic>
#include "config.h"
#include "client.h"

using json = nlohmann::json;

class MCPManager {
public:
    MCPManager(Config* config);
    ~MCPManager();

    void start_all();
    void stop_all();
    void start_all_async();
    void add_tools_to_config();
    std::string execute_tool(const std::string& name, const std::string& args_json);

    bool is_ready() const { return ready_; }

private:
    Config* config_;
    std::vector<MCPClient*> clients_;
    std::thread init_thread_;
    std::atomic<bool> ready_{false};
};
