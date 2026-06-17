#include "clear_command.h"
#include "conversation.h"
#include "ui/chat_ui.h"
#include "llm/client.h"

bool ClearCommand::execute(const std::string& args, CommandContext& ctx) {
    ctx.conv->clear();
    if (ctx.ui) {
        ctx.ui->set_status_text("");
    }
    if (ctx.llm) {
        ctx.llm->clear_conversation();
    }
    return true;
}
