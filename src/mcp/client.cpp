#include "client.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <fcntl.h>
#include <poll.h>
#include <iostream>
#include <sstream>

using json = nlohmann::json;

MCPClient::MCPClient(const MCPServerConfig& cfg) : config_(cfg) {}
MCPClient::~MCPClient() { stop(); }

bool MCPClient::start() {
    int in_pipe[2], out_pipe[2];
    if (pipe(in_pipe) < 0 || pipe(out_pipe) < 0) return false;

    pid_ = fork();
    if (pid_ < 0) return false;

    if (pid_ == 0) {
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        close(in_pipe[1]);
        close(out_pipe[0]);

        std::vector<const char*> argv;
        argv.push_back(config_.command.c_str());
        for (auto& a : config_.args) argv.push_back(a.c_str());
        argv.push_back(nullptr);

        execvp(config_.command.c_str(), (char* const*)argv.data());
        _exit(1);
    }

    stdin_fd_ = in_pipe[1];
    stdout_fd_ = out_pipe[0];
    close(in_pipe[0]);
    close(out_pipe[1]);

    fcntl(stdout_fd_, F_SETFL, O_NONBLOCK);
    running_ = true;

    json init_req = {
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "initialize"},
        {"params", {
            {"protocolVersion", "2024-11-05"},
            {"capabilities", {}}
        }}
    };

    std::string resp = send_request(init_req);

    json initialized = {
        {"jsonrpc", "2.0"},
        {"method", "notifications/initialized"}
    };
    std::string note = initialized.dump() + "\n";
    write(stdin_fd_, note.c_str(), note.size());

    return !resp.empty();
}

void MCPClient::stop() {
    if (pid_ > 0) {
        kill(pid_, SIGTERM);
        int status;
        waitpid(pid_, &status, WNOHANG);
        pid_ = -1;
    }
    if (stdin_fd_ >= 0) close(stdin_fd_);
    if (stdout_fd_ >= 0) close(stdout_fd_);
    stdin_fd_ = -1;
    stdout_fd_ = -1;
    running_ = false;
}

std::vector<MCPTool> MCPClient::list_tools() {
    json req = {
        {"jsonrpc", "2.0"},
        {"id", 2},
        {"method", "tools/list"},
        {"params", json::object()}
    };

    std::string resp = send_request(req);
    std::vector<MCPTool> tools;

    try {
        json j = json::parse(resp);
        if (j.contains("result") && j["result"].contains("tools")) {
            for (auto& t : j["result"]["tools"]) {
                MCPTool mt;
                mt.name = t.value("name", "");
                mt.description = t.value("description", "");
                mt.input_schema = t.value("inputSchema", json::object());
                tools.push_back(mt);
            }
        }
    } catch (...) {}

    return tools;
}

std::string MCPClient::call_tool(const std::string& name, const json& args) {
    json req = {
        {"jsonrpc", "2.0"},
        {"id", 3},
        {"method", "tools/call"},
        {"params", {
            {"name", name},
            {"arguments", args}
        }}
    };

    std::string resp = send_request(req);

    try {
        json j = json::parse(resp);
        if (j.contains("result") && j["result"].contains("content")) {
            std::string combined;
            for (auto& item : j["result"]["content"]) {
                if (item.value("type", "") == "text") {
                    combined += item.value("text", "");
                }
            }
            return combined;
        }
        return resp;
    } catch (...) {
        return "Error: Failed to parse MCP response";
    }
}

std::string MCPClient::send_request(const json& request) {
    if (!running_) return "";

    std::string req_str = request.dump() + "\n";
    write(stdin_fd_, req_str.c_str(), req_str.size());

    std::string response;
    char buf[4096];
    int depth = 0;
    bool in_string = false;

    struct pollfd pfd;
    pfd.fd = stdout_fd_;
    pfd.events = POLLIN;

    for (int attempt = 0; attempt < 150; attempt++) {
        int status;
        pid_t w = waitpid(pid_, &status, WNOHANG);
        if (w == pid_) {
            running_ = false;
            break;
        }

        int ret = poll(&pfd, 1, 20);
        if (ret > 0 && (pfd.revents & POLLIN)) {
            int n = read(stdout_fd_, buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = 0;
                response += buf;

                for (int i = 0; i < n; i++) {
                    char c = buf[i];
                    if (c == '"' && (i == 0 || buf[i-1] != '\\')) in_string = !in_string;
                    if (!in_string) {
                        if (c == '{') depth++;
                        if (c == '}') depth--;
                    }
                }

                if (depth <= 0 && !response.empty()) {
                    break;
                }
            } else {
                break;
            }
        }
    }

    size_t start = response.find('{');
    size_t end = response.rfind('}');
    if (start != std::string::npos && end != std::string::npos && end > start) {
        return response.substr(start, end - start + 1);
    }

    return response;
}
