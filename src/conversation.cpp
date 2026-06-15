#include "conversation.h"
#include <ctime>

Conversation::Conversation() {}

void Conversation::add_entry(const ConversationEntry& entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (has_streaming_) {
        entries_.emplace_back();
        entries_.back().type = ConversationEntry::ASSISTANT;
        entries_.back().content = streaming_text_;
        entries_.back().timestamp = std::time(nullptr);
        has_streaming_ = false;
        streaming_text_.clear();
    }
    entries_.push_back(entry);
}

void Conversation::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
    streaming_text_.clear();
    has_streaming_ = false;
}

void Conversation::set_streaming_text(const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex_);
    streaming_text_ = text;
    has_streaming_ = true;
}

bool Conversation::has_streaming() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return has_streaming_;
}

void Conversation::clear_streaming() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (has_streaming_) {
        entries_.emplace_back();
        entries_.back().type = ConversationEntry::ASSISTANT;
        entries_.back().content = streaming_text_;
        entries_.back().timestamp = std::time(nullptr);
        has_streaming_ = false;
        streaming_text_.clear();
    }
}

std::vector<ConversationEntry> Conversation::get_entries() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ConversationEntry> result;
    for (auto& e : entries_) result.push_back(e);
    if (has_streaming_) {
        ConversationEntry se;
        se.type = ConversationEntry::ASSISTANT;
        se.content = streaming_text_;
        se.timestamp = std::time(nullptr);
        result.push_back(se);
    }
    return result;
}

std::string Conversation::get_streaming_text() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return streaming_text_;
}

void Conversation::lock() { mutex_.lock(); }
void Conversation::unlock() { mutex_.unlock(); }
