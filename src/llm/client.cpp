#include "client.h"
#include "tool_exec.h"
#include "http_client.h"
#include "mcp/manager.h"
#include "features/feature_registry.h"
#include <thread>
#include <iostream>

using json = nlohmann::json;

LlmClient::LlmClient(Config* config) : config_(config) {
    provider_ = std::make_unique<OpenAIProvider>(config_->api_endpoint, config_->api_key);
    worker_ = std::thread(&LlmClient::worker_loop, this);
}

LlmClient::~LlmClient() {
    running_ = false;
    queue_cv_.notify_one();
    if (worker_.joinable()) worker_.join();
}

void LlmClient::set_stream_callback(StreamCallback cb) {
    stream_cb_ = std::move(cb);
}

void LlmClient::set_status_callback(StreamCallback cb) {
    status_cb_ = std::move(cb);
}

int LlmClient::estimate_total_chars() const {
    int total = 0;
    // Messages
    for (auto& m : messages_) {
        total += m.content.size() + m.role.size();
        if (!m.tool_calls.empty()) {
            for (auto& tc : m.tool_calls) {
                if (tc.contains("function")) {
                    total += tc["function"]["name"].get<std::string>().size()
                           + tc["function"]["arguments"].get<std::string>().size();
                }
            }
        }
    }
    // System prompt will be prepended
    total += config_->system_prompt.size();
    // Tool definitions overhead (estimated ~2x their serialized size in payload)
    json tj = config_->get_tools_json();
    total += tj.dump().size() * 2;
    // JSON framing overhead (~20%)
    total = static_cast<int>(total * 1.2);
    return total;
}

bool LlmClient::check_compress_needed() const {
    if (!config_->context_compression) return false;
    if (config_->max_context_chars <= 0) return false;
    if (compress_cooldown_ > 0) return false;
    return estimate_total_chars() > config_->max_context_chars;
}

void LlmClient::maybe_compress() {
    if (!check_compress_needed()) return;
    if (messages_.empty()) return;

    if (status_cb_) status_cb_({StreamEvent::COMPRESS, "Compressing context...", json(), ""});

    // Build summarization prompt from all messages (excluding system)
    std::string prompt = "Summarize this conversation concisely but thoroughly. "
                         "Preserve key facts, decisions, user preferences, and any context "
                         "needed to continue seamlessly:\n\n";
    for (auto& m : messages_) {
        if (m.role == "system") continue;
        prompt += "[" + m.role + "]\n" + m.content + "\n\n";
    }
    prompt += "Summary:";

    // Build non-streaming request
    json j;
    j["model"] = config_->model;
    j["stream"] = false;
    json msgs = json::array();
    json sys;
    sys["role"] = "system";
    sys["content"] = "You are a helpful assistant that summarizes conversations.";
    msgs.push_back(sys);
    json user;
    user["role"] = "user";
    user["content"] = prompt;
    msgs.push_back(user);
    j["messages"] = msgs;

    HttpClient http;
    http.set_url(config_->api_endpoint);
    http.set_method("POST");
    http.set_header("Content-Type", "application/json");
    if (!config_->api_key.empty())
        http.set_header("Authorization", "Bearer " + config_->api_key);
    http.set_body(j.dump());

    HttpResponse resp = http.perform();
    std::string summary;

    if (resp.status_code == 200) {
        try {
            auto rj = json::parse(resp.body);
            if (rj.contains("choices") && !rj["choices"].empty() &&
                rj["choices"][0].contains("message") &&
                rj["choices"][0]["message"].contains("content") &&
                !rj["choices"][0]["message"]["content"].is_null()) {
                summary = rj["choices"][0]["message"]["content"].get<std::string>();
            }
        } catch (...) {}
    }

    if (summary.empty()) {
        if (status_cb_) status_cb_({StreamEvent::COMPRESS, "", json(), ""});
        return;
    }

    // Save last user message before clearing
    std::string last_user;
    for (auto it = messages_.rbegin(); it != messages_.rend(); ++it) {
        if (it->role == "user") { last_user = it->content; break; }
    }

    // Clear and replace messages
    messages_.clear();

    Message sys_msg;
    sys_msg.role = "system";
    sys_msg.content = "Previous conversation summary:\n" + summary;
    messages_.push_back(sys_msg);

    if (!last_user.empty()) {
        Message user_msg;
        user_msg.role = "user";
        user_msg.content = last_user;
        messages_.push_back(user_msg);
    }

    // Cooldown: skip next 5 messages
    compress_cooldown_ = 5;

    // Notify UI via stream callback
    if (stream_cb_) {
        StreamEvent ev;
        ev.type = StreamEvent::COMPRESS;
        ev.tool_data["summary"] = summary;
        ev.tool_data["last_user"] = last_user;
        stream_cb_(ev);
    }

    if (status_cb_) status_cb_({StreamEvent::COMPRESS, "", json(), ""});
}

