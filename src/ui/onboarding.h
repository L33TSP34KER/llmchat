#pragma once
#include <string>
#include "config.h"

namespace Onboarding {
    struct Info {
        std::string model_name;
        std::string api_endpoint;
        int max_context_chars = 80000;
        int tool_count = 0;
        int skill_count = 0;
        int memory_count = 0;
        int streak_days = 0;
        bool is_first_run = false;
    };

    bool show(const Info& info);
};
