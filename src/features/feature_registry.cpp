#include "feature_registry.h"

void FeatureRegistry::register_feature(std::unique_ptr<IFeature> feature) {
    if (feature) {
        features_.push_back(std::move(feature));
    }
}

bool FeatureRegistry::is_handled_tool(const std::string& tool_name) const {
    for (auto& f : features_) {
        if (f->handles_tool(tool_name)) return true;
    }
    return false;
}

std::string FeatureRegistry::execute_tool(const std::string& tool_name, const std::string& args_json) {
    for (auto& f : features_) {
        if (f->handles_tool(tool_name)) {
            return f->execute_tool(tool_name, args_json);
        }
    }
    return "Error: unknown tool '" + tool_name + "'";
}

IFeature* FeatureRegistry::get_feature_by_name(const std::string& name) const {
    for (auto& f : features_) {
        if (f->name() == name) return f.get();
    }
    return nullptr;
}