void LlmClient::send_message(const std::string& content, const std::string& skill_override) {
    enqueue_message(content, skill_override);
}

void LlmClient::enqueue_message(const std::string& content, const std::string& skill_override) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    msg_queue_.push({content, skill_override});
    queue_cv_.notify_one();
}

void LlmClient::add_system_message(const std::string& content) {
    Message sys;
    sys.role = "system";
    sys.content = content;
    messages_.push_back(sys);
}

void LlmClient::clear_conversation() {
    cancel_current();
    std::lock_guard<std::mutex> lock(queue_mutex_);
    messages_.clear();
    while (!msg_queue_.empty()) msg_queue_.pop();
}

void LlmClient::cancel_current() {
    cancel_ = true;
    provider_->cancel();
}

void LlmClient::worker_loop() {
    while (running_) {
        QueuedMessage qm;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this]() {
                return !msg_queue_.empty() || !running_;
            });
            if (!running_) break;
            qm = msg_queue_.front();
            msg_queue_.pop();
        }

        processing_ = true;
        if (agentic_) {
            process_message_agentic(qm.content);
        } else if (deep_search_) {
            process_message_deep_search(qm.content);
        } else {
            process_message(qm.content, qm.skill_override);
        }
        processing_ = false;
        cancel_ = false;
        deep_search_ = false;
        agentic_ = false;
    }
}

