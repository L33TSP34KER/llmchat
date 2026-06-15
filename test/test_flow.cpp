#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <functional>

#include "llm/provider.h"
#include "llm/tool_exec.h"
#include "config.h"
#include "conversation.h"
#include "ui/markdown.h"

using json = nlohmann::json;

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { \
    std::cout << "  " << name << "... "; \
    try { (void)0; } catch (...) {} \

#define END_TEST(result) \
    if (result) { std::cout << "PASS\n"; tests_passed++; } \
    else { std::cout << "FAIL\n"; tests_failed++; } \
} while(0)

// ============================================================
// SSE streaming parser tests
// ============================================================

void test_sse_content_tokens() {
    TEST("SSE parses content tokens");
    OpenAIProvider provider("http://dummy", "");
    std::string content;
    std::map<int, std::string> names;
    std::map<int, std::string> args;

    // Simulate a normal content response
    std::string chunk =
        "data: {\"choices\":[{\"delta\":{\"content\":\"Hello\"}}]}\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\" world\"}}]}\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"!\"}}]}\n"
        "data: [DONE]\n";

    std::vector<StreamEvent> events;
    provider.set_stream_callback([&](const StreamEvent& ev) {
        events.push_back(ev);
    });

    bool ok = provider.handle_stream_chunk(chunk, content, names, args);
    END_TEST(ok && events.size() == 4 &&
             events[0].type == StreamEvent::TOKEN && events[0].text == "Hello" &&
             events[1].type == StreamEvent::TOKEN && events[1].text == " world" &&
             events[2].type == StreamEvent::TOKEN && events[2].text == "!" &&
             events[3].type == StreamEvent::DONE);
}

void test_sse_tool_call() {
    TEST("SSE detects tool calls");
    OpenAIProvider provider("http://dummy", "");
    std::string content;
    std::map<int, std::string> names;
    std::map<int, std::string> args;

    std::string chunk =
        "data: {\"choices\":[{\"delta\":{\"role\":\"assistant\",\"content\":null}}]}\n"
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"function\":{\"name\":\"calculate\",\"arguments\":\"{\\\"expression\\\":\\\"1+1\\\"}\"}}]}}]}\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n"
        "data: [DONE]\n";

    std::vector<StreamEvent> events;
    provider.set_stream_callback([&](const StreamEvent& ev) {
        events.push_back(ev);
    });

    bool ok = provider.handle_stream_chunk(chunk, content, names, args);
    bool has_tool_call = false;
    bool has_done = false;
    for (auto& e : events) {
        if (e.type == StreamEvent::TOOL_CALL) has_tool_call = true;
        if (e.type == StreamEvent::DONE) has_done = true;
    }
    // Verify per-index tracking
    bool name_ok = names.count(0) && names[0] == "calculate";
    bool args_ok = args.count(0) && args[0].find("1+1") != std::string::npos;
    END_TEST(ok && has_tool_call && has_done && name_ok && args_ok);
}

void test_sse_reasoning_content() {
    TEST("SSE reasoning_content produces REASONING events");
    OpenAIProvider provider("http://dummy", "");
    std::string content;
    std::map<int, std::string> names;
    std::map<int, std::string> args;

    std::string chunk =
        "data: {\"choices\":[{\"delta\":{\"role\":\"assistant\",\"content\":null}}]}\n"
        "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"Thinking\"}}]}\n"
        "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\" step\"}}]}\n"
        "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\" by step\"}}]}\n"
        "data: [DONE]\n";

    std::vector<StreamEvent> events;
    provider.set_stream_callback([&](const StreamEvent& ev) {
        events.push_back(ev);
    });

    bool ok = provider.handle_stream_chunk(chunk, content, names, args);
    // reasoning_content should NOT add to 'content' parameter
    // But should emit REASONING events
    bool has_reasoning = false;
    int token_count = 0;
    for (auto& e : events) {
        if (e.type == StreamEvent::REASONING) { has_reasoning = true; }
        if (e.type == StreamEvent::TOKEN) { token_count++; }
    }
    END_TEST(ok && has_reasoning && token_count == 0 && content.empty());
}

