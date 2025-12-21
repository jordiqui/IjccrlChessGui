#pragma once

#include "ijccrl/core/game/GameRunner.h"
#include "ijccrl/core/openings/Opening.h"
#include "ijccrl/core/runtime/EnginePool.h"
#include "ijccrl/core/tournament/RoundRobinScheduler.h"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace ijccrl::core::runtime {

struct MatchJob {
    ijccrl::core::tournament::Fixture fixture;
    ijccrl::core::openings::Opening opening;
    std::string event_name;
    std::string site_tag;
    std::string round_label;
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

    MatchRunner(EnginePool& pool,
                ijccrl::core::game::TimeControl time_control,
                int max_plies,
                ResultCallback result_callback,
                LiveUpdateFn live_update);

    void Run(const std::vector<MatchJob>& jobs, int concurrency);

private:
    void RunWorker(const std::vector<MatchJob>& jobs,
                   std::atomic<size_t>& next_job,
                   std::atomic<int>& game_counter);

    EnginePool& pool_;
    ijccrl::core::game::TimeControl time_control_;
    int max_plies_ = 0;
    ResultCallback result_callback_;
    LiveUpdateFn live_update_;
};

}  // namespace ijccrl::core::runtime
