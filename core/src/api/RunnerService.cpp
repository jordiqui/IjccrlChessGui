#include "ijccrl/core/api/RunnerService.h"

#include "ijccrl/core/broadcast/TlcsIniAdapter.h"
#include "ijccrl/core/export/ExportWriter.h"
#include "ijccrl/core/openings/EpdParser.h"
#include "ijccrl/core/openings/OpeningPolicy.h"
#include "ijccrl/core/openings/PgnSuite.h"
#include "ijccrl/core/persist/CheckpointState.h"
#include "ijccrl/core/pgn/PgnWriter.h"
#include "ijccrl/core/runtime/EnginePool.h"
#include "ijccrl/core/runtime/MatchRunner.h"
#include "ijccrl/core/stats/StandingsTable.h"
#include "ijccrl/core/tournament/RoundRobinScheduler.h"
#include "ijccrl/core/tournament/SwissScheduler.h"
#include "ijccrl/core/util/AtomicFileWriter.h"
#include "ijccrl/core/rules/Termination.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace ijccrl::core::api {

namespace {

bool AppendTournamentPgn(const std::string& path, const std::string& pgn) {
    const std::filesystem::path fs_path(path);
    if (!fs_path.parent_path().empty()) {
        std::filesystem::create_directories(fs_path.parent_path());
    }

    const bool exists = std::filesystem::exists(fs_path);
    std::ofstream output(path, std::ios::binary | std::ios::app);
    if (!output) {
        std::cerr << "[ijccrl] Failed to open tournament PGN: " << path << '\n';
        return false;
    }

    if (exists && std::filesystem::file_size(fs_path) > 0) {
        output << "\n";
    }
    output << pgn;
    return static_cast<bool>(output);
}

bool WriteLivePgn(const std::string& path, const std::string& pgn) {
    const std::filesystem::path fs_path(path);
    if (!fs_path.parent_path().empty()) {
        std::filesystem::create_directories(fs_path.parent_path());
    }
    return ijccrl::core::util::AtomicFileWriter::Write(path, pgn);
}

bool AppendCsvLine(const std::string& path, const std::string& line, bool write_header) {
    const std::filesystem::path fs_path(path);
    if (!fs_path.parent_path().empty()) {
        std::filesystem::create_directories(fs_path.parent_path());
    }
    const bool exists = std::filesystem::exists(fs_path);
    std::ofstream output(path, std::ios::binary | std::ios::app);
    if (!output) {
        std::cerr << "[ijccrl] Failed to open CSV: " << path << '\n';
        return false;
    }
    if (write_header && (!exists || std::filesystem::file_size(fs_path) == 0)) {
        output << "game_no,round,white,black,opening_id,fen,result,termination,pgn_path\n";
    }
    output << line << "\n";
    return static_cast<bool>(output);
}

void WriteResultsJson(const std::string& path,
                      const std::string& event_name,
                      const std::string& tc_desc,
                      const std::string& mode,
                      const ijccrl::core::stats::StandingsTable& standings,
                      const std::unordered_map<std::string, int>& termination_counts) {
    nlohmann::json results_json;
    results_json["event"] = event_name;
    results_json["tc"] = tc_desc;
    results_json["mode"] = mode;
    results_json["games_played"] = standings.games_played();
    results_json["termination_counts"] = termination_counts;
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

std::string FormatUtcTimestamp(std::time_t timestamp) {
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &timestamp);
#else
    gmtime_r(&timestamp, &utc);
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
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
    return startWithResume(false);
}

bool RunnerService::startWithResume(bool resume) {
    if (running_.load()) {
        return false;
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    stop_requested_.store(false);
    paused_.store(false);

    RunnerConfig config = getConfigSnapshot();
    worker_ = std::thread([this, config, resume]() mutable { Run(std::move(config), resume); });
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

bool RunnerService::exportResults(const std::string& directory, std::string* error) {
    const auto config = getConfigSnapshot();
    const std::string event_name = config.tournament.mode == "swiss" ? "ijccrl swiss" : "ijccrl round robin";
    std::ostringstream tc_desc;
    tc_desc << config.time_control.base_seconds << "+" << config.time_control.increment_seconds;

    std::vector<ijccrl::core::stats::EngineStats> standings_snapshot;
    {
        std::lock_guard<std::mutex> lock(standings_mutex_);
        standings_snapshot.reserve(standings_.size());
        for (const auto& entry : standings_) {
            ijccrl::core::stats::EngineStats row;
            row.name = entry.name;
            row.games = entry.games;
            row.wins = entry.wins;
            row.draws = entry.draws;
            row.losses = entry.losses;
            row.points = entry.points;
            standings_snapshot.push_back(std::move(row));
        }
    }

    const std::string standings_csv = directory + "/standings.csv";
    const std::string standings_html = directory + "/standings.html";
    const std::string summary_json = directory + "/summary.json";

    if (!ijccrl::core::exporter::WriteStandingsCsv(standings_csv, standings_snapshot)) {
        if (error) {
            *error = "Failed to write standings.csv";
        }
        return false;
    }
    if (!ijccrl::core::exporter::WriteStandingsHtml(standings_html, event_name, standings_snapshot)) {
        if (error) {
            *error = "Failed to write standings.html";
        }
        return false;
    }
    int total_games = 0;
    for (const auto& row : standings_snapshot) {
        total_games += row.games;
    }
    total_games = static_cast<int>(std::ceil(static_cast<double>(total_games) / 2.0));
    if (!ijccrl::core::exporter::WriteSummaryJson(summary_json,
                                                   event_name,
                                                   tc_desc.str(),
                                                   config.tournament.mode,
                                                   total_games,
                                                   standings_snapshot)) {
        if (error) {
            *error = "Failed to write summary.json";
        }
        return false;
    }
    return true;
}

void RunnerService::Run(RunnerConfig config, bool resume) {
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
    std::atomic<int> disk_write_errors{0};
    std::atomic<int> active_games{0};
    std::atomic<int> last_game_number{0};
    std::atomic<std::time_t> last_game_end_time{0};

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

    ijccrl::core::runtime::EnginePool pool(
        std::move(specs),
        [this](const std::string& line) { AppendLogLine(line); });
    pool.set_handshake_timeout_ms(config.watchdog.handshake_timeout_ms);
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

    if (config.tournament.mode == "swiss") {
        const std::string event_name = "ijccrl swiss";
        const int engine_count = static_cast<int>(engine_names.size());
        const int games_per_pairing = std::max(1, config.tournament.games_per_pairing);
        const int fixtures_per_round = (engine_count / 2) * games_per_pairing;
        int total_rounds = std::max(1, config.tournament.rounds);
        if (config.limits.max_games > 0 && fixtures_per_round > 0) {
            const int max_rounds = config.limits.max_games / fixtures_per_round;
            if (max_rounds > 0) {
                total_rounds = std::min(total_rounds, max_rounds);
            }
        }
        const int total_games = fixtures_per_round * total_rounds;

        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            state_.totalRounds = total_rounds;
        }

        ijccrl::core::persist::CheckpointState checkpoint_state;
        const std::string checkpoint_path = config.output.checkpoint_json;
        const std::string config_hash =
            ijccrl::core::persist::ComputeConfigHash(RunnerConfig::ToJsonString(config));
        bool has_checkpoint = false;
        if (resume && std::filesystem::exists(checkpoint_path)) {
            std::string error;
            if (ijccrl::core::persist::LoadCheckpoint(checkpoint_path, checkpoint_state, &error)) {
                if (checkpoint_state.config_hash == config_hash) {
                    has_checkpoint = true;
                    AppendLogLine("[ijccrl] Resuming from checkpoint");
                    if (!checkpoint_state.active_games.empty()) {
                        AppendLogLine("[ijccrl] Active games will be restarted on resume");
                    }
                } else {
                    AppendLogLine("[ijccrl] Checkpoint config mismatch; starting fresh");
                }
            } else {
                AppendLogLine("[ijccrl] Failed to load checkpoint: " + error);
            }
        }

        std::vector<int> completed_fixture_indices;
        std::vector<ijccrl::core::persist::CompletedGameMeta> completed_games;
        int initial_game_number = 0;
        int current_round = 0;
        int next_fixture_index = 0;
        std::vector<int> bye_history;
        std::vector<ijccrl::core::persist::CheckpointState::SwissPairing> pairings_played;
        std::vector<ijccrl::core::tournament::SwissColorState> color_history(
            static_cast<size_t>(engine_count));

        if (has_checkpoint) {
            completed_fixture_indices = checkpoint_state.completed_fixture_indices;
            completed_games = checkpoint_state.completed_games;
            initial_game_number = checkpoint_state.last_game_no;
            current_round = checkpoint_state.swiss.current_round;
            next_fixture_index = checkpoint_state.next_fixture_index;
            bye_history = checkpoint_state.swiss.bye_history;
            pairings_played = checkpoint_state.swiss.pairings_played;
            if (!checkpoint_state.swiss.color_history.empty()) {
                color_history.clear();
                for (const auto& entry : checkpoint_state.swiss.color_history) {
                    ijccrl::core::tournament::SwissColorState state;
                    state.last_color = entry.last_color;
                    state.streak = entry.streak;
                    color_history.push_back(state);
                }
            }
        }

        std::unordered_set<int> completed_set(completed_fixture_indices.begin(),
                                              completed_fixture_indices.end());
        std::atomic<int> completed_count{static_cast<int>(completed_set.size())};

        std::unordered_set<long long> pairings_played_set;
        std::vector<std::vector<int>> opponent_history(static_cast<size_t>(engine_count));
        for (const auto& pairing : pairings_played) {
            const int white = pairing.white_engine_id;
            const int black = pairing.black_engine_id;
            if (white < 0 || black < 0 || white >= engine_count || black >= engine_count) {
                continue;
            }
            const long long key = (static_cast<long long>(std::min(white, black)) << 32) |
                                  static_cast<unsigned int>(std::max(white, black));
            pairings_played_set.insert(key);
            opponent_history[static_cast<size_t>(white)].push_back(black);
            opponent_history[static_cast<size_t>(black)].push_back(white);
        }

        ijccrl::core::stats::StandingsTable standings(engine_names);
        if (has_checkpoint && !checkpoint_state.standings.empty()) {
            std::vector<ijccrl::core::stats::EngineStats> snapshot;
            snapshot.reserve(engine_names.size());
            std::unordered_map<std::string, ijccrl::core::persist::StandingsSnapshot> by_name;
            for (const auto& entry : checkpoint_state.standings) {
                by_name.emplace(entry.name, entry);
            }
            for (const auto& name : engine_names) {
                ijccrl::core::stats::EngineStats stats;
                stats.name = name;
                auto it = by_name.find(name);
                if (it != by_name.end()) {
                    stats.games = it->second.games;
                    stats.wins = it->second.wins;
                    stats.draws = it->second.draws;
                    stats.losses = it->second.losses;
                    stats.points = it->second.points;
                }
                snapshot.push_back(std::move(stats));
            }
            standings.LoadSnapshot(snapshot);
        }
        {
            std::lock_guard<std::mutex> lock(standings_mutex_);
            standings_.clear();
            standings_.reserve(engine_names.size());
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
        }

        struct PendingFixture {
            ijccrl::core::tournament::Fixture fixture;
            int fixture_index = 0;
        };

        std::vector<PendingFixture> pending_fixtures;
        if (has_checkpoint) {
            pending_fixtures.reserve(checkpoint_state.swiss.pending_pairings_current_round.size());
            for (const auto& pending : checkpoint_state.swiss.pending_pairings_current_round) {
                pending_fixtures.push_back({pending.fixture, pending.fixture_index});
            }
        }

        std::unordered_map<long long, int> pairing_games_completed;
        std::unordered_map<long long, int> pairing_games_total;
        if (!pending_fixtures.empty()) {
            std::unordered_map<long long, int> pending_counts;
            for (const auto& pending : pending_fixtures) {
                const int white = pending.fixture.white_engine_id;
                const int black = pending.fixture.black_engine_id;
                const long long key = (static_cast<long long>(std::min(white, black)) << 32) |
                                      static_cast<unsigned int>(std::max(white, black));
                pending_counts[key] += 1;
            }
            for (const auto& entry : pending_counts) {
                pairing_games_total[entry.first] = games_per_pairing;
                pairing_games_completed[entry.first] = games_per_pairing - entry.second;
            }
        }

        const auto update_pairings_list = [&](const std::vector<std::pair<int, int>>& pairings,
                                              int bye_engine_id) {
            std::vector<std::string> round_pairings;
            round_pairings.reserve(pairings.size() + (bye_engine_id >= 0 ? 1 : 0));
            for (const auto& pairing : pairings) {
                round_pairings.push_back(engine_names[static_cast<size_t>(pairing.first)] +
                                         " vs " +
                                         engine_names[static_cast<size_t>(pairing.second)]);
            }
            if (bye_engine_id >= 0) {
                round_pairings.push_back("BYE: " + engine_names[static_cast<size_t>(bye_engine_id)]);
            }
            std::lock_guard<std::mutex> lock(state_mutex_);
            state_.currentRoundPairings = std::move(round_pairings);
        };

        if (!pending_fixtures.empty()) {
            std::unordered_set<long long> pairing_seen;
            std::vector<std::pair<int, int>> round_pairings;
            for (const auto& pending : pending_fixtures) {
                const int white = pending.fixture.white_engine_id;
                const int black = pending.fixture.black_engine_id;
                const long long key = (static_cast<long long>(std::min(white, black)) << 32) |
                                      static_cast<unsigned int>(std::max(white, black));
                if (pairing_seen.insert(key).second) {
                    round_pairings.emplace_back(white, black);
                }
            }
            update_pairings_list(round_pairings, -1);
        }

        std::mutex output_mutex;
        std::mutex checkpoint_mutex;
        std::vector<ijccrl::core::persist::ActiveGameMeta> active_games_meta;
        std::unordered_map<std::string, int> termination_counts;

        const auto live_update = [&](const ijccrl::core::pgn::PgnGame& live_game) {
            const std::string live_pgn = ijccrl::core::pgn::PgnWriter::Render(live_game);
            if (adapter) {
                adapter->PublishLivePgn(live_pgn);
            }
            if (!WriteLivePgn(config.output.live_pgn, live_pgn)) {
                disk_write_errors.fetch_add(1);
            }

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
                state_.terminationReason.clear();
                state_.tablebaseUsed = false;
                state_.activeGames = active_games.load();
                {
                    std::lock_guard<std::mutex> checkpoint_lock(checkpoint_mutex);
                    ijccrl::core::persist::ActiveGameMeta active;
                    active.game_no = game_number;
                    active.fixture_index = job.fixture_index;
                    active.white = engine_names[static_cast<size_t>(job.fixture.white_engine_id)];
                    active.black = engine_names[static_cast<size_t>(job.fixture.black_engine_id)];
                    active.opening_id = job.opening.id;
                    active_games_meta.push_back(std::move(active));
                }
            } else {
                active_games.fetch_sub(1);
                std::lock_guard<std::mutex> lock(state_mutex_);
                state_.activeGames = active_games.load();
                {
                    std::lock_guard<std::mutex> checkpoint_lock(checkpoint_mutex);
                    active_games_meta.erase(std::remove_if(active_games_meta.begin(),
                                                           active_games_meta.end(),
                                                           [&](const auto& entry) {
                                                               return entry.game_no == game_number;
                                                           }),
                                            active_games_meta.end());
                }
            }
        };

        std::function<void()> write_checkpoint;
        const auto on_result = [&](const ijccrl::core::runtime::MatchResult& result) {
            const auto& fixture = result.job.fixture;
            const std::string final_pgn = ijccrl::core::pgn::PgnWriter::Render(result.result.pgn);
            long long pgn_offset = 0;
            {
                const std::filesystem::path fs_path(config.output.tournament_pgn);
                if (std::filesystem::exists(fs_path)) {
                    pgn_offset = static_cast<long long>(std::filesystem::file_size(fs_path));
                }
            }

            std::lock_guard<std::mutex> lock(output_mutex);
            if (!AppendTournamentPgn(config.output.tournament_pgn, final_pgn)) {
                disk_write_errors.fetch_add(1);
            }

            if (config.output.write_game_files) {
                const std::filesystem::path games_dir(config.output.games_dir);
                if (!games_dir.empty()) {
                    std::filesystem::create_directories(games_dir);
                    std::ostringstream name;
                    name << "game_" << std::setw(6) << std::setfill('0') << result.game_number << ".pgn";
                    const std::filesystem::path game_path = games_dir / name.str();
                    std::ofstream game_out(game_path, std::ios::binary | std::ios::trunc);
                    if (game_out) {
                        game_out << final_pgn;
                    } else {
                        disk_write_errors.fetch_add(1);
                    }
                }
            }

            standings.RecordResult(fixture.white_engine_id, fixture.black_engine_id, result.result.state.result);
            if (!result.result.state.termination.empty()) {
                termination_counts[result.result.state.termination] += 1;
            }

            const auto update_color = [&](int engine_id, int color) {
                auto& state = color_history[static_cast<size_t>(engine_id)];
                if (state.last_color == color) {
                    state.streak += 1;
                } else {
                    state.last_color = color;
                    state.streak = 1;
                }
            };
            update_color(fixture.white_engine_id, 1);
            update_color(fixture.black_engine_id, -1);

            const long long pairing_key = (static_cast<long long>(std::min(fixture.white_engine_id,
                                                                           fixture.black_engine_id))
                                           << 32) |
                                          static_cast<unsigned int>(std::max(fixture.white_engine_id,
                                                                             fixture.black_engine_id));
            const int completed = ++pairing_games_completed[pairing_key];
            const int total = pairing_games_total[pairing_key];
            if (completed == total && pairings_played_set.insert(pairing_key).second) {
                ijccrl::core::persist::CheckpointState::SwissPairing entry;
                entry.white_engine_id = std::min(fixture.white_engine_id, fixture.black_engine_id);
                entry.black_engine_id = std::max(fixture.white_engine_id, fixture.black_engine_id);
                pairings_played.push_back(entry);
                opponent_history[static_cast<size_t>(fixture.white_engine_id)].push_back(fixture.black_engine_id);
                opponent_history[static_cast<size_t>(fixture.black_engine_id)].push_back(fixture.white_engine_id);
            }

            pending_fixtures.erase(std::remove_if(pending_fixtures.begin(),
                                                  pending_fixtures.end(),
                                                  [&](const auto& pending) {
                                                      return pending.fixture_index == result.job.fixture_index;
                                                  }),
                                   pending_fixtures.end());
            if (pending_fixtures.empty()) {
                current_round += 1;
                pairing_games_completed.clear();
                pairing_games_total.clear();
            }

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
            if (!AppendCsvLine(config.output.pairings_csv, csv_line.str(), true)) {
                disk_write_errors.fetch_add(1);
            }

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
                } else {
                    disk_write_errors.fetch_add(1);
                }
            }

            {
                std::lock_guard<std::mutex> state_lock(state_mutex_);
                state_.terminationReason = result.result.state.termination;
                state_.tablebaseUsed = result.result.state.tablebase_used;
            }

            std::ostringstream tc_desc;
            tc_desc << config.time_control.base_seconds << "+" << config.time_control.increment_seconds;
            WriteResultsJson(config.output.results_json,
                             event_name,
                             tc_desc.str(),
                             config.tournament.mode,
                             standings,
                             termination_counts);

            ijccrl::core::exporter::WriteStandingsCsv(config.output.standings_csv, standings.standings());
            ijccrl::core::exporter::WriteStandingsHtml(config.output.standings_html,
                                                       event_name,
                                                       standings.standings());
            ijccrl::core::exporter::WriteSummaryJson(config.output.summary_json,
                                                     event_name,
                                                     tc_desc.str(),
                                                     config.tournament.mode,
                                                     total_games,
                                                     standings.standings());

            {
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
            }

            {
                std::lock_guard<std::mutex> checkpoint_lock(checkpoint_mutex);
                ijccrl::core::persist::CompletedGameMeta meta;
                meta.game_no = result.game_number;
                meta.fixture_index = result.job.fixture_index;
                meta.white = engine_names[static_cast<size_t>(fixture.white_engine_id)];
                meta.black = engine_names[static_cast<size_t>(fixture.black_engine_id)];
                meta.opening_id = result.job.opening.id;
                meta.result = result.result.state.result;
                meta.termination = result.result.state.termination;
                meta.pgn_offset = pgn_offset;
                meta.pgn_path = config.output.tournament_pgn;
                completed_games.push_back(std::move(meta));
                completed_set.insert(result.job.fixture_index);
                completed_count.store(static_cast<int>(completed_set.size()));
            }
            last_game_number.store(result.game_number);
            last_game_end_time.store(std::time(nullptr));
            if (write_checkpoint) {
                write_checkpoint();
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

        const auto watchdog_log = [this](const std::string& line) {
            AppendLogLine(line);
        };

        write_checkpoint = [&]() {
            ijccrl::core::persist::CheckpointState snapshot;
            snapshot.version = 2;
            snapshot.config_hash = config_hash;
            snapshot.total_games = total_games;
            snapshot.next_fixture_index = next_fixture_index;
            snapshot.opening_index = next_fixture_index;
            snapshot.rng_seed = static_cast<std::uint64_t>(config.openings.seed);
            snapshot.last_game_no = last_game_number.load();
            std::time_t last_time = last_game_end_time.load();
            snapshot.last_game_end_time = last_time == 0 ? "" : FormatUtcTimestamp(last_time);
            snapshot.swiss.current_round = current_round;
            snapshot.swiss.bye_history = bye_history;
            snapshot.swiss.pairings_played = pairings_played;

            snapshot.swiss.color_history.clear();
            for (const auto& entry : color_history) {
                ijccrl::core::persist::CheckpointState::SwissColorSnapshot color_entry;
                color_entry.last_color = entry.last_color;
                color_entry.streak = entry.streak;
                snapshot.swiss.color_history.push_back(color_entry);
            }

            std::vector<int> completed_snapshot;
            {
                std::lock_guard<std::mutex> lock(checkpoint_mutex);
                snapshot.completed_games = completed_games;
                snapshot.active_games = active_games_meta;
                completed_snapshot.assign(completed_set.begin(), completed_set.end());
            }
            snapshot.completed_fixture_indices = completed_snapshot;

            if (!pending_fixtures.empty()) {
                snapshot.next_fixture_index = next_fixture_index;
                const auto& pending = pending_fixtures.front();
                snapshot.next_game.fixture_index = pending.fixture_index;
                snapshot.next_game.white =
                    engine_names[static_cast<size_t>(pending.fixture.white_engine_id)];
                snapshot.next_game.black =
                    engine_names[static_cast<size_t>(pending.fixture.black_engine_id)];
                snapshot.next_game.opening_id =
                    ijccrl::core::openings::OpeningPolicy::AssignSwissForIndex(
                        pending.fixture_index, openings, games_per_pairing)
                        .id;
            }

            snapshot.swiss.pending_pairings_current_round.clear();
            for (const auto& pending : pending_fixtures) {
                ijccrl::core::persist::CheckpointState::SwissPendingFixture entry;
                entry.fixture = pending.fixture;
                entry.fixture_index = pending.fixture_index;
                snapshot.swiss.pending_pairings_current_round.push_back(std::move(entry));
            }

            {
                std::lock_guard<std::mutex> standings_lock(standings_mutex_);
                snapshot.standings.clear();
                for (const auto& row : standings_) {
                    ijccrl::core::persist::StandingsSnapshot entry;
                    entry.name = row.name;
                    entry.games = row.games;
                    entry.wins = row.wins;
                    entry.draws = row.draws;
                    entry.losses = row.losses;
                    entry.points = row.points;
                    snapshot.standings.push_back(std::move(entry));
                }
            }

            if (!ijccrl::core::persist::SaveCheckpoint(checkpoint_path, snapshot)) {
                disk_write_errors.fetch_add(1);
            }
        };

        std::atomic<bool> checkpoint_running{false};
        std::thread checkpoint_thread;
        if (config.output.checkpoint_interval_seconds > 0) {
            checkpoint_running.store(true);
            checkpoint_thread = std::thread([&]() {
                while (checkpoint_running.load()) {
                    std::this_thread::sleep_for(std::chrono::seconds(config.output.checkpoint_interval_seconds));
                    if (!running_.load()) {
                        break;
                    }
                    write_checkpoint();
                }
            });
        }

        std::atomic<bool> metrics_running{false};
        std::thread metrics_thread;
        if (config.output.metrics_interval_seconds > 0) {
            metrics_running.store(true);
            metrics_thread = std::thread([&]() {
                while (metrics_running.load()) {
                    std::this_thread::sleep_for(std::chrono::seconds(config.output.metrics_interval_seconds));
                    if (!running_.load()) {
                        break;
                    }
                    nlohmann::json metrics;
                    metrics["active_games"] = active_games.load();
                    metrics["queue_remaining"] = total_games - completed_count.load();
                    metrics["total_games"] = total_games;
                    metrics["engines_running"] = static_cast<int>(engine_names.size());
                    std::time_t last_time = last_game_end_time.load();
                    metrics["last_game_end_time"] = last_time == 0 ? "" : FormatUtcTimestamp(last_time);
                    metrics["disk_write_errors_count"] = disk_write_errors.load();
                    if (!ijccrl::core::util::AtomicFileWriter::Write(config.output.metrics_json,
                                                                     metrics.dump(2))) {
                        disk_write_errors.fetch_add(1);
                    }
                }
            });
        }

        ijccrl::core::rules::ConfigLimits termination_limits;
        termination_limits.max_plies = config.limits.max_plies;
        termination_limits.draw_by_repetition = config.limits.draw_by_repetition;
        termination_limits.adjudication.enabled = config.adjudication.enabled;
        termination_limits.adjudication.score_draw_cp = config.adjudication.score_draw_cp;
        termination_limits.adjudication.score_draw_moves = config.adjudication.score_draw_moves;
        termination_limits.adjudication.score_win_cp = config.adjudication.score_win_cp;
        termination_limits.adjudication.score_win_moves = config.adjudication.score_win_moves;
        termination_limits.adjudication.min_depth = config.adjudication.min_depth;
        termination_limits.tablebases.enabled = config.tablebases.enabled;
        termination_limits.tablebases.paths = config.tablebases.paths;
        termination_limits.tablebases.probe_limit_pieces = config.tablebases.probe_limit_pieces;
        termination_limits.resign.enabled = config.resign.enabled;
        termination_limits.resign.cp = config.resign.cp;
        termination_limits.resign.moves = config.resign.moves;
        termination_limits.resign.min_depth = config.resign.min_depth;

        ijccrl::core::runtime::MatchRunner match_runner(pool,
                                                        time_control,
                                                        termination_limits,
                                                        config.watchdog.go_timeout_ms,
                                                        config.limits.abort_on_stop,
                                                        config.watchdog.max_failures,
                                                        config.watchdog.failure_window_games,
                                                        config.watchdog.pause_on_unhealthy,
                                                        on_result,
                                                        live_update,
                                                        watchdog_log,
                                                        on_job_event);

        write_checkpoint();

        while (current_round < total_rounds && !stop_requested_.load()) {
            std::vector<ijccrl::core::runtime::MatchJob> jobs;
            if (pending_fixtures.empty()) {
                std::vector<double> scores;
                scores.reserve(engine_count);
                for (const auto& entry : standings.standings()) {
                    scores.push_back(entry.points);
                }

                ijccrl::core::tournament::SwissScheduler scheduler;
                const auto swiss_round = scheduler.BuildSwissRound(current_round,
                                                                   scores,
                                                                   opponent_history,
                                                                   bye_history,
                                                                   color_history,
                                                                   pairings_played_set,
                                                                   games_per_pairing,
                                                                   config.tournament.avoid_repeats);

                const int bye_engine = swiss_round.round.bye_engine_id.value_or(-1);
                if (bye_engine >= 0) {
                    if (config.tournament.bye_points > 0.0) {
                        standings.RecordBye(bye_engine, config.tournament.bye_points);
                    }
                    bye_history.push_back(bye_engine);
                    AppendLogLine("[ijccrl] Swiss bye: " + engine_names[static_cast<size_t>(bye_engine)]);
                }

                update_pairings_list(swiss_round.pairings, bye_engine);

                for (const auto& pairing : swiss_round.pairings) {
                    const long long key = (static_cast<long long>(std::min(pairing.first, pairing.second)) << 32) |
                                          static_cast<unsigned int>(std::max(pairing.first, pairing.second));
                    pairing_games_total[key] = games_per_pairing;
                }

                pending_fixtures.clear();
                for (const auto& fixture : swiss_round.round.fixtures) {
                    PendingFixture pending;
                    pending.fixture = fixture;
                    pending.fixture_index = next_fixture_index++;
                    pending_fixtures.push_back(pending);
                }
            }

            jobs.reserve(pending_fixtures.size());
            for (const auto& pending : pending_fixtures) {
                if (completed_set.count(pending.fixture_index) > 0) {
                    continue;
                }
                ijccrl::core::runtime::MatchJob job;
                job.fixture = pending.fixture;
                job.opening = ijccrl::core::openings::OpeningPolicy::AssignSwissForIndex(
                    pending.fixture_index, openings, games_per_pairing);
                job.event_name = event_name;
                job.site_tag = site_tag;
                job.round_label = std::to_string(pending.fixture.round_index + 1);
                job.fixture_index = pending.fixture_index;
                jobs.push_back(std::move(job));
            }

            if (jobs.empty()) {
                current_round += 1;
                pending_fixtures.clear();
                continue;
            }

            match_runner.Run(jobs, config.tournament.concurrency, control, initial_game_number);
            initial_game_number = last_game_number.load();
        }

        for (size_t i = 0; i < engine_names.size(); ++i) {
            pool.engine(static_cast<int>(i)).Stop();
        }

        write_checkpoint();
        if (checkpoint_running.load()) {
            checkpoint_running.store(false);
            if (checkpoint_thread.joinable()) {
                checkpoint_thread.join();
            }
        }
        if (metrics_running.load()) {
            metrics_running.store(false);
            if (metrics_thread.joinable()) {
                metrics_thread.join();
            }
        }

        AppendLogLine("[ijccrl] Runner stopped");
        running_.store(false);
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            state_.running = false;
            state_.paused = false;
            state_.activeGames = 0;
        }
        return;
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

    int total_rounds = 0;
    for (const auto& fixture : fixtures) {
        total_rounds = std::max(total_rounds, fixture.round_index + 1);
    }
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.totalRounds = total_rounds;
    }

