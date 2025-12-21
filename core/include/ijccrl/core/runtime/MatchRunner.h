#pragma once

#include "ijccrl/core/game/GameRunner.h"
#include "ijccrl/core/openings/Opening.h"
#include "ijccrl/core/runtime/EnginePool.h"
#include "ijccrl/core/tournament/TournamentTypes.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace ijccrl::core::runtime {

struct MatchJob {
    ijccrl::core::tournament::Fixture fixture;
    ijccrl::core::openings::Opening opening;
    std::string event_name;
    std::string site_tag;
    std::string round_label;
    int fixture_index = 0;
};

struct MatchResult {
    MatchJob job;
    ijccrl::core::game::GameRunner::Result result;
    int game_number = 0;
};

class MatchRunner {
public:
    using ResultCallback = std::function<void(const MatchResult&)>;
    using LiveUpdateFn = ijccrl::core::game::GameRunner::LiveUpdateFn;
    using JobEventFn = std::function<void(const MatchJob&, int game_number, bool started)>;
    using WatchdogLogFn = std::function<void(const std::string&)>;

    struct Control {
        std::atomic<bool>* stop = nullptr;
        std::atomic<bool>* paused = nullptr;
        std::mutex* pause_mutex = nullptr;
        std::condition_variable* pause_cv = nullptr;
    };

    MatchRunner(EnginePool& pool,
                ijccrl::core::game::TimeControl time_control,
                ijccrl::core::rules::ConfigLimits termination_limits,
                int go_timeout_ms,
                bool abort_on_stop,
                int max_failures,
                int failure_window_games,
                bool pause_on_unhealthy,
                ResultCallback result_callback,
                LiveUpdateFn live_update,
                WatchdogLogFn watchdog_log,
                JobEventFn job_event = {});

    void Run(const std::vector<MatchJob>& jobs,
             int concurrency,
             const Control& control,
             int initial_game_number = 0);
    void Run(const std::vector<MatchJob>& jobs,
             int concurrency,
             int initial_game_number = 0);

private:
    void RunWorker(const std::vector<MatchJob>& jobs,
                   std::atomic<size_t>& next_job,
                   std::atomic<int>& game_counter,
                   const Control& control);

    EnginePool& pool_;
    ijccrl::core::game::TimeControl time_control_;
    ijccrl::core::rules::ConfigLimits termination_limits_{};
    int go_timeout_ms_ = 0;
    bool abort_on_stop_ = true;
    int max_failures_ = 0;
    int failure_window_games_ = 0;
    bool pause_on_unhealthy_ = false;
    ResultCallback result_callback_;
    LiveUpdateFn live_update_;
    WatchdogLogFn watchdog_log_;
    JobEventFn job_event_;
    std::vector<std::deque<int>> failure_history_{};
    std::mutex failure_mutex_{};
};

}  // namespace ijccrl::core::runtime
