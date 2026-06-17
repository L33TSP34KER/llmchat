#include "model_command.h"
#include "conversation.h"
#include "config.h"
#include "ui/chat_ui.h"
#include <ctime>

bool ModelCommand::execute(const std::string& args, CommandContext& ctx) {
    if (args.empty()) {
        ConversationEntry me;
        me.type = ConversationEntry::SYSTEM;
        me.content = "Current model: **" + ctx.config->model + "**\n"
                     "Usage: `/model <name>` — change model for this session";
        me.timestamp = std::time(nullptr);
        ctx.conv->add_entry(me);
    } else {
        ctx.config->model = args;
        ConversationEntry me;
        me.type = ConversationEntry::SYSTEM;
        me.content = "Switched model to **" + ctx.config->model + "**";
        me.timestamp = std::time(nullptr);
        ctx.conv->add_entry(me);
        if (ctx.ui) {
            UIState s;
            s.model_name = ctx.config->model;
            ctx.ui->set_state(s);
        }
    }
    if (ctx.ui) {
        ctx.ui->notify_update();
    }
    return true;
}
