#pragma once

#include <string>

namespace ijccrl::core::tournament {

struct Fixture {
    int round_index = 0;
    int white_engine_id = -1;
    int black_engine_id = -1;
    int game_index_within_pairing = 0;
    std::string pairing_id;
};

}  // namespace ijccrl::core::tournament
