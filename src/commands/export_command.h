#pragma once
#include "command.h"

class ExportCommand : public ICommand {
public:
    std::string name() const override { return "export"; }
    std::string description() const override { return "Export conversation to markdown"; }
    bool execute(const std::string& args, CommandContext& ctx) override;
};