void test_sse_reasoning_with_tool_call() {
    TEST("SSE reasoning then tool call flow");
    OpenAIProvider provider("http://dummy", "");
    std::string content;
    std::map<int, std::string> names;
    std::map<int, std::string> args;

    std::string chunk =
        "data: {\"choices\":[{\"delta\":{\"role\":\"assistant\",\"content\":null}}]}\n"
        "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"I should use calculate tool\"}}]}\n"
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"function\":{\"name\":\"calculate\",\"arguments\":\"{\\\"expression\\\":\\\"2+2\\\"}\"}}]}}]}\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n"
        "data: [DONE]\n";

    std::vector<StreamEvent> events;
    provider.set_stream_callback([&](const StreamEvent& ev) {
        events.push_back(ev);
    });

    bool ok = provider.handle_stream_chunk(chunk, content, names, args);
    bool has_tool_call = false;
    bool has_reasoning = false;
    int token_count = 0;
    for (auto& e : events) {
        if (e.type == StreamEvent::TOOL_CALL) has_tool_call = true;
        if (e.type == StreamEvent::REASONING) has_reasoning = true;
        if (e.type == StreamEvent::TOKEN) token_count++;
    }
    // reasoning should NOT pollute content
    // tool call should be detected
    // content should be empty (only reasoning, no content)
    bool name_ok = names.count(0) && names[0] == "calculate";
    END_TEST(ok && has_tool_call && has_reasoning && token_count == 0 && content.empty() && name_ok);
}

void test_sse_parallel_tool_calls() {
    TEST("SSE parallel tool calls");
    OpenAIProvider provider("http://dummy", "");
    std::string content;
    std::map<int, std::string> names;
    std::map<int, std::string> args;

    // Build SSE data programmatically to avoid escaping issues
    json tool0_args;
    tool0_args["q"] = "weather";
    json tool1_args;
    tool1_args["tz"] = "UTC";

    json delta0;
    delta0["role"] = "assistant";
    delta0["content"] = nullptr;
    json choice0;
    choice0["delta"] = delta0;

    json tc0;
    tc0["index"] = 0;
    tc0["function"]["name"] = "search_web";
    tc0["function"]["arguments"] = tool0_args.dump();

    json tc1;
    tc1["index"] = 1;
    tc1["function"]["name"] = "get_time";
    tc1["function"]["arguments"] = tool1_args.dump();

    json tc_arr;
    tc_arr.push_back(tc0);
    tc_arr.push_back(tc1);
    json delta1;
    delta1["tool_calls"] = tc_arr;
    json choice1;
    choice1["delta"] = delta1;

    json delta2 = json::object();
    json choice2;
    choice2["delta"] = delta2;
    choice2["finish_reason"] = "tool_calls";

    json msg1;
    msg1["choices"] = json::array({choice0});
    json msg2;
    msg2["choices"] = json::array({choice1});
    json msg3;
    msg3["choices"] = json::array({choice2});

    std::string chunk =
        "data: " + msg1.dump() + "\n"
        "data: " + msg2.dump() + "\n"
        "data: " + msg3.dump() + "\n"
        "data: [DONE]\n";

    std::vector<StreamEvent> events;
    provider.set_stream_callback([&](const StreamEvent& ev) {
        events.push_back(ev);
    });

    bool ok = provider.handle_stream_chunk(chunk, content, names, args);

    // Check per-index tracking in the maps
    bool maps_ok = names.count(0) && names[0] == "search_web" &&
                   names.count(1) && names[1] == "get_time" &&
                   args.count(0) && args[0].find("weather") != std::string::npos &&
                   args.count(1) && args[1].find("UTC") != std::string::npos;

    // Check the event has a "calls" array with both tools
    bool event_ok = false;
    bool has_done = false;
    for (auto& e : events) {
        if (e.type == StreamEvent::TOOL_CALL) {
            if (e.tool_data.contains("calls") && e.tool_data["calls"].is_array()) {
                auto& calls = e.tool_data["calls"];
                event_ok = calls.size() == 2 &&
                           calls[0]["name"] == "search_web" &&
                           calls[1]["name"] == "get_time" &&
                           calls[0]["arguments"].get<std::string>().find("weather") != std::string::npos &&
                           calls[1]["arguments"].get<std::string>().find("UTC") != std::string::npos;
            }
        }
        if (e.type == StreamEvent::DONE) has_done = true;
    }

    END_TEST(ok && maps_ok && event_ok && has_done);
}

