#pragma once
#include <string>
#include <mutex>

class PersistentShell {
public:
    PersistentShell();
    ~PersistentShell();

    std::string execute(const std::string& command, int timeout_ms = 15000);
    bool is_alive() const { return pid_ > 0; }

private:
    pid_t pid_ = 0;
    int stdin_fd_ = -1;
    int stdout_fd_ = -1;
    std::mutex mutex_;

    bool spawn();
    void kill();
    void write_all(int fd, const std::string& data);
    std::string read_until_marker(int fd, const std::string& marker, int timeout_ms);
};