void LlmClient::process_message(const std::string& content, const std::string& skill_override) {
    Message user_msg;
    user_msg.role = "user";
    user_msg.content = content;
    messages_.push_back(user_msg);

    int max_turns = 10;

    while (max_turns-- > 0 && !cancel_) {
        std::string effective_system = config_->system_prompt;
        if (!skill_override.empty()) {
            for (auto& s : config_->skills) {
                if (s.name == skill_override) {
                    effective_system = s.system_prompt;
                    break;
                }
            }
        }

        std::vector<Message> history;
        Message sys;
        sys.role = "system";
        sys.content = effective_system;
        history.push_back(sys);
        for (auto& m : messages_) history.push_back(m);

        bool include_tools = config_->include_tools_in_context && !config_->tools.empty();
        std::string payload = build_payload(history, include_tools);

        std::string accumulated_content;
        std::vector<ToolCall> pending_tool_calls;
        bool got_tool_call = false;

        provider_->set_stream_callback([&](const StreamEvent& ev) {
            switch (ev.type) {
                case StreamEvent::TOKEN:
                    accumulated_content += ev.text;
                    if (stream_cb_) stream_cb_(ev);
                    break;
                case StreamEvent::REASONING:
                    if (stream_cb_) stream_cb_(ev);
                    break;
                case StreamEvent::TOOL_CALL:
                    got_tool_call = true;
                    pending_tool_calls.clear();
                    if (ev.tool_data.contains("calls")) {
                        for (auto& call : ev.tool_data["calls"]) {
                            ToolCall tc;
                            tc.name = call.value("name", "");
                            tc.arguments = call.value("arguments", "");
                            pending_tool_calls.push_back(tc);
                        }
                    }
                    if (stream_cb_) stream_cb_(ev);
                    break;
                case StreamEvent::DONE: {
                    // Per-stream done: clear streaming buffer but don't end processing
                    if (stream_cb_) {
                        StreamEvent cev;
                        cev.type = StreamEvent::CLEAR_STREAMING;
                        stream_cb_(cev);
                    }
                    break;
                }
                case StreamEvent::CLEAR_STREAMING:
                    break; // synthesized by this callback, never received from provider
                case StreamEvent::ERROR:
                case StreamEvent::COMPRESS:
                    if (stream_cb_) stream_cb_(ev);
                    break;
            }
        });

        bool ok = provider_->send_request(payload);

        if (cancel_) {
            if (stream_cb_) stream_cb_({StreamEvent::DONE, "", json(), ""});
            return;
        }

        if (!ok) {
            messages_.pop_back();
            return;
        }

        if (got_tool_call) {
            // Build assistant message with all tool calls
            Message asst_tool;
            asst_tool.role = "assistant";
            asst_tool.content = accumulated_content;
            json tc_arr = json::array();
            int call_idx = 0;
            for (auto& ptc : pending_tool_calls) {
                json tc_entry;
                tc_entry["id"] = "call_" + std::to_string(messages_.size()) + "_" + std::to_string(call_idx++);
                tc_entry["type"] = "function";
                tc_entry["function"]["name"] = ptc.name;
                tc_entry["function"]["arguments"] = ptc.arguments;
                tc_arr.push_back(tc_entry);
            }
            asst_tool.tool_calls = tc_arr;
            messages_.push_back(asst_tool);

            // Execute each tool and add results
            for (auto& ptc : pending_tool_calls) {
                ToolDefinition* tool_def = nullptr;
                for (auto& t : config_->tools) {
                    if (t.name == ptc.name) { tool_def = &t; break; }
                }

                if (!tool_def) {
                    Message tool_msg;
                    tool_msg.role = "tool";
                    tool_msg.content = "Error: Unknown tool '" + ptc.name + "'";
                    tool_msg.tool_call_id = "call_" + std::to_string(messages_.size());
                    tool_msg.tool_name = ptc.name;
                    messages_.push_back(tool_msg);
                    if (stream_cb_) stream_cb_({StreamEvent::ERROR, "Unknown tool: " + ptc.name, json(), ""});
                    continue;
                }

                std::string result;
                if (features_ && features_->is_handled_tool(ptc.name)) {
                    result = features_->execute_tool(ptc.name, ptc.arguments);
                } else if (tool_exec::is_mcp_tool(ptc.name)) {
                    result = mcp_manager_
                        ? mcp_manager_->execute_tool(ptc.name, ptc.arguments)
                        : "Error: MCP manager not available";
                } else if (!tool_def->command.empty()) {
                    result = tool_exec::execute_shell(*tool_def, ptc.arguments);
                } else {
                    result = "Error: No handler for tool '" + ptc.name + "'";
                }

                Message tool_msg;
                tool_msg.role = "tool";
                tool_msg.content = result;
                tool_msg.tool_call_id = "call_" + std::to_string(messages_.size());
                tool_msg.tool_name = ptc.name;
                messages_.push_back(tool_msg);

                if (!config_->hide_tool_results && stream_cb_) {
                    StreamEvent se;
                    se.type = StreamEvent::TOKEN;
                    se.text = "\n[Tool " + ptc.name + " returned: " + result.substr(0, 200) + "]\n";
                    stream_cb_(se);
                }
            }
            continue;
        }

        if (!accumulated_content.empty()) {
            Message asst;
            asst.role = "assistant";
            asst.content = accumulated_content;
            messages_.push_back(asst);
        }

        break;
    }

    if (compress_cooldown_ > 0) compress_cooldown_--;
    if (!cancel_) maybe_compress();

    if (stream_cb_) stream_cb_({StreamEvent::DONE, "", json(), ""});

    message_count_++;
    if (!cancel_ && config_->auto_name_conversations && config_->conversation_title.empty()
        && message_count_ == 5) {
        maybe_generate_title();
    }
}

