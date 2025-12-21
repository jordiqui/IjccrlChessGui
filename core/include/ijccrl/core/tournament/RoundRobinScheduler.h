#pragma once

#include <string>
#include <vector>

namespace ijccrl::core::tournament {

struct Fixture {
    int round_index = 0;
    int white_engine_id = -1;
    int black_engine_id = -1;
    int game_index_within_pairing = 0;
    std::string pairing_id;
};

class RoundRobinScheduler {
public:
    static std::vector<Fixture> BuildSchedule(int engine_count,
                                              bool double_round_robin,
                                              int games_per_pairing,
                                              int repeat_count = 1);
};

}  // namespace ijccrl::core::tournament
