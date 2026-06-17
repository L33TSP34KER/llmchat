#pragma once
#include "command.h"

class CopyCommand : public ICommand {
public:
    std::string name() const override { return "copy"; }
    std::string description() const override { return "Copy message N to clipboard"; }
    bool execute(const std::string& args, CommandContext& ctx) override;
};
