#include "stats_command.h"
#include "conversation.h"
#include "config.h"
#include "ui/chat_ui.h"
#include "llm/client.h"
#include <ctime>

bool StatsCommand::execute(const std::string& args, CommandContext& ctx) {
    auto entries = ctx.conv->get_entries();
    int user_msgs = 0, asst_msgs = 0, tool_calls = 0, errors = 0;
    for (auto& e : entries) {
        switch (e.type) {
            case ConversationEntry::USER: user_msgs++; break;
            case ConversationEntry::ASSISTANT: asst_msgs++; break;
            case ConversationEntry::TOOL_CALL: tool_calls++; break;
            case ConversationEntry::ERROR: errors++; break;
            default: break;
        }
    }
    char timebuf[64];
    auto now = std::time(nullptr);
    std::strftime(timebuf, sizeof(timebuf), "%H:%M:%S", std::localtime(&now));

    int total_chars = 0;
    size_t msg_count = 0;
    if (ctx.llm) {
        auto& msgs = ctx.llm->get_messages();
        msg_count = msgs.size();
        total_chars = ctx.llm->estimate_total_chars();
    }

    ConversationEntry se;
    se.type = ConversationEntry::SYSTEM;
    se.content =
        "## Session Stats\n"
        "\n"
        "| Metric | Value |\n"
        "|--------|-------|\n"
        "| Messages sent | " + std::to_string(user_msgs) + " |\n"
        "| Responses received | " + std::to_string(asst_msgs) + " |\n"
        "| Tool calls made | " + std::to_string(tool_calls) + " |\n"
        "| Errors | " + std::to_string(errors) + " |\n"
        "| API messages | " + std::to_string(msg_count) + " |\n"
        "| Est. context chars | " + std::to_string(total_chars) + " |\n"
        "| Current time | " + timebuf + " |";
    se.timestamp = now;
    ctx.conv->add_entry(se);
    if (ctx.ui) {
        ctx.ui->notify_update();
    }
    return true;
}
