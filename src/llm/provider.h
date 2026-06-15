#pragma once
#include <string>
#include <functional>
#include <atomic>
#include <nlohmann/json.hpp>
#include "http_client.h"

using json = nlohmann::json;

struct StreamEvent {
    enum Type { TOKEN, TOOL_CALL, DONE, ERROR, REASONING, COMPRESS, CLEAR_STREAMING };
    Type type;
    std::string text;
    json tool_data;
    std::string error_msg;
};

class LLMProvider {
public:
    virtual ~LLMProvider() = default;
    using StreamCallback = std::function<void(const StreamEvent&)>;

    virtual void set_stream_callback(StreamCallback cb) = 0;
    virtual bool send_request(const std::string& payload) = 0;
    virtual void cancel() = 0;
};

class OpenAIProvider : public LLMProvider {
public:
    OpenAIProvider(const std::string& endpoint, const std::string& api_key);
    void set_stream_callback(StreamCallback cb) override;
    bool send_request(const std::string& payload) override;
    void cancel() override;

    bool handle_stream_chunk(const std::string& chunk,
                             std::string& current_content,
                             std::string& pending_tool_name,
                             std::string& pending_tool_args);

private:
    HttpClient http_;
    std::string endpoint_;
    std::string api_key_;
    StreamCallback stream_cb_;
    std::atomic<bool> cancel_{false};
};
