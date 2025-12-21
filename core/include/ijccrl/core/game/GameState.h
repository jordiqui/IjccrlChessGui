#pragma once

#include <string>
#include <vector>

namespace ijccrl::core::game {

enum class Side {
    White,
    Black
};

struct GameState {
    std::vector<std::string> moves_uci;
    Side side_to_move = Side::White;
    int wtime_ms = 0;
    int btime_ms = 0;
    int winc_ms = 0;
    int binc_ms = 0;
    std::string result = "*";
    std::string termination;
};

}  // namespace ijccrl::core::game
