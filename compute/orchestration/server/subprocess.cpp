#include "subprocess.h"
#ifdef _WIN32
#include <windows.h>
#include <thread>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/select.h>
#include <fcntl.h>
#include <signal.h>
#endif
#include <iostream>
#include <array>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <filesystem>

// POSIX (fork/exec, a process group) and Win32 (CreateProcess, a kill-on-close job object) implementations.

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

#ifdef _WIN32
namespace {
// One argument as CommandLineToArgvW reads it back: quoted when it has spaces, tabs or quotes; backslashes
// doubled before a quote
std::string quoteArg(const std::string& a) {
    if (!a.empty() && a.find_first_of(" \t\"") == std::string::npos) return a;
    std::string q = "\"";
    size_t slashes = 0;
    for (char c : a) {
        if (c == '\\') { ++slashes; continue; }
        q.append(c == '"' ? 2 * slashes + 1 : slashes, '\\');
        slashes = 0;
        q += c;
    }
    q.append(2 * slashes, '\\');
    return q + "\"";
}
}  // namespace

SubprocessResult Subprocess::runWithStreaming(OutputCallback stdoutCb, OutputCallback stderrCb) {
    SubprocessResult result;
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE outR = nullptr, outW = nullptr, errR = nullptr, errW = nullptr;
    if (!CreatePipe(&outR, &outW, &sa, 0) || !CreatePipe(&errR, &errW, &sa, 0)) {
        result.stderr_output = "Failed to create pipes";
        return result;
    }
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errR, HANDLE_FLAG_INHERIT, 0);

    // The inherited environment with env_ on top, as a sorted NUL-separated block. The CRT's narrow _environ:
    // GetEnvironmentStringsA exists only without UNICODE (a Qt build defines it)
    std::map<std::string, std::string> env;
    for (char** e = _environ; e && *e; ++e) {
        const std::string kv(*e);
        const size_t eq = kv.find('=', 1);  // "=C:=C:\..." entries start with '='
        if (eq != std::string::npos) env[kv.substr(0, eq)] = kv.substr(eq + 1);
    }
    for (const auto& [k, v] : env_) env[k] = v;
    std::string envBlock;
    for (const auto& [k, v] : env) { envBlock += k + "=" + v; envBlock.push_back('\0'); }
    envBlock.push_back('\0');

    std::string cmdline = quoteArg(command_);
    for (const auto& a : args_) cmdline += " " + quoteArg(a);
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = outW;
    si.hStdError = errW;
    PROCESS_INFORMATION pi{};
    // The job takes the whole tree: a timeout or cancel kills the tools a make or shell started too
    HANDLE job = CreateJobObjectA(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION lim{};
    lim.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (job) SetInformationJobObject(job, JobObjectExtendedLimitInformation, &lim, sizeof(lim));
    const auto start_time = std::chrono::steady_clock::now();
    const BOOL ok = CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED, envBlock.data(),
                                   workDir_.empty() ? nullptr : workDir_.c_str(), &si, &pi);
    CloseHandle(outW);
    CloseHandle(errW);
    if (!ok) {
        CloseHandle(outR);
        CloseHandle(errR);
        if (job) CloseHandle(job);
        result.stderr_output = "CreateProcess failed (error " + std::to_string(GetLastError()) + ")";
        return result;
    }
    if (job) AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    // Read until both pipes are closed (output written just before exit is not lost), polling the
    // watchdog and the cancel flag
    bool outOpen = true, errOpen = true, killed = false;
    auto drain = [](HANDLE h, std::string& all, const OutputCallback& cb) {
        DWORD avail = 0;
        if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) return -1;  // closed: EOF
        if (avail == 0) return 0;
        char buffer[4096];
        DWORD n = 0;
        if (!ReadFile(h, buffer, sizeof(buffer), &n, nullptr) || n == 0) return -1;
        all.append(buffer, n);
        if (cb) cb(std::string(buffer, n));
        return 1;
    };
    while (outOpen || errOpen) {
        if (!killed) {
            const bool cancelled = cancel_ && cancel_->load();
            const bool expired = std::chrono::steady_clock::now() - start_time > timeout_;
            if (cancelled || expired) {
                result.cancelled = cancelled;
                result.timed_out = expired && !cancelled;
                if (job) TerminateJobObject(job, 1); else TerminateProcess(pi.hProcess, 1);
                killed = true;
            }
        }
        int got = 0;
        if (outOpen) { const int r = drain(outR, result.stdout_output, stdoutCb); outOpen = r >= 0; got += r > 0; }
        if (errOpen) { const int r = drain(errR, result.stderr_output, stderrCb); errOpen = r >= 0; got += r > 0; }
        if (!got) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    CloseHandle(outR);
    CloseHandle(errR);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    result.exit_code = GetExitCodeProcess(pi.hProcess, &code) && !killed ? static_cast<int>(code) : -1;
    CloseHandle(pi.hProcess);
    if (job) CloseHandle(job);
    return result;
}
#else
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
#endif

} // namespace acutesim::orchestration
