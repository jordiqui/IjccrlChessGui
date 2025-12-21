#pragma once

#include <cstdint>
#include <string>
#include <vector>

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
};

std::string ComputeConfigHash(const std::string& payload);
bool SaveCheckpoint(const std::string& path, const CheckpointState& state);
bool LoadCheckpoint(const std::string& path, CheckpointState& state, std::string* error);

}  // namespace ijccrl::core::persist
