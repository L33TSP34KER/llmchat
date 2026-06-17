#pragma once
#include "feature.h"

class ArxivFeature : public IFeature {
public:
    std::string name() const override { return "arxiv"; }
    bool is_tool_provider() const override { return true; }
    bool handles_tool(const std::string& tool_name) const override;
    std::string execute_tool(const std::string& tool_name, const std::string& args_json) override;

private:
    std::string search_arxiv(const std::string& query, int max_results);
    std::string fetch_paper(const std::string& id);
    std::string extract_xml_content(const std::string& xml, const std::string& tag);
    std::string xml_to_text(const std::string& xml);
    std::string decode_xml_entities(const std::string& s);
};
