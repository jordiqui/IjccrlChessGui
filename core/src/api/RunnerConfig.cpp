#include "ijccrl/core/api/RunnerConfig.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace ijccrl::core::api {

namespace {

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

bool LoadJson(const std::string& path, nlohmann::json& config, std::string* error) {
    std::ifstream input(path);
    if (!input) {
        if (error) {
            *error = "Failed to open config: " + path;
        }
        return false;
    }
    try {
        input >> config;
    } catch (const std::exception& ex) {
        if (error) {
            *error = std::string("Failed to parse JSON: ") + ex.what();
        }
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

nlohmann::json WriteEngine(const EngineConfig& engine) {
    nlohmann::json node;
    node["name"] = engine.name;
    node["cmd"] = engine.cmd;
    if (!engine.args.empty()) {
        node["args"] = engine.args;
    }
    if (!engine.uci_options.empty()) {
        node["uci_options"] = engine.uci_options;
    }
    return node;
}

}  // namespace

bool RunnerConfig::LoadFromFile(const std::string& path, RunnerConfig& config, std::string* error) {
    nlohmann::json root;
    if (!LoadJson(path, root, error)) {
        return false;
    }

    config = RunnerConfig{};

    if (root.contains("engines")) {
        for (const auto& engine_node : root.at("engines")) {
            EngineConfig engine;
            if (!ParseEngine(engine_node, engine)) {
                if (error) {
                    *error = "Failed to parse engine configs.";
                }
                return false;
            }
            config.engines.push_back(std::move(engine));
        }
    }

    if (root.contains("time_control")) {
        const auto& tc = root.at("time_control");
        config.time_control.base_seconds = tc.value("base_seconds", config.time_control.base_seconds);
        config.time_control.increment_seconds = tc.value("increment_seconds", config.time_control.increment_seconds);
        config.time_control.move_time_ms = tc.value("move_time_ms", config.time_control.move_time_ms);
    }

    if (root.contains("tournament")) {
        const auto& node = root.at("tournament");
        config.tournament.mode = node.value("mode", config.tournament.mode);
        config.tournament.double_round_robin = node.value("double_round_robin", config.tournament.double_round_robin);
        config.tournament.rounds = node.value("rounds", config.tournament.rounds);
        config.tournament.games_per_pairing = node.value("games_per_pairing", config.tournament.games_per_pairing);
        config.tournament.concurrency = node.value("concurrency", config.tournament.concurrency);
        config.tournament.avoid_repeats = node.value("avoid_repeats", config.tournament.avoid_repeats);
        config.tournament.bye_points = node.value("bye_points", config.tournament.bye_points);
    }

    if (root.contains("openings")) {
        const auto& node = root.at("openings");
        config.openings.type = node.value("type", config.openings.type);
        config.openings.path = node.value("path", config.openings.path);
        config.openings.policy = node.value("policy", config.openings.policy);
        config.openings.seed = node.value("seed", config.openings.seed);
    }

    if (root.contains("output")) {
        const auto& output = root.at("output");
        config.output.tournament_pgn = output.value("tournament_pgn", config.output.tournament_pgn);
        config.output.live_pgn = output.value("live_pgn", config.output.live_pgn);
        config.output.results_json = output.value("results_json", config.output.results_json);
        config.output.pairings_csv = output.value("pairings_csv", config.output.pairings_csv);
        config.output.progress_log = output.value("progress_log", config.output.progress_log);
        config.output.checkpoint_json = output.value("checkpoint_json", config.output.checkpoint_json);
        config.output.standings_csv = output.value("standings_csv", config.output.standings_csv);
        config.output.standings_html = output.value("standings_html", config.output.standings_html);
        config.output.summary_json = output.value("summary_json", config.output.summary_json);
        config.output.metrics_json = output.value("metrics_json", config.output.metrics_json);
        config.output.games_dir = output.value("games_dir", config.output.games_dir);
        config.output.write_game_files = output.value("write_game_files", config.output.write_game_files);
        config.output.checkpoint_interval_seconds =
            output.value("checkpoint_interval_seconds", config.output.checkpoint_interval_seconds);
        config.output.metrics_interval_seconds =
            output.value("metrics_interval_seconds", config.output.metrics_interval_seconds);
    }

    if (root.contains("broadcast")) {
        const auto& broadcast = root.at("broadcast");
        config.broadcast.adapter = broadcast.value("adapter", config.broadcast.adapter);
        config.broadcast.server_ini = broadcast.value("server_ini", config.broadcast.server_ini);
        if (broadcast.contains("tlcs")) {
            const auto& tlcs = broadcast.at("tlcs");
            config.broadcast.tlcs.server_ini = tlcs.value("server_ini", config.broadcast.tlcs.server_ini);
            config.broadcast.tlcs.feed_path = tlcs.value("feed_path", config.broadcast.tlcs.feed_path);
            config.broadcast.tlcs.auto_write_server_ini =
                tlcs.value("auto_write_server_ini", config.broadcast.tlcs.auto_write_server_ini);
            config.broadcast.tlcs.force_update_path =
                tlcs.value("force_update_path", config.broadcast.tlcs.force_update_path);
            config.broadcast.tlcs.tlcs_exe = tlcs.value("tlcs_exe", config.broadcast.tlcs.tlcs_exe);
            config.broadcast.tlcs.autostart = tlcs.value("autostart", config.broadcast.tlcs.autostart);
        }
    }

    if (root.contains("limits")) {
        const auto& limits = root.at("limits");
        config.limits.max_plies = limits.value("max_plies", config.limits.max_plies);
        config.limits.draw_by_repetition = limits.value("draw_by_repetition", config.limits.draw_by_repetition);
        config.limits.max_games = limits.value("max_games", config.limits.max_games);
        config.limits.abort_on_stop = limits.value("abort_on_stop", config.limits.abort_on_stop);
    } else {
        config.limits.max_plies = root.value("max_plies", config.limits.max_plies);
        config.limits.max_games = root.value("max_games", config.limits.max_games);
    }

    if (root.contains("adjudication")) {
        const auto& node = root.at("adjudication");
        config.adjudication.enabled = node.value("enabled", config.adjudication.enabled);
        config.adjudication.score_draw_cp = node.value("score_draw_cp", config.adjudication.score_draw_cp);
        config.adjudication.score_draw_moves = node.value("score_draw_moves", config.adjudication.score_draw_moves);
        config.adjudication.score_win_cp = node.value("score_win_cp", config.adjudication.score_win_cp);
        config.adjudication.score_win_moves = node.value("score_win_moves", config.adjudication.score_win_moves);
        config.adjudication.min_depth = node.value("min_depth", config.adjudication.min_depth);
    }

    if (root.contains("tablebases")) {
        const auto& node = root.at("tablebases");
        config.tablebases.enabled = node.value("enabled", config.tablebases.enabled);
        config.tablebases.probe_limit_pieces =
            node.value("probe_limit_pieces", config.tablebases.probe_limit_pieces);
        config.tablebases.paths.clear();
        if (node.contains("paths")) {
            for (const auto& path : node.at("paths")) {
                config.tablebases.paths.push_back(path.get<std::string>());
            }
        }
    }

    if (root.contains("resign")) {
        const auto& node = root.at("resign");
        config.resign.enabled = node.value("enabled", config.resign.enabled);
        config.resign.cp = node.value("cp", config.resign.cp);
        config.resign.moves = node.value("moves", config.resign.moves);
        config.resign.min_depth = node.value("min_depth", config.resign.min_depth);
    }

    if (root.contains("watchdog")) {
        const auto& watchdog = root.at("watchdog");
        config.watchdog.enabled = watchdog.value("enabled", config.watchdog.enabled);
        config.watchdog.handshake_timeout_ms =
            watchdog.value("handshake_timeout_ms", config.watchdog.handshake_timeout_ms);
        config.watchdog.go_timeout_ms =
            watchdog.value("go_timeout_ms", config.watchdog.go_timeout_ms);
        config.watchdog.max_failures =
            watchdog.value("max_failures", config.watchdog.max_failures);
        config.watchdog.failure_window_games =
            watchdog.value("failure_window_games", config.watchdog.failure_window_games);
        config.watchdog.pause_on_unhealthy =
            watchdog.value("pause_on_unhealthy", config.watchdog.pause_on_unhealthy);
    }

    return true;
}

bool RunnerConfig::SaveToFile(const std::string& path, const RunnerConfig& config, std::string* error) {
    nlohmann::json root;
    root["engines"] = nlohmann::json::array();
    for (const auto& engine : config.engines) {
        root["engines"].push_back(WriteEngine(engine));
    }

    root["time_control"] = {
        {"base_seconds", config.time_control.base_seconds},
        {"increment_seconds", config.time_control.increment_seconds},
        {"move_time_ms", config.time_control.move_time_ms},
    };

    root["tournament"] = {
        {"mode", config.tournament.mode},
        {"double_round_robin", config.tournament.double_round_robin},
        {"rounds", config.tournament.rounds},
        {"games_per_pairing", config.tournament.games_per_pairing},
        {"concurrency", config.tournament.concurrency},
        {"avoid_repeats", config.tournament.avoid_repeats},
        {"bye_points", config.tournament.bye_points},
    };

    root["openings"] = {
        {"type", config.openings.type},
        {"path", config.openings.path},
        {"policy", config.openings.policy},
        {"seed", config.openings.seed},
    };

    root["output"] = {
        {"tournament_pgn", config.output.tournament_pgn},
        {"live_pgn", config.output.live_pgn},
        {"results_json", config.output.results_json},
        {"pairings_csv", config.output.pairings_csv},
        {"progress_log", config.output.progress_log},
        {"checkpoint_json", config.output.checkpoint_json},
        {"standings_csv", config.output.standings_csv},
        {"standings_html", config.output.standings_html},
        {"summary_json", config.output.summary_json},
        {"metrics_json", config.output.metrics_json},
        {"games_dir", config.output.games_dir},
        {"write_game_files", config.output.write_game_files},
        {"checkpoint_interval_seconds", config.output.checkpoint_interval_seconds},
        {"metrics_interval_seconds", config.output.metrics_interval_seconds},
    };

    root["broadcast"] = {
        {"adapter", config.broadcast.adapter},
        {"server_ini", config.broadcast.server_ini},
        {"tlcs",
         {
             {"server_ini", config.broadcast.tlcs.server_ini},
             {"feed_path", config.broadcast.tlcs.feed_path},
             {"auto_write_server_ini", config.broadcast.tlcs.auto_write_server_ini},
             {"force_update_path", config.broadcast.tlcs.force_update_path},
             {"tlcs_exe", config.broadcast.tlcs.tlcs_exe},
             {"autostart", config.broadcast.tlcs.autostart},
         }},
    };

    root["limits"] = {
        {"max_plies", config.limits.max_plies},
        {"max_games", config.limits.max_games},
        {"draw_by_repetition", config.limits.draw_by_repetition},
        {"abort_on_stop", config.limits.abort_on_stop},
    };

    root["adjudication"] = {
        {"enabled", config.adjudication.enabled},
        {"score_draw_cp", config.adjudication.score_draw_cp},
        {"score_draw_moves", config.adjudication.score_draw_moves},
        {"score_win_cp", config.adjudication.score_win_cp},
        {"score_win_moves", config.adjudication.score_win_moves},
        {"min_depth", config.adjudication.min_depth},
    };

    root["tablebases"] = {
        {"enabled", config.tablebases.enabled},
        {"paths", config.tablebases.paths},
        {"probe_limit_pieces", config.tablebases.probe_limit_pieces},
    };

    root["resign"] = {
        {"enabled", config.resign.enabled},
        {"cp", config.resign.cp},
        {"moves", config.resign.moves},
        {"min_depth", config.resign.min_depth},
    };

    root["watchdog"] = {
        {"enabled", config.watchdog.enabled},
        {"handshake_timeout_ms", config.watchdog.handshake_timeout_ms},
        {"go_timeout_ms", config.watchdog.go_timeout_ms},
        {"max_failures", config.watchdog.max_failures},
        {"failure_window_games", config.watchdog.failure_window_games},
        {"pause_on_unhealthy", config.watchdog.pause_on_unhealthy},
    };

    const std::filesystem::path fs_path(path);
    if (!fs_path.parent_path().empty()) {
        std::filesystem::create_directories(fs_path.parent_path());
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        if (error) {
            *error = "Failed to write config: " + path;
        }
        return false;
    }

    output << root.dump(2);
    return true;
}

std::string RunnerConfig::ToJsonString(const RunnerConfig& config) {
    nlohmann::json root;
    root["engines"] = nlohmann::json::array();
    for (const auto& engine : config.engines) {
        root["engines"].push_back(WriteEngine(engine));
    }
    root["time_control"] = {
        {"base_seconds", config.time_control.base_seconds},
        {"increment_seconds", config.time_control.increment_seconds},
        {"move_time_ms", config.time_control.move_time_ms},
    };
    root["tournament"] = {
        {"mode", config.tournament.mode},
        {"double_round_robin", config.tournament.double_round_robin},
        {"rounds", config.tournament.rounds},
        {"games_per_pairing", config.tournament.games_per_pairing},
        {"concurrency", config.tournament.concurrency},
        {"avoid_repeats", config.tournament.avoid_repeats},
        {"bye_points", config.tournament.bye_points},
    };
    root["openings"] = {
        {"type", config.openings.type},
        {"path", config.openings.path},
        {"policy", config.openings.policy},
        {"seed", config.openings.seed},
    };
    root["output"] = {
        {"tournament_pgn", config.output.tournament_pgn},
        {"live_pgn", config.output.live_pgn},
        {"results_json", config.output.results_json},
        {"pairings_csv", config.output.pairings_csv},
        {"progress_log", config.output.progress_log},
        {"checkpoint_json", config.output.checkpoint_json},
        {"standings_csv", config.output.standings_csv},
        {"standings_html", config.output.standings_html},
        {"summary_json", config.output.summary_json},
        {"metrics_json", config.output.metrics_json},
        {"games_dir", config.output.games_dir},
        {"write_game_files", config.output.write_game_files},
        {"checkpoint_interval_seconds", config.output.checkpoint_interval_seconds},
        {"metrics_interval_seconds", config.output.metrics_interval_seconds},
    };
    root["broadcast"] = {
        {"adapter", config.broadcast.adapter},
        {"server_ini", config.broadcast.server_ini},
        {"tlcs",
         {
             {"server_ini", config.broadcast.tlcs.server_ini},
             {"feed_path", config.broadcast.tlcs.feed_path},
             {"auto_write_server_ini", config.broadcast.tlcs.auto_write_server_ini},
             {"force_update_path", config.broadcast.tlcs.force_update_path},
             {"tlcs_exe", config.broadcast.tlcs.tlcs_exe},
             {"autostart", config.broadcast.tlcs.autostart},
         }},
    };
    root["limits"] = {
        {"max_plies", config.limits.max_plies},
        {"max_games", config.limits.max_games},
        {"draw_by_repetition", config.limits.draw_by_repetition},
        {"abort_on_stop", config.limits.abort_on_stop},
    };
    root["adjudication"] = {
        {"enabled", config.adjudication.enabled},
        {"score_draw_cp", config.adjudication.score_draw_cp},
        {"score_draw_moves", config.adjudication.score_draw_moves},
        {"score_win_cp", config.adjudication.score_win_cp},
        {"score_win_moves", config.adjudication.score_win_moves},
        {"min_depth", config.adjudication.min_depth},
    };
    root["tablebases"] = {
        {"enabled", config.tablebases.enabled},
        {"paths", config.tablebases.paths},
        {"probe_limit_pieces", config.tablebases.probe_limit_pieces},
    };
    root["resign"] = {
        {"enabled", config.resign.enabled},
        {"cp", config.resign.cp},
        {"moves", config.resign.moves},
        {"min_depth", config.resign.min_depth},
    };
    root["watchdog"] = {
        {"enabled", config.watchdog.enabled},
        {"handshake_timeout_ms", config.watchdog.handshake_timeout_ms},
        {"go_timeout_ms", config.watchdog.go_timeout_ms},
        {"max_failures", config.watchdog.max_failures},
        {"failure_window_games", config.watchdog.failure_window_games},
        {"pause_on_unhealthy", config.watchdog.pause_on_unhealthy},
    };
    return root.dump();
}

}  // namespace ijccrl::core::api
