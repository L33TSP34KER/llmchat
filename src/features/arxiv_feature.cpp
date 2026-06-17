#include "arxiv_feature.h"
#include "http_client.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <algorithm>
#include <cctype>

using json = nlohmann::json;

bool ArxivFeature::handles_tool(const std::string& tool_name) const {
    return tool_name == "search_arxiv" || tool_name == "fetch_arxiv_paper";
}

std::string ArxivFeature::execute_tool(const std::string& tool_name, const std::string& args_json) {
    if (tool_name == "search_arxiv") {
        try {
            json args = json::parse(args_json);
            std::string query = args.value("query", "");
            if (query.empty()) return "Error: no query provided";
            int max_results = args.value("max_results", 5);
            if (max_results < 1) max_results = 1;
            if (max_results > 50) max_results = 50;
            return search_arxiv(query, max_results);
        } catch (...) { return "Error: invalid arguments"; }
    }
    if (tool_name == "fetch_arxiv_paper") {
        try {
            json args = json::parse(args_json);
            std::string id = args.value("id", "");
            if (id.empty()) return "Error: no paper ID provided";
            return fetch_paper(id);
        } catch (...) { return "Error: invalid arguments"; }
    }
    return "Error: unknown arxiv tool '" + tool_name + "'";
}

std::string ArxivFeature::decode_xml_entities(const std::string& s) {
    std::string r = s;
    auto replace_all = [&](const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = r.find(from, pos)) != std::string::npos) {
            r.replace(pos, from.size(), to);
            pos += to.size();
        }
    };
    replace_all("&amp;", "&");
    replace_all("&lt;", "<");
    replace_all("&gt;", ">");
    replace_all("&quot;", "\"");
    replace_all("&apos;", "'");
    return r;
}

std::string ArxivFeature::extract_xml_content(const std::string& xml, const std::string& tag) {
    std::string open = "<" + tag + ">";
    std::string close = "</" + tag + ">";
    size_t start = xml.find(open);
    if (start == std::string::npos) {
        open = "<" + tag + " ";
        start = xml.find(open);
        if (start != std::string::npos) {
            start = xml.find('>', start);
            if (start == std::string::npos) return "";
            start++;
        } else {
            return "";
        }
    } else {
        start += open.size();
    }
    size_t end = xml.find(close, start);
    if (end == std::string::npos) return "";
    return xml.substr(start, end - start);
}

std::string ArxivFeature::xml_to_text(const std::string& xml) {
    std::string text;
    bool in_tag = false;
    for (size_t i = 0; i < xml.size(); i++) {
        if (xml[i] == '<') { in_tag = true; continue; }
        if (xml[i] == '>') { in_tag = false; continue; }
        if (!in_tag) text += xml[i];
    }
    return text;
}

std::string ArxivFeature::search_arxiv(const std::string& query, int max_results) {
    std::string encoded_query;
    for (char c : query) {
        if (c == ' ') encoded_query += "+";
        else if (c == ':') encoded_query += "%3A";
        else if (c == '/') encoded_query += "%2F";
        else if (c == '&') encoded_query += "%26";
        else if (c == '?') encoded_query += "%3F";
        else if (c == '=') encoded_query += "%3D";
        else if (c == '#') encoded_query += "%23";
        else if (c == '%') encoded_query += "%25";
        else if (c == '"') encoded_query += "%22";
        else encoded_query += c;
    }

    std::string url = "http://export.arxiv.org/api/query?search_query=all:"
                    + encoded_query + "&start=0&max_results=" + std::to_string(max_results);

    HttpClient http;
    http.set_url(url);
    http.set_header("User-Agent", "llmchat/1.0");
    HttpResponse resp = http.perform();

    if (resp.status_code != 200) {
        return "Error: arXiv API returned HTTP " + std::to_string(resp.status_code);
    }

    std::string xml = resp.body;
    std::string result;
    int count = 0;

    size_t pos = 0;
    while ((pos = xml.find("<entry", pos)) != std::string::npos) {
        size_t entry_end = xml.find("</entry>", pos);
        if (entry_end == std::string::npos) break;
        std::string entry = xml.substr(pos, entry_end - pos + 8);
        pos = entry_end + 8;
        count++;

        std::string id = extract_xml_content(entry, "id");
        std::string title = extract_xml_content(entry, "title");
        std::string summary = extract_xml_content(entry, "summary");
        std::string published = extract_xml_content(entry, "published");

        title = decode_xml_entities(xml_to_text(title));
        summary = decode_xml_entities(xml_to_text(summary));

        std::string arxiv_id;
        size_t last_slash = id.rfind('/');
        if (last_slash != std::string::npos) {
            arxiv_id = id.substr(last_slash + 1);
        } else {
            arxiv_id = id;
        }

        while (!title.empty() && (title.front() == ' ' || title.front() == '\n')) title.erase(0, 1);
        while (!title.empty() && (title.back() == ' ' || title.back() == '\n')) title.pop_back();
        while (!summary.empty() && (summary.front() == ' ' || summary.front() == '\n')) summary.erase(0, 1);
        while (!summary.empty() && (summary.back() == ' ' || summary.back() == '\n')) summary.pop_back();
        if (!published.empty() && published.size() >= 10) published = published.substr(0, 10);

        if (summary.size() > 500) {
            summary = summary.substr(0, 500);
            size_t last_space = summary.rfind(' ');
            if (last_space != std::string::npos) summary = summary.substr(0, last_space);
            summary += "...";
        }

        result += "--- Paper " + std::to_string(count) + " ---\n";
        result += "Title: " + title + "\n";
        result += "arXiv ID: " + arxiv_id + "\n";
        result += "Published: " + published + "\n";
        result += "Abstract: " + summary + "\n";
        result += "URL: https://arxiv.org/abs/" + arxiv_id + "\n\n";
    }

    if (count == 0) {
        return "No papers found for query: " + query;
    }

    return result;
}

