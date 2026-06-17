#include "webfetch_feature.h"
#include "http_client.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <algorithm>
#include <cctype>

using json = nlohmann::json;

bool WebFetchFeature::handles_tool(const std::string& tool_name) const {
    return tool_name == "fetch_web_page";
}

std::string WebFetchFeature::execute_tool(const std::string& tool_name, const std::string& args_json) {
    try {
        json args = json::parse(args_json);
        std::string url = args.value("url", "");
        if (url.empty()) return "Error: no URL provided";
        std::string format = args.value("format", "markdown");
        bool as_markdown = (format == "markdown");
        return fetch_page(url, as_markdown);
    } catch (...) { return "Error: invalid arguments"; }
}

std::string WebFetchFeature::decode_html_entities(const std::string& s) {
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
    replace_all("&#39;", "'");
    replace_all("&#x27;", "'");
    replace_all("&#x2F;", "/");
    replace_all("&#x60;", "`");
    replace_all("&#x3C;", "<");
    replace_all("&#x3E;", ">");
    replace_all("&#160;", " ");
    replace_all("&nbsp;", " ");
    replace_all("&copy;", "(c)");
    replace_all("&reg;", "(r)");
    replace_all("&mdash;", "\u2014");
    replace_all("&ndash;", "\u2013");
    replace_all("&hellip;", "...");
    replace_all("&lsquo;", "'");
    replace_all("&rsquo;", "'");
    replace_all("&ldquo;", "\"");
    replace_all("&rdquo;", "\"");
    replace_all("&bull;", " * ");
    replace_all("&middot;", ".");
    replace_all("&raquo;", ">>");
    replace_all("&laquo;", "<<");
    replace_all("&trade;", "(tm)");
    replace_all("&frac12;", "1/2");
    replace_all("&frac14;", "1/4");
    replace_all("&frac34;", "3/4");
    replace_all("&deg;", " degrees ");

    for (size_t i = 0; i + 3 < r.size();) {
        if (r[i] == '&' && r[i+1] == '#') {
            size_t j = i + 2;
            bool hex = false;
            if (j < r.size() && (r[j] == 'x' || r[j] == 'X')) { hex = true; j++; }
            std::string num;
            while (j < r.size() && std::isdigit(r[j])) { num += r[j]; j++; }
            if (j < r.size() && r[j] == ';' && !num.empty()) {
                int code = std::stoi(num, nullptr, hex ? 16 : 10);
                std::string replacement;
                if (code == 32) replacement = " ";
                else if (code == 60) replacement = "<";
                else if (code == 62) replacement = ">";
                else if (code == 38) replacement = "&";
                else if (code == 34) replacement = "\"";
                else if (code == 39) replacement = "'";
                else if (code == 8211) replacement = "\u2013";
                else if (code == 8212) replacement = "\u2014";
                else if (code == 8216) replacement = "'";
                else if (code == 8217) replacement = "'";
                else if (code == 8220) replacement = "\"";
                else if (code == 8221) replacement = "\"";
                else if (code == 8230) replacement = "...";
                else if (code >= 128) replacement = std::string(1, static_cast<char>(code > 127 ? '?' : code));
                else replacement = std::string(1, static_cast<char>(code));
                r.replace(i, j - i + 1, replacement);
                i += replacement.size();
                continue;
            }
        }
        i++;
    }

    return r;
}

void WebFetchFeature::strip_tag(std::string& s, const std::string& tag) {
    std::string open = "<" + tag;
    std::string close = "</" + tag + ">";
    size_t pos = 0;
    while ((pos = s.find(open, pos)) != std::string::npos) {
        size_t end = s.find('>', pos);
        if (end == std::string::npos) break;
        size_t close_pos = s.find(close, end);
        if (close_pos == std::string::npos) {
            s.erase(pos, end - pos + 1);
        } else {
            s.erase(pos, close_pos - pos + close.size());
        }
    }
}

