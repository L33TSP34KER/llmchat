#include <iostream>
#include <fstream>
#include <csignal>
#include <clocale>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdio>
#include <sstream>
#include <ctime>

#include "config.h"
#include "llm/client.h"
#include "ui/chat_ui.h"
#include "conversation.h"
#include "mcp/manager.h"

std::atomic<bool> g_running{true};

void signal_handler(int) {
    g_running = false;
}

static bool copy_to_clipboard(const std::string& text) {
    if (text.empty()) return false;

    // Try xclip
    FILE* pipe = popen("xclip -selection clipboard 2>/dev/null", "w");
    if (pipe) {
        fwrite(text.data(), 1, text.size(), pipe);
        int rc = pclose(pipe);
        if (rc == 0 || WEXITSTATUS(rc) == 0) return true;
    }

    // Try wl-copy (Wayland)
    pipe = popen("wl-copy 2>/dev/null", "w");
    if (pipe) {
        fwrite(text.data(), 1, text.size(), pipe);
        int rc = pclose(pipe);
        if (rc == 0 || WEXITSTATUS(rc) == 0) return true;
    }

    // Try xsel
    pipe = popen("xsel -b 2>/dev/null", "w");
    if (pipe) {
        fwrite(text.data(), 1, text.size(), pipe);
        int rc = pclose(pipe);
        if (rc == 0 || WEXITSTATUS(rc) == 0) return true;
    }

    return false;
}

