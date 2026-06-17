#pragma once
#include "feature.h"

class WebFetchFeature : public IFeature {
public:
    std::string name() const override { return "webfetch"; }
    bool is_tool_provider() const override { return true; }
    bool handles_tool(const std::string& tool_name) const override;
    std::string execute_tool(const std::string& tool_name, const std::string& args_json) override;

private:
    std::string fetch_page(const std::string& url, bool as_markdown);
    std::string html_to_markdown(const std::string& html);
    std::string html_to_text(const std::string& html);
    std::string decode_html_entities(const std::string& s);
    void strip_tag(std::string& s, const std::string& tag);
    std::string strip_tags(const std::string& html);
};
