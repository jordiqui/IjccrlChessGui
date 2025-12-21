#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace ijccrl::core::process {

class Process {
public:
    Process();
    ~Process();

    bool Start(const std::string& command,
               const std::vector<std::string>& args,
               const std::string& working_dir);
    bool WriteLine(const std::string& line);
    bool ReadLineBlocking(std::string& line, int timeout_ms);
    bool TryReadLine(std::string& line);
    bool IsRunning() const;
    bool Terminate();
    bool WaitForExit(int timeout_ms);
    int ExitCode() const;

private:
    void ReaderLoop();
    void CloseHandles();

    std::atomic<bool> running_{false};
    std::atomic<bool> logged_exit_{false};
    int exit_code_ = 0;

#ifdef _WIN32
    void* process_handle_ = nullptr;
    void* thread_handle_ = nullptr;
    void* stdin_write_ = nullptr;
    void* stdout_read_ = nullptr;
#else
    int stdin_fd_ = -1;
    int stdout_fd_ = -1;
    int pid_ = -1;
#endif

    std::thread reader_thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<std::string> lines_;
    std::string buffered_;
};

}  // namespace ijccrl::core::process
