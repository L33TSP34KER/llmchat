#pragma once
#include "command.h"

class ClearCommand : public ICommand {
public:
    std::string name() const override { return "clear"; }
    std::string description() const override { return "Clear conversation"; }
    bool execute(const std::string& args, CommandContext& ctx) override;
};
