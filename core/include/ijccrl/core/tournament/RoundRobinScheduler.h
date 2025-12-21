#pragma once

#include "ijccrl/core/tournament/TournamentScheduler.h"

namespace ijccrl::core::tournament {

class RoundRobinScheduler : public ITournamentScheduler {
public:
    TournamentRound BuildRound(const TournamentContext& context) override;
    static std::vector<Fixture> BuildSchedule(int engine_count,
                                              bool double_round_robin,
                                              int games_per_pairing,
                                              int repeat_count = 1);
};

}  // namespace ijccrl::core::tournament
