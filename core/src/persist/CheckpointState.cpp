#include "ijccrl/core/persist/CheckpointState.h"

#include "ijccrl/core/util/AtomicFileWriter.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace ijccrl::core::persist {

namespace {

std::uint64_t Fnv1a64(const std::string& payload) {
    constexpr std::uint64_t kOffset = 14695981039346656037ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t hash = kOffset;
    for (unsigned char ch : payload) {
        hash ^= static_cast<std::uint64_t>(ch);
        hash *= kPrime;
    }
    return hash;
}

}  // namespace

std::string ComputeConfigHash(const std::string& payload) {
    const auto hash = Fnv1a64(payload);
    return std::to_string(hash);
}

bool SaveCheckpoint(const std::string& path, const CheckpointState& state) {
    const std::filesystem::path fs_path(path);
    if (!fs_path.parent_path().empty()) {
        std::filesystem::create_directories(fs_path.parent_path());
    }
    nlohmann::json root;
    root["version"] = state.version;
    root["config_hash"] = state.config_hash;
    root["total_games"] = state.total_games;
    root["next_fixture_index"] = state.next_fixture_index;
    root["opening_index"] = state.opening_index;
    root["completed_fixture_indices"] = state.completed_fixture_indices;
    root["rng_seed"] = state.rng_seed;
    root["last_game_no"] = state.last_game_no;
    root["last_game_end_time"] = state.last_game_end_time;

    root["completed_games"] = nlohmann::json::array();
    for (const auto& game : state.completed_games) {
        root["completed_games"].push_back({
            {"game_no", game.game_no},
            {"fixture_index", game.fixture_index},
            {"white", game.white},
            {"black", game.black},
            {"opening_id", game.opening_id},
            {"result", game.result},
            {"termination", game.termination},
            {"pgn_offset", game.pgn_offset},
            {"pgn_path", game.pgn_path},
        });
    }

    root["standings"] = nlohmann::json::array();
    for (const auto& row : state.standings) {
        root["standings"].push_back({
            {"name", row.name},
            {"games", row.games},
            {"wins", row.wins},
            {"draws", row.draws},
            {"losses", row.losses},
            {"points", row.points},
        });
    }

    root["active_games"] = nlohmann::json::array();
    for (const auto& active : state.active_games) {
        root["active_games"].push_back({
            {"game_no", active.game_no},
            {"fixture_index", active.fixture_index},
            {"white", active.white},
            {"black", active.black},
            {"opening_id", active.opening_id},
        });
    }

    root["next_game"] = {
        {"fixture_index", state.next_game.fixture_index},
        {"white", state.next_game.white},
        {"black", state.next_game.black},
        {"opening_id", state.next_game.opening_id},
    };

    root["swiss"] = {
        {"current_round", state.swiss.current_round},
        {"bye_history", state.swiss.bye_history},
    };

    root["swiss"]["pairings_played"] = nlohmann::json::array();
    for (const auto& pairing : state.swiss.pairings_played) {
        root["swiss"]["pairings_played"].push_back({
            {"white_engine_id", pairing.white_engine_id},
            {"black_engine_id", pairing.black_engine_id},
        });
    }

    root["swiss"]["color_history"] = nlohmann::json::array();
    for (const auto& entry : state.swiss.color_history) {
        root["swiss"]["color_history"].push_back({
            {"last_color", entry.last_color},
            {"streak", entry.streak},
        });
    }

    root["swiss"]["pending_pairings_current_round"] = nlohmann::json::array();
    for (const auto& pending : state.swiss.pending_pairings_current_round) {
        root["swiss"]["pending_pairings_current_round"].push_back({
            {"fixture_index", pending.fixture_index},
            {"round_index", pending.fixture.round_index},
            {"white_engine_id", pending.fixture.white_engine_id},
            {"black_engine_id", pending.fixture.black_engine_id},
            {"game_index_within_pairing", pending.fixture.game_index_within_pairing},
            {"pairing_id", pending.fixture.pairing_id},
        });
    }

    return ijccrl::core::util::AtomicFileWriter::Write(path, root.dump(2));
}

