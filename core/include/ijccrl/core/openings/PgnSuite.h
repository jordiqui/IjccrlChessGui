#pragma once

#include "ijccrl/core/openings/Opening.h"

#include <string>
#include <vector>

namespace ijccrl::core::openings {

class PgnSuite {
public:
    static std::vector<Opening> LoadFile(const std::string& path);
};

}  // namespace ijccrl::core::openings
