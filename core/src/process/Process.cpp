#include "ijccrl/core/process/Process.h"

#include <chrono>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace ijccrl::core::process {

namespace {

#ifdef _WIN32
std::wstring ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int size_needed = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    std::wstring result(size_needed - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), size_needed);
    return result;
}

std::wstring QuoteArg(const std::wstring& arg) {
    if (arg.find_first_of(L" \t\"") == std::wstring::npos) {
        return arg;
    }
    std::wstring quoted = L"\"";
    for (wchar_t ch : arg) {
        if (ch == L'\"') {
            quoted.push_back(L'\\');
        }
        quoted.push_back(ch);
    }
    quoted.push_back(L'\"');
    return quoted;
}
#endif

}  // namespace

Process::Process() = default;

Process::~Process() {
    Terminate();
    if (reader_thread_.joinable()) {
        reader_thread_.join();
    }
    CloseHandles();
}

bool Process::Start(const std::string& command,
                    const std::vector<std::string>& args,
                    const std::string& working_dir) {
    if (running_) {
        return false;
    }

#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE stdin_read = nullptr;
    HANDLE stdout_read = nullptr;
    HANDLE stdout_write = nullptr;

    if (!CreatePipe(&stdout_read, &stdout_write, &sa, 0)) {
        std::cerr << "[process] Failed to create stdout pipe." << '\n';
        return false;
    }
    if (!SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0)) {
        std::cerr << "[process] Failed to configure stdout pipe." << '\n';
        CloseHandle(stdout_write);
        CloseHandle(stdout_read);
        return false;
    }

    HANDLE stdin_write = nullptr;
    if (!CreatePipe(&stdin_read, &stdin_write, &sa, 0)) {
        std::cerr << "[process] Failed to create stdin pipe." << '\n';
        CloseHandle(stdout_write);
        CloseHandle(stdout_read);
        return false;
    }
    if (!SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0)) {
        std::cerr << "[process] Failed to configure stdin pipe." << '\n';
        CloseHandle(stdin_read);
        CloseHandle(stdin_write);
        CloseHandle(stdout_write);
        CloseHandle(stdout_read);
        stdin_write_ = nullptr;
        return false;
    }
    stdout_read_ = stdout_read;
    stdin_write_ = stdin_write;

    std::wstringstream cmdline;
    cmdline << QuoteArg(ToWide(command));
    for (const auto& arg : args) {
        cmdline << L' ' << QuoteArg(ToWide(arg));
    }

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    startup_info.dwFlags = STARTF_USESTDHANDLES;
    startup_info.hStdInput = stdin_read;
    startup_info.hStdOutput = stdout_write;
    startup_info.hStdError = stdout_write;

    PROCESS_INFORMATION process_info{};
    std::wstring working_dir_w = ToWide(working_dir);
    const wchar_t* cwd = working_dir.empty() ? nullptr : working_dir_w.c_str();

    const std::wstring cmdline_string = cmdline.str();
    std::wstring mutable_cmdline = cmdline_string;

    BOOL created = CreateProcessW(
        nullptr,
        mutable_cmdline.data(),
        nullptr,
        nullptr,
        TRUE,
        0,
        nullptr,
        cwd,
        &startup_info,
        &process_info);

    CloseHandle(stdin_read);
    CloseHandle(stdout_write);

    if (!created) {
        std::cerr << "[process] CreateProcessW failed for: " << command << '\n';
        CloseHandle(stdout_read);
        CloseHandle(stdin_write);
        stdout_read_ = nullptr;
        stdin_write_ = nullptr;
        return false;
    }

    process_handle_ = process_info.hProcess;
    thread_handle_ = process_info.hThread;
    running_ = true;
    logged_exit_ = false;

    std::cout << "[process] Started: " << command;
    for (const auto& arg : args) {
        std::cout << ' ' << arg;
    }
    std::cout << " (PID " << process_info.dwProcessId << ')' << '\n';

    reader_thread_ = std::thread([this]() { ReaderLoop(); });
    return true;
#else
    int stdin_pipe[2] = {-1, -1};
    int stdout_pipe[2] = {-1, -1};

    if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0) {
        std::cerr << "[process] Failed to create pipes." << '\n';
        return false;
    }

    pid_t pid = fork();
    if (pid == 0) {
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stdout_pipe[1], STDERR_FILENO);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);

        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(command.c_str()));
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        if (!working_dir.empty()) {
            chdir(working_dir.c_str());
        }
        execvp(command.c_str(), argv.data());
        _exit(127);
    }

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);

    pid_ = pid;
    stdin_fd_ = stdin_pipe[1];
    stdout_fd_ = stdout_pipe[0];

    running_ = true;
    logged_exit_ = false;

    std::cout << "[process] Started: " << command;
    for (const auto& arg : args) {
        std::cout << ' ' << arg;
    }
    std::cout << " (PID " << pid_ << ')' << '\n';

    reader_thread_ = std::thread([this]() { ReaderLoop(); });
    return true;
