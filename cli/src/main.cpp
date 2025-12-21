#include "ijccrl/core/broadcast/TlcsIniAdapter.h"
#include "ijccrl/core/api/RunnerConfig.h"
#include "ijccrl/core/export/ExportWriter.h"
#include "ijccrl/core/game/GameRunner.h"
#include "ijccrl/core/game/TimeControl.h"
#include "ijccrl/core/openings/EpdParser.h"
#include "ijccrl/core/openings/OpeningPolicy.h"
#include "ijccrl/core/openings/PgnSuite.h"
#include "ijccrl/core/persist/CheckpointState.h"
#include "ijccrl/core/pgn/PgnWriter.h"
#include "ijccrl/core/runtime/EnginePool.h"
#include "ijccrl/core/runtime/MatchRunner.h"
#include "ijccrl/core/stats/StandingsTable.h"
#include "ijccrl/core/tournament/RoundRobinScheduler.h"
#include "ijccrl/core/util/AtomicFileWriter.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <algorithm>
#include <condition_variable>
#include <ctime>
#include <iomanip>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <vector>

namespace {

using ijccrl::core::api::RunnerConfig;

bool AppendTournamentPgn(const std::string& path, const std::string& pgn) {
    const std::filesystem::path fs_path(path);
    if (!fs_path.parent_path().empty()) {
        std::filesystem::create_directories(fs_path.parent_path());
    }

    const bool exists = std::filesystem::exists(fs_path);
    std::ofstream output(path, std::ios::binary | std::ios::app);
    if (!output) {
        std::cerr << "[ijccrlcli] Failed to open tournament PGN: " << path << '\n';
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
        std::cerr << "[ijccrlcli] Failed to open CSV: " << path << '\n';
        return false;
    }
    if (write_header && (!exists || std::filesystem::file_size(fs_path) == 0)) {
        output << "game_no,round,white,black,opening_id,fen,result,termination,pgn_path\n";
    }
    output << line << "\n";
    return static_cast<bool>(output);
}

bool AppendLogLine(const std::string& path, const std::string& line) {
    if (path.empty()) {
        return true;
    }
    const std::filesystem::path fs_path(path);
    if (!fs_path.parent_path().empty()) {
        std::filesystem::create_directories(fs_path.parent_path());
    }
    std::ofstream output(path, std::ios::binary | std::ios::app);
    if (!output) {
        std::cerr << "[ijccrlcli] Failed to open log: " << path << '\n';
        return false;
    }
    output << line << "\n";
    return static_cast<bool>(output);
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

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: ijccrlcli [--resume|--fresh] <config.json>" << '\n';
        return 1;
    }

