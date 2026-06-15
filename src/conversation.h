#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <deque>
#include <functional>

struct ConversationEntry {
    enum Type { USER, ASSISTANT, SYSTEM, TOOL_CALL, TOOL_RESULT, ERROR };
    Type type;
    std::string content;
    std::string tool_name;
    int64_t timestamp;
};

class Conversation {
public:
    Conversation();

    void add_entry(const ConversationEntry& entry);
    void clear();
    void set_streaming_text(const std::string& text);
    void clear_streaming();
    bool has_streaming() const;

    std::vector<ConversationEntry> get_entries() const;
    std::string get_streaming_text() const;

    void lock();
    void unlock();

private:
    mutable std::mutex mutex_;
    std::deque<ConversationEntry> entries_;
    std::string streaming_text_;
    bool has_streaming_ = false;
};
