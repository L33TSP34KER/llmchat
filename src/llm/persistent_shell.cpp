#include "persistent_shell.h"
#include <unistd.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <poll.h>
#include <fcntl.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <ctime>

PersistentShell::PersistentShell() {
    spawn();
    if (pid_ > 0) {
        // Kill the prompt
        std::string init = "PS1=\nPS2=\nunset PROMPT_COMMAND\nstty -echo\n";
        write(stdin_fd_, init.c_str(), init.size());
        // Drain startup output + prompt
        char drain[4096];
        int flags = fcntl(stdout_fd_, F_GETFL, 0);
        fcntl(stdout_fd_, F_SETFL, flags | O_NONBLOCK);
        usleep(100000);
        while (read(stdout_fd_, drain, sizeof(drain) - 1) > 0) {}
        fcntl(stdout_fd_, F_SETFL, flags);
    }
}

PersistentShell::~PersistentShell() {
    kill();
}

bool PersistentShell::spawn() {
    int stdin_pipe[2], stdout_pipe[2];

    if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0)
        return false;

    pid_ = fork();
    if (pid_ < 0) {
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        return false;
    }

    if (pid_ == 0) {
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stdout_pipe[1], STDERR_FILENO);
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
        // Minimal bash: no rc, no prompt
        execl("/bin/bash", "bash", "--norc", "--noprofile", nullptr);
        execl("/bin/sh", "sh", nullptr);
        _exit(127);
    }

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    stdin_fd_ = stdin_pipe[1];
    stdout_fd_ = stdout_pipe[0];

    int flags = fcntl(stdout_fd_, F_GETFL, 0);
    fcntl(stdout_fd_, F_SETFL, flags | O_NONBLOCK);

    return true;
}

void PersistentShell::kill() {
    if (pid_ > 0) {
        ::kill(pid_, SIGTERM);
        struct timespec ts = {0, 200000000};
        for (int i = 0; i < 5; i++) {
            if (waitpid(pid_, nullptr, WNOHANG) == pid_) { pid_ = 0; break; }
            nanosleep(&ts, nullptr);
        }
        if (pid_ > 0) {
            ::kill(pid_, SIGKILL);
            waitpid(pid_, nullptr, 0);
        }
    }
    if (stdin_fd_ >= 0) close(stdin_fd_);
    if (stdout_fd_ >= 0) close(stdout_fd_);
    pid_ = 0;
    stdin_fd_ = -1;
    stdout_fd_ = -1;
}

std::string PersistentShell::read_until_marker(int fd, const std::string& marker, int timeout_ms) {
    std::string buf;
    char tmp[4096];
    int elapsed = 0;

    while (elapsed < timeout_ms) {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        int ret = poll(&pfd, 1, 50);
        if (ret < 0) break;
        if (ret == 0) { elapsed += 50; continue; }
        if (pfd.revents & POLLIN) {
            ssize_t n = read(fd, tmp, sizeof(tmp) - 1);
            if (n > 0) {
                tmp[n] = '\0';
                buf += tmp;
                size_t pos = buf.find(marker);
                if (pos != std::string::npos) {
                    // Return everything before the marker
                    buf = buf.substr(0, pos);
                    // Trim trailing whitespace
                    while (!buf.empty() && (buf.back() == '\n' || buf.back() == '\r' || buf.back() == ' '))
                        buf.pop_back();
                    return buf;
                }
            }
        }
    }

    // Timeout: return what we have
    while (!buf.empty() && (buf.back() == '\n' || buf.back() == '\r'))
        buf.pop_back();
    return buf;
}

void PersistentShell::write_all(int fd, const std::string& data) {
    const char* p = data.c_str();
    size_t remaining = data.size();
    while (remaining > 0) {
        ssize_t n = write(fd, p, remaining);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        p += n;
        remaining -= n;
    }
}

std::string PersistentShell::execute(const std::string& command, int timeout_ms) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (pid_ <= 0 && !spawn()) return "Error: failed to start shell";

    // Check if shell is still alive
    int status;
    if (waitpid(pid_, &status, WNOHANG) == pid_) {
        if (!spawn()) return "Error: shell died";
        // Re-init new shell
        std::string init = "PS1=\nPS2=\nunset PROMPT_COMMAND\nstty -echo\n";
        write(stdin_fd_, init.c_str(), init.size());
        char drain[4096];
        int flags = fcntl(stdout_fd_, F_GETFL, 0);
        fcntl(stdout_fd_, F_SETFL, flags | O_NONBLOCK);
        usleep(100000);
        while (read(stdout_fd_, drain, sizeof(drain) - 1) > 0) {}
        fcntl(stdout_fd_, F_SETFL, flags);
    }

    // Drain any stale prompt output
    {
        char drain[4096];
        int flags = fcntl(stdout_fd_, F_GETFL, 0);
        fcntl(stdout_fd_, F_SETFL, flags | O_NONBLOCK);
        while (read(stdout_fd_, drain, sizeof(drain) - 1) > 0) {}
        fcntl(stdout_fd_, F_SETFL, flags);
    }

    // Generate a unique marker
    unsigned int seed = time(nullptr) ^ (getpid() << 16) ^ (reinterpret_cast<uintptr_t>(this) & 0xFFFF);
    srand(seed);
    std::string marker = "M" + std::to_string(rand() % 1000000) + "END";

    std::string safe_cmd = command;
    // Ensure command ends with newline
    if (!safe_cmd.empty() && safe_cmd.back() != '\n') safe_cmd += '\n';
    std::string full_cmd = safe_cmd + "echo " + marker + "\n";

    write_all(stdin_fd_, full_cmd);

    std::string result = read_until_marker(stdout_fd_, marker, timeout_ms);

    if (result.empty() && !command.empty())
        result = "(no output)";

    return result;
}
