#pragma once
#include "command.h"

class TempCommand : public ICommand {
public:
    std::string name() const override { return "temp"; }
    std::string description() const override { return "Override temperature (e.g. /temp 0.8, /temp off to reset)"; }
    bool execute(const std::string& args, CommandContext& ctx) override;
};
