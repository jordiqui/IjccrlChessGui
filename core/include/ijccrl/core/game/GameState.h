#pragma once

#include <string>
#include <vector>

namespace ijccrl::core::game {

enum class Side {
    White,
    Black
};

struct GameState {
    struct EvalInfo {
        bool has_cp = false;
        int cp = 0;
        bool has_mate = false;
        int mate = 0;
        int depth = 0;
    };

    std::vector<std::string> moves_uci;
    Side side_to_move = Side::White;
    int wtime_ms = 0;
    int btime_ms = 0;
    int winc_ms = 0;
    int binc_ms = 0;
    EvalInfo last_eval_white;
    EvalInfo last_eval_black;
    std::string result = "*";
    std::string termination;
    std::string termination_detail;
    bool tablebase_used = false;
};

}  // namespace ijccrl::core::game