bool LoadCheckpoint(const std::string& path, CheckpointState& state, std::string* error) {
    std::ifstream input(path);
    if (!input) {
        if (error) {
            *error = "Failed to open checkpoint: " + path;
        }
        return false;
    }

    nlohmann::json root;
    try {
        input >> root;
    } catch (const std::exception& ex) {
        if (error) {
            *error = std::string("Failed to parse checkpoint: ") + ex.what();
        }
        return false;
    }

    state = CheckpointState{};
    state.version = root.value("version", state.version);
    state.config_hash = root.value("config_hash", state.config_hash);
    state.total_games = root.value("total_games", state.total_games);
    state.next_fixture_index = root.value("next_fixture_index", state.next_fixture_index);
    state.opening_index = root.value("opening_index", state.opening_index);
    state.rng_seed = root.value("rng_seed", state.rng_seed);
    state.last_game_no = root.value("last_game_no", state.last_game_no);
    state.last_game_end_time = root.value("last_game_end_time", state.last_game_end_time);

    if (root.contains("completed_fixture_indices")) {
        state.completed_fixture_indices = root.at("completed_fixture_indices").get<std::vector<int>>();
    }

    if (root.contains("completed_games")) {
        for (const auto& node : root.at("completed_games")) {
            CompletedGameMeta game;
            game.game_no = node.value("game_no", 0);
            game.fixture_index = node.value("fixture_index", 0);
            game.white = node.value("white", "");
            game.black = node.value("black", "");
            game.opening_id = node.value("opening_id", "");
            game.result = node.value("result", "");
            game.termination = node.value("termination", "");
            game.pgn_offset = node.value("pgn_offset", 0LL);
            game.pgn_path = node.value("pgn_path", "");
            state.completed_games.push_back(std::move(game));
        }
    }

    if (root.contains("standings")) {
        for (const auto& node : root.at("standings")) {
            StandingsSnapshot row;
            row.name = node.value("name", "");
            row.games = node.value("games", 0);
            row.wins = node.value("wins", 0);
            row.draws = node.value("draws", 0);
            row.losses = node.value("losses", 0);
            row.points = node.value("points", 0.0);
            state.standings.push_back(std::move(row));
        }
    }

    if (root.contains("active_games")) {
        for (const auto& node : root.at("active_games")) {
            ActiveGameMeta active;
            active.game_no = node.value("game_no", 0);
            active.fixture_index = node.value("fixture_index", 0);
            active.white = node.value("white", "");
            active.black = node.value("black", "");
            active.opening_id = node.value("opening_id", "");
            state.active_games.push_back(std::move(active));
        }
    }

    if (root.contains("next_game")) {
        const auto& next_game = root.at("next_game");
        state.next_game.fixture_index = next_game.value("fixture_index", -1);
        state.next_game.white = next_game.value("white", "");
        state.next_game.black = next_game.value("black", "");
        state.next_game.opening_id = next_game.value("opening_id", "");
    }

    if (root.contains("swiss")) {
        const auto& swiss = root.at("swiss");
        state.swiss.current_round = swiss.value("current_round", state.swiss.current_round);
        if (swiss.contains("bye_history")) {
            state.swiss.bye_history = swiss.at("bye_history").get<std::vector<int>>();
        }
        if (swiss.contains("pairings_played")) {
            for (const auto& node : swiss.at("pairings_played")) {
                CheckpointState::SwissPairing pairing;
                pairing.white_engine_id = node.value("white_engine_id", -1);
                pairing.black_engine_id = node.value("black_engine_id", -1);
                state.swiss.pairings_played.push_back(std::move(pairing));
            }
        }
        if (swiss.contains("color_history")) {
            for (const auto& node : swiss.at("color_history")) {
                CheckpointState::SwissColorSnapshot entry;
                entry.last_color = node.value("last_color", 0);
                entry.streak = node.value("streak", 0);
                state.swiss.color_history.push_back(std::move(entry));
            }
        }
        if (swiss.contains("pending_pairings_current_round")) {
            for (const auto& node : swiss.at("pending_pairings_current_round")) {
                CheckpointState::SwissPendingFixture pending;
                pending.fixture_index = node.value("fixture_index", 0);
                pending.fixture.round_index = node.value("round_index", 0);
                pending.fixture.white_engine_id = node.value("white_engine_id", -1);
                pending.fixture.black_engine_id = node.value("black_engine_id", -1);
                pending.fixture.game_index_within_pairing = node.value("game_index_within_pairing", 0);
                pending.fixture.pairing_id = node.value("pairing_id", "");
                state.swiss.pending_pairings_current_round.push_back(std::move(pending));
            }
        }
    }

    return true;
}

}  // namespace ijccrl::core::persist
