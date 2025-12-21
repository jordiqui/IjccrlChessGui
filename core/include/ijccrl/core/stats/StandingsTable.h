#pragma once

#include <string>
#include <vector>

namespace ijccrl::core::stats {

struct EngineStats {
    std::string name;
    int games = 0;
    int wins = 0;
    int draws = 0;
    int losses = 0;
    double points = 0.0;

    double score_percent() const {
        if (games == 0) {
            return 0.0;
        }
        return (points / static_cast<double>(games)) * 100.0;
    }
};

class StandingsTable {
public:
    explicit StandingsTable(std::vector<std::string> engine_names);

    void RecordResult(int white_id, int black_id, const std::string& result);
    const std::vector<EngineStats>& standings() const { return standings_; }
    int games_played() const { return games_played_; }

private:
    std::vector<EngineStats> standings_;
    int games_played_ = 0;
};

}  // namespace ijccrl::core::stats
