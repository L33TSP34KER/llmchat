#pragma once
#include "command.h"

class SearchCommand : public ICommand {
public:
    std::string name() const override { return "search"; }
    std::string description() const override { return "Search conversation history"; }
    bool execute(const std::string& args, CommandContext& ctx) override;
};