int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "");
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    Config config = Config::load();

    // CLI args override
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--model" && i + 1 < argc) {
            config.model = argv[++i];
        } else if (arg == "--endpoint" && i + 1 < argc) {
            config.api_endpoint = argv[++i];
            // Normalize endpoint URL
            if (config.api_endpoint.find("/chat/completions") == std::string::npos) {
                if (config.api_endpoint.back() != '/') config.api_endpoint += '/';
                std::string ep = config.api_endpoint;
                bool has_v1 = ep.find("/v1/") != std::string::npos ||
                              (ep.size() >= 3 && ep.substr(ep.size() - 3) == "/v1");
                if (!has_v1) {
                    config.api_endpoint += "v1/";
                }
                config.api_endpoint += "chat/completions";
            }
        } else if (arg == "--key" && i + 1 < argc) {
            config.api_key = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "llmchat - LLM Chat Client with ncurses UI\n"
                      << "Usage: llmchat [options]\n"
                      << "  --model <name>      Set model name\n"
                      << "  --endpoint <url>    Set API endpoint\n"
                      << "  --key <key>         Set API key\n"
                      << "  --help              Show this help\n";
            return 0;
        }
    }

    Conversation conv;
    LlmClient llm(&config);
    ChatUI ui(&conv, &config);

    // First-run welcome + streak tracking
    std::string first_run_file = config.get_config_dir() + "/.first-run";
    std::ifstream first_run_check(first_run_file);
    bool is_first_run = !first_run_check.good();

    // Daily streak
    std::string stats_dir = config.get_config_dir();
    std::string stats_file = stats_dir + "/stats.json";
    int streak_days = 0;
    std::string last_date;
    {
        json stats_json;
        std::ifstream sf(stats_file);
        if (sf.good()) {
            try { sf >> stats_json; } catch (...) {}
        }
        last_date = stats_json.value("last_date", "");
        streak_days = stats_json.value("streak", 0);
        char today_buf[16];
        auto now_t = std::time(nullptr);
        std::strftime(today_buf, sizeof(today_buf), "%Y-%m-%d", std::localtime(&now_t));
        std::string today(today_buf);
        if (today != last_date) {
            if (!last_date.empty()) {
                // Check if yesterday
                char yest_buf[16];
                auto yest_t = now_t - 86400;
                std::strftime(yest_buf, sizeof(yest_buf), "%Y-%m-%d", std::localtime(&yest_t));
                std::string yesterday(yest_buf);
                if (last_date == yesterday) {
                    streak_days++;
                } else {
                    streak_days = 1;
                }
            } else {
                streak_days = 1;
            }
            stats_json["streak"] = streak_days;
            stats_json["last_date"] = today;
            std::ofstream sf_out(stats_file);
            if (sf_out.good()) sf_out << stats_json.dump(2);
        }
    }

    if (is_first_run) {
        ConversationEntry we;
        we.type = ConversationEntry::SYSTEM;
        we.content =
            "Welcome to **llmchat**!\n"
            "\n"
            "Quick start:\n"
            "- Type a message and press **Enter** to chat\n"
            "- Type **/help** to see all commands\n"
            "- Press **Tab** for command completion\n"
            "- Press **Ctrl+Y** to copy responses\n"
            "\n"
            "**Skills**: Use `/skill <name>` to switch between personas.\n"
            "**Deep Search**: Use `/deepsearch <query>` for multi-round research.\n"
            "**Themes**: Edit `config.json` to change the look.\n"
            "\n"
            "Current model: **" + config.model + "**\n"
            "\n"
            "Tip: Click the tamagotchi (◕‿◕) to pet it!";
        we.timestamp = std::time(nullptr);
        conv.add_entry(we);
        // Mark first run as done
        std::ofstream ff(first_run_file);
        ff << "1";
    } else {
        // Show streak in status
        ui.set_status_text("streak " + std::to_string(streak_days) + "d");
    }

    // Start MCP servers asynchronously (don't block UI startup)
    MCPManager mcp_manager(&config);
    mcp_manager.start_all_async();
    llm.set_mcp_manager(&mcp_manager);
    config.save();

    UIState uistate;
    uistate.model_name = config.model;
    ui.set_state(uistate);

    // Wire up stream events from LLM to UI
    llm.set_stream_callback([&](const StreamEvent& ev) {
        switch (ev.type) {
            case StreamEvent::TOKEN: {
                std::string current = conv.get_streaming_text();
                current += ev.text;
                conv.set_streaming_text(current);
                ui.notify_update();
                break;
            }
            case StreamEvent::REASONING:
                break;
            case StreamEvent::CLEAR_STREAMING: {
                conv.clear_streaming();
                break;
            }
            case StreamEvent::TOOL_CALL: {
                break;
            }
            case StreamEvent::COMPRESS: {
                // Replace conversation with compressed summary
                conv.clear();
                ConversationEntry se;
                se.type = ConversationEntry::SYSTEM;
                se.content = "Previous conversation summary:\n"
                           + ev.tool_data.value("summary", std::string());
                se.timestamp = std::time(nullptr);
                conv.add_entry(se);

                std::string last_user = ev.tool_data.value("last_user", std::string());
                if (!last_user.empty()) {
                    ConversationEntry ue;
                    ue.type = ConversationEntry::USER;
                    ue.content = last_user;
                    ue.timestamp = std::time(nullptr);
                    conv.add_entry(ue);
                }

                ConversationEntry note;
                note.type = ConversationEntry::SYSTEM;
                note.content = "Context was compressed. Conversation summary injected.";
                note.timestamp = std::time(nullptr);
                conv.add_entry(note);
                break;
            }
            case StreamEvent::DONE: {
                conv.clear_streaming();
                uistate.processing = false;
                uistate.status_text = "";
                ui.set_state(uistate);
                break;
            }
            case StreamEvent::ERROR: {
                ConversationEntry ee;
                ee.type = ConversationEntry::ERROR;
                ee.content = ev.error_msg;
                ee.timestamp = std::time(nullptr);
                conv.add_entry(ee);
                uistate.processing = false;
                uistate.status_text = "Error";
                ui.set_state(uistate);
                break;
            }
        }
        ui.notify_update();
    });

    // Wire up compression status updates
    llm.set_status_callback([&](const StreamEvent& ev) {
        if (ev.type == StreamEvent::COMPRESS) {
            uistate.status_text = ev.text;
            ui.set_state(uistate);
        }
    });

    // Clear callback: clear LLM client state too
    ui.set_clear_callback([&]() {
        llm.clear_conversation();
    });

    // Cancel callback: cancel current request
    ui.set_cancel_callback([&]() {
        llm.cancel_current();
    });

    // Copy callback: copy last assistant answer to clipboard
    // Clipboard write callback (for click-to-copy)
    ui.set_clipboard_callback([&](const std::string& text) -> bool {
        return copy_to_clipboard(text);
    });

    // Copy callback: copy last assistant answer to clipboard
    ui.set_copy_callback([&]() {
        auto entries = conv.get_entries();
        for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
            if (it->type == ConversationEntry::ASSISTANT && !it->content.empty()) {
                if (copy_to_clipboard(it->content)) {
                    uistate.status_text = "Copied to clipboard";
                } else {
                    uistate.status_text = "Copy failed (install xclip or wl-copy)";
                }
                ui.set_state(uistate);
                return;
            }
        }
        uistate.status_text = "Nothing to copy";
        ui.set_state(uistate);
    });

    // Wire up deep search callback
    ui.set_deep_search_callback([&](const std::string& query) {
        uistate.processing = true;
        uistate.status_text = "Deep search: " + query;
        ui.set_state(uistate);
        llm.set_deep_search(true);
        llm.enqueue_message(query);
    });

    // Wire up send callback from UI to LLM
    ui.set_send_callback([&](const std::string& msg) {
        uistate.processing = true;
        uistate.status_text = "";
        ui.set_state(uistate);

        std::string skill = "";
        if (msg[0] == '/') {
            std::string cmdline = msg.substr(1);
            std::string cmd = cmdline;
            std::string args;
            size_t sp = cmdline.find(' ');
            if (sp != std::string::npos) {
                cmd = cmdline.substr(0, sp);
                args = cmdline.substr(sp + 1);
            }

            if (cmd == "skill") {
                skill = args;
                skill.erase(0, skill.find_first_not_of(" \t"));
                skill.erase(skill.find_last_not_of(" \t") + 1);
                uistate.status_text = "Skill: " + (skill.empty() ? "(default)" : skill);
                ui.set_state(uistate);
                return;
            }

            if (cmd == "help") {
                ConversationEntry he;
                he.type = ConversationEntry::SYSTEM;
                he.content =
                    "## Commands\n"
                    "\n"
                    "| Command | Description |\n"
                    "|---------|-------------|\n"
                    "| `/clear` | Clear conversation |\n"
                    "| `/help` | Show this help |\n"
                    "| `/skill <name>` | Switch skill |\n"
                    "| `/deepsearch <q>` | Start deep search |\n"
                    "| `/stats` | Show session statistics |\n"
                    "| `/model <name>` | Switch model (session only) |\n"
                    "| `/export` | Export conversation to markdown |\n"
                    "\n"
                    "### Shortcuts\n"
                    "\n"
                    "- **Ctrl+Y** — Copy last assistant answer\n"
                    "- **Ctrl+C** — Cancel current request\n"
                    "- **Ctrl+L** — Clear screen\n"
                    "- **Ctrl+Q** — Quit\n"
                    "- **PgUp/PgDn** — Scroll chat\n"
                    "- **TAB** — Command completion";
                he.timestamp = std::time(nullptr);
                conv.add_entry(he);
                ui.notify_update();
                uistate.processing = false;
                ui.set_state(uistate);
                return;
            }

            if (cmd == "stats") {
                auto entries = conv.get_entries();
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
                auto& msgs = llm.get_messages();
                int total_chars = llm.estimate_total_chars();
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
                    "| API messages | " + std::to_string(msgs.size()) + " |\n"
                    "| Est. context chars | " + std::to_string(total_chars) + " |\n"
                    "| Current time | " + timebuf + " |";
                se.timestamp = now;
                conv.add_entry(se);
                ui.notify_update();
                uistate.processing = false;
                ui.set_state(uistate);
                return;
            }

            if (cmd == "model") {
                if (args.empty()) {
                    ConversationEntry me;
                    me.type = ConversationEntry::SYSTEM;
                    me.content = "Current model: **" + config.model + "**\n"
                                 "Usage: `/model <name>` — change model for this session";
                    me.timestamp = std::time(nullptr);
                    conv.add_entry(me);
                } else {
                    config.model = args;
                    uistate.model_name = config.model;
                    ConversationEntry me;
                    me.type = ConversationEntry::SYSTEM;
                    me.content = "Switched model to **" + config.model + "**";
                    me.timestamp = std::time(nullptr);
                    conv.add_entry(me);
                }
                ui.notify_update();
                uistate.processing = false;
                ui.set_state(uistate);
                return;
            }

            if (cmd == "export") {
                auto entries = conv.get_entries();
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
                    conv.add_entry(me);
                } else {
                    ConversationEntry me;
                    me.type = ConversationEntry::ERROR;
                    me.content = "Error: could not write to " + filename;
                    me.timestamp = std::time(nullptr);
                    conv.add_entry(me);
                }
                ui.notify_update();
                uistate.processing = false;
                ui.set_state(uistate);
                return;
            }

            uistate.processing = false;
            ui.set_state(uistate);
            return;
        }

        llm.enqueue_message(msg, skill);
    });

    // Run the UI (blocking)
    ui.run();

    mcp_manager.stop_all();

    return 0;
}