void LlmClient::maybe_generate_title() {
    if (!stream_cb_ || !title_cb_) return;
    std::string prompt = "Generate a very short title (max 5 words) for this conversation. "
                         "Reply with ONLY the title, no quotes, no punctuation:\n\n";
    for (auto& m : messages_) {
        if (m.role == "system") continue;
        prompt += "[" + m.role + "]\n" + m.content.substr(0, 200) + "\n\n";
        if (prompt.size() > 3000) break;
    }
    prompt += "Title:";

    json j;
    j["model"] = config_->model;
    j["stream"] = false;
    json msgs = json::array();
    json sys; sys["role"] = "system"; sys["content"] = "You generate short conversation titles.";
    msgs.push_back(sys);
    json usr; usr["role"] = "user"; usr["content"] = prompt;
    msgs.push_back(usr);
    j["messages"] = msgs;

    HttpClient http;
    http.set_url(config_->api_endpoint);
    http.set_method("POST");
    http.set_header("Content-Type", "application/json");
    if (!config_->api_key.empty())
        http.set_header("Authorization", "Bearer " + config_->api_key);
    http.set_body(j.dump());

    HttpResponse resp = http.perform();
    if (resp.status_code == 200) {
        try {
            auto rj = json::parse(resp.body);
            if (rj.contains("choices") && !rj["choices"].empty()
                && rj["choices"][0].contains("message")
                && rj["choices"][0]["message"].contains("content")
                && !rj["choices"][0]["message"]["content"].is_null()) {
                std::string title = rj["choices"][0]["message"]["content"].get<std::string>();
                while (!title.empty() && title.back() == '.') title.pop_back();
                while (!title.empty() && title.front() == '"') title.erase(0, 1);
                while (!title.empty() && title.back() == '"') title.pop_back();
                config_->conversation_title = title;
                title_cb_(title);
            }
        } catch (...) {}
    }
}

