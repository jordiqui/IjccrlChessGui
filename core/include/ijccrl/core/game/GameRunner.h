#pragma once

#include "ijccrl/core/game/GameState.h"
#include "ijccrl/core/game/TimeControl.h"
#include "ijccrl/core/pgn/PgnGame.h"
#include "ijccrl/core/rules/Termination.h"
#include "ijccrl/core/uci/UciEngine.h"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace ijccrl::core::game {

class GameRunner {
public:
    using LiveUpdateFn = std::function<void(const ijccrl::core::pgn::PgnGame&)>;
    using MoveUpdateFn = std::function<void(const std::string&, const std::string&)>;

    struct Result {
        GameState state;
        ijccrl::core::pgn::PgnGame pgn;
        std::string final_fen;
    };

    Result PlayGame(ijccrl::core::uci::UciEngine& white,
                    ijccrl::core::uci::UciEngine& black,
                    const TimeControl& time_control,
                    const ijccrl::core::rules::ConfigLimits& termination_limits,
                    int go_timeout_ms,
                    const std::atomic<bool>* stop_requested,
                    ijccrl::core::pgn::PgnGame pgn_template,
                    const std::string& initial_fen,
                    const std::vector<std::string>& opening_moves,
                    const LiveUpdateFn& live_update,
                    const MoveUpdateFn& move_update);
};

}  // namespace ijccrl::core::game
