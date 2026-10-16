#include "subprocess.h"
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/select.h>
#include <fcntl.h>
#include <signal.h>
#include <iostream>
#include <array>
#include <sstream>
#include <cstring>
#include <filesystem>

// POSIX implementation (macOS, Linux). ponytail: no Windows path (CreateProcess + pipes).

namespace acutesim::orchestration {

Subprocess::Subprocess(const std::string& command, const std::vector<std::string>& args)
    : command_(command), args_(args) {}

void Subprocess::setWorkingDirectory(const std::string& path) {
    workDir_ = path;
}

void Subprocess::setEnvironment(const std::map<std::string, std::string>& env) {
    env_ = env;
}

void Subprocess::setTimeout(std::chrono::milliseconds timeout) {
    timeout_ = timeout;
}

void Subprocess::setCancelFlag(const std::atomic<bool>* cancel) {
    cancel_ = cancel;
}

SubprocessResult Subprocess::run() {
    return runWithStreaming(nullptr, nullptr);
}

SubprocessResult Subprocess::runWithStreaming(OutputCallback stdoutCb, OutputCallback stderrCb) {
    int pipe_out[2], pipe_err[2];
    if (pipe(pipe_out) == -1 || pipe(pipe_err) == -1) {
        SubprocessResult r;
        r.stderr_output = "Failed to create pipes";
        return r;
    }

    const auto start_time = std::chrono::steady_clock::now();
    const pid_t pid = fork();

    if (pid == 0) {
        // Own process group: a timeout or cancel kills the tools a make or shell started too
        setpgid(0, 0);
        dup2(pipe_out[1], STDOUT_FILENO);
        dup2(pipe_err[1], STDERR_FILENO);
        for (int fd : {pipe_out[0], pipe_out[1], pipe_err[0], pipe_err[1]}) close(fd);
        if (!workDir_.empty() && chdir(workDir_.c_str()) != 0) {
            perror("chdir failed");
            _exit(127);
        }
        for (const auto& [k, v] : env_) setenv(k.c_str(), v.c_str(), 1);
        std::vector<char*> c_args;
        c_args.push_back(const_cast<char*>(command_.c_str()));
        for (const auto& arg : args_) c_args.push_back(const_cast<char*>(arg.c_str()));
        c_args.push_back(nullptr);
        execvp(command_.c_str(), c_args.data());
        perror("execvp failed");
        _exit(127);
    }
    close(pipe_out[1]);
    close(pipe_err[1]);
    SubprocessResult result;
    if (pid < 0) {
        close(pipe_out[0]);
        close(pipe_err[0]);
        result.stderr_output = "Fork failed";
        return result;
    }
    setpgid(pid, pid);  // also from the parent: no race with the kill below

    // Read until both pipes hit EOF (output written just before exit is not lost),
    // polling the watchdog and the cancel flag every 200 ms
    bool outOpen = true, errOpen = true, killed = false;
    auto drain = [](int fd, std::string& all, const OutputCallback& cb) {
        char buffer[4096];
        const ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n <= 0) return false;
        all.append(buffer, n);
        if (cb) cb(std::string(buffer, n));
        return true;
    };
    while (outOpen || errOpen) {
        if (!killed) {
            const bool cancelled = cancel_ && cancel_->load();
            const bool expired = std::chrono::steady_clock::now() - start_time > timeout_;
            if (cancelled || expired) {
                result.cancelled = cancelled;
                result.timed_out = expired && !cancelled;
                kill(-pid, SIGKILL);
                killed = true;
            }
        }
        fd_set read_fds;
        FD_ZERO(&read_fds);
        if (outOpen) FD_SET(pipe_out[0], &read_fds);
        if (errOpen) FD_SET(pipe_err[0], &read_fds);
        struct timeval tv {0, 200000};
        const int activity = select(std::max(pipe_out[0], pipe_err[0]) + 1, &read_fds, nullptr, nullptr, &tv);
        if (activity < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (outOpen && FD_ISSET(pipe_out[0], &read_fds)) outOpen = drain(pipe_out[0], result.stdout_output, stdoutCb);
        if (errOpen && FD_ISSET(pipe_err[0], &read_fds)) errOpen = drain(pipe_err[0], result.stderr_output, stderrCb);
    }
    close(pipe_out[0]);
    close(pipe_err[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return result;
}

} // namespace acutesim::orchestration