void LlmClient::process_message_deep_search(const std::string& content) {
    // Add initial user message
    {
        Message user;
        user.role = "user";
        user.content = content;
        messages_.push_back(user);
    }

    int max_rounds = 6;
    int min_rounds = 2;
    int round = 0;

    while (round < max_rounds && !cancel_) {
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
        for (auto& m : messages_) history.push_back(m);

        bool include_tools = config_->include_tools_in_context && !config_->tools.empty();
        std::string payload = build_payload(history, include_tools);

        std::string accumulated_content;
        std::vector<ToolCall> pending_tool_calls;
        bool got_tool_call = false;

        provider_->set_stream_callback([&](const StreamEvent& ev) {
            switch (ev.type) {
                case StreamEvent::TOKEN:
                    accumulated_content += ev.text;
                    if (stream_cb_) stream_cb_(ev);
                    break;
                case StreamEvent::REASONING:
                    if (stream_cb_) stream_cb_(ev);
                    break;
                case StreamEvent::TOOL_CALL:
                    got_tool_call = true;
                    pending_tool_calls.clear();
                    if (ev.tool_data.contains("calls")) {
                        for (auto& call : ev.tool_data["calls"]) {
                            ToolCall tc;
                            tc.name = call.value("name", "");
                            tc.arguments = call.value("arguments", "");
                            pending_tool_calls.push_back(tc);
                        }
                    }
                    if (stream_cb_) stream_cb_(ev);
                    break;
                case StreamEvent::DONE: {
                    if (stream_cb_) {
                        StreamEvent cev;
                        cev.type = StreamEvent::CLEAR_STREAMING;
                        stream_cb_(cev);
                    }
                    break;
                }
                case StreamEvent::CLEAR_STREAMING:
                    break;
                case StreamEvent::ERROR:
                case StreamEvent::COMPRESS:
                    if (stream_cb_) stream_cb_(ev);
                    break;
            }
        });

        bool ok = provider_->send_request(payload);

        if (cancel_) {
            if (stream_cb_) stream_cb_({StreamEvent::DONE, "", json(), ""});
            return;
        }

        if (!ok) {
            messages_.pop_back();
            return;
        }

        // Handle tool calls (inner loop)
        int tool_turns = 0;
        while (got_tool_call && tool_turns < 5 && !cancel_) {
            tool_turns++;
            got_tool_call = false;

            // Build assistant message with all tool calls
            Message asst_tool;
            asst_tool.role = "assistant";
            asst_tool.content = accumulated_content;
            json tc_arr = json::array();
            int call_idx = 0;
            for (auto& ptc : pending_tool_calls) {
                json tc_entry;
                tc_entry["id"] = "call_" + std::to_string(messages_.size()) + "_" + std::to_string(call_idx++);
                tc_entry["type"] = "function";
                tc_entry["function"]["name"] = ptc.name;
                tc_entry["function"]["arguments"] = ptc.arguments;
                tc_arr.push_back(tc_entry);
            }
            asst_tool.tool_calls = tc_arr;
            messages_.push_back(asst_tool);

            // Execute each tool and add results
            for (auto& ptc : pending_tool_calls) {
                ToolDefinition* tool_def = nullptr;
                for (auto& t : config_->tools) {
                    if (t.name == ptc.name) { tool_def = &t; break; }
                }

                if (!tool_def) {
                    Message tool_msg;
                    tool_msg.role = "tool";
                    tool_msg.content = "Error: Unknown tool '" + ptc.name + "'";
                    tool_msg.tool_call_id = "call_" + std::to_string(messages_.size());
                    tool_msg.tool_name = ptc.name;
                    messages_.push_back(tool_msg);
                    if (stream_cb_) stream_cb_({StreamEvent::ERROR, "Unknown tool: " + ptc.name, json(), ""});
                    continue;
                }

                std::string result;
                if (features_ && features_->is_handled_tool(ptc.name)) {
                    result = features_->execute_tool(ptc.name, ptc.arguments);
                } else if (tool_exec::is_mcp_tool(ptc.name)) {
                    result = mcp_manager_
                        ? mcp_manager_->execute_tool(ptc.name, ptc.arguments)
                        : "Error: MCP manager not available";
                } else if (!tool_def->command.empty()) {
                    result = tool_exec::execute_shell(*tool_def, ptc.arguments);
                } else {
                    result = "Error: No handler for tool '" + ptc.name + "'";
                }

                Message tool_msg;
                tool_msg.role = "tool";
                tool_msg.content = result;
                tool_msg.tool_call_id = "call_" + std::to_string(messages_.size());
                tool_msg.tool_name = ptc.name;
                messages_.push_back(tool_msg);

                if (!config_->hide_tool_results && stream_cb_) {
                    StreamEvent se;
                    se.type = StreamEvent::TOKEN;
                    se.text = "\n[Tool " + ptc.name + " returned: " + result.substr(0, 200) + "]\n";
                    stream_cb_(se);
                }
            }

            // Send tool results back to LLM
            accumulated_content.clear();

            history.clear();
            history.push_back(sys);
            for (auto& m : messages_) history.push_back(m);
            payload = build_payload(history, include_tools);

            provider_->set_stream_callback([&](const StreamEvent& ev) {
                switch (ev.type) {
                    case StreamEvent::TOKEN:
                        accumulated_content += ev.text;
                        if (stream_cb_) stream_cb_(ev);
                        break;
                    case StreamEvent::REASONING:
                        if (stream_cb_) stream_cb_(ev);
                        break;
                    case StreamEvent::TOOL_CALL:
                        got_tool_call = true;
                        pending_tool_calls.clear();
                        if (ev.tool_data.contains("calls")) {
                            for (auto& call : ev.tool_data["calls"]) {
                                ToolCall tc;
                                tc.name = call.value("name", "");
                                tc.arguments = call.value("arguments", "");
                                pending_tool_calls.push_back(tc);
                            }
                        }
                        if (stream_cb_) stream_cb_(ev);
                        break;
                    case StreamEvent::DONE: {
                        if (stream_cb_) {
                            StreamEvent cev;
                            cev.type = StreamEvent::CLEAR_STREAMING;
                            stream_cb_(cev);
                        }
                        break;
                    }
                    case StreamEvent::CLEAR_STREAMING:
                        break;
                    case StreamEvent::ERROR:
                    case StreamEvent::COMPRESS:
                        if (stream_cb_) stream_cb_(ev);
                        break;
                }
            });

            ok = provider_->send_request(payload);
            if (!ok || cancel_) {
                if (stream_cb_) stream_cb_({StreamEvent::DONE, "", json(), ""});
                return;
            }
        }

        // After tool calls handled, push assistant response
        if (!accumulated_content.empty()) {
            Message asst;
            asst.role = "assistant";
            asst.content = accumulated_content;
            messages_.push_back(asst);
        }

        // Check if LLM requested more rounds
        bool need_more = false;
        if (round >= min_rounds) {
            size_t pos = accumulated_content.find("NEED_MORE_ROUNDS:");
            if (pos != std::string::npos) {
                need_more = true;
                // Extract number if present
                std::string after = accumulated_content.substr(pos + 15);
                int extra = 1;
                try { extra = std::stoi(after); } catch (...) {}
                if (extra > 0) {
                    // Limit total
                    int available = max_rounds - round;
                    int requested = std::min(extra, available);
                    if (requested > 0) {
                        // Auto-continue by adding a user message
                        Message cont;
                        cont.role = "user";
                        cont.content = "Continue researching. You requested " + std::to_string(requested) + " more round(s).";
                        messages_.push_back(cont);
                        round += requested - 1; // outer loop will increment by 1
                    }
                }
            }
        }

        if (!need_more) break;
    }

    // Clear any pending streaming text
    if (stream_cb_) {
        StreamEvent cev;
        cev.type = StreamEvent::CLEAR_STREAMING;
        stream_cb_(cev);
    }

    if (!cancel_) maybe_compress();
    if (stream_cb_) stream_cb_({StreamEvent::DONE, "", json(), ""});
}

