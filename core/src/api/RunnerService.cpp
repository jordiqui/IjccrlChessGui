#include "ijccrl/core/api/RunnerService.h"

#include "ijccrl/core/broadcast/TlcsIniAdapter.h"
#include "ijccrl/core/openings/EpdParser.h"
#include "ijccrl/core/openings/OpeningPolicy.h"
#include "ijccrl/core/openings/PgnSuite.h"
#include "ijccrl/core/pgn/PgnWriter.h"
#include "ijccrl/core/runtime/EnginePool.h"
#include "ijccrl/core/runtime/MatchRunner.h"
#include "ijccrl/core/stats/StandingsTable.h"
#include "ijccrl/core/tournament/RoundRobinScheduler.h"
#include "ijccrl/core/util/AtomicFileWriter.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>

namespace ijccrl::core::api {

namespace {

void AppendTournamentPgn(const std::string& path, const std::string& pgn) {
    const std::filesystem::path fs_path(path);
    if (!fs_path.parent_path().empty()) {
        std::filesystem::create_directories(fs_path.parent_path());
    }

    const bool exists = std::filesystem::exists(fs_path);
    std::ofstream output(path, std::ios::binary | std::ios::app);
    if (!output) {
        std::cerr << "[ijccrl] Failed to open tournament PGN: " << path << '\n';
        return;
    }

    if (exists && std::filesystem::file_size(fs_path) > 0) {
        output << "\n";
    }
    output << pgn;
}

void WriteLivePgn(const std::string& path, const std::string& pgn) {
    const std::filesystem::path fs_path(path);
    if (!fs_path.parent_path().empty()) {
        std::filesystem::create_directories(fs_path.parent_path());
    }
    ijccrl::core::util::AtomicFileWriter::Write(path, pgn);
}

void AppendCsvLine(const std::string& path, const std::string& line, bool write_header) {
    const std::filesystem::path fs_path(path);
    if (!fs_path.parent_path().empty()) {
        std::filesystem::create_directories(fs_path.parent_path());
    }
    const bool exists = std::filesystem::exists(fs_path);
    std::ofstream output(path, std::ios::binary | std::ios::app);
    if (!output) {
        std::cerr << "[ijccrl] Failed to open CSV: " << path << '\n';
        return;
    }
    if (write_header && (!exists || std::filesystem::file_size(fs_path) == 0)) {
        output << "game_no,round,white,black,opening_id,fen,result,termination,pgn_path\n";
    }
    output << line << "\n";
}

void WriteResultsJson(const std::string& path,
                      const std::string& event_name,
                      const std::string& tc_desc,
                      const std::string& mode,
                      const ijccrl::core::stats::StandingsTable& standings) {
    nlohmann::json results_json;
    results_json["event"] = event_name;
    results_json["tc"] = tc_desc;
    results_json["mode"] = mode;
    results_json["games_played"] = standings.games_played();
    results_json["standings"] = nlohmann::json::array();
    for (const auto& entry : standings.standings()) {
        results_json["standings"].push_back({
            {"name", entry.name},
            {"pts", entry.points},
            {"g", entry.games},
            {"w", entry.wins},
            {"d", entry.draws},
            {"l", entry.losses},
        });
    }

    const std::filesystem::path results_path(path);
    if (!results_path.parent_path().empty()) {
        std::filesystem::create_directories(results_path.parent_path());
    }
    std::ofstream results_out(path, std::ios::binary | std::ios::trunc);
    if (results_out) {
        results_out << results_json.dump(2);
    }
}

}  // namespace

RunnerService::RunnerService() {
    state_.concurrency = 1;
}

RunnerService::~RunnerService() {
    requestStop();
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool RunnerService::loadConfig(const std::string& path) {
    RunnerConfig config;
    std::string error;
    if (!RunnerConfig::LoadFromFile(path, config, &error)) {
        AppendLogLine(error);
        return false;
    }
    setConfig(config);
    return true;
}

bool RunnerService::saveConfig(const std::string& path) const {
    std::string error;
    if (!RunnerConfig::SaveToFile(path, getConfigSnapshot(), &error)) {
        return false;
    }
    return true;
}

void RunnerService::setConfig(const RunnerConfig& config) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_ = config;
}

RunnerConfig RunnerService::getConfigSnapshot() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return config_;
}

bool RunnerService::start() {
    if (running_.load()) {
        return false;
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    stop_requested_.store(false);
    paused_.store(false);

    RunnerConfig config = getConfigSnapshot();
    worker_ = std::thread([this, config]() mutable { Run(std::move(config)); });
    return true;
}

void RunnerService::requestStop() {
    stop_requested_.store(true);
    paused_.store(false);
    pause_cv_.notify_all();
}

void RunnerService::pause() {
    if (!running_.load()) {
        return;
    }
    paused_.store(true);
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.paused = true;
    }
}

void RunnerService::resume() {
    paused_.store(false);
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.paused = false;
    }
    pause_cv_.notify_all();
}

