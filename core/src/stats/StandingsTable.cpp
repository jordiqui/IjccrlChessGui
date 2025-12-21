#include "ijccrl/core/stats/StandingsTable.h"

namespace ijccrl::core::stats {

StandingsTable::StandingsTable(std::vector<std::string> engine_names) {
    standings_.reserve(engine_names.size());
    for (auto& name : engine_names) {
        EngineStats stats;
        stats.name = std::move(name);
        standings_.push_back(std::move(stats));
    }
}

void StandingsTable::RecordResult(int white_id, int black_id, const std::string& result) {
    if (white_id < 0 || black_id < 0 ||
        white_id >= static_cast<int>(standings_.size()) ||
        black_id >= static_cast<int>(standings_.size())) {
        return;
    }

    auto& white = standings_[static_cast<size_t>(white_id)];
    auto& black = standings_[static_cast<size_t>(black_id)];
    white.games += 1;
    black.games += 1;
    games_played_ += 1;

    if (result == "1-0") {
        white.wins += 1;
        white.points += 1.0;
        black.losses += 1;
    } else if (result == "0-1") {
        black.wins += 1;
        black.points += 1.0;
        white.losses += 1;
    } else if (result == "1/2-1/2") {
        white.draws += 1;
        black.draws += 1;
        white.points += 0.5;
        black.points += 0.5;
    }
}

void StandingsTable::RecordBye(int engine_id, double points) {
    if (engine_id < 0 || engine_id >= static_cast<int>(standings_.size())) {
        return;
    }
    auto& entry = standings_[static_cast<size_t>(engine_id)];
    entry.games += 1;
    entry.wins += points >= 1.0 ? 1 : 0;
    entry.draws += points > 0.0 && points < 1.0 ? 1 : 0;
    entry.points += points;
    games_played_ += 1;
}

void StandingsTable::LoadSnapshot(std::vector<EngineStats> snapshot) {
    standings_ = std::move(snapshot);
    int total_engine_games = 0;
    for (const auto& entry : standings_) {
        total_engine_games += entry.games;
    }
    games_played_ = total_engine_games / 2;
}

}  // namespace ijccrl::core::stats
