#include "deepsearch_command.h"
#include "conversation.h"
#include "ui/chat_ui.h"
#include "llm/client.h"
#include <ctime>

bool DeepSearchCommand::execute(const std::string& args, CommandContext& ctx) {
    if (args.empty()) {
        if (ctx.ui) {
            ctx.ui->set_status_text("Usage: /deepsearch <query>");
        }
        return true;
    }
    std::string query = args;
    query.erase(0, query.find_first_not_of(" \t"));
    query.erase(query.find_last_not_of(" \t") + 1);

    ConversationEntry ue;
    ue.type = ConversationEntry::USER;
    ue.content = "/deepsearch " + query;
    ue.timestamp = std::time(nullptr);
    ctx.conv->add_entry(ue);

    if (ctx.ui) {
        ctx.ui->set_status_text("Deep search started...");
    }
    if (ctx.llm) {
        ctx.llm->set_deep_search(true);
        ctx.llm->enqueue_message(query);
    }
    return true;
}
