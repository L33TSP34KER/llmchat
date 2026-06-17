#include "copy_command.h"
#include "conversation.h"
#include "ui/chat_ui.h"
#include <cstdlib>

bool CopyCommand::execute(const std::string& args, CommandContext& ctx) {
    if (args.empty()) {
        if (ctx.ui) ctx.ui->set_status_text("Usage: /copy <message number>");
        return true;
    }
    int idx = std::atoi(args.c_str());
    if (idx <= 0) {
        if (ctx.ui) ctx.ui->set_status_text("Usage: /copy <message number>");
        return true;
    }
    auto entries = ctx.conv->get_entries();
    if (idx > (int)entries.size()) {
        if (ctx.ui) {
            ctx.ui->set_status_text("Only " + std::to_string(entries.size()) + " messages");
        }
        return true;
    }
    if (ctx.clipboard_fn && ctx.clipboard_fn(entries[idx - 1].content)) {
        if (ctx.ui) ctx.ui->set_status_text("Copied message " + std::to_string(idx));
    } else {
        if (ctx.ui) ctx.ui->set_status_text("Copy failed");
    }
    return true;
}
