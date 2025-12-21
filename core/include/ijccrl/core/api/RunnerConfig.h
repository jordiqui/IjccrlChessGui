#pragma once

#include <map>
#include <string>
#include <vector>

namespace ijccrl::core::api {

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

struct BroadcastConfig {
    std::string adapter;
    std::string server_ini;
};

struct TimeControlConfig {
    int base_seconds = 60;
    int increment_seconds = 0;
    int move_time_ms = 200;
};

struct LimitsConfig {
    int max_plies = 400;
    int max_games = -1;
    bool draw_by_repetition = false;
};

struct RunnerConfig {
    std::vector<EngineConfig> engines;
    TimeControlConfig time_control;
    TournamentConfig tournament;
    OpeningConfig openings;
    OutputConfig output;
    BroadcastConfig broadcast;
    LimitsConfig limits;

    static bool LoadFromFile(const std::string& path, RunnerConfig& config, std::string* error);
    static bool SaveToFile(const std::string& path, const RunnerConfig& config, std::string* error);
};

}  // namespace ijccrl::core::api
