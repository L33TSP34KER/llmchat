#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>
#include "command.h"

class CommandRegistry {
public:
    void register_command(std::unique_ptr<ICommand> cmd);
    bool execute(const std::string& input, CommandContext& ctx);
    std::vector<std::string> get_completions(const std::string& prefix) const;
    std::string get_help_text() const;

private:
    std::map<std::string, std::unique_ptr<ICommand>> commands_;
};
