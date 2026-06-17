#include "export_command.h"
#include "conversation.h"
#include "ui/chat_ui.h"
#include <ctime>
#include <fstream>

bool ExportCommand::execute(const std::string& args, CommandContext& ctx) {
    auto entries = ctx.conv->get_entries();
    std::string filename = "llmchat-export-" + std::to_string(std::time(nullptr)) + ".md";
    std::ofstream f(filename);
    if (f.is_open()) {
        std::time_t now_export = std::time(nullptr);
        f << "# llmchat Conversation Export\n\n";
        f << "Exported at: " << std::ctime(&now_export) << "\n\n";
        f << "---\n\n";
        for (auto& e : entries) {
            const char* role = "";
            switch (e.type) {
                case ConversationEntry::USER: role = "User"; break;
                case ConversationEntry::ASSISTANT: role = "Assistant"; break;
                case ConversationEntry::SYSTEM: role = "System"; break;
                case ConversationEntry::TOOL_CALL:
                    role = ("Tool Call: " + e.tool_name).c_str();
                    break;
                case ConversationEntry::TOOL_RESULT:
                    role = ("Tool Result: " + e.tool_name).c_str();
                    break;
                case ConversationEntry::ERROR: role = "Error"; break;
            }
            char tbuf[32];
            std::strftime(tbuf, sizeof(tbuf), "%H:%M",
                std::localtime(&e.timestamp));
            f << "### " << role << "  •  " << tbuf << "\n\n";
            f << e.content << "\n\n";
        }
        f.close();
        ConversationEntry me;
        me.type = ConversationEntry::SYSTEM;
        me.content = "Exported conversation to **" + filename + "**";
        me.timestamp = std::time(nullptr);
        ctx.conv->add_entry(me);
    } else {
        ConversationEntry me;
        me.type = ConversationEntry::ERROR;
        me.content = "Error: could not write to " + filename;
        me.timestamp = std::time(nullptr);
        ctx.conv->add_entry(me);
    }
    if (ctx.ui) {
        ctx.ui->notify_update();
    }
    return true;
}
