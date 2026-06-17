#include "deepsearch_feature.h"
#include "http_client.h"
#include "llm/provider.h"
#include "config.h"
#include "mcp/manager.h"
#include "feature_registry.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>

using json = nlohmann::json;

std::string DeepSearchFeature::build_payload(const std::vector<Message>& history, Config* config, bool include_tools) {
    json j;
    j["model"] = config->model;
    j["stream"] = true;
    json msgs = json::array();
    for (auto& m : history) msgs.push_back(m.to_json());
    j["messages"] = msgs;
    if (include_tools) {
        j["tools"] = config->get_tools_json();
    }
    return j.dump();
}

std::string DeepSearchFeature::execute_generic_command(const std::string& command_template, const std::string& args_json) {
    std::string command = command_template;
    try {
        json args = json::parse(args_json);
        for (auto it = args.begin(); it != args.end(); ++it) {
            std::string placeholder = "{" + it.key() + "}";
            std::string val = it.value().dump();
            if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
                val = val.substr(1, val.size() - 2);
            size_t pos = 0;
            while ((pos = command.find(placeholder, pos)) != std::string::npos) {
                command.replace(pos, placeholder.size(), val);
                pos += val.size();
            }
        }
    } catch (...) {
        return "Error: Failed to parse tool arguments";
    }

    std::string result;
    FILE* pipe = popen((command + " 2>&1").c_str(), "r");
    if (pipe) {
        char buffer[4096];
        while (fgets(buffer, sizeof(buffer), pipe)) result += buffer;
        int rc = pclose(pipe);
        if (rc != 0 && result.empty()) result = "Command exited with code " + std::to_string(rc);
    } else {
        result = "Error: Failed to execute command";
    }
    if (result.empty()) result = "(no output)";
    return result;
}

