#pragma once
#include <string>
#include <vector>
#include <functional>
#include <nlohmann/json.hpp>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <memory>
#include "config.h"
#include "provider.h"

using json = nlohmann::json;

struct Message {
    std::string role;
    std::string content;
    std::string tool_call_id;
    std::string tool_name;
    json tool_calls = json::array();

    json to_json() const {
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

struct QueuedMessage {
    std::string content;
    std::string skill_override;
};

class LlmClient {
public:
    using StreamCallback = std::function<void(const StreamEvent&)>;

    LlmClient(Config* config);
    ~LlmClient();

    void set_stream_callback(StreamCallback cb);
    void send_message(const std::string& content, const std::string& skill_override = "");
    void enqueue_message(const std::string& content, const std::string& skill_override = "");
    void add_system_message(const std::string& content);
    void clear_conversation();
    void cancel_current();
    void set_status_callback(StreamCallback cb);
    void set_deep_search(bool v) { deep_search_ = v; }

    bool is_processing() const { return processing_; }
    bool is_deep_search() const { return deep_search_; }
    const std::vector<Message>& get_messages() const { return messages_; }
    std::vector<Message>& get_messages() { return messages_; }

private:
    Config* config_;
    std::unique_ptr<LLMProvider> provider_;
    std::vector<Message> messages_;
    std::queue<QueuedMessage> msg_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::atomic<bool> running_{true};
    std::atomic<bool> processing_{false};
    std::atomic<bool> cancel_{false};
    StreamCallback stream_cb_;
    StreamCallback status_cb_;
    std::atomic<bool> deep_search_{false};
    int compress_cooldown_ = 0;
    std::thread worker_;

    void worker_loop();
    void process_message(const std::string& content, const std::string& skill_override);
    void process_message_deep_search(const std::string& content);
    std::string build_payload(const std::vector<Message>& history, bool include_tools);
    int estimate_total_chars() const;
    bool check_compress_needed() const;
    void maybe_compress();
};