    bool resume = false;
    bool fresh = false;
    std::string config_path;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--resume") {
            resume = true;
        } else if (arg == "--fresh") {
            fresh = true;
        } else if (config_path.empty()) {
            config_path = arg;
        }
    }

    if (config_path.empty()) {
        std::cerr << "Usage: ijccrlcli [--resume|--fresh] <config.json>" << '\n';
        return 1;
    }

    if (resume && fresh) {
        std::cerr << "[ijccrlcli] --resume and --fresh are mutually exclusive." << '\n';
        return 1;
    }

    std::cout << "[ijccrlcli] Runner config: " << config_path << '\n';

    RunnerConfig runner_config;
    std::string config_error;
    if (!RunnerConfig::LoadFromFile(config_path, runner_config, &config_error)) {
        std::cerr << "[ijccrlcli] " << config_error << '\n';
        return 1;
    }

    if (runner_config.engines.size() < 2) {
        std::cerr << "[ijccrlcli] Config must define two engines." << '\n';
        return 1;
    }

    ijccrl::core::game::TimeControl time_control;
    time_control.base_ms = runner_config.time_control.base_seconds * 1000;
    time_control.increment_ms = runner_config.time_control.increment_seconds * 1000;
    time_control.move_time_ms = runner_config.time_control.move_time_ms;

    const int max_plies = runner_config.limits.max_plies;
    const bool draw_by_repetition = runner_config.limits.draw_by_repetition;
    const int max_games = runner_config.limits.max_games;

    const auto& tournament = runner_config.tournament;
    const auto& opening_config = runner_config.openings;
    const auto& output_config = runner_config.output;

    std::unique_ptr<ijccrl::core::broadcast::IBroadcastAdapter> adapter;
    std::string site_tag;

    if (runner_config.broadcast.adapter == "tlcs_ini") {
        const auto& server_ini = runner_config.broadcast.server_ini;
        auto tlcs = std::make_unique<ijccrl::core::broadcast::TlcsIniAdapter>();
        if (server_ini.empty() || !tlcs->Configure(server_ini)) {
            std::cerr << "[ijccrlcli] Failed to configure TLCS adapter." << '\n';
            return 1;
        }
        site_tag = tlcs->site();
        adapter = std::move(tlcs);
    }

    if (!adapter) {
        std::cerr << "[ijccrlcli] No broadcast adapter configured." << '\n';
        return 1;
    }

    if (draw_by_repetition) {
        std::cout << "[ijccrlcli] draw_by_repetition requested (not yet enforced)." << '\n';
    }

    std::vector<ijccrl::core::runtime::EngineSpec> specs;
    specs.reserve(runner_config.engines.size());
    std::vector<std::string> engine_names;
    engine_names.reserve(runner_config.engines.size());
    for (const auto& engine : runner_config.engines) {
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
        [](const std::string& line) { std::cout << line << '\n'; });
    pool.set_handshake_timeout_ms(runner_config.watchdog.handshake_timeout_ms);
    if (!pool.StartAll("")) {
        std::cerr << "[ijccrlcli] Failed to start engine pool." << '\n';
        return 1;
    }

    std::vector<ijccrl::core::openings::Opening> openings;
    if (!opening_config.path.empty()) {
        if (opening_config.type == "epd") {
            openings = ijccrl::core::openings::EpdParser::LoadFile(opening_config.path);
        } else if (opening_config.type == "pgn") {
            openings = ijccrl::core::openings::PgnSuite::LoadFile(opening_config.path);
        }
    }
    if (openings.empty()) {
        ijccrl::core::openings::Opening startpos;
        startpos.id = "startpos";
        openings.push_back(startpos);
    }
    if (opening_config.seed != 0) {
        std::mt19937 rng(static_cast<uint32_t>(opening_config.seed));
        std::shuffle(openings.begin(), openings.end(), rng);
    }

    auto fixtures = ijccrl::core::tournament::RoundRobinScheduler::BuildSchedule(
        static_cast<int>(runner_config.engines.size()),
        tournament.double_round_robin,
        tournament.games_per_pairing,
        tournament.rounds);

    if (max_games > 0 && static_cast<int>(fixtures.size()) > max_games) {
        fixtures.resize(static_cast<size_t>(max_games));
    }

    auto assigned_openings = ijccrl::core::openings::OpeningPolicy::AssignRoundRobin(
        fixtures, openings, tournament.games_per_pairing);

    ijccrl::core::persist::CheckpointState checkpoint_state;
    const std::string checkpoint_path = output_config.checkpoint_json;
    const std::string config_hash =
        ijccrl::core::persist::ComputeConfigHash(RunnerConfig::ToJsonString(runner_config));
    bool has_checkpoint = false;
    if (resume && !fresh && std::filesystem::exists(checkpoint_path)) {
        std::string error;
        if (ijccrl::core::persist::LoadCheckpoint(checkpoint_path, checkpoint_state, &error)) {
            if (checkpoint_state.config_hash == config_hash) {
                has_checkpoint = true;
                std::cout << "[ijccrlcli] Resuming from checkpoint." << '\n';
                if (!checkpoint_state.active_games.empty()) {
                    std::cout << "[ijccrlcli] Active games will be restarted on resume." << '\n';
                }
            } else {
                std::cout << "[ijccrlcli] Checkpoint config mismatch; starting fresh." << '\n';
            }
        } else {
            std::cout << "[ijccrlcli] Failed to load checkpoint: " << error << '\n';
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
    std::mutex output_mutex;
    std::mutex checkpoint_mutex;
    std::vector<ijccrl::core::persist::ActiveGameMeta> active_games_meta;
    std::atomic<int> active_games{0};
    std::atomic<int> disk_write_errors{0};
    std::atomic<int> last_game_number{initial_game_number};
    std::atomic<std::time_t> last_game_end_time{0};
    const int total_games = static_cast<int>(fixtures.size());

    const auto live_update = [&](const ijccrl::core::pgn::PgnGame& live_game) {
        const std::string live_pgn = ijccrl::core::pgn::PgnWriter::Render(live_game);
        if (adapter) {
            adapter->PublishLivePgn(live_pgn);
        }
        if (!WriteLivePgn(output_config.live_pgn, live_pgn)) {
            disk_write_errors.fetch_add(1);
        }
    };

    std::function<void()> write_checkpoint;
    const auto on_result = [&](const ijccrl::core::runtime::MatchResult& result) {
        const auto& fixture = result.job.fixture;
        const std::string final_pgn = ijccrl::core::pgn::PgnWriter::Render(result.result.pgn);
        long long pgn_offset = 0;
        {
            const std::filesystem::path fs_path(output_config.tournament_pgn);
            if (std::filesystem::exists(fs_path)) {
                pgn_offset = static_cast<long long>(std::filesystem::file_size(fs_path));
            }
        }

        std::lock_guard<std::mutex> lock(output_mutex);
        if (!AppendTournamentPgn(output_config.tournament_pgn, final_pgn)) {
            disk_write_errors.fetch_add(1);
        }

        if (output_config.write_game_files) {
            const std::filesystem::path games_dir(output_config.games_dir);
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

        std::ostringstream csv_line;
        csv_line << result.game_number << ','
                 << (fixture.round_index + 1) << ','
                 << engine_names[static_cast<size_t>(fixture.white_engine_id)] << ','
                 << engine_names[static_cast<size_t>(fixture.black_engine_id)] << ','
                 << result.job.opening.id << ','
                 << result.job.opening.fen << ','
                 << result.result.state.result << ','
                 << result.result.state.termination << ','
                 << output_config.tournament_pgn;
        if (!AppendCsvLine(output_config.pairings_csv, csv_line.str(), true)) {
            disk_write_errors.fetch_add(1);
        }

        std::ostringstream log_line;
        log_line << "GAME END #" << result.game_number << " | "
                 << engine_names[static_cast<size_t>(fixture.white_engine_id)] << " vs "
                 << engine_names[static_cast<size_t>(fixture.black_engine_id)] << " | "
                 << result.result.state.result << " | term="
                 << result.result.state.termination << " | opening="
                 << result.job.opening.id;
        std::cout << log_line.str() << '\n';
        if (!AppendLogLine(output_config.progress_log, log_line.str())) {
            disk_write_errors.fetch_add(1);
        }

        nlohmann::json results_json;
        results_json["event"] = "ijccrl round robin";
        std::ostringstream tc_desc;
        tc_desc << (time_control.base_ms / 1000) << "+" << (time_control.increment_ms / 1000);
        results_json["tc"] = tc_desc.str();
        results_json["mode"] = tournament.mode;
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
        const std::filesystem::path results_path(output_config.results_json);
        if (!results_path.parent_path().empty()) {
            std::filesystem::create_directories(results_path.parent_path());
        }
        std::ofstream results_out(output_config.results_json, std::ios::binary | std::ios::trunc);
        if (results_out) {
            results_out << results_json.dump(2);
        } else {
            disk_write_errors.fetch_add(1);
        }

        ijccrl::core::exporter::WriteStandingsCsv(output_config.standings_csv, standings.standings());
        ijccrl::core::exporter::WriteStandingsHtml(output_config.standings_html,
                                                   "ijccrl round robin",
                                                   standings.standings());
        ijccrl::core::exporter::WriteSummaryJson(output_config.summary_json,
                                                 "ijccrl round robin",
                                                 tc_desc.str(),
                                                 tournament.mode,
                                                 total_games,
                                                 standings.standings());

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
            meta.pgn_path = output_config.tournament_pgn;
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

    const auto on_job_event = [&](const ijccrl::core::runtime::MatchJob& job,
                                  int game_number,
                                  bool started) {
        if (started) {
            active_games.fetch_add(1);
            std::lock_guard<std::mutex> checkpoint_lock(checkpoint_mutex);
            ijccrl::core::persist::ActiveGameMeta active;
            active.game_no = game_number;
            active.fixture_index = job.fixture_index;
            active.white = engine_names[static_cast<size_t>(job.fixture.white_engine_id)];
            active.black = engine_names[static_cast<size_t>(job.fixture.black_engine_id)];
            active.opening_id = job.opening.id;
            active_games_meta.push_back(std::move(active));
        } else {
            active_games.fetch_sub(1);
            std::lock_guard<std::mutex> checkpoint_lock(checkpoint_mutex);
            active_games_meta.erase(std::remove_if(active_games_meta.begin(),
                                                   active_games_meta.end(),
                                                   [&](const auto& entry) {
                                                       return entry.game_no == game_number;
                                                   }),
                                    active_games_meta.end());
        }
    };

    write_checkpoint = [&]() {
        ijccrl::core::persist::CheckpointState snapshot;
        snapshot.version = 1;
        snapshot.config_hash = config_hash;
        snapshot.total_games = total_games;
        snapshot.rng_seed = static_cast<std::uint64_t>(opening_config.seed);
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

        snapshot.standings.clear();
        for (const auto& row : standings.standings()) {
            ijccrl::core::persist::StandingsSnapshot entry;
            entry.name = row.name;
            entry.games = row.games;
            entry.wins = row.wins;
            entry.draws = row.draws;
            entry.losses = row.losses;
            entry.points = row.points;
            snapshot.standings.push_back(std::move(entry));
        }

        if (!ijccrl::core::persist::SaveCheckpoint(checkpoint_path, snapshot)) {
            disk_write_errors.fetch_add(1);
        }
    };

    std::atomic<bool> stop_requested{false};
    std::atomic<bool> paused{false};
    std::mutex pause_mutex;
    std::condition_variable pause_cv;
    ijccrl::core::runtime::MatchRunner::Control control;
    control.stop = &stop_requested;
    control.paused = &paused;
    control.pause_mutex = &pause_mutex;
    control.pause_cv = &pause_cv;

    std::atomic<bool> checkpoint_running{false};
    std::thread checkpoint_thread;
    if (output_config.checkpoint_interval_seconds > 0) {
        checkpoint_running.store(true);
        checkpoint_thread = std::thread([&]() {
            while (checkpoint_running.load()) {
                std::this_thread::sleep_for(std::chrono::seconds(output_config.checkpoint_interval_seconds));
                write_checkpoint();
            }
        });
    }

    std::atomic<bool> metrics_running{false};
    std::thread metrics_thread;
    if (output_config.metrics_interval_seconds > 0) {
        metrics_running.store(true);
        metrics_thread = std::thread([&]() {
            while (metrics_running.load()) {
                std::this_thread::sleep_for(std::chrono::seconds(output_config.metrics_interval_seconds));
                nlohmann::json metrics;
                metrics["active_games"] = active_games.load();
                metrics["queue_remaining"] = total_games - completed_count.load();
                metrics["total_games"] = total_games;
                metrics["engines_running"] = static_cast<int>(engine_names.size());
                std::time_t last_time = last_game_end_time.load();
                metrics["last_game_end_time"] = last_time == 0 ? "" : FormatUtcTimestamp(last_time);
                metrics["disk_write_errors_count"] = disk_write_errors.load();
                if (!ijccrl::core::util::AtomicFileWriter::Write(output_config.metrics_json,
                                                                 metrics.dump(2))) {
                    disk_write_errors.fetch_add(1);
                }
            }
        });
    }

    ijccrl::core::runtime::MatchRunner match_runner(pool,
                                                    time_control,
                                                    max_plies,
                                                    runner_config.watchdog.go_timeout_ms,
                                                    runner_config.limits.abort_on_stop,
                                                    runner_config.watchdog.max_failures,
                                                    runner_config.watchdog.failure_window_games,
                                                    runner_config.watchdog.pause_on_unhealthy,
                                                    on_result,
                                                    live_update,
                                                    [](const std::string& line) { std::cout << line << '\n'; },
                                                    on_job_event);
    write_checkpoint();
    match_runner.Run(jobs, tournament.concurrency, control, initial_game_number);

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

    return 0;
}
