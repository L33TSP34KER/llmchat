#include "review_command.h"
#include "conversation.h"
#include "ui/chat_ui.h"
#include "llm/client.h"
#include <ctime>
#include <cstdio>
#include <sstream>

bool ReviewCommand::execute(const std::string& args, CommandContext& ctx) {
    std::string base = args.empty() ? "HEAD" : args;

    // Run git diff
    std::string cmd = "git diff " + base + " 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        if (ctx.ui) ctx.ui->set_status_text("Error: failed to run git diff");
        return true;
    }

    std::string diff;
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) diff += buf;
    int rc = pclose(pipe);

    if (rc != 0 || diff.empty()) {
        if (ctx.ui) ctx.ui->set_status_text("No changes to review (or not a git repo)");
        return true;
    }

    // Truncate very large diffs
    if (diff.size() > 8000) {
        diff = diff.substr(0, 8000) + "\n... [diff truncated]";
    }

    std::string content = "Review the following git diff. Suggest fixes, potential bugs, "
                          "code improvements, and cleanup opportunities:\n\n```diff\n"
                          + diff + "\n```";

    if (ctx.ui) {
        ctx.ui->set_status_text("Reviewing code...");
        ctx.ui->set_processing(true);
    }
    if (ctx.llm) ctx.llm->enqueue_message(content);
    return true;
}
