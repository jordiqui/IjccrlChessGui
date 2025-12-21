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
    std::string checkpoint_json = "out/checkpoint.json";
    std::string standings_csv = "out/standings.csv";
    std::string standings_html = "out/standings.html";
    std::string summary_json = "out/summary.json";
    std::string metrics_json = "out/metrics.json";
    std::string games_dir = "out/games";
    bool write_game_files = false;
    int checkpoint_interval_seconds = 120;
    int metrics_interval_seconds = 5;
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
    bool abort_on_stop = true;
};

struct WatchdogConfig {
    int handshake_timeout_ms = 10000;
    int go_timeout_ms = 0;
    int max_failures = 3;
    int failure_window_games = 10;
    bool pause_on_unhealthy = true;
};

struct RunnerConfig {
    std::vector<EngineConfig> engines;
    TimeControlConfig time_control;
    TournamentConfig tournament;
    OpeningConfig openings;
    OutputConfig output;
    BroadcastConfig broadcast;
    LimitsConfig limits;
    WatchdogConfig watchdog;

    static bool LoadFromFile(const std::string& path, RunnerConfig& config, std::string* error);
    static bool SaveToFile(const std::string& path, const RunnerConfig& config, std::string* error);
    static std::string ToJsonString(const RunnerConfig& config);
};

}  // namespace ijccrl::core::api