    std::vector<std::vector<std::string>> round_pairings_strings(static_cast<size_t>(total_rounds));
    std::vector<std::unordered_set<long long>> round_pairing_keys(static_cast<size_t>(total_rounds));
    for (const auto& fixture : fixtures) {
        if (fixture.game_index_within_pairing != 0) {
            continue;
        }
        const int round_index = fixture.round_index;
        if (round_index < 0 || round_index >= total_rounds) {
            continue;
        }
        const int white = fixture.white_engine_id;
        const int black = fixture.black_engine_id;
        const long long key = (static_cast<long long>(std::min(white, black)) << 32) |
                              static_cast<unsigned int>(std::max(white, black));
        if (round_pairing_keys[static_cast<size_t>(round_index)].insert(key).second) {
            round_pairings_strings[static_cast<size_t>(round_index)].push_back(
                engine_names[static_cast<size_t>(white)] + " vs " +
                engine_names[static_cast<size_t>(black)]);
        }
    }

    ijccrl::core::persist::CheckpointState checkpoint_state;
    const std::string checkpoint_path = config.output.checkpoint_json;
    const std::string config_hash =
        ijccrl::core::persist::ComputeConfigHash(RunnerConfig::ToJsonString(config));
    bool has_checkpoint = false;
    if (resume && std::filesystem::exists(checkpoint_path)) {
        std::string error;
        if (ijccrl::core::persist::LoadCheckpoint(checkpoint_path, checkpoint_state, &error)) {
            if (checkpoint_state.config_hash == config_hash) {
                has_checkpoint = true;
                AppendLogLine("[ijccrl] Resuming from checkpoint");
                if (!checkpoint_state.active_games.empty()) {
                    AppendLogLine("[ijccrl] Active games will be restarted on resume");
                }
            } else {
                AppendLogLine("[ijccrl] Checkpoint config mismatch; starting fresh");
            }
        } else {
            AppendLogLine("[ijccrl] Failed to load checkpoint: " + error);
        }
    }

