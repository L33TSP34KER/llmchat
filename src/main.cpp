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
#include "commands/command_registry.h"
#include "commands/clear_command.h"
#include "commands/copy_command.h"
#include "commands/deepsearch_command.h"
#include "commands/export_command.h"
#include "commands/help_command.h"
#include "commands/model_command.h"
#include "commands/skill_command.h"
#include "commands/stats_command.h"
#include "commands/search_command.h"
#include "commands/review_command.h"
#include "commands/temp_command.h"
#include "features/feature_registry.h"
#include "features/fileio_feature.h"
#include "features/memory_feature.h"
#include "features/terminal_feature.h"
#include "features/arxiv_feature.h"
#include "features/webfetch_feature.h"

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

    srand(time(nullptr));
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
    // Show config error as system message if any
    if (!config.last_error.empty()) {
        ConversationEntry ce;
        ce.type = ConversationEntry::SYSTEM;
        ce.content = config.last_error;
        ce.timestamp = std::time(nullptr);
        conv.add_entry(ce);
    }

    ChatUI ui(&conv, &config);
    LlmClient llm(&config);

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
        json stats_json = json::object();
        std::ifstream sf(stats_file);
        if (sf.good()) {
            try {
                json tmp;
                sf >> tmp;
                if (tmp.is_object()) stats_json = tmp;
            } catch (...) {}
        }
        if (stats_json.is_object()) {
            last_date = stats_json.value("last_date", "");
            streak_days = stats_json.value("streak", 0);
        }
        char today_buf[16];
        auto now_t = std::time(nullptr);
        std::strftime(today_buf, sizeof(today_buf), "%Y-%m-%d", std::localtime(&now_t));
        std::string today(today_buf);
        if (today != last_date) {
            if (!last_date.empty()) {
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
            if (stats_json.is_object()) {
                stats_json["streak"] = streak_days;
                stats_json["last_date"] = today;
                std::ofstream sf_out(stats_file);
                if (sf_out.good()) sf_out << stats_json.dump(2);
            }
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

    // Set up built-in feature registry
    FeatureRegistry features;
    features.register_feature(std::make_unique<FileIOFeature>());
    features.register_feature(std::make_unique<MemoryFeature>());
    features.register_feature(std::make_unique<TerminalFeature>());
    features.register_feature(std::make_unique<ArxivFeature>());
    features.register_feature(std::make_unique<WebFetchFeature>());
    llm.set_features(&features);

    config.save();

    // Load saved memories into LLM context at startup
    auto inject_memories = [&]() {
        std::string mem_path = Config::get_config_dir() + "/memory.json";
        std::ifstream mf(mem_path);
        if (!mf.is_open()) return;
        try {
            json mem;
            mf >> mem;
            if (mem.is_object() && !mem.empty()) {
                std::string ctx = "## Saved user information\n\n";
                for (auto it = mem.begin(); it != mem.end(); ++it) {
                    ctx += "- " + it.key() + ": " + it.value().get<std::string>() + "\n";
                }
                llm.add_system_message(ctx);
            }
        } catch (...) {}
    };
    inject_memories();

    // Set up command registry
    CommandRegistry cmd_registry;
    cmd_registry.register_command(std::make_unique<ClearCommand>());
    cmd_registry.register_command(std::make_unique<CopyCommand>());
    cmd_registry.register_command(std::make_unique<DeepSearchCommand>());
    cmd_registry.register_command(std::make_unique<ExportCommand>());
    cmd_registry.register_command(std::make_unique<HelpCommand>());
    cmd_registry.register_command(std::make_unique<ModelCommand>());
    cmd_registry.register_command(std::make_unique<SkillCommand>());
    cmd_registry.register_command(std::make_unique<StatsCommand>());
    cmd_registry.register_command(std::make_unique<SearchCommand>());
    cmd_registry.register_command(std::make_unique<TempCommand>());
    cmd_registry.register_command(std::make_unique<ReviewCommand>());

    UIState uistate;
    uistate.model_name = config.model;
    uistate.conversation_title = config.conversation_title;
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
            case StreamEvent::REASONING: {
                if (uistate.thinking_phrase.empty()) {
                    static const char* thinking_phrases[] = {
                        "Pondering the mysteries of the universe",
                        "Consulting the ancient scrolls",
                        "Gathering starlight for an answer",
                        "Brewing a potion of knowledge",
                        "Polishing the crystal ball",
                        "Feeding the hamsters in the thought-machine",
                        "Summoning a knowledgeable djinn",
                        "Reading tea leaves",
                        "Contemplating the meaning of 42",
                        "Whispering to the oracle",
                        "Dusting off the old textbooks",
                        "Staring intensely at the abyss",
                        "Sharpening the reasoning pencils",
                        "Walking the garden of forking paths",
                        "Rewinding the tape of thought",
                        "Chasing down a runaway idea",
                        "Connecting dots in hyperspace",
                        "Refueling the logic engines",
                        "Marinating in deep thought",
                        "Shaking the answer tree",
                        "Flipping through the great book of answers",
                        "Consulting the inner council of wisdom",
                        "Traversing the neural pathways",
                        "Unspooling the thread of logic",
                        "Decanting a fine vintage of reasoning",
                    };
                    int idx = rand() % 25;
                    uistate.thinking_phrase = thinking_phrases[idx];
                    uistate.status_text = "";
                    ui.set_state(uistate);
                }
                break;
            }
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
                uistate.thinking_phrase = "";
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
        inject_memories();
        config.conversation_title = "";
        uistate.conversation_title = "";
        ui.set_state(uistate);
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

    // Wire up auto-naming title callback
    llm.set_title_callback([&](const std::string& title) {
        config.conversation_title = title;
        uistate.conversation_title = title;
        uistate.status_text = "";
        ui.set_state(uistate);
    });

    // Handle title display in DONE events
    // (the DONE handler already resets status, but we keep title in model_name area)

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
        if (!msg.empty() && msg[0] == '/') {
            std::string cmdline = msg.substr(1);
            std::string cmd = cmdline;
            size_t sp = cmdline.find(' ');
            if (sp != std::string::npos) cmd = cmdline.substr(0, sp);

            // Handle agentic mode inline (simple wrapper)
            if (cmd == "agent") {
                std::string query = (sp != std::string::npos) ? cmdline.substr(sp + 1) : "";
                if (query.empty()) {
                    uistate.status_text = "Usage: /agent <task>";
                    uistate.processing = false;
                    ui.set_state(uistate);
                    return;
                }
                ConversationEntry ue;
                ue.type = ConversationEntry::USER;
                ue.content = "/agent " + query;
                ue.timestamp = std::time(nullptr);
                conv.add_entry(ue);
                uistate.processing = true;
                uistate.status_text = "Agentic: " + query.substr(0, 40);
                ui.set_state(uistate);
                llm.set_agentic(true);
                llm.enqueue_message(query);
                return;
            }

            CommandContext cmd_ctx;
            cmd_ctx.conv = &conv;
            cmd_ctx.config = &config;
            cmd_ctx.llm = &llm;
            cmd_ctx.ui = &ui;
            cmd_ctx.cmd_registry = &cmd_registry;
            cmd_ctx.clipboard_fn = [&](const std::string& t) -> bool { return copy_to_clipboard(t); };

            if (cmd_registry.execute(msg, cmd_ctx)) {
                uistate.conversation_title = config.conversation_title;
                return;
            }

            uistate.processing = false;
            ui.set_state(uistate);
            return;
        }

        uistate.processing = true;
        uistate.status_text = "";
        ui.set_state(uistate);
        llm.enqueue_message(msg, config.current_skill);
    });

    // Run the UI (blocking)
    ui.run();

    mcp_manager.stop_all();

    return 0;
}