std::string ArxivFeature::fetch_paper(const std::string& id) {
    std::string clean_id = id;
    size_t pos;
    while ((pos = clean_id.find("http://arxiv.org/abs/")) != std::string::npos)
        clean_id = clean_id.substr(pos + 20);
    while ((pos = clean_id.find("https://arxiv.org/abs/")) != std::string::npos)
        clean_id = clean_id.substr(pos + 21);
    while ((pos = clean_id.find("arxiv.org/abs/")) != std::string::npos)
        clean_id = clean_id.substr(pos + 14);

    while (!clean_id.empty() && clean_id.front() == ' ') clean_id.erase(0, 1);
    while (!clean_id.empty() && clean_id.back() == ' ') clean_id.pop_back();

    std::string url = "http://export.arxiv.org/api/query?id_list=" + clean_id;

    HttpClient http;
    http.set_url(url);
    http.set_header("User-Agent", "llmchat/1.0");
    HttpResponse resp = http.perform();

    if (resp.status_code != 200) {
        return "Error: arXiv API returned HTTP " + std::to_string(resp.status_code);
    }

    std::string xml = resp.body;
    size_t entry_start = xml.find("<entry");
    if (entry_start == std::string::npos) {
        return "No paper found with ID: " + id;
    }
    size_t entry_end = xml.find("</entry>", entry_start);
    if (entry_end == std::string::npos) return "Error parsing arXiv response";
    std::string entry = xml.substr(entry_start, entry_end - entry_start + 8);

    std::string title = extract_xml_content(entry, "title");
    std::string summary = extract_xml_content(entry, "summary");
    std::string published = extract_xml_content(entry, "published");
    std::string arxiv_id = extract_xml_content(entry, "id");
    std::string doi = extract_xml_content(entry, "arxiv:doi");

    title = decode_xml_entities(xml_to_text(title));
    summary = decode_xml_entities(xml_to_text(summary));

    while (!title.empty() && (title.front() == ' ' || title.front() == '\n')) title.erase(0, 1);
    while (!title.empty() && (title.back() == ' ' || title.back() == '\n')) title.pop_back();
    while (!summary.empty() && (summary.front() == ' ' || summary.front() == '\n')) summary.erase(0, 1);
    while (!summary.empty() && (summary.back() == ' ' || summary.back() == '\n')) summary.pop_back();
    if (!published.empty() && published.size() >= 10) published = published.substr(0, 10);

    std::string short_id;
    size_t last_slash = arxiv_id.rfind('/');
    short_id = (last_slash != std::string::npos) ? arxiv_id.substr(last_slash + 1) : arxiv_id;

    std::string authors;
    size_t auth_pos = 0;
    int auth_count = 0;
    while ((auth_pos = entry.find("<author>", auth_pos)) != std::string::npos) {
        size_t auth_end = entry.find("</author>", auth_pos);
        if (auth_end == std::string::npos) break;
        std::string author_block = entry.substr(auth_pos, auth_end - auth_pos + 9);
        std::string name = extract_xml_content(author_block, "name");
        name = decode_xml_entities(xml_to_text(name));
        if (!authors.empty()) authors += ", ";
        authors += name;
        auth_count++;
        auth_pos = auth_end + 9;
    }

    std::string categories;
    size_t cat_pos = 0;
    while ((cat_pos = entry.find("<category", cat_pos)) != std::string::npos) {
        size_t cat_end = entry.find("/>", cat_pos);
        if (cat_end == std::string::npos) break;
        std::string cat_block = entry.substr(cat_pos, cat_end - cat_pos + 2);
        size_t term_pos = cat_block.find("term=\"");
        if (term_pos != std::string::npos) {
            term_pos += 6;
            size_t term_end = cat_block.find("\"", term_pos);
            if (term_end != std::string::npos) {
                if (!categories.empty()) categories += ", ";
                categories += cat_block.substr(term_pos, term_end - term_pos);
            }
        }
        cat_pos = cat_end + 2;
    }

    std::string result = "Title: " + title + "\n";
    result += "arXiv ID: " + short_id + "\n";
    result += "Published: " + published + "\n";
    result += "Authors: " + authors + "\n";
    if (!categories.empty()) result += "Categories: " + categories + "\n";
    if (!doi.empty()) {
        doi = decode_xml_entities(xml_to_text(doi));
        result += "DOI: " + doi + "\n";
    }
    result += "URL: https://arxiv.org/abs/" + short_id + "\n\n";
    result += "Abstract:\n" + summary + "\n";

    return result;
}
