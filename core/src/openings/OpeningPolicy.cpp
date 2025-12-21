#include "ijccrl/core/openings/OpeningPolicy.h"

#include <algorithm>

namespace ijccrl::core::openings {

std::vector<Opening> OpeningPolicy::AssignRoundRobin(
    const std::vector<ijccrl::core::tournament::Fixture>& fixtures,
    const std::vector<Opening>& openings,
    int games_per_pairing) {
    std::vector<Opening> assigned;
    assigned.reserve(fixtures.size());
    if (fixtures.empty()) {
        return assigned;
    }
    if (openings.empty()) {
        assigned.assign(fixtures.size(), Opening{});
        return assigned;
    }

    size_t pairing_index = 0;
    for (const auto& fixture : fixtures) {
        if (games_per_pairing <= 1 || fixture.game_index_within_pairing == 0) {
            pairing_index = assigned.size() / static_cast<size_t>(std::max(1, games_per_pairing));
        }
        const size_t opening_index = pairing_index % openings.size();
        assigned.push_back(openings[opening_index]);
    }

    return assigned;
}

Opening OpeningPolicy::AssignSwissForIndex(int global_game_index,
                                           const std::vector<Opening>& openings,
                                           int games_per_pairing) {
    if (openings.empty()) {
        return Opening{};
    }
    const int pairing_index = games_per_pairing <= 0
                                  ? global_game_index
                                  : (global_game_index / games_per_pairing);
    const size_t opening_index = static_cast<size_t>(pairing_index) % openings.size();
    return openings[opening_index];
}

}  // namespace ijccrl::core::openings
