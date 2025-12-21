#pragma once

#include "ijccrl/core/pgn/PgnGame.h"

#include <string>

namespace ijccrl::core::pgn {

class PgnWriter {
public:
    static std::string Render(const PgnGame& game);
};

}  // namespace ijccrl::core::pgn