void LlmClient::process_message_agentic(const std::string& content) {
    Message user;
    user.role = "user";
    user.content = content;
    messages_.push_back(user);

    int max_rounds = 10;
    int round = 0;
    std::string last_instructions;

    while (round < max_rounds && !cancel_) {
        round++;

        // === CODER PHASE ===
        if (stream_cb_) {
            StreamEvent se;
            se.type = StreamEvent::TOKEN;
            se.text = "\n--- Agent Round " + std::to_string(round) + ": Working ---\n";
            stream_cb_(se);
        }

        std::string coder_prompt = config_->agentic_coder_prompt;
        auto replace_placeholders = [&](std::string tmpl) -> std::string {
            size_t pos;
            while ((pos = tmpl.find("{task}")) != std::string::npos)
                tmpl.replace(pos, 6, content);
            while ((pos = tmpl.find("{instructions}")) != std::string::npos)
                tmpl.replace(pos, 14, last_instructions.empty() ? "(none yet)" : last_instructions);
            return tmpl;
        };
        coder_prompt = replace_placeholders(coder_prompt);

        std::vector<Message> coder_history;
        Message coder_sys;
        coder_sys.role = "system";
        coder_sys.content = coder_prompt;
        coder_history.push_back(coder_sys);
        for (auto& m : messages_) coder_history.push_back(m);

        auto coder_callback = [&](const StreamEvent& ev) {
            switch (ev.type) {
                case StreamEvent::TOKEN:
                    if (stream_cb_) stream_cb_(ev);
                    break;
                case StreamEvent::REASONING:
                    if (stream_cb_) stream_cb_(ev);
                    break;
                case StreamEvent::TOOL_CALL:
                    if (stream_cb_) stream_cb_(ev);
                    break;
                case StreamEvent::DONE:
                    if (stream_cb_) { StreamEvent c; c.type = StreamEvent::CLEAR_STREAMING; stream_cb_(c); }
                    break;
                case StreamEvent::ERROR:
                case StreamEvent::COMPRESS:
                    if (stream_cb_) stream_cb_(ev);
                    break;
                default: break;
            }
        };

        bool include_tools = config_->include_tools_in_context && !config_->tools.empty();

        std::string accumulated;
        std::vector<ToolCall> pending_tools;
        bool got_tool = false;

        // Send coder request
        {
            std::string payload = build_payload(coder_history, include_tools);
            provider_->set_stream_callback(coder_callback);
            bool ok = provider_->send_request(payload);
            if (!ok || cancel_) {
                if (stream_cb_) stream_cb_({StreamEvent::DONE, "", json(), ""});
                return;
            }
        }

        if (stream_cb_) { StreamEvent c; c.type = StreamEvent::CLEAR_STREAMING; stream_cb_(c); }

        // Handle tool calls during coder phase
        int tool_turns = 0;
        while (got_tool && tool_turns < 5 && !cancel_) {
            tool_turns++;
            got_tool = false;

            Message asst_tool;
            asst_tool.role = "assistant";
            asst_tool.content = accumulated;
            json tc_arr = json::array();
            int ci = 0;
            for (auto& ptc : pending_tools) {
                json tc_entry;
                tc_entry["id"] = "call_" + std::to_string(messages_.size()) + "_" + std::to_string(ci++);
                tc_entry["type"] = "function";
                tc_entry["function"]["name"] = ptc.name;
                tc_entry["function"]["arguments"] = ptc.arguments;
                tc_arr.push_back(tc_entry);
            }
            asst_tool.tool_calls = tc_arr;
            messages_.push_back(asst_tool);

            for (auto& ptc : pending_tools) {
                ToolDefinition* tool_def = nullptr;
                for (auto& t : config_->tools) {
                    if (t.name == ptc.name) { tool_def = &t; break; }
                }

                std::string result;
                if (!tool_def) {
                    result = "Error: Unknown tool '" + ptc.name + "'";
                } else if (features_ && features_->is_handled_tool(ptc.name)) {
                    result = features_->execute_tool(ptc.name, ptc.arguments);
                } else if (ptc.name.size() >= 4 && ptc.name.substr(0, 4) == "mcp_") {
                    result = mcp_manager_
                        ? mcp_manager_->execute_tool(ptc.name, ptc.arguments)
                        : "Error: MCP manager not available";
                } else if (!tool_def->command.empty()) {
                    result = tool_exec::execute_shell(*tool_def, ptc.arguments);
                } else {
                    result = "Error: No handler for tool '" + ptc.name + "'";
                }

                Message tool_msg;
                tool_msg.role = "tool";
                tool_msg.content = result;
                tool_msg.tool_call_id = "call_" + std::to_string(messages_.size());
                tool_msg.tool_name = ptc.name;
                messages_.push_back(tool_msg);
            }

            accumulated.clear();
            coder_history.clear();
            coder_history.push_back(coder_sys);
            for (auto& m : messages_) coder_history.push_back(m);
            std::string payload = build_payload(coder_history, include_tools);

            provider_->set_stream_callback(coder_callback);
            bool ok = provider_->send_request(payload);
            if (!ok || cancel_) {
                if (stream_cb_) stream_cb_({StreamEvent::DONE, "", json(), ""});
                return;
            }
            if (stream_cb_) { StreamEvent c; c.type = StreamEvent::CLEAR_STREAMING; stream_cb_(c); }
        }

        // Add coder's final response to messages
        if (!accumulated.empty()) {
            Message asst;
            asst.role = "assistant";
            asst.content = accumulated;
            messages_.push_back(asst);
        }

        bool needs_check = round > 1 || accumulated.find("AGENT_CHECK") != std::string::npos;

        // === CHECKER PHASE ===
        if (needs_check && !cancel_) {
            if (stream_cb_) {
                StreamEvent se;
                se.type = StreamEvent::TOKEN;
                se.text = "\n--- Agent Round " + std::to_string(round) + ": Verifying ---\n";
                stream_cb_(se);
            }

            std::string checker_prompt = config_->agentic_checker_prompt;
            {
                size_t pos;
                while ((pos = checker_prompt.find("{task}")) != std::string::npos)
                    checker_prompt.replace(pos, 6, content);
            }

            std::vector<Message> checker_history;
            Message checker_sys;
            checker_sys.role = "system";
            checker_sys.content = checker_prompt;
            checker_history.push_back(checker_sys);
            for (auto& m : messages_) checker_history.push_back(m);

            std::string checker_result;
            provider_->set_stream_callback([&](const StreamEvent& ev) {
                if (ev.type == StreamEvent::TOKEN) {
                    checker_result += ev.text;
                    if (stream_cb_) stream_cb_(ev);
                } else if (ev.type == StreamEvent::DONE) {
                    if (stream_cb_) { StreamEvent c; c.type = StreamEvent::CLEAR_STREAMING; stream_cb_(c); }
                } else if (ev.type == StreamEvent::ERROR) {
                    if (stream_cb_) stream_cb_(ev);
                }
            });

            std::string checker_payload = build_payload(checker_history, false);
            bool ok = provider_->send_request(checker_payload);
            if (!ok || cancel_) {
                if (stream_cb_) stream_cb_({StreamEvent::DONE, "", json(), ""});
                return;
            }

            if (stream_cb_) { StreamEvent c; c.type = StreamEvent::CLEAR_STREAMING; stream_cb_(c); }

            // Check if task is complete
            std::string upper;
            for (auto& c : checker_result) upper += std::toupper(c);
            if (upper.find("TASK COMPLETE") != std::string::npos) {
                if (stream_cb_) {
                    StreamEvent se;
                    se.type = StreamEvent::TOKEN;
                    se.text = "\n--- Task Complete! ---\n";
                    stream_cb_(se);
                }
                break;
            }

            // Extract instructions for next coder round
            std::string instr_marker = "INSTRUCTIONS FOR NEXT ROUND:";
            size_t instr_pos = checker_result.find(instr_marker);
            if (instr_pos != std::string::npos) {
                last_instructions = checker_result.substr(instr_pos + instr_marker.size());
                while (!last_instructions.empty() && last_instructions.front() == '\n')
                    last_instructions.erase(0, 1);
            } else {
                last_instructions = checker_result;
            }

            // Add checker feedback as user message for context
            Message feedback;
            feedback.role = "user";
            feedback.content = "Verifier review:\n" + checker_result;
            messages_.push_back(feedback);

            if (stream_cb_) {
                StreamEvent se;
                se.type = StreamEvent::TOKEN;
                se.text = "\n--- Continuing with instructions ---\n";
                stream_cb_(se);
            }
        }
    }

    if (round >= max_rounds && stream_cb_) {
        StreamEvent se;
        se.type = StreamEvent::TOKEN;
        se.text = "\n--- Max rounds reached ---\n";
        stream_cb_(se);
    }

    if (stream_cb_) { StreamEvent c; c.type = StreamEvent::CLEAR_STREAMING; stream_cb_(c); }
    if (!cancel_) maybe_compress();
    if (stream_cb_) stream_cb_({StreamEvent::DONE, "", json(), ""});
}

