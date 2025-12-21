#pragma once

namespace ijccrl::core::game {

struct TimeControl {
    int base_ms = 0;
    int increment_ms = 0;
    int move_time_ms = 200;
};

}  // namespace ijccrl::core::game
