#include "ijccrl/core/tournament/SwissScheduler.h"

#include <algorithm>
#include <sstream>

namespace ijccrl::core::tournament {

namespace {

long long PairKey(int a, int b) {
    const int low = std::min(a, b);
    const int high = std::max(a, b);
    return (static_cast<long long>(low) << 32) | static_cast<unsigned int>(high);
}

std::string PairingIdFor(int a, int b) {
    const int low = std::min(a, b);
    const int high = std::max(a, b);
    std::ostringstream out;
    out << "pair_" << low << "_" << high;
    return out.str();
}

int ColorPenalty(const SwissColorState& state, int color) {
    if (state.last_color == 0) {
        return 0;
    }
    if (state.last_color != color) {
        return 0;
    }
    if (state.streak >= 2) {
        return 100;
    }
    return 10;
}

std::pair<int, int> ChooseColors(int a,
                                 int b,
                                 const std::vector<SwissColorState>& color_history) {
    const auto& a_state = color_history[static_cast<size_t>(a)];
    const auto& b_state = color_history[static_cast<size_t>(b)];
    const int penalty_a_white = ColorPenalty(a_state, 1);
    const int penalty_a_black = ColorPenalty(a_state, -1);
    const int penalty_b_white = ColorPenalty(b_state, 1);
    const int penalty_b_black = ColorPenalty(b_state, -1);

    const int option1 = penalty_a_white + penalty_b_black;
    const int option2 = penalty_a_black + penalty_b_white;

    if (option1 < option2) {
        return {a, b};
    }
    if (option2 < option1) {
        return {b, a};
    }
    if (a < b) {
        return {a, b};
    }
    return {b, a};
}

}  // namespace

SwissRound SwissScheduler::BuildSwissRound(int round_index,
                                           const std::vector<double>& scores,
                                           const std::vector<std::vector<int>>& opponent_history,
                                           const std::vector<int>& bye_history,
                                           const std::vector<SwissColorState>& color_history,
                                           const std::unordered_set<long long>& pairings_played,
                                           int games_per_pairing,
                                           bool avoid_repeats) {
    SwissRound result;
    result.round.round_index = round_index;
    const int engine_count = static_cast<int>(scores.size());
    if (engine_count < 2) {
        return result;
    }

    struct PlayerEntry {
        int engine_id = -1;
        double points = 0.0;
        double buchholz = 0.0;
    };

    std::vector<PlayerEntry> players;
    players.reserve(static_cast<size_t>(engine_count));
    for (int i = 0; i < engine_count; ++i) {
        double buchholz = 0.0;
        for (int opp : opponent_history[static_cast<size_t>(i)]) {
            if (opp >= 0 && opp < engine_count) {
                buchholz += scores[static_cast<size_t>(opp)];
            }
        }
        players.push_back({i, scores[static_cast<size_t>(i)], buchholz});
    }

    std::stable_sort(players.begin(), players.end(), [](const auto& a, const auto& b) {
        if (a.points != b.points) {
            return a.points > b.points;
        }
        if (a.buchholz != b.buchholz) {
            return a.buchholz > b.buchholz;
        }
        return a.engine_id < b.engine_id;
    });

    if (engine_count % 2 == 1) {
        for (auto it = players.rbegin(); it != players.rend(); ++it) {
            const int engine_id = it->engine_id;
            if (std::find(bye_history.begin(), bye_history.end(), engine_id) == bye_history.end()) {
                result.round.bye_engine_id = engine_id;
                players.erase(std::next(it).base());
                break;
            }
        }
        if (!result.round.bye_engine_id.has_value() && !players.empty()) {
            result.round.bye_engine_id = players.back().engine_id;
            players.pop_back();
        }
    }

    std::vector<std::vector<int>> groups;
    for (const auto& entry : players) {
        if (groups.empty() || entry.points != scores[static_cast<size_t>(groups.back().front())]) {
            groups.emplace_back();
        }
        groups.back().push_back(entry.engine_id);
    }

    std::vector<int> carry;
    for (size_t group_index = 0; group_index < groups.size(); ++group_index) {
        std::vector<int> list;
        list.reserve(carry.size() + groups[group_index].size());
        list.insert(list.end(), carry.begin(), carry.end());
        list.insert(list.end(), groups[group_index].begin(), groups[group_index].end());
        carry.clear();

        while (list.size() >= 2) {
            const int a = list.front();
            list.erase(list.begin());
            int opponent_index = -1;
            for (size_t i = 0; i < list.size(); ++i) {
                const int b = list[i];
                const long long key = PairKey(a, b);
                if (!avoid_repeats || pairings_played.count(key) == 0) {
                    opponent_index = static_cast<int>(i);
                    break;
                }
            }
            if (opponent_index < 0) {
                if (avoid_repeats && group_index + 1 < groups.size()) {
                    carry.push_back(a);
                    continue;
                }
                opponent_index = 0;
            }

            const int b = list[static_cast<size_t>(opponent_index)];
            list.erase(list.begin() + opponent_index);

            const auto colors = ChooseColors(a, b, color_history);
            const int white = colors.first;
            const int black = colors.second;
            const std::string pairing_id = PairingIdFor(a, b);
            result.pairings.emplace_back(a, b);
            for (int g = 0; g < games_per_pairing; ++g) {
                Fixture fixture;
                fixture.round_index = round_index;
                fixture.game_index_within_pairing = g;
                fixture.white_engine_id = (g % 2 == 0) ? white : black;
                fixture.black_engine_id = (g % 2 == 0) ? black : white;
                fixture.pairing_id = pairing_id;
                result.round.fixtures.push_back(std::move(fixture));
            }
        }

        if (!list.empty()) {
            carry.push_back(list.front());
        }
    }

    if (!carry.empty() && !result.round.bye_engine_id.has_value()) {
        result.round.bye_engine_id = carry.front();
    }

    return result;
}

TournamentRound SwissScheduler::BuildRound(const TournamentContext& context) {
    std::unordered_set<long long> pairings_played;
    for (size_t i = 0; i < context.opponents.size(); ++i) {
        for (int opp : context.opponents[i]) {
            const long long key = PairKey(static_cast<int>(i), opp);
            pairings_played.insert(key);
        }
    }
    std::vector<SwissColorState> color_history(context.engine_count);
    SwissRound swiss_round = BuildSwissRound(context.round_index,
                                             context.scores,
                                             context.opponents,
                                             context.bye_history,
                                             color_history,
                                             pairings_played,
                                             context.games_per_pairing,
                                             context.avoid_repeats);
    return swiss_round.round;
}

}  // namespace ijccrl::core::tournament
