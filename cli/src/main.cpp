#include "ijccrl/core/broadcast/TlcsIniAdapter.h"
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
#include <map>
#include <mutex>
#include <algorithm>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct EngineConfig {
    std::string name;
    std::string cmd;
    std::vector<std::string> args;
    std::map<std::string, std::string> uci_options;
};

struct TournamentConfig {
    std::string mode = "round_robin";
    bool double_round_robin = false;
    int rounds = 1;
    int games_per_pairing = 1;
    int concurrency = 1;
};

struct OpeningConfig {
    std::string type = "epd";
    std::string path;
    std::string policy = "round_robin";
    int seed = 0;
};

struct OutputConfig {
    std::string tournament_pgn = "out/tournament.pgn";
    std::string live_pgn = "out/live.pgn";
    std::string results_json = "out/results.json";
    std::string pairings_csv = "out/pairings.csv";
    std::string progress_log;
};

std::string JsonValueToString(const nlohmann::json& value) {
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (value.is_boolean()) {
        return value.get<bool>() ? "true" : "false";
    }
    if (value.is_number_integer()) {
        return std::to_string(value.get<long long>());
    }
    if (value.is_number_unsigned()) {
        return std::to_string(value.get<unsigned long long>());
    }
    if (value.is_number_float()) {
        return std::to_string(value.get<double>());
    }
    return value.dump();
}

bool LoadConfig(const std::string& path, nlohmann::json& config) {
    std::ifstream input(path);
    if (!input) {
        std::cerr << "[ijccrlcli] Failed to open config: " << path << '\n';
        return false;
    }
    try {
        input >> config;
    } catch (const std::exception& ex) {
        std::cerr << "[ijccrlcli] Failed to parse JSON: " << ex.what() << '\n';
        return false;
    }
    return true;
}

bool ParseEngine(const nlohmann::json& node, EngineConfig& out) {
    if (!node.contains("cmd")) {
        return false;
    }
    out.name = node.value("name", "UCI");
    out.cmd = node.value("cmd", "");
    if (node.contains("args")) {
        for (const auto& arg : node.at("args")) {
            out.args.push_back(arg.get<std::string>());
        }
    }
    if (node.contains("uci_options")) {
        for (const auto& item : node.at("uci_options").items()) {
            out.uci_options[item.key()] = JsonValueToString(item.value());
        }
    }
    return true;
}

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

    nlohmann::json config;
    if (!LoadConfig(config_path, config)) {
        return 1;
    }

    if (!config.contains("engines") || config.at("engines").size() < 2) {
        std::cerr << "[ijccrlcli] Config must define two engines." << '\n';
        return 1;
    }

    std::vector<EngineConfig> engine_configs;
    engine_configs.reserve(config.at("engines").size());
    for (const auto& engine_node : config.at("engines")) {
        EngineConfig engine;
        if (!ParseEngine(engine_node, engine)) {
            std::cerr << "[ijccrlcli] Failed to parse engine configs." << '\n';
            return 1;
        }
        engine_configs.push_back(std::move(engine));
    }

    ijccrl::core::game::TimeControl time_control;
    time_control.base_ms = 60000;
    time_control.increment_ms = 0;
    time_control.move_time_ms = 200;
    if (config.contains("time_control")) {
        const auto& tc = config.at("time_control");
        time_control.base_ms = tc.value("base_seconds", 60) * 1000;
        time_control.increment_ms = tc.value("increment_seconds", 0) * 1000;
        time_control.move_time_ms = tc.value("move_time_ms", 200);
    }

    const auto limits_node = config.value("limits", nlohmann::json::object());
    const int max_plies = limits_node.value("max_plies", config.value("max_plies", 400));
    const bool draw_by_repetition = limits_node.value("draw_by_repetition", false);
    const int max_games = config.value("max_games", -1);

    TournamentConfig tournament;
    if (config.contains("tournament")) {
        const auto& node = config.at("tournament");
        tournament.mode = node.value("mode", tournament.mode);
        tournament.double_round_robin = node.value("double_round_robin", tournament.double_round_robin);
        tournament.rounds = node.value("rounds", tournament.rounds);
        tournament.games_per_pairing = node.value("games_per_pairing", tournament.games_per_pairing);
        tournament.concurrency = node.value("concurrency", tournament.concurrency);
    }

    OpeningConfig opening_config;
    if (config.contains("openings")) {
        const auto& node = config.at("openings");
        opening_config.type = node.value("type", opening_config.type);
        opening_config.path = node.value("path", opening_config.path);
        opening_config.policy = node.value("policy", opening_config.policy);
        opening_config.seed = node.value("seed", opening_config.seed);
    }

    OutputConfig output_config;
    if (config.contains("output")) {
        const auto& output = config.at("output");
        output_config.tournament_pgn = output.value("tournament_pgn", output_config.tournament_pgn);
        output_config.live_pgn = output.value("live_pgn", output_config.live_pgn);
        output_config.results_json = output.value("results_json", output_config.results_json);
        output_config.pairings_csv = output.value("pairings_csv", output_config.pairings_csv);
        output_config.progress_log = output.value("progress_log", output_config.progress_log);
    }

    std::unique_ptr<ijccrl::core::broadcast::IBroadcastAdapter> adapter;
    std::string site_tag;

    if (config.contains("broadcast")) {
        const auto& broadcast = config.at("broadcast");
        const auto adapter_name = broadcast.value("adapter", "");
        if (adapter_name == "tlcs_ini") {
            const auto server_ini = broadcast.value("server_ini", "");
            auto tlcs = std::make_unique<ijccrl::core::broadcast::TlcsIniAdapter>();
            if (server_ini.empty() || !tlcs->Configure(server_ini)) {
                std::cerr << "[ijccrlcli] Failed to configure TLCS adapter." << '\n';
                return 1;
            }
            site_tag = tlcs->site();
            adapter = std::move(tlcs);
        }
    }

    if (!adapter) {
        std::cerr << "[ijccrlcli] No broadcast adapter configured." << '\n';
        return 1;
    }

    if (draw_by_repetition) {
        std::cout << "[ijccrlcli] draw_by_repetition requested (not yet enforced)." << '\n';
    }

    std::vector<ijccrl::core::runtime::EngineSpec> specs;
    specs.reserve(engine_configs.size());
    std::vector<std::string> engine_names;
    engine_names.reserve(engine_configs.size());
    for (const auto& engine : engine_configs) {
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
        static_cast<int>(engine_configs.size()),
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