std::string WebFetchFeature::strip_tags(const std::string& html) {
    std::string result;
    bool in_tag = false;
    bool in_script = false;
    bool in_style = false;
    std::string skip_tag;

    const std::string skip_tags_list[] = {
        "script", "style", "svg", "noscript", "iframe", "canvas",
        "nav", "header", "footer", "aside", "form", "input",
        "button", "select", "textarea", "label", "option", "optgroup",
        "picture", "source", "video", "audio", "track",
        "object", "embed", "param", "applet"
    };

    auto is_skip_tag = [&](const std::string& tag) -> bool {
        for (const auto& st : skip_tags_list) {
            if (tag == st || (tag.size() > st.size() && tag.substr(0, st.size()) == st && tag[st.size()] == ' ')) {
                return true;
            }
        }
        return false;
    };

    std::string lower;
    for (size_t i = 0; i < html.size(); i++) lower += std::tolower(html[i]);

    for (size_t i = 0; i < html.size(); i++) {
        if (skip_tag.empty() && !in_script && !in_style && html[i] == '<') {
            in_tag = true;
            if (i + 6 < html.size()) {
                std::string context = lower.substr(i, std::min(size_t(9), html.size() - i));
                if (context.substr(0, 7) == "<script") in_script = true;
                if (context.substr(0, 6) == "<style" && !in_script) in_style = true;
                if (!in_script && !in_style) {
                    size_t name_end = html.find_first_of(" >\n\t", i + 1);
                    if (name_end != std::string::npos) {
                        std::string tag_name = lower.substr(i + 1, name_end - i - 1);
                        if (is_skip_tag(tag_name)) {
                            skip_tag = tag_name;
                        }
                    }
                }
            }
            continue;
        }
        if (in_tag && html[i] == '>') {
            in_tag = false;
            if (in_script && i >= 8) {
                std::string check = lower.substr(std::max(size_t(0), i - 8), 9);
                if (check.find("</script") != std::string::npos) in_script = false;
            }
            if (in_style && i >= 7) {
                std::string check = lower.substr(std::max(size_t(0), i - 7), 8);
                if (check.find("</style") != std::string::npos) in_style = false;
            }
            continue;
        }
        if (!in_tag && !in_script && !in_style && skip_tag.empty()) {
            result += html[i];
        }
        if (!skip_tag.empty() && html[i] == '<') {
            std::string close = "</" + skip_tag + ">";
            std::string close_lower = close;
            if (i + close.size() <= html.size()) {
                std::string upcoming = lower.substr(i, close.size());
                if (upcoming == close_lower) {
                    i += close.size() - 1;
                    skip_tag.clear();
                    continue;
                }
            }
        }
    }

    result = decode_html_entities(result);

    std::string cleaned;
    for (size_t i = 0; i < result.size(); i++) {
        if (result[i] == '\r') continue;
        cleaned += result[i];
    }

    std::string final;
    int blank_lines = 0;
    for (size_t i = 0; i < cleaned.size(); i++) {
        if (cleaned[i] == '\n') {
            blank_lines++;
            if (blank_lines <= 2) final += '\n';
        } else {
            blank_lines = 0;
            final += cleaned[i];
        }
    }

    while (!final.empty() && final.back() == '\n') final.pop_back();

    return final;
}