    std::vector<int> completed_fixture_indices;
    std::vector<ijccrl::core::persist::CompletedGameMeta> completed_games;
    int initial_game_number = 0;

    if (has_checkpoint) {
        completed_fixture_indices = checkpoint_state.completed_fixture_indices;
        completed_games = checkpoint_state.completed_games;
        initial_game_number = checkpoint_state.last_game_no;
    }

    std::unordered_set<int> completed_set(completed_fixture_indices.begin(),
                                          completed_fixture_indices.end());
    std::atomic<int> completed_count{static_cast<int>(completed_set.size())};

    std::vector<ijccrl::core::runtime::MatchJob> jobs;
    jobs.reserve(fixtures.size());
    for (size_t i = 0; i < fixtures.size(); ++i) {
        if (completed_set.count(static_cast<int>(i)) > 0) {
            continue;
        }
        ijccrl::core::runtime::MatchJob job;
        job.fixture = fixtures[i];
        job.opening = assigned_openings[i];
        job.event_name = "ijccrl round robin";
        job.site_tag = site_tag;
        job.round_label = std::to_string(fixtures[i].round_index + 1);
        job.fixture_index = static_cast<int>(i);
        jobs.push_back(std::move(job));
    }

    ijccrl::core::stats::StandingsTable standings(engine_names);
    if (has_checkpoint && !checkpoint_state.standings.empty()) {
        std::vector<ijccrl::core::stats::EngineStats> snapshot;
        snapshot.reserve(engine_names.size());
        std::unordered_map<std::string, ijccrl::core::persist::StandingsSnapshot> by_name;
        for (const auto& entry : checkpoint_state.standings) {
            by_name.emplace(entry.name, entry);
        }
        for (const auto& name : engine_names) {
            ijccrl::core::stats::EngineStats stats;
            stats.name = name;
            auto it = by_name.find(name);
            if (it != by_name.end()) {
                stats.games = it->second.games;
                stats.wins = it->second.wins;
                stats.draws = it->second.draws;
                stats.losses = it->second.losses;
                stats.points = it->second.points;
            }
            snapshot.push_back(std::move(stats));
        }
        standings.LoadSnapshot(snapshot);
    }
    {
        std::lock_guard<std::mutex> lock(standings_mutex_);
        standings_.clear();
        standings_.reserve(engine_names.size());
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
    }

