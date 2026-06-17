#include "command_registry.h"
#include <sstream>

void CommandRegistry::register_command(std::unique_ptr<ICommand> cmd) {
    if (cmd) {
        commands_[cmd->name()] = std::move(cmd);
    }
}

bool CommandRegistry::execute(const std::string& input, CommandContext& ctx) {
    if (input.empty() || input[0] != '/') return false;

    std::string cmdline = input.substr(1);
    std::string cmd = cmdline;
    std::string args;
    size_t sp = cmdline.find(' ');
    if (sp != std::string::npos) {
        cmd = cmdline.substr(0, sp);
        args = cmdline.substr(sp + 1);
    }

    auto it = commands_.find(cmd);
    if (it != commands_.end()) {
        it->second->execute(args, ctx);
        return true;
    }

    return false;
}

std::vector<std::string> CommandRegistry::get_completions(const std::string& prefix) const {
    std::vector<std::string> result;
    for (auto& [name, cmd] : commands_) {
        std::string full = "/" + name;
        if (full.size() >= prefix.size() && full.substr(0, prefix.size()) == prefix) {
            result.push_back(full);
        }
    }
    return result;
}

std::string CommandRegistry::get_help_text() const {
    std::string text = "## Commands\n\n| Command | Description |\n|---------|-------------|\n";
    for (auto& [name, cmd] : commands_) {
        text += "| `/" + name + "` | " + cmd->description() + " |\n";
    }
    text += "\n### Shortcuts\n\n"
            "- **Ctrl+Y** — Copy last assistant answer\n"
            "- **Ctrl+C** — Cancel current request\n"
            "- **Ctrl+L** — Clear screen\n"
            "- **Ctrl+Q** — Quit\n"
            "- **PgUp/PgDn** — Scroll chat\n"
            "- **TAB** — Command completion";
    return text;
}