std::string WebFetchFeature::html_to_markdown(const std::string& html) {
    std::string result;
    std::string lower;
    for (size_t i = 0; i < html.size(); i++) lower += std::tolower(html[i]);

    // Track inline formatting context
    struct MdCtx {
        bool strong = false;
        bool em = false;
        bool code = false;
        bool link_open = false;
        std::string link_url;
        bool list_item = false;
        bool blockquote = false;
    };
    MdCtx ctx;

    size_t i = 0;
    while (i < html.size()) {
        if (html[i] == '<') {
            size_t end = html.find('>', i);
            if (end == std::string::npos) break;
            std::string tag_content = html.substr(i + 1, end - i - 1);
            std::string tag_lower;
            for (char c : tag_content) tag_lower += std::tolower(c);

            size_t space_pos = tag_lower.find(' ');
            std::string tag_name = (space_pos != std::string::npos)
                ? tag_lower.substr(0, space_pos) : tag_lower;

            const std::string strip_tags_list[] = {
                "script", "style", "svg", "noscript", "iframe", "canvas",
                "nav", "header", "footer", "aside", "form", "input",
                "button", "select", "textarea", "label", "option", "optgroup",
                "picture", "source", "video", "audio", "track",
                "object", "embed", "param", "applet"
            };
            bool strip_content = false;
            for (const auto& st : strip_tags_list) {
                if (tag_name == st || (tag_name.size() > st.size() && tag_name.substr(0, st.size()) == st && tag_name[st.size()] == ' ')) {
                    std::string close = "</" + st + ">";
                    std::string close_lower = close;
                    size_t close_pos = lower.find(close_lower, end);
                    if (close_pos != std::string::npos) {
                        i = close_pos + close.size();
                        strip_content = true;
                    }
                    break;
                }
            }
            if (strip_content) continue;

            if (tag_name == "br" || tag_name == "br/") {
                result += "\n";
            } else if (tag_name == "p" || tag_name == "/p") {
                if (tag_name == "/p") result += "\n\n";
            } else if (tag_name == "/li") {
                result += "\n";
                ctx.list_item = false;
            } else if (tag_name == "/tr" || tag_name == "/table") {
                result += "\n";
            } else if (tag_name == "/div") {
                result += "\n";
            } else if (tag_name == "hr" || tag_name == "hr/") {
                result += "\n---\n";
            } else if (tag_name == "h1" || tag_name == "/h1") {
                if (tag_name == "/h1") result += "\n";
            } else if (tag_name == "h2" || tag_name == "/h2") {
                if (tag_name == "/h2") result += "\n";
            } else if (tag_name == "h3" || tag_name == "/h3") {
                if (tag_name == "/h3") result += "\n";
            } else if (tag_name == "h4" || tag_name == "/h4") {
                if (tag_name == "/h4") result += "\n";
            } else if (tag_name == "h5" || tag_name == "/h5") {
                if (tag_name == "/h5") result += "\n";
            } else if (tag_name == "h6" || tag_name == "/h6") {
                if (tag_name == "/h6") result += "\n";
            } else if (tag_name == "strong" || tag_name == "b" ||
                       tag_lower.find("strong ") == 0 || tag_lower.find("b ") == 0) {
                if (!ctx.strong) { result += "**"; ctx.strong = true; }
            } else if (tag_name == "/strong" || tag_name == "/b") {
                if (ctx.strong) { result += "**"; ctx.strong = false; }
            } else if (tag_name == "em" || tag_name == "i" ||
                       tag_lower.find("em ") == 0 || tag_lower.find("i ") == 0) {
                if (!ctx.em) { result += "*"; ctx.em = true; }
            } else if (tag_name == "/em" || tag_name == "/i") {
                if (ctx.em) { result += "*"; ctx.em = false; }
            } else if (tag_name == "code" || tag_lower.find("code ") == 0) {
                if (!ctx.code) { result += "`"; ctx.code = true; }
            } else if (tag_name == "/code") {
                if (ctx.code) { result += "`"; ctx.code = false; }
            } else if (tag_name == "pre" || tag_lower.find("pre ") == 0) {
                result += "\n```\n";
            } else if (tag_name == "/pre") {
                result += "\n```\n";
            } else if (tag_name == "blockquote" || tag_lower.find("blockquote ") == 0) {
                ctx.blockquote = true;
            } else if (tag_name == "/blockquote") {
                ctx.blockquote = false;
                result += "\n";
            } else if (tag_name == "ul" || tag_lower.find("ul ") == 0) {
                // nothing
            } else if (tag_name == "/ul") {
                result += "\n";
            } else if (tag_name == "ol" || tag_lower.find("ol ") == 0) {
                // nothing
            } else if (tag_name == "/ol") {
                result += "\n";
            } else if (tag_name == "li" || tag_lower.find("li ") == 0) {
                result += "\n- ";
                ctx.list_item = true;
            } else if (tag_name.find("h1") == 0 && tag_name.size() > 1 && tag_name[1] == ' ') {
                // <h1 with attributes
            } else if (tag_name.find("h2") == 0 && tag_name.size() > 1 && tag_name[1] == ' ') {
            } else if (tag_name.find("h3") == 0 && tag_name.size() > 1 && tag_name[1] == ' ') {
            } else if (tag_name.find("h4") == 0 && tag_name.size() > 1 && tag_name[1] == ' ') {
            } else if (tag_name.find("h5") == 0 && tag_name.size() > 1 && tag_name[1] == ' ') {
            } else if (tag_name.find("h6") == 0 && tag_name.size() > 1 && tag_name[1] == ' ') {
            } else if (tag_name == "a" || tag_lower.find("a ") == 0) {
                if (!ctx.link_open) {
                    // Extract href from the opening <a> tag and store it
                    size_t eq_pos = tag_content.find('=');
                    while (eq_pos != std::string::npos) {
                        std::string attr_name;
                        size_t an = eq_pos;
                        while (an > 0 && tag_content[an-1] == ' ') an--;
                        size_t an_start = tag_content.rfind(' ', an);
                        if (an_start == std::string::npos) an_start = 0;
                        else an_start++;
                        attr_name = tag_content.substr(an_start, an - an_start);
                        for (auto& c : attr_name) c = std::tolower(c);
                        if (attr_name == "href") {
                            char q = tag_content[eq_pos + 1];
                            if (q == '"' || q == '\'') {
                                size_t us = eq_pos + 2;
                                size_t ue = tag_content.find(q, us);
                                if (ue != std::string::npos) {
                                    ctx.link_url = tag_content.substr(us, ue - us);
                                    ctx.link_open = true;
                                    result += "[";
                                    break;
                                }
                            }
                        }
                        eq_pos = tag_content.find('=', eq_pos + 1);
                    }
                }
            } else if (tag_name == "/a") {
                if (ctx.link_open) {
                    ctx.link_open = false;
                    if (!ctx.link_url.empty()) {
                        result += "](" + ctx.link_url + ")";
                        ctx.link_url.clear();
                    } else {
                        result += "]()";
                    }
                }
            } else if (tag_name.find("img") == 0) {
                size_t alt_pos = tag_lower.find("alt=\"");
                std::string alt;
                if (alt_pos != std::string::npos) {
                    size_t as = tag_content.find('"', alt_pos - (tag_content.find("alt") - tag_lower.find("alt")) + 5);
                    size_t ae = tag_content.find('"', as + 1);
                    if (ae != std::string::npos) alt = tag_content.substr(as + 1, ae - as - 1);
                }
                size_t src_pos = tag_lower.find("src=\"");
                std::string src;
                if (src_pos != std::string::npos) {
                    size_t ss = tag_content.find('"', src_pos - (tag_content.find("src") - tag_lower.find("src")) + 5);
                    size_t se = tag_content.find('"', ss + 1);
                    if (se != std::string::npos) src = tag_content.substr(ss + 1, se - ss - 1);
                }
                if (!alt.empty() && !src.empty()) result += "![" + alt + "](" + src + ")";
                else if (!src.empty()) result += "![](" + src + ")";
            }

            i = end + 1;
        } else if (ctx.blockquote && html[i] == '\n') {
            result += "\n> ";
            i++;
        } else {
            result += html[i];
            i++;
        }
    }

    result = strip_tags(result);

    // Add header markers by looking at adjacent newlines and content pattern
    std::string final;
    {
        std::vector<std::string> lines;
        std::string line;
        for (size_t i = 0; i < result.size(); i++) {
            if (result[i] == '\n') {
                lines.push_back(line);
                line.clear();
            } else {
                line += result[i];
            }
        }
        if (!line.empty() || (!result.empty() && result.back() == '\n')) {
            lines.push_back(line);
        }

        for (auto& l : lines) {
            // Detect header-like lines (short all-caps or title-cased lines)
            if (l.size() < 100 && !l.empty()) {
                int upper = 0, total = 0;
                for (char c : l) {
                    if (std::isalpha((unsigned char)c)) {
                        total++;
                        if (std::isupper((unsigned char)c)) upper++;
                    }
                }
                // If mostly uppercase, likely a heading — prefix with ##
                if (total > 0 && upper * 10 >= total * 8 && l.size() < 60) {
                    l = "\n\n## " + l;
                    final += l + "\n";
                    continue;
                }
            }
            final += l + "\n";
        }
    }

    // Collapse blank lines
    std::string collapsed;
    int blank_lines = 0;
    for (size_t i = 0; i < final.size(); i++) {
        if (final[i] == '\n') {
            blank_lines++;
            if (blank_lines <= 2) collapsed += '\n';
        } else {
            blank_lines = 0;
            collapsed += final[i];
        }
    }

    while (!collapsed.empty() && collapsed.back() == '\n') collapsed.pop_back();

    if (collapsed.size() > 50000) {
        collapsed = collapsed.substr(0, 50000);
        size_t last_newline = collapsed.rfind('\n');
        if (last_newline != std::string::npos) collapsed = collapsed.substr(0, last_newline);
        collapsed += "\n\n[... content truncated at 50,000 characters]";
    }

    return collapsed;
}