void test_sse_parallel_tool_calls_streaming_args() {
    TEST("SSE parallel tool calls with streaming args");
    OpenAIProvider provider("http://dummy", "");
    std::string content;
    std::map<int, std::string> names;
    std::map<int, std::string> args;

    // Build chunk1: initial role and partial arguments for both tools
    json delta0;
    delta0["role"] = "assistant";
    delta0["content"] = nullptr;
    json choice0;
    choice0["delta"] = delta0;

    json tc0_chunk1;
    tc0_chunk1["index"] = 0;
    tc0_chunk1["function"]["name"] = "search_web";
    tc0_chunk1["function"]["arguments"] = "{\"q\":\"we";

    json tc1_chunk1;
    tc1_chunk1["index"] = 1;
    tc1_chunk1["function"]["name"] = "get_time";
    tc1_chunk1["function"]["arguments"] = "{\"tz\":\"U";

    json tc_arr1;
    tc_arr1.push_back(tc0_chunk1);
    tc_arr1.push_back(tc1_chunk1);
    json delta1;
    delta1["tool_calls"] = tc_arr1;
    json choice1;
    choice1["delta"] = delta1;

    json msg1;
    msg1["choices"] = json::array({choice0});
    json msg2;
    msg2["choices"] = json::array({choice1});

    std::string chunk1 =
        "data: " + msg1.dump() + "\n"
        "data: " + msg2.dump() + "\n";

    // Build chunk2: remaining arguments and finish_reason
    json tc0_chunk2;
    tc0_chunk2["index"] = 0;
    tc0_chunk2["function"]["arguments"] = "ather\"}";

    json tc1_chunk2;
    tc1_chunk2["index"] = 1;
    tc1_chunk2["function"]["arguments"] = "TC\"}";

    json tc_arr2;
    tc_arr2.push_back(tc0_chunk2);
    tc_arr2.push_back(tc1_chunk2);
    json delta2;
    delta2["tool_calls"] = tc_arr2;
    json choice2;
    choice2["delta"] = delta2;

    json delta3 = json::object();
    json choice3;
    choice3["delta"] = delta3;
    choice3["finish_reason"] = "tool_calls";

    json msg3;
    msg3["choices"] = json::array({choice2});
    json msg4;
    msg4["choices"] = json::array({choice3});

    std::string chunk2 =
        "data: " + msg3.dump() + "\n"
        "data: " + msg4.dump() + "\n"
        "data: [DONE]\n";

    std::vector<StreamEvent> events;
    provider.set_stream_callback([&](const StreamEvent& ev) {
        events.push_back(ev);
    });

    // Process chunks sequentially (as they would arrive over the wire)
    bool ok1 = provider.handle_stream_chunk(chunk1, content, names, args);
    bool ok2 = provider.handle_stream_chunk(chunk2, content, names, args);

    // Check per-index accumulation across chunks
    bool maps_ok = names.count(0) && names[0] == "search_web" &&
                   names.count(1) && names[1] == "get_time" &&
                   args.count(0) && args[0] == "{\"q\":\"weather\"}" &&
                   args.count(1) && args[1] == "{\"tz\":\"UTC\"}";

    // Check the TOOL_CALL event has both calls with complete args
    bool event_ok = false;
    for (auto& e : events) {
        if (e.type == StreamEvent::TOOL_CALL) {
            if (e.tool_data.contains("calls") && e.tool_data["calls"].is_array()) {
                auto& calls = e.tool_data["calls"];
                event_ok = calls.size() == 2 &&
                           calls[0]["name"] == "search_web" &&
                           calls[1]["name"] == "get_time" &&
                           calls[0]["arguments"] == "{\"q\":\"weather\"}" &&
                           calls[1]["arguments"] == "{\"tz\":\"UTC\"}";
            }
        }
    }

    END_TEST(ok1 && ok2 && maps_ok && event_ok);
}

// ============================================================
// Tool execution tests
// ============================================================

void test_shell_tool_execution() {
    TEST("Shell tool execution - calculate");
    ToolDefinition tool;
    tool.name = "calculate";
    tool.command = "python3 -c \"print(eval('{expression}'))\"";
    tool.input_schema = R"({
        "type": "object",
        "properties": {
            "expression": {"type": "string"}
        },
        "required": ["expression"]
    })"_json;

    std::string result = tool_exec::execute_shell(tool, "{\"expression\": \"1+1\"}");
    // Result should contain "2" (strip whitespace)
    bool ok = !result.empty() && result.find("2") != std::string::npos;
    END_TEST(ok);
}

void test_shell_tool_with_modulo() {
    TEST("Shell tool handles modulo operator");
    ToolDefinition tool;
    tool.name = "calculate";
    tool.command = "python3 -c \"print(eval('{expression}'))\"";
    tool.input_schema = R"({
        "type": "object",
        "properties": {
            "expression": {"type": "string"}
        },
        "required": ["expression"]
    })"_json;

    std::string result = tool_exec::execute_shell(tool, "{\"expression\": \"193 % 2\"}");
    bool ok = !result.empty() && result.find("1") != std::string::npos;
    END_TEST(ok);
}

void test_shell_tool_error() {
    TEST("Shell tool handles error gracefully");
    ToolDefinition tool;
    tool.name = "bad_tool";
    tool.command = "python3 -c \"print(eval('{expr}'))\"";
    tool.input_schema = R"({
        "type": "object",
        "properties": {
            "expr": {"type": "string"}
        },
        "required": ["expr"]
    })"_json;

    std::string result = tool_exec::execute_shell(tool, "{\"expr\": \"bad_syntax!!!\"}");
    // Should return error message, not crash
    bool ok = !result.empty();
    END_TEST(ok);
}

