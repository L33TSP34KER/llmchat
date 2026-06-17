#pragma once
#include "command.h"

class ReviewCommand : public ICommand {
public:
    std::string name() const override { return "review"; }
    std::string description() const override { return "Review git diff since last commit via LLM"; }
    bool execute(const std::string& args, CommandContext& ctx) override;
};
