#pragma once

#include "ijccrl/core/api/RunnerConfig.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ijccrl::core::api {

struct RunnerState {
    bool running = false;
    bool paused = false;
    int gameNo = 0;
    int roundNo = 0;
    std::string whiteName;
    std::string blackName;
    std::string lastMove;
    std::string fen;
    std::string openingId;
    std::string livePgnPath;
    std::string tourneyPgnPath;
    int concurrency = 1;
    int activeGames = 0;
};

struct StandingRow {
    std::string name;
    int games = 0;
    int wins = 0;
    int draws = 0;
    int losses = 0;
    double points = 0.0;
    double scorePercent = 0.0;
};

class RunnerService {
public:
    RunnerService();
    ~RunnerService();

    bool loadConfig(const std::string& path);
    bool saveConfig(const std::string& path) const;

    void setConfig(const RunnerConfig& config);
    RunnerConfig getConfigSnapshot() const;

    bool start();
    bool startWithResume(bool resume);
    void requestStop();
    void pause();
    void resume();
    bool exportResults(const std::string& directory, std::string* error);

    RunnerState getStateSnapshot() const;
    std::vector<StandingRow> getStandingsSnapshot() const;
    std::string getLastLogLines(int n) const;

private:
    void Run(RunnerConfig config, bool resume);
    void AppendLogLine(const std::string& line);

    mutable std::mutex config_mutex_;
    RunnerConfig config_{};

    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> running_{false};

    std::thread worker_{};
    mutable std::mutex state_mutex_;
    RunnerState state_{};

    mutable std::mutex standings_mutex_;
    std::vector<StandingRow> standings_{};

    mutable std::mutex log_mutex_;
    std::deque<std::string> log_lines_{};
    size_t max_log_lines_ = 2000;

    std::mutex pause_mutex_{};
    std::condition_variable pause_cv_{};
};

}  // namespace ijccrl::core::api