void test_is_mcp_tool() {
    TEST("is_mcp_tool detection");
    END_TEST(tool_exec::is_mcp_tool("mcp_calculate") &&
             tool_exec::is_mcp_tool("mcp_") &&
             !tool_exec::is_mcp_tool("calculate") &&
             !tool_exec::is_mcp_tool(""));
}

// ============================================================
// Full live integration test (requires running server)
// ============================================================

void test_live_api_streaming() {
    TEST("Live API: streaming content");
    OpenAIProvider provider("http://localhost:8080/v1/chat/completions", "");

    json payload;
    payload["model"] = "llama3.2";
    payload["stream"] = true;
    payload["messages"] = json::array();
    json sys; sys["role"] = "system"; sys["content"] = "You are a helpful assistant. Keep answers very short.";
    payload["messages"].push_back(sys);
    json usr; usr["role"] = "user"; usr["content"] = "Say hello in one word";
    payload["messages"].push_back(usr);

    bool got_token = false;
    bool got_done = false;
    std::string error;

    provider.set_stream_callback([&](const StreamEvent& ev) {
        if (ev.type == StreamEvent::TOKEN) got_token = true;
        if (ev.type == StreamEvent::DONE) got_done = true;
        if (ev.type == StreamEvent::ERROR) error = ev.error_msg;
    });

    bool ok = provider.send_request(payload.dump());
    if (!ok) {
        std::cout << "FAIL (request failed: " << error << ")\n";
        tests_failed++;
        return;
    }
    END_TEST(got_token && got_done);
}

void test_live_api_tool_call() {
    TEST("Live API: tool call detection");
    OpenAIProvider provider("http://localhost:8080/v1/chat/completions", "");

    json payload;
    payload["model"] = "llama3.2";
    payload["stream"] = true;
    json sys; sys["role"] = "system"; sys["content"] = "You are a helpful assistant with access to tools. Use them when appropriate.";
    payload["messages"] = json::array();
    payload["messages"].push_back(sys);
    json usr; usr["role"] = "user"; usr["content"] = "Calculate 1+1";
    payload["messages"].push_back(usr);

    json tools = json::array();
    json calc;
    calc["type"] = "function";
    calc["function"]["name"] = "calculate";
    calc["function"]["description"] = "Evaluate a mathematical expression";
    calc["function"]["parameters"] = {{"type", "object"},
        {"properties", {{"expression", {{"type", "string"}}}}},
        {"required", json::array({"expression"})}};
    tools.push_back(calc);
    payload["tools"] = tools;

    bool got_tool_call = false;
    bool got_done = false;
    bool got_reasoning = false;
    bool got_content = false;
    std::string error;

    provider.set_stream_callback([&](const StreamEvent& ev) {
        if (ev.type == StreamEvent::TOOL_CALL) got_tool_call = true;
        if (ev.type == StreamEvent::DONE) got_done = true;
        if (ev.type == StreamEvent::REASONING) got_reasoning = true;
        if (ev.type == StreamEvent::TOKEN) got_content = true;
        if (ev.type == StreamEvent::ERROR) error = ev.error_msg;
    });

    bool ok = provider.send_request(payload.dump());
    if (!ok) {
        std::cout << "FAIL (request failed: " << error << ")\n";
        tests_failed++;
        return;
    }
    END_TEST(got_tool_call && got_done && !got_content);
}

// ============================================================
// LaTeX → Unicode conversion tests
// ============================================================

static bool contains(const std::string& s, const std::string& sub) {
    return s.find(sub) != std::string::npos;
}

void test_latex_greek_letters() {
    TEST("LaTeX: Greek letters");
    bool ok = true;
    ok = ok && md_latex_to_unicode("\\alpha") == "\xce\xb1";
    ok = ok && md_latex_to_unicode("\\beta") == "\xce\xb2";
    ok = ok && md_latex_to_unicode("\\gamma") == "\xce\xb3";
    ok = ok && md_latex_to_unicode("\\delta") == "\xce\xb4";
    ok = ok && md_latex_to_unicode("\\pi") == "\xcf\x80";
    ok = ok && md_latex_to_unicode("\\sigma") == "\xcf\x83";
    ok = ok && md_latex_to_unicode("\\omega") == "\xcf\x89";
    ok = ok && md_latex_to_unicode("\\Gamma") == "\xce\x93";
    ok = ok && md_latex_to_unicode("\\Delta") == "\xce\x94";
    ok = ok && md_latex_to_unicode("\\Omega") == "\xce\xa9";
    ok = ok && md_latex_to_unicode("\\varepsilon") == "\xce\xb5";
    ok = ok && md_latex_to_unicode("\\varphi") == "\xcf\x95";
    END_TEST(ok);
}

