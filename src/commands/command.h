#pragma once
#include <string>
#include <functional>

class Conversation;
class Config;
class ChatUI;
class LlmClient;
class CommandRegistry;

struct CommandContext {
    Conversation* conv = nullptr;
    Config* config = nullptr;
    LlmClient* llm = nullptr;
    ChatUI* ui = nullptr;
    CommandRegistry* cmd_registry = nullptr;
    std::function<bool(const std::string&)> clipboard_fn;
};

class ICommand {
public:
    virtual ~ICommand() = default;
    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
    virtual bool execute(const std::string& args, CommandContext& ctx) = 0;
};
