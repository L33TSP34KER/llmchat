#include <iostream>
#include <csignal>
#include <clocale>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdio>
#include <sstream>

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

    // Start MCP servers asynchronously (don't block UI startup)
    MCPManager mcp_manager(&config);
    mcp_manager.start_all_async();
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

    // Wire up send callback from UI to LLM
    ui.set_send_callback([&](const std::string& msg) {
        uistate.processing = true;
        uistate.status_text = "";
        ui.set_state(uistate);

        std::string skill = "";
        // Check for skill command
        if (msg[0] == '/') {
            std::string cmd = msg.substr(1);
            if (cmd.substr(0, 5) == "skill") {
                skill = cmd.size() > 6 ? cmd.substr(6) : "";
                skill.erase(0, skill.find_first_not_of(" \t"));
                skill.erase(skill.find_last_not_of(" \t") + 1);
                uistate.status_text = "Skill: " + (skill.empty() ? "(default)" : skill);
                ui.set_state(uistate);
                return;
            }
            if (cmd == "help") {
                ConversationEntry he;
                he.type = ConversationEntry::SYSTEM;
                he.content = "Commands:\n"
                             "  /clear       Clear conversation\n"
                             "  /help        Show this help\n"
                             "  /skill <n>   Switch skill\n"
                             "Ctrl+Y:   Copy last assistant answer\n"
                             "Ctrl+C:   Cancel current request\n"
                             "Ctrl+L:   Clear screen\n"
                             "Ctrl+Q:   Quit\n"
                             "PgUp/PgDn: Scroll chat history";
                he.timestamp = std::time(nullptr);
                conv.add_entry(he);
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
