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

// Simple POSIX implementation for macOS
// NOTE: For a production cross-platform tool on Windows, we'd wrap CreateProcess/pipes.

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

SubprocessResult Subprocess::run() {
    return runWithStreaming(nullptr, nullptr);
}

SubprocessResult Subprocess::runWithStreaming(OutputCallback stdoutCb, OutputCallback stderrCb) {
    int pipe_out[2], pipe_err[2];
    if (pipe(pipe_out) == -1 || pipe(pipe_err) == -1) {
        return {-1, "", "Failed to create pipes", false};
    }

    auto start_time = std::chrono::steady_clock::now();
    pid_t pid = fork();

    if (pid == 0) {
        // Child Process
        
        // redirect stdout -> pipe_out[1]
        dup2(pipe_out[1], STDOUT_FILENO);
        close(pipe_out[0]);
        close(pipe_out[1]);

        // redirect stderr -> pipe_err[1]
        dup2(pipe_err[1], STDERR_FILENO);
        close(pipe_err[0]);
        close(pipe_err[1]);

        // Change Dir
        if (!workDir_.empty()) {
            if (chdir(workDir_.c_str()) != 0) {
                perror("chdir failed");
                exit(127);
            }
        }

        // Prepare args
        std::vector<char*> c_args;
        c_args.push_back(strdup(command_.c_str()));
        for (const auto& arg : args_) {
            c_args.push_back(strdup(arg.c_str()));
        }
        c_args.push_back(nullptr);

        // Environment would be set here with setenv/execvpe ideally
        // For now inheriting + PATH is usually sufficient for yosys if in brew path
        execvp(command_.c_str(), c_args.data());
        
        // If we get here, exec failed
        perror("execvp failed");
        exit(127);
    } else if (pid > 0) {
        // Parent Process
        close(pipe_out[1]);
        close(pipe_err[1]);

        std::string full_stdout, full_stderr;
        SubprocessResult result;
        result.timed_out = false;

        // Monitoring loop with select for timeout
        while (true) {
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(pipe_out[0], &read_fds);
            FD_SET(pipe_err[0], &read_fds);
            int max_fd = std::max(pipe_out[0], pipe_err[0]);

            // Calculate remaining time
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);
            if (elapsed > timeout_) {
                result.timed_out = true;
                kill(pid, SIGKILL);
                break;
            }
            
            struct timeval tv;
            long remaining_ms = (timeout_ - elapsed).count();
            tv.tv_sec = remaining_ms / 1000;
            tv.tv_usec = (remaining_ms % 1000) * 1000;

            int activity = select(max_fd + 1, &read_fds, nullptr, nullptr, &tv);

            if (activity == -1) {
                if (errno == EINTR) continue;
                break; // Error
            }
            if (activity == 0) {
                // Timeout inside select (redundant check but safe)
                result.timed_out = true;
                kill(pid, SIGKILL);
                break;
            }

            char buffer[1024];
            bool got_data = false;

            // Read stdout
            if (FD_ISSET(pipe_out[0], &read_fds)) {
                ssize_t bytes = read(pipe_out[0], buffer, sizeof(buffer) - 1);
                if (bytes > 0) {
                    buffer[bytes] = 0;
                    std::string chunk(buffer);
                    full_stdout += chunk;
                    if (stdoutCb) stdoutCb(chunk);
                    got_data = true;
                } else if (bytes == 0) {
                    // unexpected EOF? usually wait handles this
                }
            }
            
            // Read stderr
            if (FD_ISSET(pipe_err[0], &read_fds)) {
                ssize_t bytes = read(pipe_err[0], buffer, sizeof(buffer) - 1);
                if (bytes > 0) {
                    buffer[bytes] = 0;
                    std::string chunk(buffer);
                    full_stderr += chunk;
                    if (stderrCb) stderrCb(chunk);
                    got_data = true;
                }
            }

            // Check if child is dead
            int status;
            pid_t wait_res = waitpid(pid, &status, WNOHANG);
            if (wait_res == pid) {
                result.exit_code = WEXITSTATUS(status);
                break;
            }
        }
        
        close(pipe_out[0]);
        close(pipe_err[0]);
        
        result.stdout_output = full_stdout;
        result.stderr_output = full_stderr;
        return result;

    } else {
        return {-1, "", "Fork failed", false};
    }
}

} // namespace acutesim::orchestration
