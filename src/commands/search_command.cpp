#include "search_command.h"
#include "conversation.h"
#include "ui/chat_ui.h"
#include <ctime>
#include <algorithm>
#include <cctype>

bool SearchCommand::execute(const std::string& args, CommandContext& ctx) {
    if (args.empty()) {
        if (ctx.ui) ctx.ui->set_status_text("Usage: /search <term>");
        return true;
    }
    std::string term = args;
    std::string term_lower;
    for (char c : term) term_lower += std::tolower(c);

    auto entries = ctx.conv->get_entries();
    std::string result = "## Search results for \"" + term + "\"\n\n";
    int count = 0;
    int idx = 0;
    for (auto& e : entries) {
        idx++;
        std::string content_lower;
        for (char c : e.content) content_lower += std::tolower(c);
        if (content_lower.find(term_lower) != std::string::npos) {
            count++;
            const char* role = "";
            switch (e.type) {
                case ConversationEntry::USER: role = "User"; break;
                case ConversationEntry::ASSISTANT: role = "Assistant"; break;
                case ConversationEntry::SYSTEM: role = "System"; break;
                case ConversationEntry::TOOL_CALL: role = "Tool"; break;
                case ConversationEntry::TOOL_RESULT: role = "Result"; break;
                case ConversationEntry::ERROR: role = "Error"; break;
            }
            std::string snippet = e.content.substr(0, 200);
            if (e.content.size() > 200) snippet += "...";
            result += "**" + std::string(role) + "** (#" + std::to_string(idx) + "): "
                    + snippet + "\n\n";
        }
    }
    if (count == 0) {
        result += "No matches found.";
    } else {
        result += "--- " + std::to_string(count) + " match" + (count > 1 ? "es" : "") + " ---";
    }

    ConversationEntry se;
    se.type = ConversationEntry::SYSTEM;
    se.content = result;
    se.timestamp = std::time(nullptr);
    ctx.conv->add_entry(se);
    if (ctx.ui) ctx.ui->notify_update();
    return true;
}
