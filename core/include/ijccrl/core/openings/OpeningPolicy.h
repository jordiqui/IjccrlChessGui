#pragma once

#include "ijccrl/core/openings/Opening.h"
#include "ijccrl/core/tournament/RoundRobinScheduler.h"

#include <vector>

namespace ijccrl::core::openings {

class OpeningPolicy {
public:
    static std::vector<Opening> AssignRoundRobin(const std::vector<ijccrl::core::tournament::Fixture>& fixtures,
                                                 const std::vector<Opening>& openings,
                                                 int games_per_pairing);
};

}  // namespace ijccrl::core::openings
