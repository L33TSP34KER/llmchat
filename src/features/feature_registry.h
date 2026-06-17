#pragma once
#include <string>
#include <vector>
#include <memory>
#include "feature.h"

class Config;

class FeatureRegistry {
public:
    void register_feature(std::unique_ptr<IFeature> feature);

    bool is_handled_tool(const std::string& tool_name) const;
    std::string execute_tool(const std::string& tool_name, const std::string& args_json);

    template <typename T>
    T* get_feature() const {
        for (auto& f : features_) {
            T* casted = dynamic_cast<T*>(f.get());
            if (casted) return casted;
        }
        return nullptr;
    }

    IFeature* get_feature_by_name(const std::string& name) const;

private:
    std::vector<std::unique_ptr<IFeature>> features_;
};
