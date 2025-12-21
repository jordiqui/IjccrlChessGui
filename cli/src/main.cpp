#include "ijccrl/core/broadcast/TlcsIniAdapter.h"
#include "ijccrl/core/api/RunnerConfig.h"
#include "ijccrl/core/game/GameRunner.h"
#include "ijccrl/core/game/TimeControl.h"
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

#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {

using ijccrl::core::api::RunnerConfig;

void AppendTournamentPgn(const std::string& path, const std::string& pgn) {
    const std::filesystem::path fs_path(path);
    if (!fs_path.parent_path().empty()) {
        std::filesystem::create_directories(fs_path.parent_path());
    }

    const bool exists = std::filesystem::exists(fs_path);
    std::ofstream output(path, std::ios::binary | std::ios::app);
    if (!output) {
        std::cerr << "[ijccrlcli] Failed to open tournament PGN: " << path << '\n';
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
        std::cerr << "[ijccrlcli] Failed to open CSV: " << path << '\n';
        return;
    }
    if (write_header && (!exists || std::filesystem::file_size(fs_path) == 0)) {
        output << "game_no,round,white,black,opening_id,fen,result,termination,pgn_path\n";
    }
    output << line << "\n";
}

void AppendLogLine(const std::string& path, const std::string& line) {
    if (path.empty()) {
        return;
    }
    const std::filesystem::path fs_path(path);
    if (!fs_path.parent_path().empty()) {
        std::filesystem::create_directories(fs_path.parent_path());
    }
    std::ofstream output(path, std::ios::binary | std::ios::app);
    if (!output) {
        std::cerr << "[ijccrlcli] Failed to open log: " << path << '\n';
        return;
    }
    output << line << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: ijccrlcli <config.json>" << '\n';
        return 1;
    }

    const std::string config_path = argv[1];
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

    ijccrl::core::runtime::EnginePool pool(std::move(specs));
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
    std::mutex output_mutex;

    const auto live_update = [&](const ijccrl::core::pgn::PgnGame& live_game) {
        const std::string live_pgn = ijccrl::core::pgn::PgnWriter::Render(live_game);
        if (adapter) {
            adapter->PublishLivePgn(live_pgn);
        }
        WriteLivePgn(output_config.live_pgn, live_pgn);
    };

    const auto on_result = [&](const ijccrl::core::runtime::MatchResult& result) {
        const auto& fixture = result.job.fixture;
        const std::string final_pgn = ijccrl::core::pgn::PgnWriter::Render(result.result.pgn);

        std::lock_guard<std::mutex> lock(output_mutex);
        AppendTournamentPgn(output_config.tournament_pgn, final_pgn);

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
        AppendCsvLine(output_config.pairings_csv, csv_line.str(), true);

        std::ostringstream log_line;
        log_line << "GAME END #" << result.game_number << " | "
                 << engine_names[static_cast<size_t>(fixture.white_engine_id)] << " vs "
                 << engine_names[static_cast<size_t>(fixture.black_engine_id)] << " | "
                 << result.result.state.result << " | term="
                 << result.result.state.termination << " | opening="
                 << result.job.opening.id;
        std::cout << log_line.str() << '\n';
        AppendLogLine(output_config.progress_log, log_line.str());

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
        }
    };

    ijccrl::core::runtime::MatchRunner match_runner(pool,
                                                    time_control,
                                                    max_plies,
                                                    on_result,
                                                    live_update);
    match_runner.Run(jobs, tournament.concurrency);

    return 0;
}
