#pragma once

#include <string>
#include <vector>

namespace ijccrl::core::openings {

struct Opening {
    std::string id;
    std::string fen;
    std::vector<std::string> moves;
};

}  // namespace ijccrl::core::openings
