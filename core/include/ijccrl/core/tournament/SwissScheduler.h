#pragma once

#include "ijccrl/core/tournament/TournamentScheduler.h"

#include <unordered_set>

namespace ijccrl::core::tournament {

struct SwissColorState {
    int last_color = 0;
    int streak = 0;
};

struct SwissRound {
    TournamentRound round;
    std::vector<std::pair<int, int>> pairings;
};

class SwissScheduler final : public ITournamentScheduler {
public:
    SwissRound BuildSwissRound(int round_index,
                               const std::vector<double>& scores,
                               const std::vector<std::vector<int>>& opponent_history,
                               const std::vector<int>& bye_history,
                               const std::vector<SwissColorState>& color_history,
                               const std::unordered_set<long long>& pairings_played,
                               int games_per_pairing,
                               bool avoid_repeats);

    TournamentRound BuildRound(const TournamentContext& context) override;
};

}  // namespace ijccrl::core::tournament
