#pragma once
#include "command.h"

class SkillCommand : public ICommand {
public:
    std::string name() const override { return "skill"; }
    std::string description() const override { return "Switch skill"; }
    bool execute(const std::string& args, CommandContext& ctx) override;
};
