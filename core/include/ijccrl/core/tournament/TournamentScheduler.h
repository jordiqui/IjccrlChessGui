#pragma once

#include "ijccrl/core/tournament/TournamentTypes.h"

#include <optional>
#include <vector>

namespace ijccrl::core::tournament {

struct TournamentContext {
    int round_index = 0;
    int engine_count = 0;
    int games_per_pairing = 1;
    bool double_round_robin = false;
    int repeat_count = 1;
    bool avoid_repeats = true;
    double bye_points = 1.0;
    std::vector<double> scores;
    std::vector<std::vector<int>> opponents;
    std::vector<int> bye_history;
};

struct TournamentRound {
    int round_index = 0;
    std::vector<Fixture> fixtures;
    std::optional<int> bye_engine_id;
};

class ITournamentScheduler {
public:
    virtual ~ITournamentScheduler() = default;
    virtual TournamentRound BuildRound(const TournamentContext& context) = 0;
};

}  // namespace ijccrl::core::tournament
