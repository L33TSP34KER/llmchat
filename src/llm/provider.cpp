#include "provider.h"
#include <sstream>
#include <iostream>

using json = nlohmann::json;

OpenAIProvider::OpenAIProvider(const std::string& endpoint, const std::string& api_key)
    : endpoint_(endpoint), api_key_(api_key) {}

void OpenAIProvider::set_stream_callback(StreamCallback cb) {
    stream_cb_ = std::move(cb);
}

bool OpenAIProvider::send_request(const std::string& payload) {
    http_.set_url(endpoint_);
    http_.set_method("POST");
    http_.set_header("Content-Type", "application/json");
    if (!api_key_.empty()) {
        http_.set_header("Authorization", "Bearer " + api_key_);
    }
    http_.set_body(payload);

    std::string current_content;
    std::map<int, std::string> pending_tool_names;
    std::map<int, std::string> pending_tool_args;

    http_.set_stream_callback([this, &current_content, &pending_tool_names, &pending_tool_args](const std::string& chunk) -> bool {
        if (cancel_) return false;
        return handle_stream_chunk(chunk, current_content, pending_tool_names, pending_tool_args);
    });

    bool ok = http_.perform_stream();
    if (!ok) {
        std::string err = http_.get_error_body();
        if (err.empty()) err = "HTTP request failed";
        if (stream_cb_) stream_cb_({StreamEvent::ERROR, "", json(), err});
    }
    return ok;
}

void OpenAIProvider::cancel() {
    cancel_ = true;
}

bool OpenAIProvider::handle_stream_chunk(const std::string& chunk,
                                         std::string& current_content,
                                         std::map<int, std::string>& pending_tool_names,
                                         std::map<int, std::string>& pending_tool_args) {
    std::istringstream stream(chunk);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        if (line.substr(0, 6) == "data: ") {
            std::string data = line.substr(6);
            if (data == "[DONE]") {
                if (stream_cb_) stream_cb_({StreamEvent::DONE, "", json(), ""});
                return true;
            }

            try {
                json j = json::parse(data);

                if (!j.contains("choices") || j["choices"].empty()) continue;

                auto& delta = j["choices"][0]["delta"];

                if (delta.contains("content") && !delta["content"].is_null()) {
                    std::string token = delta["content"];
                    current_content += token;
                    if (stream_cb_) stream_cb_({StreamEvent::TOKEN, token, json(), ""});
                } else if (delta.contains("reasoning_content") && !delta["reasoning_content"].is_null()) {
                    std::string token = delta["reasoning_content"];
                    if (stream_cb_) stream_cb_({StreamEvent::REASONING, token, json(), ""});
                }

                if (delta.contains("tool_calls")) {
                    for (auto& tc : delta["tool_calls"]) {
                        int index = tc.value("index", 0);
                        if (tc.contains("function")) {
                            auto& fn = tc["function"];
                            if (fn.contains("name") && !fn["name"].is_null()) {
                                pending_tool_names[index] += fn["name"].get<std::string>();
                            }
                            if (fn.contains("arguments") && !fn["arguments"].is_null()) {
                                pending_tool_args[index] += fn["arguments"].get<std::string>();
                            }
                        }
                    }
                }

                {
                    // finish_reason can be at choice level or in delta
                    std::string reason;
                    if (j["choices"][0].contains("finish_reason") && !j["choices"][0]["finish_reason"].is_null())
                        reason = j["choices"][0]["finish_reason"].get<std::string>();
                    else if (delta.contains("finish_reason") && !delta["finish_reason"].is_null())
                        reason = delta["finish_reason"].get<std::string>();
                    if (reason == "tool_calls") {
                        json td;
                        json calls = json::array();
                        for (auto& [idx, name] : pending_tool_names) {
                            json call;
                            call["name"] = name;
                            call["arguments"] = pending_tool_args[idx];
                            calls.push_back(call);
                        }
                        td["calls"] = calls;
                        if (stream_cb_) {
                            stream_cb_({StreamEvent::TOOL_CALL, "", td, ""});
                        }
                    }
                }

            } catch (std::exception&) {
            }
        }
    }
    return true;
}
