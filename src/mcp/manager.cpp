#include "manager.h"
#include <iostream>

using json = nlohmann::json;

MCPManager::MCPManager(Config* config) : config_(config) {}

MCPManager::~MCPManager() {
    stop_all();
}

void MCPManager::start_all() {
    for (auto& mcp_cfg : config_->mcp_servers) {
        auto* client = new MCPClient(mcp_cfg);
        if (client->start()) {
            auto tools = client->list_tools();
            for (auto& t : tools) {
                ToolDefinition td;
                td.name = "mcp_" + t.name;
                td.description = t.description;
                td.input_schema = t.input_schema;
                td.command = "__mcp__";
                config_->tools.push_back(td);
            }
            clients_.push_back(client);
        } else {
            delete client;
        }
    }
    ready_ = true;
}

void MCPManager::stop_all() {
    for (auto* c : clients_) {
        c->stop();
        delete c;
    }
    clients_.clear();
}

void MCPManager::start_all_async() {
    init_thread_ = std::thread([this]() {
        start_all();
    });
    init_thread_.detach();
}

void MCPManager::add_tools_to_config() {
    start_all();
}

std::string MCPManager::execute_tool(const std::string& name, const std::string& args_json) {
    if (name.size() >= 4 && name.substr(0, 4) == "mcp_") {
        std::string mcp_tool_name = name.substr(4);
        for (auto* mc : clients_) {
            try {
                json args = json::parse(args_json);
                return mc->call_tool(mcp_tool_name, args);
            } catch (...) {
                return "Error executing MCP tool";
            }
        }
    }
    return "";
}
