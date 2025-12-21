#include "ijccrl/core/runtime/MatchRunner.h"

#include <algorithm>
#include <mutex>
#include <sstream>
#include <thread>

namespace {

bool IsStartposFen(const std::string& fen) {
    return fen == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
}

}  // namespace

namespace ijccrl::core::runtime {

MatchRunner::MatchRunner(EnginePool& pool,
                         ijccrl::core::game::TimeControl time_control,
                         int max_plies,
                         int go_timeout_ms,
                         bool abort_on_stop,
                         int max_failures,
                         int failure_window_games,
                         bool pause_on_unhealthy,
                         ResultCallback result_callback,
                         LiveUpdateFn live_update,
                         WatchdogLogFn watchdog_log,
                         JobEventFn job_event)
    : pool_(pool),
      time_control_(time_control),
      max_plies_(max_plies),
      go_timeout_ms_(go_timeout_ms),
      abort_on_stop_(abort_on_stop),
      max_failures_(max_failures),
      failure_window_games_(failure_window_games),
      pause_on_unhealthy_(pause_on_unhealthy),
      result_callback_(std::move(result_callback)),
      live_update_(std::move(live_update)),
      watchdog_log_(std::move(watchdog_log)),
      job_event_(std::move(job_event)) {}

void MatchRunner::Run(const std::vector<MatchJob>& jobs,
                      int concurrency,
                      const Control& control,
                      int initial_game_number) {
    if (jobs.empty()) {
        return;
    }

    const int worker_count = std::max(1, concurrency);
    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(worker_count));
    std::atomic<size_t> next_job{0};
    std::atomic<int> game_counter{initial_game_number};
    {
        std::lock_guard<std::mutex> lock(failure_mutex_);
        failure_history_.assign(pool_.specs().size(), {});
    }

    for (int i = 0; i < worker_count; ++i) {
        workers.emplace_back([&]() {
            RunWorker(jobs, next_job, game_counter, control);
        });
    }

    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void MatchRunner::RunWorker(const std::vector<MatchJob>& jobs,
                            std::atomic<size_t>& next_job,
                            std::atomic<int>& game_counter,
                            const Control& control) {
    ijccrl::core::game::GameRunner runner;

    while (true) {
        if (control.stop && control.stop->load()) {
            return;
        }
        if (control.paused && control.pause_mutex && control.pause_cv) {
            std::unique_lock<std::mutex> lock(*control.pause_mutex);
            control.pause_cv->wait(lock, [&]() {
                return !control.paused->load() || (control.stop && control.stop->load());
            });
        }
        if (control.stop && control.stop->load()) {
            return;
        }

        const size_t index = next_job.fetch_add(1);
        if (index >= jobs.size()) {
            return;
        }

        const auto& job = jobs[index];
        const int game_number = game_counter.fetch_add(1) + 1;
        if (job_event_) {
            job_event_(job, game_number, true);
        }
        auto lease = pool_.AcquirePair(job.fixture.white_engine_id,
                                       job.fixture.black_engine_id);
        auto& white = lease.white();
        auto& black = lease.black();

        white.NewGame();
        black.NewGame();
        white.IsReady();
        black.IsReady();

        ijccrl::core::pgn::PgnGame pgn;
        pgn.SetTag("Event", job.event_name);
        if (!job.site_tag.empty()) {
            pgn.SetTag("Site", job.site_tag);
        }
        pgn.SetTag("Round", job.round_label);
        pgn.SetTag("White", white.name());
        pgn.SetTag("Black", black.name());
        pgn.SetTag("Result", "*");
        if (!job.opening.fen.empty() && !IsStartposFen(job.opening.fen)) {
            pgn.SetTag("SetUp", "1");
            pgn.SetTag("FEN", job.opening.fen);
        }

        const auto live_update = [&](const ijccrl::core::pgn::PgnGame& live_game) {
            if (live_update_) {
                live_update_(live_game);
            }
        };

        auto result = runner.PlayGame(white,
                                      black,
                                      time_control_,
                                      max_plies_,
                                      go_timeout_ms_,
                                      abort_on_stop_ ? control.stop : nullptr,
                                      pgn,
                                      job.opening.fen,
                                      job.opening.moves,
                                      live_update);

        const auto handle_failure = [&](int engine_id,
                                         ijccrl::core::uci::UciEngine& engine,
                                         const std::string& label) {
            const auto failure = engine.last_failure();
            const bool crashed = !engine.IsRunning();
            if (failure == ijccrl::core::uci::UciEngine::Failure::None && !crashed) {
                return;
            }
            std::ostringstream message;
            if (crashed) {
                message << "WATCHDOG: Engine \"" << label << "\" crashed, exitCode=" << engine.exit_code();
            } else {
                message << "WATCHDOG: Engine \"" << label << "\" unresponsive, restarting...";
            }
            if (watchdog_log_) {
                watchdog_log_(message.str());
            }
            {
                std::lock_guard<std::mutex> lock(failure_mutex_);
                if (engine_id >= 0 && engine_id < static_cast<int>(failure_history_.size())) {
                    auto& history = failure_history_[static_cast<size_t>(engine_id)];
                    history.push_back(game_number);
                    const int window = std::max(1, failure_window_games_);
                    while (!history.empty() && history.front() <= (game_number - window)) {
                        history.pop_front();
                    }
                    if (max_failures_ > 0 && static_cast<int>(history.size()) > max_failures_) {
                        const std::string warn = "WATCHDOG: Engine \"" + label +
                                                 "\" unhealthy (too many failures).";
                        if (watchdog_log_) {
                            watchdog_log_(warn);
                        }
                        if (pause_on_unhealthy_) {
                            if (control.paused) {
                                control.paused->store(true);
                                if (control.pause_cv) {
                                    control.pause_cv->notify_all();
                                }
                            } else if (control.stop) {
                                control.stop->store(true);
                            }
                        }
                    }
                }
            }
            pool_.RestartEngine(engine_id);
        };

        handle_failure(job.fixture.white_engine_id, white, white.name());
        handle_failure(job.fixture.black_engine_id, black, black.name());

        if (job_event_) {
            job_event_(job, game_number, false);
        }
        MatchResult payload{job, result, game_number};
        if (result_callback_) {
            result_callback_(payload);
        }
    }
}

}  // namespace ijccrl::core::runtime
