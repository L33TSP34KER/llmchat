#pragma once
#include "command.h"

class HelpCommand : public ICommand {
public:
    std::string name() const override { return "help"; }
    std::string description() const override { return "Show this help"; }
    bool execute(const std::string& args, CommandContext& ctx) override;
};