void test_latex_superscript_subscript() {
    TEST("LaTeX: superscripts and subscripts");
    bool ok = true;
    ok = ok && md_latex_to_unicode("x^2") == "x\xc2\xb2";
    ok = ok && md_latex_to_unicode("x_1") == "x\xe2\x82\x81";
    ok = ok && md_latex_to_unicode("x^{2y}") == "x\xc2\xb2\xe1\x81\xbf";
    ok = ok && md_latex_to_unicode("x_{ij}") == "x\xe2\x82\x92\xe2\x82\x97";
    ok = ok && md_latex_to_unicode("a^{b}") == "a\xe2\x81\xbf";
    ok = ok && md_latex_to_unicode("x^{y+z}") == "x\xe1\x81\xbf\xe2\x81\xba\xe1\xb5\x96";
    END_TEST(ok);
}

void test_latex_mathbf() {
    TEST("LaTeX: \\mathbf{}");
    std::string r = md_latex_to_unicode("\\mathbf{A}a");
    bool ok = true;
    ok = ok && r.size() > 2;
    // A should be bold, a should be normal
    ok = ok && r != "Aa";
    END_TEST(ok);
}

void test_latex_mathbb() {
    TEST("LaTeX: \\mathbb{}");
    bool ok = true;
    ok = ok && contains(md_latex_to_unicode("\\mathbb{R}"), "\xe2\x84\x9d"); // ℝ
    ok = ok && contains(md_latex_to_unicode("\\mathbb{N}"), "\xe2\x84\x95"); // ℕ
    ok = ok && contains(md_latex_to_unicode("\\mathbb{Z}"), "\xe2\x84\xa4"); // ℤ
    ok = ok && contains(md_latex_to_unicode("\\mathbb{C}"), "\xe2\x84\x82"); // ℂ
    ok = ok && contains(md_latex_to_unicode("\\mathbb{Q}"), "\xe2\x84\x9a"); // ℚ
    END_TEST(ok);
}

void test_latex_mathcal() {
    TEST("LaTeX: \\mathcal{}");
    std::string r = md_latex_to_unicode("\\mathcal{L}");
    bool ok = contains(r, "\xe2\x84\x92"); // ℒ
    END_TEST(ok);
}

void test_latex_mathfrak() {
    TEST("LaTeX: \\mathfrak{}");
    std::string r = md_latex_to_unicode("\\mathfrak{g}");
    // Should not be plain "g"
    bool ok = r != "g";
    END_TEST(ok);
}

void test_latex_frac() {
    TEST("LaTeX: \\frac{a}{b}");
    bool ok = true;
    ok = ok && md_latex_to_unicode("\\frac{1}{2}") == "1/2";
    ok = ok && md_latex_to_unicode("\\frac{x}{y}") == "x/y";
    ok = ok && md_latex_to_unicode("\\frac{\\alpha}{\\beta}") == "\xce\xb1/\xce\xb2";
    ok = ok && md_latex_to_unicode("\\frac{a+b}{c-d}") == "a+b/c-d";
    ok = ok && md_latex_to_unicode("\\frac{12}{34}") == "12/34";
    END_TEST(ok);
}

void test_latex_dfrac_tfrac() {
    TEST("LaTeX: \\dfrac{} \\tfrac{}");
    bool ok = true;
    ok = ok && md_latex_to_unicode("\\dfrac{1}{2}") == "1/2";
    ok = ok && md_latex_to_unicode("\\tfrac{3}{4}") == "3/4";
    END_TEST(ok);
}

void test_latex_pmod() {
    TEST("LaTeX: \\pmod{}");
    bool ok = true;
    ok = ok && md_latex_to_unicode("\\pmod{n}") == " (mod n)";
    ok = ok && md_latex_to_unicode("x \\pmod{p}") == "x (mod p)";
    ok = ok && md_latex_to_unicode("\\pmod{2k+1}") == " (mod 2k+1)";
    END_TEST(ok);
}

void test_latex_bmod() {
    TEST("LaTeX: \\bmod");
    bool ok = true;
    ok = ok && contains(md_latex_to_unicode("a \\bmod b"), "mod");
    END_TEST(ok);
}

void test_latex_binom() {
    TEST("LaTeX: \\binom{n}{k}");
    std::string r = md_latex_to_unicode("\\binom{n}{k}");
    bool ok = contains(r, "choose") && contains(r, "n") && contains(r, "k");
    END_TEST(ok);
}

void test_latex_operators() {
    TEST("LaTeX: sum/int/prod/oint");
    bool ok = true;
    ok = ok && md_latex_to_unicode("\\sum") == "\xe2\x88\x91";
    ok = ok && md_latex_to_unicode("\\int") == "\xe2\x88\xab";
    ok = ok && md_latex_to_unicode("\\prod") == "\xe2\x88\x8f";
    ok = ok && md_latex_to_unicode("\\oint") == "\xe2\x88\xae";
    ok = ok && md_latex_to_unicode("\\partial") == "\xe2\x88\x82";
    ok = ok && md_latex_to_unicode("\\nabla") == "\xe2\x88\x87";
    END_TEST(ok);
}

