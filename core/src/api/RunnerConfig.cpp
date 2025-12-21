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
    }

    if (root.contains("broadcast")) {
        const auto& broadcast = root.at("broadcast");
        config.broadcast.adapter = broadcast.value("adapter", config.broadcast.adapter);
        config.broadcast.server_ini = broadcast.value("server_ini", config.broadcast.server_ini);
    }

    if (root.contains("limits")) {
        const auto& limits = root.at("limits");
        config.limits.max_plies = limits.value("max_plies", config.limits.max_plies);
        config.limits.draw_by_repetition = limits.value("draw_by_repetition", config.limits.draw_by_repetition);
        config.limits.max_games = limits.value("max_games", config.limits.max_games);
    } else {
        config.limits.max_plies = root.value("max_plies", config.limits.max_plies);
        config.limits.max_games = root.value("max_games", config.limits.max_games);
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
    };

    root["broadcast"] = {
        {"adapter", config.broadcast.adapter},
        {"server_ini", config.broadcast.server_ini},
    };

    root["limits"] = {
        {"max_plies", config.limits.max_plies},
        {"max_games", config.limits.max_games},
        {"draw_by_repetition", config.limits.draw_by_repetition},
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

}  // namespace ijccrl::core::api
