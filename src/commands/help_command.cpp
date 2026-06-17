#include "help_command.h"
#include "command_registry.h"
#include "conversation.h"
#include "ui/chat_ui.h"
#include <ctime>

bool HelpCommand::execute(const std::string& args, CommandContext& ctx) {
    ConversationEntry he;
    he.type = ConversationEntry::SYSTEM;
    he.content = ctx.cmd_registry ? ctx.cmd_registry->get_help_text()
                                  : "Command registry not available";
    he.timestamp = std::time(nullptr);
    ctx.conv->add_entry(he);
    if (ctx.ui) {
        ctx.ui->notify_update();
    }
    return true;
}