RunnerState RunnerService::getStateSnapshot() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return state_;
}

std::vector<StandingRow> RunnerService::getStandingsSnapshot() const {
    std::lock_guard<std::mutex> lock(standings_mutex_);
    return standings_;
}

std::string RunnerService::getLastLogLines(int n) const {
    std::lock_guard<std::mutex> lock(log_mutex_);
    const int start = std::max(0, static_cast<int>(log_lines_.size()) - n);
    std::ostringstream output;
    for (size_t i = static_cast<size_t>(start); i < log_lines_.size(); ++i) {
        output << log_lines_[i];
        if (i + 1 < log_lines_.size()) {
            output << '\n';
        }
    }
    return output.str();
}

void RunnerService::AppendLogLine(const std::string& line) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    if (log_lines_.size() >= max_log_lines_) {
        log_lines_.pop_front();
    }
    log_lines_.push_back(line);
}

void RunnerService::Run(RunnerConfig config) {
    running_.store(true);
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_ = RunnerState{};
        state_.running = true;
        state_.paused = false;
        state_.concurrency = config.tournament.concurrency;
        state_.livePgnPath = config.output.live_pgn;
        state_.tourneyPgnPath = config.output.tournament_pgn;
    }

    AppendLogLine("[ijccrl] Runner starting");

    std::unique_ptr<ijccrl::core::broadcast::IBroadcastAdapter> adapter;
    std::string site_tag;

    if (config.broadcast.adapter == "tlcs_ini") {
        auto tlcs = std::make_unique<ijccrl::core::broadcast::TlcsIniAdapter>();
        if (!config.broadcast.server_ini.empty() && tlcs->Configure(config.broadcast.server_ini)) {
            site_tag = tlcs->site();
            adapter = std::move(tlcs);
            AppendLogLine("[ijccrl] TLCS adapter configured");
        } else {
            AppendLogLine("[ijccrl] Failed to configure TLCS adapter");
        }
    }

    std::vector<ijccrl::core::runtime::EngineSpec> specs;
    std::vector<std::string> engine_names;
    specs.reserve(config.engines.size());
    engine_names.reserve(config.engines.size());
    for (const auto& engine : config.engines) {
        ijccrl::core::runtime::EngineSpec spec;
        spec.name = engine.name;
        spec.command = engine.cmd;
        spec.args = engine.args;
        spec.uci_options = engine.uci_options;
        specs.push_back(std::move(spec));
        engine_names.push_back(engine.name);
    }

    ijccrl::core::runtime::EnginePool pool(std::move(specs));
    if (!pool.StartAll("")) {
        AppendLogLine("[ijccrl] Failed to start engine pool");
        running_.store(false);
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            state_.running = false;
        }
        return;
    }

    std::vector<ijccrl::core::openings::Opening> openings;
    if (!config.openings.path.empty()) {
        if (config.openings.type == "epd") {
            openings = ijccrl::core::openings::EpdParser::LoadFile(config.openings.path);
        } else if (config.openings.type == "pgn") {
            openings = ijccrl::core::openings::PgnSuite::LoadFile(config.openings.path);
        }
    }
    if (openings.empty()) {
        ijccrl::core::openings::Opening startpos;
        startpos.id = "startpos";
        openings.push_back(startpos);
    }
    if (config.openings.seed != 0) {
        std::mt19937 rng(static_cast<uint32_t>(config.openings.seed));
        std::shuffle(openings.begin(), openings.end(), rng);
    }

    auto fixtures = ijccrl::core::tournament::RoundRobinScheduler::BuildSchedule(
        static_cast<int>(config.engines.size()),
        config.tournament.double_round_robin,
        config.tournament.games_per_pairing,
        config.tournament.rounds);

    if (config.limits.max_games > 0 && static_cast<int>(fixtures.size()) > config.limits.max_games) {
        fixtures.resize(static_cast<size_t>(config.limits.max_games));
    }

    auto assigned_openings = ijccrl::core::openings::OpeningPolicy::AssignRoundRobin(
        fixtures, openings, config.tournament.games_per_pairing);

    std::vector<ijccrl::core::runtime::MatchJob> jobs;
    jobs.reserve(fixtures.size());
    for (size_t i = 0; i < fixtures.size(); ++i) {
        ijccrl::core::runtime::MatchJob job;
        job.fixture = fixtures[i];
        job.opening = assigned_openings[i];
        job.event_name = "ijccrl round robin";
        job.site_tag = site_tag;
        job.round_label = std::to_string(fixtures[i].round_index + 1);
        jobs.push_back(std::move(job));
    }

    ijccrl::core::stats::StandingsTable standings(engine_names);
    {
        std::lock_guard<std::mutex> lock(standings_mutex_);
        standings_.clear();
        standings_.reserve(engine_names.size());
        for (const auto& name : engine_names) {
            standings_.push_back({name, 0, 0, 0, 0, 0.0, 0.0});
        }
    }

    std::mutex output_mutex;
    std::atomic<int> active_games{0};

    const auto live_update = [&](const ijccrl::core::pgn::PgnGame& live_game) {
        const std::string live_pgn = ijccrl::core::pgn::PgnWriter::Render(live_game);
        if (adapter) {
            adapter->PublishLivePgn(live_pgn);
        }
        WriteLivePgn(config.output.live_pgn, live_pgn);

        std::lock_guard<std::mutex> lock(state_mutex_);
        if (!live_game.moves.empty()) {
            state_.lastMove = live_game.moves.back();
        }
    };

    const auto on_job_event = [&](const ijccrl::core::runtime::MatchJob& job,
                                  int game_number,
                                  bool started) {
        if (started) {
            active_games.fetch_add(1);
            std::lock_guard<std::mutex> lock(state_mutex_);
            state_.gameNo = game_number;
            state_.roundNo = job.fixture.round_index + 1;
            state_.whiteName = engine_names[static_cast<size_t>(job.fixture.white_engine_id)];
            state_.blackName = engine_names[static_cast<size_t>(job.fixture.black_engine_id)];
            state_.openingId = job.opening.id;
            state_.lastMove.clear();
            state_.fen = job.opening.fen;
            state_.activeGames = active_games.load();
        } else {
            active_games.fetch_sub(1);
            std::lock_guard<std::mutex> lock(state_mutex_);
            state_.activeGames = active_games.load();
        }
    };

    const auto on_result = [&](const ijccrl::core::runtime::MatchResult& result) {
        const auto& fixture = result.job.fixture;
        const std::string final_pgn = ijccrl::core::pgn::PgnWriter::Render(result.result.pgn);

        std::lock_guard<std::mutex> lock(output_mutex);
        AppendTournamentPgn(config.output.tournament_pgn, final_pgn);

        standings.RecordResult(fixture.white_engine_id, fixture.black_engine_id, result.result.state.result);

        std::ostringstream csv_line;
        csv_line << result.game_number << ','
                 << (fixture.round_index + 1) << ','
                 << engine_names[static_cast<size_t>(fixture.white_engine_id)] << ','
                 << engine_names[static_cast<size_t>(fixture.black_engine_id)] << ','
                 << result.job.opening.id << ','
                 << result.job.opening.fen << ','
                 << result.result.state.result << ','
                 << result.result.state.termination << ','
                 << config.output.tournament_pgn;
        AppendCsvLine(config.output.pairings_csv, csv_line.str(), true);

        std::ostringstream log_line;
        log_line << "GAME END #" << result.game_number << " | "
                 << engine_names[static_cast<size_t>(fixture.white_engine_id)] << " vs "
                 << engine_names[static_cast<size_t>(fixture.black_engine_id)] << " | "
                 << result.result.state.result << " | term="
                 << result.result.state.termination << " | opening="
                 << result.job.opening.id;
        AppendLogLine(log_line.str());
        if (!config.output.progress_log.empty()) {
            std::ofstream log_out(config.output.progress_log, std::ios::binary | std::ios::app);
            if (log_out) {
                log_out << log_line.str() << "\n";
            }
        }

        std::ostringstream tc_desc;
        tc_desc << config.time_control.base_seconds << "+" << config.time_control.increment_seconds;
        WriteResultsJson(config.output.results_json,
                         "ijccrl round robin",
                         tc_desc.str(),
                         config.tournament.mode,
                         standings);

        std::lock_guard<std::mutex> standings_lock(standings_mutex_);
        standings_.clear();
        for (const auto& entry : standings.standings()) {
            standings_.push_back({
                entry.name,
                entry.games,
                entry.wins,
                entry.draws,
                entry.losses,
                entry.points,
                entry.score_percent(),
            });
        }
    };

    ijccrl::core::game::TimeControl time_control;
    time_control.base_ms = config.time_control.base_seconds * 1000;
    time_control.increment_ms = config.time_control.increment_seconds * 1000;
    time_control.move_time_ms = config.time_control.move_time_ms;

    ijccrl::core::runtime::MatchRunner::Control control;
    control.stop = &stop_requested_;
    control.paused = &paused_;
    control.pause_mutex = &pause_mutex_;
    control.pause_cv = &pause_cv_;

    ijccrl::core::runtime::MatchRunner match_runner(pool,
                                                    time_control,
                                                    config.limits.max_plies,
                                                    on_result,
                                                    live_update,
                                                    on_job_event);

    match_runner.Run(jobs, config.tournament.concurrency, control);

    for (size_t i = 0; i < engine_names.size(); ++i) {
        pool.engine(static_cast<int>(i)).Stop();
    }

    AppendLogLine("[ijccrl] Runner stopped");
    running_.store(false);
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.running = false;
        state_.paused = false;
        state_.activeGames = 0;
    }
}

}  // namespace ijccrl::core::api