#endif
}

bool Process::WriteLine(const std::string& line) {
    if (!running_) {
        return false;
    }
    const std::string payload = line + "\n";
#ifdef _WIN32
    DWORD written = 0;
    if (!WriteFile(reinterpret_cast<HANDLE>(stdin_write_), payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr)) {
        return false;
    }
    return true;
#else
    ssize_t written = write(stdin_fd_, payload.data(), payload.size());
    return written == static_cast<ssize_t>(payload.size());
#endif
}

bool Process::ReadLineBlocking(std::string& line, int timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (timeout_ms < 0) {
        cv_.wait(lock, [this]() { return !lines_.empty() || !running_; });
    } else {
        cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this]() {
            return !lines_.empty() || !running_;
        });
    }

    if (lines_.empty()) {
        return false;
    }

    line = std::move(lines_.front());
    lines_.pop();
    return true;
}

bool Process::TryReadLine(std::string& line) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (lines_.empty()) {
        return false;
    }
    line = std::move(lines_.front());
    lines_.pop();
    return true;
}

bool Process::IsRunning() const {
#ifdef _WIN32
    if (!process_handle_) {
        return false;
    }
    DWORD code = STILL_ACTIVE;
    if (!GetExitCodeProcess(reinterpret_cast<HANDLE>(process_handle_), &code)) {
        return false;
    }
    return code == STILL_ACTIVE;
#else
    if (pid_ <= 0) {
        return false;
    }
    int status = 0;
    pid_t result = waitpid(pid_, &status, WNOHANG);
    return result == 0;
#endif
}

bool Process::Terminate() {
    if (!running_) {
        return false;
    }

#ifdef _WIN32
    TerminateProcess(reinterpret_cast<HANDLE>(process_handle_), 1);
#else
    kill(pid_, SIGTERM);
#endif
    running_ = false;
    cv_.notify_all();
    return true;
}

int Process::ExitCode() const {
    return exit_code_;
}

void Process::ReaderLoop() {
#ifdef _WIN32
    char buffer[4096];
    DWORD bytes_read = 0;
    while (ReadFile(reinterpret_cast<HANDLE>(stdout_read_), buffer, sizeof(buffer), &bytes_read, nullptr)) {
        if (bytes_read == 0) {
            break;
        }
        buffered_.append(buffer, buffer + bytes_read);
        std::size_t pos = 0;
        while ((pos = buffered_.find('\n')) != std::string::npos) {
            std::string line = buffered_.substr(0, pos);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                lines_.push(std::move(line));
            }
            cv_.notify_all();
            buffered_.erase(0, pos + 1);
        }
    }

    DWORD exit_code = 0;
    if (process_handle_ && GetExitCodeProcess(reinterpret_cast<HANDLE>(process_handle_), &exit_code)) {
        exit_code_ = static_cast<int>(exit_code);
    }
#else
    char buffer[4096];
    ssize_t bytes_read = 0;
    while ((bytes_read = read(stdout_fd_, buffer, sizeof(buffer))) > 0) {
        buffered_.append(buffer, buffer + bytes_read);
        std::size_t pos = 0;
        while ((pos = buffered_.find('\n')) != std::string::npos) {
            std::string line = buffered_.substr(0, pos);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                lines_.push(std::move(line));
            }
            cv_.notify_all();
            buffered_.erase(0, pos + 1);
        }
    }

    int status = 0;
    if (pid_ > 0 && waitpid(pid_, &status, 0) > 0) {
        if (WIFEXITED(status)) {
            exit_code_ = WEXITSTATUS(status);
        }
    }
#endif

    running_ = false;
    cv_.notify_all();

    if (!logged_exit_.exchange(true)) {
        std::cout << "[process] Exit code: " << exit_code_ << '\n';
    }
}

void Process::CloseHandles() {
#ifdef _WIN32
    if (stdout_read_) {
        CloseHandle(reinterpret_cast<HANDLE>(stdout_read_));
        stdout_read_ = nullptr;
    }
    if (stdin_write_) {
        CloseHandle(reinterpret_cast<HANDLE>(stdin_write_));
        stdin_write_ = nullptr;
    }
    if (process_handle_) {
        CloseHandle(reinterpret_cast<HANDLE>(process_handle_));
        process_handle_ = nullptr;
    }
    if (thread_handle_) {
        CloseHandle(reinterpret_cast<HANDLE>(thread_handle_));
        thread_handle_ = nullptr;
    }
#else
    if (stdin_fd_ >= 0) {
        close(stdin_fd_);
        stdin_fd_ = -1;
    }
    if (stdout_fd_ >= 0) {
        close(stdout_fd_);
        stdout_fd_ = -1;
    }
#endif
}

}  // namespace ijccrl::core::process
