#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <functional>

#include "llm/provider.h"
#include "llm/tool_exec.h"
#include "config.h"
#include "conversation.h"

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
    std::string name;
    std::string args;

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

    bool ok = provider.handle_stream_chunk(chunk, content, name, args);
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
    std::string name;
    std::string args;

    std::string chunk =
        "data: {\"choices\":[{\"delta\":{\"role\":\"assistant\",\"content\":null}}]}\n"
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"function\":{\"name\":\"calculate\",\"arguments\":\"{\\\"expression\\\":\\\"1+1\\\"}\"}}]}}]}\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n"
        "data: [DONE]\n";

    std::vector<StreamEvent> events;
    provider.set_stream_callback([&](const StreamEvent& ev) {
        events.push_back(ev);
    });

    bool ok = provider.handle_stream_chunk(chunk, content, name, args);
    bool has_tool_call = false;
    bool has_done = false;
    for (auto& e : events) {
        if (e.type == StreamEvent::TOOL_CALL) has_tool_call = true;
        if (e.type == StreamEvent::DONE) has_done = true;
    }
    END_TEST(ok && has_tool_call && has_done &&
             name == "calculate" &&
             args.find("1+1") != std::string::npos);
}

void test_sse_reasoning_content() {
    TEST("SSE reasoning_content produces REASONING events");
    OpenAIProvider provider("http://dummy", "");
    std::string content;
    std::string name;
    std::string args;

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

    bool ok = provider.handle_stream_chunk(chunk, content, name, args);
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
    std::string name;
    std::string args;

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

    bool ok = provider.handle_stream_chunk(chunk, content, name, args);
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
    END_TEST(ok && has_tool_call && has_reasoning && token_count == 0 && content.empty() && name == "calculate");
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

    // Tool execution tests (no server needed)
    std::cout << "\n[Tool Execution]\n";
    test_shell_tool_execution();
    test_shell_tool_with_modulo();
    test_shell_tool_error();
    test_is_mcp_tool();

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
