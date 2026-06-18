#pragma once
#include <string>
#include <vector>
#include <functional>
#include "config.h"

namespace Onboarding {
    struct ModelStats {
        std::string model_id;
        int64_t n_params = 0;
        int64_t n_ctx_train = 0;
        int64_t model_size = 0;
    };

    struct ModelsResponse {
        std::vector<std::string> ids;
        std::string first_id;
        int64_t first_n_params = 0;
        int64_t first_n_ctx_train = 0;
    };

    struct Info {
        std::string model_name;
        std::string api_endpoint;
        int max_context_chars = 80000;
        int tool_count = 0;
        int skill_count = 0;
        int memory_count = 0;
        int streak_days = 0;
        bool is_first_run = false;
        ModelStats model_stats;
        std::vector<std::string> available_models;
        std::function<void(const std::string&)> on_model_selected;
    };

    ModelsResponse fetch_models(const std::string& api_endpoint, const std::string& api_key);
    bool show(const Info& info);
};