void test_latex_arrows() {
    TEST("LaTeX: arrows");
    bool ok = true;
    ok = ok && md_latex_to_unicode("\\rightarrow") == "\xe2\x86\x92";
    ok = ok && md_latex_to_unicode("\\leftarrow") == "\xe2\x86\x90";
    ok = ok && md_latex_to_unicode("\\Rightarrow") == "\xe2\x87\x92";
    ok = ok && md_latex_to_unicode("\\mapsto") == "\xe2\x86\xa6";
    ok = ok && md_latex_to_unicode("\\implies") == "\xe2\x9f\xb9";
    ok = ok && md_latex_to_unicode("\\iff") == "\xe2\x9f\xba";
    END_TEST(ok);
}

void test_latex_relations() {
    TEST("LaTeX: relations");
    bool ok = true;
    ok = ok && md_latex_to_unicode("\\neq") == "\xe2\x89\xa0";
    ok = ok && md_latex_to_unicode("\\leq") == "\xe2\x89\xa4";
    ok = ok && md_latex_to_unicode("\\geq") == "\xe2\x89\xa5";
    ok = ok && md_latex_to_unicode("\\approx") == "\xe2\x89\x88";
    ok = ok && md_latex_to_unicode("\\equiv") == "\xe2\x89\xa1";
    ok = ok && md_latex_to_unicode("\\subset") == "\xe2\x8a\x82";
    ok = ok && md_latex_to_unicode("\\subseteq") == "\xe2\x8a\x86";
    ok = ok && md_latex_to_unicode("\\in") == "\xe2\x88\x88";
    ok = ok && md_latex_to_unicode("\\notin") == "\xe2\x88\x89";
    END_TEST(ok);
}

void test_latex_symbols() {
    TEST("LaTeX: symbols");
    bool ok = true;
    ok = ok && md_latex_to_unicode("\\infty") == "\xe2\x88\x9e";
    ok = ok && md_latex_to_unicode("\\emptyset") == "\xe2\x88\x85";
    ok = ok && md_latex_to_unicode("\\forall") == "\xe2\x88\x80";
    ok = ok && md_latex_to_unicode("\\exists") == "\xe2\x88\x83";
    ok = ok && md_latex_to_unicode("\\neg") == "\xc2\xac";
    ok = ok && md_latex_to_unicode("\\wedge") == "\xe2\x88\xa7";
    ok = ok && md_latex_to_unicode("\\vee") == "\xe2\x88\xa8";
    ok = ok && md_latex_to_unicode("\\oplus") == "\xe2\x8a\x95";
    ok = ok && md_latex_to_unicode("\\otimes") == "\xe2\x8a\x97";
    ok = ok && md_latex_to_unicode("\\times") == "\xc3\x97";
    ok = ok && md_latex_to_unicode("\\div") == "\xc3\xb7";
    ok = ok && md_latex_to_unicode("\\pm") == "\xc2\xb1";
    ok = ok && md_latex_to_unicode("\\cdot") == "\xc2\xb7";
    ok = ok && md_latex_to_unicode("\\dots") == "\xe2\x80\xa6";
    END_TEST(ok);
}

void test_latex_sqrt() {
    TEST("LaTeX: \\sqrt{}");
    bool ok = true;
    ok = ok && contains(md_latex_to_unicode("\\sqrt{x}"), "\xe2\x88\x9a");
    ok = ok && contains(md_latex_to_unicode("\\sqrt{2}"), "\xe2\x88\x9a");
    ok = ok && md_latex_to_unicode("\\sqrt{x}") != "x";
    END_TEST(ok);
}

void test_latex_text() {
    TEST("LaTeX: \\text{}");
    bool ok = true;
    ok = ok && md_latex_to_unicode("\\text{hello}") == "hello";
    ok = ok && md_latex_to_unicode("\\text{something}") == "something";
    END_TEST(ok);
}

void test_latex_unknown_commands() {
    TEST("LaTeX: unknown command passthrough");
    bool ok = true;
    ok = ok && contains(md_latex_to_unicode("\\unknowncmd"), "unknowncmd");
    ok = ok && contains(md_latex_to_unicode("\\foobarbaz"), "foobarbaz");
    // Known commands should NOT passthrough
    ok = ok && md_latex_to_unicode("\\alpha") != "\\alpha";
    END_TEST(ok);
}

