#include "temp_command.h"
#include "conversation.h"
#include "config.h"
#include "ui/chat_ui.h"
#include <ctime>
#include <cstdlib>

bool TempCommand::execute(const std::string& args, CommandContext& ctx) {
    ConversationEntry me;
    me.type = ConversationEntry::SYSTEM;
    me.timestamp = std::time(nullptr);

    std::string trimmed = args;
    trimmed.erase(0, trimmed.find_first_not_of(" \t"));
    trimmed.erase(trimmed.find_last_not_of(" \t") + 1);

    if (trimmed.empty() || trimmed == "off") {
        if (trimmed == "off") {
            ctx.config->temperature_override = -1.0f;
            me.content = "Temperature override reset to default.";
        } else {
            if (ctx.config->temperature_override >= 0.0f) {
                me.content = "Current temperature override: **" + std::to_string(ctx.config->temperature_override) + "**\n"
                             "Use `/temp off` to reset to default.";
            } else {
                me.content = "No temperature override set. Use `/temp <value>` (0.0-2.0).";
            }
        }
    } else {
        float val = std::atof(trimmed.c_str());
        if (val < 0.0f) val = 0.0f;
        if (val > 2.0f) val = 2.0f;
        ctx.config->temperature_override = val;
        me.content = "Temperature override set to **" + std::to_string(val) + "**";
    }

    ctx.conv->add_entry(me);
    if (ctx.ui) ctx.ui->notify_update();
    return true;
}
