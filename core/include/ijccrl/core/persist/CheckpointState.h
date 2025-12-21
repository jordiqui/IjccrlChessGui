#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ijccrl/core/tournament/TournamentTypes.h"

namespace ijccrl::core::persist {

struct CompletedGameMeta {
    int game_no = 0;
    int fixture_index = 0;
    std::string white;
    std::string black;
    std::string opening_id;
    std::string result;
    std::string termination;
    long long pgn_offset = 0;
    std::string pgn_path;
};

struct ActiveGameMeta {
    int game_no = 0;
    int fixture_index = 0;
    std::string white;
    std::string black;
    std::string opening_id;
};

struct StandingsSnapshot {
    std::string name;
    int games = 0;
    int wins = 0;
    int draws = 0;
    int losses = 0;
    double points = 0.0;
};

struct NextGameSnapshot {
    int fixture_index = -1;
    std::string white;
    std::string black;
    std::string opening_id;
};

struct CheckpointState {
    int version = 1;
    std::string config_hash;
    int total_games = 0;
    int next_fixture_index = 0;
    int opening_index = 0;
    std::vector<int> completed_fixture_indices;
    std::vector<CompletedGameMeta> completed_games;
    std::vector<StandingsSnapshot> standings;
    std::vector<ActiveGameMeta> active_games;
    NextGameSnapshot next_game;
    std::uint64_t rng_seed = 0;
    int last_game_no = 0;
    std::string last_game_end_time;

    struct SwissPairing {
        int white_engine_id = -1;
        int black_engine_id = -1;
    };

    struct SwissColorSnapshot {
        int last_color = 0;
        int streak = 0;
    };

    struct SwissPendingFixture {
        ijccrl::core::tournament::Fixture fixture;
        int fixture_index = 0;
    };

    struct SwissCheckpointState {
        int current_round = 0;
        std::vector<SwissPairing> pairings_played;
        std::vector<int> bye_history;
        std::vector<SwissColorSnapshot> color_history;
        std::vector<SwissPendingFixture> pending_pairings_current_round;
    };

    SwissCheckpointState swiss;
};

std::string ComputeConfigHash(const std::string& payload);
bool SaveCheckpoint(const std::string& path, const CheckpointState& state);
bool LoadCheckpoint(const std::string& path, CheckpointState& state, std::string* error);

}  // namespace ijccrl::core::persist