void test_latex_empty() {
    TEST("LaTeX: empty input");
    bool ok = true;
    ok = ok && md_latex_to_unicode("") == "";
    ok = ok && md_latex_to_unicode("  ") == "  ";
    END_TEST(ok);
}

void test_latex_complex_expression() {
    TEST("LaTeX: complex expression");
    std::string r = md_latex_to_unicode("\\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}");
    bool ok = contains(r, "/") && contains(r, "\xc2\xb1") && contains(r, "\xe2\x88\x9a");
    END_TEST(ok);
}

void test_latex_mixed_text_and_commands() {
    TEST("LaTeX: mixed text and commands");
    std::string r = md_latex_to_unicode("E = mc^2");
    bool ok = r == "E = mc\xc2\xb2";
    END_TEST(ok);
}

void test_latex_tilde() {
    TEST("LaTeX: \\tilde{}");
    bool ok = contains(md_latex_to_unicode("\\tilde{x}"), "~");
    END_TEST(ok);
}

void test_latex_hat() {
    TEST("LaTeX: \\hat{}");
    bool ok = contains(md_latex_to_unicode("\\hat{y}"), "^");
    END_TEST(ok);
}

void test_latex_vec() {
    TEST("LaTeX: \\vec{}");
    bool ok = contains(md_latex_to_unicode("\\vec{v}"), "\xe2\x86\x92");
    END_TEST(ok);
}

void test_latex_set_minus() {
    TEST("LaTeX: \\setminus");
    bool ok = md_latex_to_unicode("\\setminus") == "\xe2\x88\x96";
    END_TEST(ok);
}

void test_latex_aleph() {
    TEST("LaTeX: \\aleph");
    bool ok = md_latex_to_unicode("\\aleph") == "\xe2\x84\xb5";
    END_TEST(ok);
}

void test_latex_hbar() {
    TEST("LaTeX: \\hbar / \\hslash");
    bool ok = true;
    ok = ok && md_latex_to_unicode("\\hbar") == "\xe2\x84\x8f";
    ok = ok && md_latex_to_unicode("\\hslash") == "\xe2\x84\x8f";
    END_TEST(ok);
}

void test_latex_ell() {
    TEST("LaTeX: \\ell");
    bool ok = md_latex_to_unicode("\\ell") == "\xe2\x84\x93";
    END_TEST(ok);
}

void test_latex_dots() {
    TEST("LaTeX: dots variants");
    bool ok = true;
    ok = ok && md_latex_to_unicode("\\dots") == "\xe2\x80\xa6";
    ok = ok && md_latex_to_unicode("\\cdots") == "\xe2\x8b\xaf";
    ok = ok && md_latex_to_unicode("\\vdots") == "\xe2\x8b\xae";
    ok = ok && md_latex_to_unicode("\\ddots") == "\xe2\x8b\xb1";
    END_TEST(ok);
}

void test_latex_mathrm() {
    TEST("LaTeX: \\mathrm{}");
    bool ok = md_latex_to_unicode("\\mathrm{sin}") == "sin";
    END_TEST(ok);
}

void test_latex_mod_with_mod() {
    TEST("LaTeX: \\mod{} (braced mod)");
    bool ok = contains(md_latex_to_unicode("\\mod{n}"), "mod");
    END_TEST(ok);
}

void test_latex_operatorname() {
    TEST("LaTeX: \\operatorname{}");
    bool ok = md_latex_to_unicode("\\operatorname{rank}") == "rank";
    END_TEST(ok);
}

void test_latex_braces_removed() {
    TEST("LaTeX: braces are removed");
    bool ok = true;
    ok = ok && md_latex_to_unicode("{abc}") == "abc";
    ok = ok && md_latex_to_unicode("a{b}c") == "abc";
    END_TEST(ok);
}

void test_latex_tilde_as_space() {
    TEST("LaTeX: ~ as non-breaking space");
    bool ok = md_latex_to_unicode("a~b") == "a b";
    END_TEST(ok);
}

void test_latex_mathit() {
    TEST("LaTeX: \\mathit{}");
    std::string r = md_latex_to_unicode("\\mathit{A}");
    bool ok = r != "A";
    END_TEST(ok);
}

void test_latex_bm() {
    TEST("LaTeX: \\bm{} (bold math, synonym for \\mathbf)");
    std::string r = md_latex_to_unicode("\\bm{x}");
    bool ok = r != "x";
    END_TEST(ok);
}

void test_latex_frac_nested() {
    TEST("LaTeX: nested \\frac");
    std::string r = md_latex_to_unicode("\\frac{\\frac{1}{2}}{3}");
    bool ok = r == "1/2/3";
    END_TEST(ok);
}

void test_latex_color() {
    TEST("LaTeX: \\color{} is stripped");
    std::string r = md_latex_to_unicode("\\color{red}x");
    bool ok = r == "x";
    END_TEST(ok);
}

