#pragma once
#include "command.h"

class StatsCommand : public ICommand {
public:
    std::string name() const override { return "stats"; }
    std::string description() const override { return "Show session statistics"; }
    bool execute(const std::string& args, CommandContext& ctx) override;
};
