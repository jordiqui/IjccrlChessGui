#include "ijccrl/core/tournament/RoundRobinScheduler.h"

#include <algorithm>
#include <sstream>

namespace ijccrl::core::tournament {

namespace {

std::string PairingIdFor(int a, int b) {
    const int low = std::min(a, b);
    const int high = std::max(a, b);
    std::ostringstream out;
    out << "pair_" << low << "_" << high;
    return out.str();
}

std::vector<int> BuildTeamList(int engine_count) {
    std::vector<int> teams;
    teams.reserve(static_cast<size_t>(engine_count + 1));
    for (int i = 0; i < engine_count; ++i) {
        teams.push_back(i);
    }
    if (engine_count % 2 == 1) {
        teams.push_back(-1);
    }
    return teams;
}

void RotateTeams(std::vector<int>& teams) {
    if (teams.size() <= 2) {
        return;
    }
    const int fixed = teams.front();
    const int last = teams.back();
    for (size_t i = teams.size() - 1; i > 1; --i) {
        teams[i] = teams[i - 1];
    }
    teams[1] = last;
    teams[0] = fixed;
}

}  // namespace

std::vector<Fixture> RoundRobinScheduler::BuildSchedule(int engine_count,
                                                        bool double_round_robin,
                                                        int games_per_pairing,
                                                        int repeat_count) {
    std::vector<Fixture> fixtures;
    if (engine_count < 2 || games_per_pairing < 1 || repeat_count < 1) {
        return fixtures;
    }

    auto teams = BuildTeamList(engine_count);
    const int team_count = static_cast<int>(teams.size());
    const int rounds = team_count - 1;

    std::vector<Fixture> base_fixtures;
    base_fixtures.reserve(static_cast<size_t>(rounds * team_count));

    for (int round = 0; round < rounds; ++round) {
        for (int i = 0; i < team_count / 2; ++i) {
            const int t1 = teams[i];
            const int t2 = teams[team_count - 1 - i];
            if (t1 == -1 || t2 == -1) {
                continue;
            }

            bool swap_colors = (round % 2 == 1);
            if (i == 0) {
                swap_colors = !swap_colors;
            }

            const int white = swap_colors ? t2 : t1;
            const int black = swap_colors ? t1 : t2;

            for (int g = 0; g < games_per_pairing; ++g) {
                const bool swap_for_game = (g % 2 == 1);
                Fixture fixture;
                fixture.round_index = round;
                fixture.white_engine_id = swap_for_game ? black : white;
                fixture.black_engine_id = swap_for_game ? white : black;
                fixture.game_index_within_pairing = g;
                fixture.pairing_id = PairingIdFor(white, black);
                base_fixtures.push_back(fixture);
            }
        }
        RotateTeams(teams);
    }

    const int cycles = double_round_robin ? 2 : 1;
    fixtures.reserve(base_fixtures.size() * static_cast<size_t>(cycles) *
                     static_cast<size_t>(repeat_count));

    for (int repeat = 0; repeat < repeat_count; ++repeat) {
        for (int cycle = 0; cycle < cycles; ++cycle) {
            const int round_offset = (repeat * rounds * cycles) + (cycle * rounds);
            for (const auto& fixture : base_fixtures) {
                Fixture next = fixture;
                next.round_index = fixture.round_index + round_offset;
                fixtures.push_back(next);
            }
        }
    }

    return fixtures;
}

}  // namespace ijccrl::core::tournament