std::string WebFetchFeature::html_to_text(const std::string& html) {
    std::string result = strip_tags(html);

    std::string final;
    int blank_lines = 0;
    for (size_t i = 0; i < result.size(); i++) {
        if (result[i] == '\n') {
            blank_lines++;
            if (blank_lines <= 2) final += '\n';
        } else {
            blank_lines = 0;
            final += result[i];
        }
    }

    while (!final.empty() && final.back() == '\n') final.pop_back();

    if (final.size() > 50000) {
        final = final.substr(0, 50000);
        size_t last_newline = final.rfind('\n');
        if (last_newline != std::string::npos) final = final.substr(0, last_newline);
        final += "\n\n[... content truncated at 50,000 characters]";
    }

    return final;
}

std::string WebFetchFeature::fetch_page(const std::string& url, bool as_markdown) {
    HttpClient http;
    http.set_url(url);
    http.set_header("User-Agent", "Mozilla/5.0 (compatible; llmchat/1.0)");
    http.set_header("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
    HttpResponse resp = http.perform();

    if (resp.status_code == 0) {
        return "Error: Failed to connect to " + url;
    }

    if (resp.status_code >= 400) {
        return "Error: HTTP " + std::to_string(resp.status_code) + " for " + url;
    }

    std::string content_type;
    (void)content_type;

    if (as_markdown) {
        return html_to_markdown(resp.body);
    } else {
        return html_to_text(resp.body);
    }
}