void DeepSearchFeature::execute(const std::string& query, ResearchContext& ctx) {
    if (!ctx.provider || !ctx.config || !ctx.messages) return;

    Message user;
    user.role = "user";
    user.content = query;
    ctx.messages->push_back(user);

    int max_rounds = 6;
    int min_rounds = 2;
    int round = 0;

    auto send_and_process = [&](const std::vector<Message>& history, const std::string& payload,
                                std::string& accumulated, std::vector<ToolCall>& tools, bool& got_tool) -> bool {
        accumulated.clear();
        tools.clear();
        got_tool = false;

        ctx.provider->set_stream_callback([&](const StreamEvent& ev) {
            if (ev.type == StreamEvent::TOKEN) {
                accumulated += ev.text;
                if (ctx.stream_cb) ctx.stream_cb(ev);
            } else if (ev.type == StreamEvent::REASONING) {
                if (ctx.stream_cb) ctx.stream_cb(ev);
            } else if (ev.type == StreamEvent::TOOL_CALL) {
                got_tool = true;
                tools.clear();
                if (ev.tool_data.contains("calls")) {
                    for (auto& call : ev.tool_data["calls"]) {
                        ToolCall tc;
                        tc.name = call.value("name", "");
                        tc.arguments = call.value("arguments", "");
                        tools.push_back(tc);
                    }
                }
                if (ctx.stream_cb) ctx.stream_cb(ev);
            } else if (ev.type == StreamEvent::ERROR || ev.type == StreamEvent::COMPRESS) {
                if (ctx.stream_cb) ctx.stream_cb(ev);
            }
        });

        bool ok = ctx.provider->send_request(payload);
        if (!ok || (ctx.cancel && *ctx.cancel)) return false;

        if (ctx.stream_cb) {
            StreamEvent cev;
            cev.type = StreamEvent::CLEAR_STREAMING;
            ctx.stream_cb(cev);
        }
        return true;
    };

    while (round < max_rounds && !(ctx.cancel && *ctx.cancel)) {
        round++;

        std::string round_prompt;
        if (round == 1) {
            round_prompt = "You are doing deep research. Round 1: Search for initial information about the topic. "
                           "Use search_web and fetch_page to gather data. Identify what you know and what gaps exist.";
        } else {
            round_prompt = "You are doing deep research. Round " + std::to_string(round)
                         + ": Review your findings so far. Search for any missing details or unanswered questions. "
                           "Use tools to fill gaps.";
        }
        round_prompt += " After your response, if you need more rounds, end with: 'NEED_MORE_ROUNDS: <number>' "
                        "(e.g. NEED_MORE_ROUNDS: 1 for one more round). "
                        "If you have enough information, just provide a comprehensive answer without that line.";

        std::vector<Message> history;
        Message sys;
        sys.role = "system";
        sys.content = round_prompt;
        history.push_back(sys);
        for (auto& m : *ctx.messages) history.push_back(m);

        bool include_tools = ctx.config->include_tools_in_context && !ctx.config->tools.empty();
        std::string payload = build_payload(history, ctx.config, include_tools);

        std::string accumulated;
        std::vector<ToolCall> pending_tools;
        bool got_tool = false;

        if (!send_and_process(history, payload, accumulated, pending_tools, got_tool)) return;

        int tool_turns = 0;
        while (got_tool && tool_turns < 5 && !(ctx.cancel && *ctx.cancel)) {
            tool_turns++;
            got_tool = false;

            Message asst_tool;
            asst_tool.role = "assistant";
            asst_tool.content = accumulated;
            json tc_arr = json::array();
            int call_idx = 0;
            for (auto& ptc : pending_tools) {
                json tc_entry;
                tc_entry["id"] = "call_" + std::to_string(ctx.messages->size()) + "_" + std::to_string(call_idx++);
                tc_entry["type"] = "function";
                tc_entry["function"]["name"] = ptc.name;
                tc_entry["function"]["arguments"] = ptc.arguments;
                tc_arr.push_back(tc_entry);
            }
            asst_tool.tool_calls = tc_arr;
            ctx.messages->push_back(asst_tool);

            for (auto& ptc : pending_tools) {
                ToolDefinition* tool_def = nullptr;
                for (auto& t : ctx.config->tools) {
                    if (t.name == ptc.name) { tool_def = &t; break; }
                }

                std::string result;
                if (!tool_def) {
                    result = "Error: Unknown tool '" + ptc.name + "'";
                } else if (ptc.name.size() >= 4 && ptc.name.substr(0, 4) == "mcp_") {
                    result = ctx.mcp_manager
                        ? ctx.mcp_manager->execute_tool(ptc.name, ptc.arguments)
                        : "Error: MCP manager not available";
                } else if (ctx.features && ctx.features->is_handled_tool(ptc.name)) {
                    result = ctx.features->execute_tool(ptc.name, ptc.arguments);
                } else if (!tool_def->command.empty()) {
                    result = execute_generic_command(tool_def->command, ptc.arguments);
                } else {
                    result = "Error: No handler for tool '" + ptc.name + "'";
                }

                Message tool_msg;
                tool_msg.role = "tool";
                tool_msg.content = result;
                tool_msg.tool_call_id = "call_" + std::to_string(ctx.messages->size());
                tool_msg.tool_name = ptc.name;
                ctx.messages->push_back(tool_msg);
            }

            accumulated.clear();
            history.clear();
            history.push_back(sys);
            for (auto& m : *ctx.messages) history.push_back(m);
            payload = build_payload(history, ctx.config, include_tools);

            if (!send_and_process(history, payload, accumulated, pending_tools, got_tool)) return;
        }

        if (!accumulated.empty()) {
            Message asst;
            asst.role = "assistant";
            asst.content = accumulated;
            ctx.messages->push_back(asst);
        }

        bool need_more = false;
        if (round >= min_rounds) {
            size_t pos = accumulated.find("NEED_MORE_ROUNDS:");
            if (pos != std::string::npos) {
                need_more = true;
                std::string after = accumulated.substr(pos + 15);
                int extra = 1;
                try { extra = std::stoi(after); } catch (...) {}
                if (extra > 0) {
                    int available = max_rounds - round;
                    int requested = std::min(extra, available);
                    if (requested > 0) {
                        Message cont;
                        cont.role = "user";
                        cont.content = "Continue researching. You requested " + std::to_string(requested) + " more round(s).";
                        ctx.messages->push_back(cont);
                        round += requested - 1;
                    }
                }
            }
        }

        if (!need_more) break;
    }

    if (ctx.stream_cb) {
        StreamEvent cev;
        cev.type = StreamEvent::CLEAR_STREAMING;
        ctx.stream_cb(cev);
    }

    if (ctx.compress_fn) ctx.compress_fn();
    if (ctx.stream_cb) ctx.stream_cb({StreamEvent::DONE, "", json(), ""});
}