std::string LlmClient::build_payload(const std::vector<Message>& history, bool include_tools) {
    json j;
    j["model"] = config_->model;
    j["stream"] = true;

    // Detect Qwen models (they have strict Jinja template requirements)
    std::string model_lower;
    for (auto& c : config_->model) model_lower += std::tolower(c);
    bool is_qwen = model_lower.find("qwen") != std::string::npos;

    json msgs = json::array();
    if (is_qwen) {
        // Qwen's auto-generated template fails with system messages.
        // Collect system content and inject into the last user message.
        std::string system_content;
        for (auto& m : history) {
            if (m.role == "system") {
                if (!system_content.empty()) system_content += "\n\n";
                system_content += m.content;
            }
        }
        // Find the last user message (the most recent one)
        int last_user_idx = -1;
        for (int i = (int)history.size() - 1; i >= 0; i--) {
            if (history[i].role == "user") { last_user_idx = i; break; }
        }
        for (int i = 0; i < (int)history.size(); i++) {
            if (history[i].role == "system") continue;
            if (i == last_user_idx && !system_content.empty()) {
                json u;
                u["role"] = "user";
                u["content"] = system_content + "\n\n" + history[i].content;
                msgs.push_back(u);
            } else {
                msgs.push_back(history[i].to_json());
            }
        }
    } else {
        // Merge consecutive system messages at the start into one
        size_t si = 0;
        if (!history.empty() && history[0].role == "system") {
            std::string combined;
            while (si < history.size() && history[si].role == "system") {
                if (!combined.empty()) combined += "\n\n";
                combined += history[si].content;
                si++;
            }
            json sys;
            sys["role"] = "system";
            sys["content"] = combined;
            msgs.push_back(sys);
        }
        for (; si < history.size(); si++) {
            msgs.push_back(history[si].to_json());
        }
    }
    j["messages"] = msgs;

        if (include_tools && !is_qwen) {
            j["tools"] = config_->get_tools_json();
            if (config_->force_tool_use) {
                j["tool_choice"] = "required";
            }
        }

        if (config_->temperature_override >= 0.0f) {
            j["temperature"] = config_->temperature_override;
        }

        if (config_->max_thinking_tokens > 0) {
            std::string model_lower;
            for (auto& c : config_->model) model_lower += std::tolower(c);
            // Only send thinking to models known to support it
            bool supports_thinking = false;
            if (model_lower.find("claude") != std::string::npos) supports_thinking = true;
            if (model_lower.find("deepseek") != std::string::npos && model_lower.find("r1") != std::string::npos) supports_thinking = true;
            if (model_lower.find("o1") != std::string::npos || model_lower.find("o3") != std::string::npos) supports_thinking = true;
            if (supports_thinking) {
                j["thinking"] = {{"budget_tokens", config_->max_thinking_tokens}};
            }
        }

        return j.dump();
}
