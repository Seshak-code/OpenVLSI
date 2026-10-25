#pragma once

#include <atomic>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <functional>

namespace acutesim::orchestration {

struct SubprocessResult {
    int exit_code = -1;
    std::string stdout_output;
    std::string stderr_output;
    bool timed_out = false;
    bool cancelled = false;
};

class Subprocess {
public:
    // Receives output chunks as they arrive (not split into lines)
    using OutputCallback = std::function<void(const std::string&)>;

    Subprocess(const std::string& command, const std::vector<std::string>& args);

    void setWorkingDirectory(const std::string& path);

    // Added to (or overriding) the inherited environment
    void setEnvironment(const std::map<std::string, std::string>& env);

    // Watchdog: the whole process group is killed when it expires
    void setTimeout(std::chrono::milliseconds timeout);

    // Polled while running; setting it kills the process group
    void setCancelFlag(const std::atomic<bool>* cancel);

    SubprocessResult run();
    SubprocessResult runWithStreaming(OutputCallback stdoutCb, OutputCallback stderrCb);

private:
    std::string command_;
    std::vector<std::string> args_;
    std::string workDir_;
    std::map<std::string, std::string> env_;
    std::chrono::milliseconds timeout_ = std::chrono::minutes(10);
    const std::atomic<bool>* cancel_ = nullptr;
};

} // namespace acutesim::orchestration
