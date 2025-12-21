#pragma once

#include "ijccrl/core/openings/Opening.h"
#include "ijccrl/core/tournament/TournamentTypes.h"

#include <vector>

namespace ijccrl::core::openings {

class OpeningPolicy {
public:
    static std::vector<Opening> AssignRoundRobin(const std::vector<ijccrl::core::tournament::Fixture>& fixtures,
                                                 const std::vector<Opening>& openings,
                                                 int games_per_pairing);
    static Opening AssignSwissForIndex(int global_game_index,
                                       const std::vector<Opening>& openings,
                                       int games_per_pairing);
};

}  // namespace ijccrl::core::openings