    std::mutex output_mutex;
    std::mutex checkpoint_mutex;
    std::vector<ijccrl::core::persist::ActiveGameMeta> active_games_meta;
    std::unordered_map<std::string, int> termination_counts;
    int total_games = static_cast<int>(fixtures.size());

    const auto live_update = [&](const ijccrl::core::pgn::PgnGame& live_game) {
        const std::string live_pgn = ijccrl::core::pgn::PgnWriter::Render(live_game);
        if (adapter) {
            adapter->PublishLivePgn(live_pgn);
        }
        if (!WriteLivePgn(config.output.live_pgn, live_pgn)) {
            disk_write_errors.fetch_add(1);
        }

        std::lock_guard<std::mutex> lock(state_mutex_);
        if (!live_game.moves.empty()) {
            state_.lastMove = live_game.moves.back();
        }
    };

    int last_pairings_round = -1;
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
            state_.terminationReason.clear();
            state_.tablebaseUsed = false;
            state_.activeGames = active_games.load();
            if (job.fixture.round_index != last_pairings_round &&
                job.fixture.round_index >= 0 &&
                job.fixture.round_index < static_cast<int>(round_pairings_strings.size())) {
                state_.currentRoundPairings =
                    round_pairings_strings[static_cast<size_t>(job.fixture.round_index)];
                last_pairings_round = job.fixture.round_index;
            }
            {
                std::lock_guard<std::mutex> checkpoint_lock(checkpoint_mutex);
                ijccrl::core::persist::ActiveGameMeta active;
                active.game_no = game_number;
                active.fixture_index = job.fixture_index;
                active.white = engine_names[static_cast<size_t>(job.fixture.white_engine_id)];
                active.black = engine_names[static_cast<size_t>(job.fixture.black_engine_id)];
                active.opening_id = job.opening.id;
                active_games_meta.push_back(std::move(active));
            }
        } else {
            active_games.fetch_sub(1);
            std::lock_guard<std::mutex> lock(state_mutex_);
            state_.activeGames = active_games.load();
            {
                std::lock_guard<std::mutex> checkpoint_lock(checkpoint_mutex);
                active_games_meta.erase(std::remove_if(active_games_meta.begin(),
                                                       active_games_meta.end(),
                                                       [&](const auto& entry) {
                                                           return entry.game_no == game_number;
                                                       }),
                                        active_games_meta.end());
            }
        }
    };

    std::function<void()> write_checkpoint;
    const auto on_result = [&](const ijccrl::core::runtime::MatchResult& result) {
        const auto& fixture = result.job.fixture;
        const std::string final_pgn = ijccrl::core::pgn::PgnWriter::Render(result.result.pgn);
        long long pgn_offset = 0;
        {
            const std::filesystem::path fs_path(config.output.tournament_pgn);
            if (std::filesystem::exists(fs_path)) {
                pgn_offset = static_cast<long long>(std::filesystem::file_size(fs_path));
            }
        }

        std::lock_guard<std::mutex> lock(output_mutex);
        if (!AppendTournamentPgn(config.output.tournament_pgn, final_pgn)) {
            disk_write_errors.fetch_add(1);
        }

        if (config.output.write_game_files) {
            const std::filesystem::path games_dir(config.output.games_dir);
            if (!games_dir.empty()) {
                std::filesystem::create_directories(games_dir);
                std::ostringstream name;
                name << "game_" << std::setw(6) << std::setfill('0') << result.game_number << ".pgn";
                const std::filesystem::path game_path = games_dir / name.str();
                std::ofstream game_out(game_path, std::ios::binary | std::ios::trunc);
                if (game_out) {
                    game_out << final_pgn;
                } else {
                    disk_write_errors.fetch_add(1);
                }
            }
        }

        standings.RecordResult(fixture.white_engine_id, fixture.black_engine_id, result.result.state.result);
        if (!result.result.state.termination.empty()) {
            termination_counts[result.result.state.termination] += 1;
        }

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
        if (!AppendCsvLine(config.output.pairings_csv, csv_line.str(), true)) {
            disk_write_errors.fetch_add(1);
        }

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
            } else {
                disk_write_errors.fetch_add(1);
            }
        }

        {
            std::lock_guard<std::mutex> state_lock(state_mutex_);
            state_.terminationReason = result.result.state.termination;
            state_.tablebaseUsed = result.result.state.tablebase_used;
        }

        std::ostringstream tc_desc;
        tc_desc << config.time_control.base_seconds << "+" << config.time_control.increment_seconds;
        WriteResultsJson(config.output.results_json,
                         "ijccrl round robin",
                         tc_desc.str(),
                         config.tournament.mode,
                         standings,
                         termination_counts);

        ijccrl::core::exporter::WriteStandingsCsv(config.output.standings_csv, standings.standings());
        ijccrl::core::exporter::WriteStandingsHtml(config.output.standings_html,
                                                   "ijccrl round robin",
                                                   standings.standings());
        ijccrl::core::exporter::WriteSummaryJson(config.output.summary_json,
                                                 "ijccrl round robin",
                                                 tc_desc.str(),
                                                 config.tournament.mode,
                                                 total_games,
                                                 standings.standings());

        {
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
        }

        {
            std::lock_guard<std::mutex> checkpoint_lock(checkpoint_mutex);
            ijccrl::core::persist::CompletedGameMeta meta;
            meta.game_no = result.game_number;
            meta.fixture_index = result.job.fixture_index;
            meta.white = engine_names[static_cast<size_t>(fixture.white_engine_id)];
            meta.black = engine_names[static_cast<size_t>(fixture.black_engine_id)];
            meta.opening_id = result.job.opening.id;
            meta.result = result.result.state.result;
            meta.termination = result.result.state.termination;
            meta.pgn_offset = pgn_offset;
            meta.pgn_path = config.output.tournament_pgn;
            completed_games.push_back(std::move(meta));
            completed_set.insert(result.job.fixture_index);
            completed_count.store(static_cast<int>(completed_set.size()));
        }
        last_game_number.store(result.game_number);
        last_game_end_time.store(std::time(nullptr));
        if (write_checkpoint) {
            write_checkpoint();
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

    const auto watchdog_log = [this](const std::string& line) {
        AppendLogLine(line);
    };

    write_checkpoint = [&]() {
        ijccrl::core::persist::CheckpointState snapshot;
        snapshot.version = 1;
        snapshot.config_hash = config_hash;
        snapshot.total_games = total_games;
        snapshot.rng_seed = static_cast<std::uint64_t>(config.openings.seed);
        snapshot.last_game_no = last_game_number.load();
        std::time_t last_time = last_game_end_time.load();
        snapshot.last_game_end_time = last_time == 0 ? "" : FormatUtcTimestamp(last_time);

        std::vector<int> completed_snapshot;
        {
            std::lock_guard<std::mutex> lock(checkpoint_mutex);
            snapshot.completed_games = completed_games;
            snapshot.active_games = active_games_meta;
            completed_snapshot.assign(completed_set.begin(), completed_set.end());
        }
        snapshot.completed_fixture_indices = completed_snapshot;

        std::unordered_set<int> completed_local(completed_snapshot.begin(), completed_snapshot.end());
        snapshot.next_fixture_index = total_games;
        snapshot.opening_index = total_games;
        for (int i = 0; i < total_games; ++i) {
            if (completed_local.count(i) == 0) {
                snapshot.next_fixture_index = i;
                snapshot.opening_index = i;
                const auto& fixture = fixtures[static_cast<size_t>(i)];
                snapshot.next_game.fixture_index = i;
                snapshot.next_game.white = engine_names[static_cast<size_t>(fixture.white_engine_id)];
                snapshot.next_game.black = engine_names[static_cast<size_t>(fixture.black_engine_id)];
                snapshot.next_game.opening_id = assigned_openings[static_cast<size_t>(i)].id;
                break;
            }
        }

        {
            std::lock_guard<std::mutex> standings_lock(standings_mutex_);
            snapshot.standings.clear();
            for (const auto& row : standings_) {
                ijccrl::core::persist::StandingsSnapshot entry;
                entry.name = row.name;
                entry.games = row.games;
                entry.wins = row.wins;
                entry.draws = row.draws;
                entry.losses = row.losses;
                entry.points = row.points;
                snapshot.standings.push_back(std::move(entry));
            }
        }

        if (!ijccrl::core::persist::SaveCheckpoint(checkpoint_path, snapshot)) {
            disk_write_errors.fetch_add(1);
        }
    };

    std::atomic<bool> checkpoint_running{false};
    std::thread checkpoint_thread;
    if (config.output.checkpoint_interval_seconds > 0) {
        checkpoint_running.store(true);
        checkpoint_thread = std::thread([&]() {
            while (checkpoint_running.load()) {
                std::this_thread::sleep_for(std::chrono::seconds(config.output.checkpoint_interval_seconds));
                if (!running_.load()) {
                    break;
                }
                write_checkpoint();
            }
        });
    }

    std::atomic<bool> metrics_running{false};
    std::thread metrics_thread;
    if (config.output.metrics_interval_seconds > 0) {
        metrics_running.store(true);
        metrics_thread = std::thread([&]() {
            while (metrics_running.load()) {
                std::this_thread::sleep_for(std::chrono::seconds(config.output.metrics_interval_seconds));
                if (!running_.load()) {
                    break;
                }
                nlohmann::json metrics;
                metrics["active_games"] = active_games.load();
                metrics["queue_remaining"] = total_games - completed_count.load();
                metrics["total_games"] = total_games;
                metrics["engines_running"] = static_cast<int>(engine_names.size());
                std::time_t last_time = last_game_end_time.load();
                metrics["last_game_end_time"] = last_time == 0 ? "" : FormatUtcTimestamp(last_time);
                metrics["disk_write_errors_count"] = disk_write_errors.load();
                if (!ijccrl::core::util::AtomicFileWriter::Write(config.output.metrics_json,
                                                                 metrics.dump(2))) {
                    disk_write_errors.fetch_add(1);
                }
            }
        });
    }

    ijccrl::core::rules::ConfigLimits termination_limits;
    termination_limits.max_plies = config.limits.max_plies;
    termination_limits.draw_by_repetition = config.limits.draw_by_repetition;
    termination_limits.adjudication.enabled = config.adjudication.enabled;
    termination_limits.adjudication.score_draw_cp = config.adjudication.score_draw_cp;
    termination_limits.adjudication.score_draw_moves = config.adjudication.score_draw_moves;
    termination_limits.adjudication.score_win_cp = config.adjudication.score_win_cp;
    termination_limits.adjudication.score_win_moves = config.adjudication.score_win_moves;
    termination_limits.adjudication.min_depth = config.adjudication.min_depth;
    termination_limits.tablebases.enabled = config.tablebases.enabled;
    termination_limits.tablebases.paths = config.tablebases.paths;
    termination_limits.tablebases.probe_limit_pieces = config.tablebases.probe_limit_pieces;
    termination_limits.resign.enabled = config.resign.enabled;
    termination_limits.resign.cp = config.resign.cp;
    termination_limits.resign.moves = config.resign.moves;
    termination_limits.resign.min_depth = config.resign.min_depth;

    ijccrl::core::runtime::MatchRunner match_runner(pool,
                                                    time_control,
                                                    termination_limits,
                                                    config.watchdog.go_timeout_ms,
                                                    config.limits.abort_on_stop,
                                                    config.watchdog.max_failures,
                                                    config.watchdog.failure_window_games,
                                                    config.watchdog.pause_on_unhealthy,
                                                    on_result,
                                                    live_update,
                                                    watchdog_log,
                                                    on_job_event);

    write_checkpoint();
    match_runner.Run(jobs, config.tournament.concurrency, control, initial_game_number);

    for (size_t i = 0; i < engine_names.size(); ++i) {
        pool.engine(static_cast<int>(i)).Stop();
    }

    write_checkpoint();
    if (checkpoint_running.load()) {
        checkpoint_running.store(false);
        if (checkpoint_thread.joinable()) {
            checkpoint_thread.join();
        }
    }
    if (metrics_running.load()) {
        metrics_running.store(false);
        if (metrics_thread.joinable()) {
            metrics_thread.join();
        }
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
