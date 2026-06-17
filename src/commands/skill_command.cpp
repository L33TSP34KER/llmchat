#include "skill_command.h"
#include "config.h"
#include "ui/chat_ui.h"
#include <algorithm>

bool SkillCommand::execute(const std::string& args, CommandContext& ctx) {
    std::string skill = args;
    skill.erase(0, skill.find_first_not_of(" \t"));
    skill.erase(skill.find_last_not_of(" \t") + 1);
    if (ctx.ui) {
        ctx.ui->set_status_text("Skill: " + (skill.empty() ? "(default)" : skill));
    }
    return true;
}