void test_latex_textcolor() {
    TEST("LaTeX: \\textcolor{} is stripped");
    std::string r = md_latex_to_unicode("\\textcolor{blue}{hello}");
    bool ok = r == "hello";
    END_TEST(ok);
}

void test_latex_begin_end_skipped() {
    TEST("LaTeX: \\begin{} \\end{} environments skipped");
    std::string r = md_latex_to_unicode("\\begin{align}x &= 1\\\\y &= 2\\end{align}");
    bool ok = !contains(r, "begin") && !contains(r, "end");
    END_TEST(ok);
}

void test_latex_tag_stripped() {
    TEST("LaTeX: \\tag{} \\label{} stripped");
    bool ok = true;
    ok = ok && md_latex_to_unicode("x \\tag{1}") == "x ";
    ok = ok && md_latex_to_unicode("\\label{eq:1} x") == " x";
    END_TEST(ok);
}

// ============================================================
// Conversation state tests
// ============================================================

void test_conversation_streaming() {
    TEST("Conversation streaming text lifecycle");
    Conversation conv;

    // Initially no streaming
    assert(!conv.has_streaming());

    // Set streaming text
    conv.set_streaming_text("Hello");
    assert(conv.has_streaming());

    auto entries = conv.get_entries();
    // Should have 0 permanent entries + 1 streaming entry
    assert(entries.size() == 1);
    assert(entries[0].type == ConversationEntry::ASSISTANT);
    assert(entries[0].content == "Hello");

    // Append to streaming
    conv.set_streaming_text("Hello world");
    entries = conv.get_entries();
    assert(entries.size() == 1);
    assert(entries[0].content == "Hello world");

    // Add a permanent entry (should finalize streaming)
    ConversationEntry user;
    user.type = ConversationEntry::USER;
    user.content = "hi";
    user.timestamp = 0;
    conv.add_entry(user);

    entries = conv.get_entries();
    assert(entries.size() == 2);
    assert(entries[0].type == ConversationEntry::ASSISTANT);
    assert(entries[0].content == "Hello world");
    assert(entries[1].type == ConversationEntry::USER);
    assert(entries[1].content == "hi");

    // clear_streaming with nothing streaming is no-op
    conv.clear_streaming();
    entries = conv.get_entries();
    assert(entries.size() == 2);

    END_TEST(true);
}

// ============================================================

int main() {
    std::cout << "=== llmchat unit tests ===\n\n";

    // SSE parser tests (no server needed)
    std::cout << "\n[SSE Parser]\n";
    test_sse_content_tokens();
    test_sse_tool_call();
    test_sse_reasoning_content();
    test_sse_reasoning_with_tool_call();
    test_sse_parallel_tool_calls();
    test_sse_parallel_tool_calls_streaming_args();

    // Tool execution tests (no server needed)
    std::cout << "\n[Tool Execution]\n";
    test_shell_tool_execution();
    test_shell_tool_with_modulo();
    test_shell_tool_error();
    test_is_mcp_tool();

    // LaTeX conversion tests
    std::cout << "\n[LaTeX → Unicode]\n";
    test_latex_greek_letters();
    test_latex_superscript_subscript();
    test_latex_mathbf();
    test_latex_mathbb();
    test_latex_mathcal();
    test_latex_mathfrak();
    test_latex_frac();
    test_latex_dfrac_tfrac();
    test_latex_pmod();
    test_latex_bmod();
    test_latex_binom();
    test_latex_operators();
    test_latex_arrows();
    test_latex_relations();
    test_latex_symbols();
    test_latex_sqrt();
    test_latex_text();
    test_latex_unknown_commands();
    test_latex_empty();
    test_latex_complex_expression();
    test_latex_mixed_text_and_commands();
    test_latex_tilde();
    test_latex_hat();
    test_latex_vec();
    test_latex_set_minus();
    test_latex_aleph();
    test_latex_hbar();
    test_latex_ell();
    test_latex_dots();
    test_latex_mathrm();
    test_latex_mod_with_mod();
    test_latex_operatorname();
    test_latex_braces_removed();
    test_latex_tilde_as_space();
    test_latex_mathit();
    test_latex_bm();
    test_latex_frac_nested();
    test_latex_color();
    test_latex_textcolor();
    test_latex_begin_end_skipped();
    test_latex_tag_stripped();

    // Conversation tests
    std::cout << "\n[Conversation]\n";
    test_conversation_streaming();

    // Live API tests (need running server)
    std::cout << "\n[Live API (requires localhost:8080)]\n";
    test_live_api_streaming();
    test_live_api_tool_call();

    std::cout << "\n=== Results: " << tests_passed << " passed, "
              << tests_failed << " failed ===\n";
    return tests_failed > 0 ? 1 : 0;
}
