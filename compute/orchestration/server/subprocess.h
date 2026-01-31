#pragma once

#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <memory>
#include <functional>

namespace acutesim::orchestration {

struct SubprocessResult {
    int exit_code;
    std::string stdout_output;
    std::string stderr_output;
    bool timed_out;
};

class Subprocess {
public:
    // Callback for streaming stdout/stderr line by line
    using OutputCallback = std::function<void(const std::string&)>;

    // Configure the subprocess
    Subprocess(const std::string& command, const std::vector<std::string>& args);

    // Set working directory (Sandboxing)
    void setWorkingDirectory(const std::string& path);

    // Set Environment variables (Sanitization)
    void setEnvironment(const std::map<std::string, std::string>& env);

    // Set timeout in milliseconds (Watchdog)
    void setTimeout(std::chrono::milliseconds timeout);

    // Run synchronously and return result
    SubprocessResult run();

    // Run and stream output via callback (blocks until finish)
    SubprocessResult runWithStreaming(OutputCallback stdoutCb, OutputCallback stderrCb);

private:
    std::string command_;
    std::vector<std::string> args_;
    std::string workDir_;
    std::map<std::string, std::string> env_;
    std::chrono::milliseconds timeout_ = std::chrono::minutes(10);
};

} // namespace acutesim::orchestration
