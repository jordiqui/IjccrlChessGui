#include "ijccrl/core/broadcast/TlcsIniAdapter.h"
#include "ijccrl/core/game/GameRunner.h"
#include "ijccrl/core/game/TimeControl.h"
#include "ijccrl/core/pgn/PgnWriter.h"
#include "ijccrl/core/uci/UciEngine.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

struct EngineConfig {
    std::string name;
    std::string cmd;
    std::vector<std::string> args;
    std::map<std::string, std::string> uci_options;
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

    EngineConfig engine_a;
    EngineConfig engine_b;
    if (!ParseEngine(config.at("engines").at(0), engine_a) ||
        !ParseEngine(config.at("engines").at(1), engine_b)) {
        std::cerr << "[ijccrlcli] Failed to parse engine configs." << '\n';
        return 1;
    }

    ijccrl::core::uci::UciEngine engine_a_proc(engine_a.name, engine_a.cmd, engine_a.args);
    ijccrl::core::uci::UciEngine engine_b_proc(engine_b.name, engine_b.cmd, engine_b.args);

    if (!engine_a_proc.Start("")) {
        std::cerr << "[ijccrlcli] Failed to start engine A." << '\n';
        return 1;
    }
    if (!engine_b_proc.Start("")) {
        std::cerr << "[ijccrlcli] Failed to start engine B." << '\n';
        return 1;
    }

    if (!engine_a_proc.UciHandshake() || !engine_b_proc.UciHandshake()) {
        std::cerr << "[ijccrlcli] UCI handshake failed." << '\n';
        return 1;
    }

    for (const auto& [name, value] : engine_a.uci_options) {
        engine_a_proc.SetOption(name, value);
    }
    for (const auto& [name, value] : engine_b.uci_options) {
        engine_b_proc.SetOption(name, value);
    }

    engine_a_proc.IsReady();
    engine_b_proc.IsReady();

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

    const int max_plies = config.value("max_plies", 400);
    const int max_games = config.value("max_games", -1);

    std::string tournament_pgn_path = "out/tournament.pgn";
    if (config.contains("output")) {
        const auto& output = config.at("output");
        tournament_pgn_path = output.value("tournament_pgn", tournament_pgn_path);
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

    ijccrl::core::game::GameRunner runner;
    int game_index = 0;

    while (max_games < 0 || game_index < max_games) {
        const bool swap_colors = (game_index % 2 == 1);
        auto& white = swap_colors ? engine_b_proc : engine_a_proc;
        auto& black = swap_colors ? engine_a_proc : engine_b_proc;

        white.NewGame();
        black.NewGame();
        white.IsReady();
        black.IsReady();

        ijccrl::core::pgn::PgnGame pgn;
        pgn.SetTag("Event", "ijccrlcli match");
        if (!site_tag.empty()) {
            pgn.SetTag("Site", site_tag);
        }
        pgn.SetTag("Round", std::to_string(game_index + 1));
        pgn.SetTag("White", white.name());
        pgn.SetTag("Black", black.name());
        pgn.SetTag("Result", "*");

        const auto live_update = [&](const ijccrl::core::pgn::PgnGame& live_game) {
            const std::string live_pgn = ijccrl::core::pgn::PgnWriter::Render(live_game);
            adapter->PublishLivePgn(live_pgn);
        };

        auto result = runner.PlayGame(white, black, time_control, max_plies, pgn, live_update);
        const std::string final_pgn = ijccrl::core::pgn::PgnWriter::Render(result.pgn);
        AppendTournamentPgn(tournament_pgn_path, final_pgn);

        ++game_index;
    }

    return 0;
}
