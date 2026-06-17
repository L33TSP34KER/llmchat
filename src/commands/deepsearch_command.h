#pragma once
#include "command.h"

class DeepSearchCommand : public ICommand {
public:
    std::string name() const override { return "deepsearch"; }
    std::string description() const override { return "Start deep search"; }
    bool execute(const std::string& args, CommandContext& ctx) override;
};
