#pragma once
#include "command.h"

class ModelCommand : public ICommand {
public:
    std::string name() const override { return "model"; }
    std::string description() const override { return "Switch model (session only)"; }
    bool execute(const std::string& args, CommandContext& ctx) override;
};
