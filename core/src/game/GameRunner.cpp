#include "ijccrl/core/game/GameRunner.h"

#include "ijccrl/core/pgn/PgnWriter.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace ijccrl::core::game {

namespace {

std::string CurrentDateUtc() {
    std::time_t now = std::time(nullptr);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y.%m.%d");
    return out.str();
}

bool IsStartposFen(const std::string& fen) {
    return fen == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
}

}  // namespace

GameRunner::Result GameRunner::PlayGame(ijccrl::core::uci::UciEngine& white,
                                        ijccrl::core::uci::UciEngine& black,
                                        const TimeControl& time_control,
                                        int max_plies,
                                        int go_timeout_ms,
                                        const std::atomic<bool>* stop_requested,
                                        ijccrl::core::pgn::PgnGame pgn_template,
                                        const std::string& initial_fen,
                                        const std::vector<std::string>& opening_moves,
                                        const LiveUpdateFn& live_update) {
    Result result;
    result.state.wtime_ms = time_control.base_ms;
    result.state.btime_ms = time_control.base_ms;
    result.state.winc_ms = time_control.increment_ms;
    result.state.binc_ms = time_control.increment_ms;
    result.state.moves_uci = opening_moves;
    if (opening_moves.size() % 2 == 1) {
        result.state.side_to_move = Side::Black;
    }

    result.pgn = std::move(pgn_template);
    result.pgn.SetTag("Date", CurrentDateUtc());

    auto publish_live = [&](const std::string& outcome) {
        result.pgn.moves = result.state.moves_uci;
        result.pgn.result = outcome;
        if (live_update) {
            live_update(result.pgn);
        }
    };

    const std::string position_fen = IsStartposFen(initial_fen) ? "" : initial_fen;

    for (int ply = 0; ply < max_plies; ++ply) {
        if (stop_requested && stop_requested->load()) {
            result.state.result = "*";
            result.state.termination = "aborted";
            break;
        }
        auto& engine = (result.state.side_to_move == Side::White) ? white : black;

        if (!engine.IsRunning()) {
            result.state.result = (result.state.side_to_move == Side::White) ? "0-1" : "1-0";
            result.state.termination = "engine crash";
            break;
        }

        engine.Position(position_fen, result.state.moves_uci);

        const int movetime_ms = time_control.move_time_ms;
        const int timeout_ms = go_timeout_ms > 0 ? go_timeout_ms : (movetime_ms + 5000);
        std::string bestmove;
        const bool got_move = engine.Go(result.state.wtime_ms,
                                        result.state.btime_ms,
                                        result.state.winc_ms,
                                        result.state.binc_ms,
                                        movetime_ms,
                                        timeout_ms,
                                        bestmove);
        if (!got_move || bestmove.empty()) {
            result.state.result = (result.state.side_to_move == Side::White) ? "0-1" : "1-0";
            result.state.termination = got_move ? "no move" : "timeout";
            break;
        }

        result.state.moves_uci.push_back(bestmove);

        if (result.state.side_to_move == Side::White) {
            result.state.wtime_ms -= movetime_ms;
            result.state.wtime_ms += result.state.winc_ms;
        } else {
            result.state.btime_ms -= movetime_ms;
            result.state.btime_ms += result.state.binc_ms;
        }

        publish_live("*");

        if (result.state.wtime_ms <= 0) {
            result.state.result = "0-1";
            result.state.termination = "timeout";
            break;
        }
        if (result.state.btime_ms <= 0) {
            result.state.result = "1-0";
            result.state.termination = "timeout";
            break;
        }

        result.state.side_to_move =
            (result.state.side_to_move == Side::White) ? Side::Black : Side::White;
    }

    if (result.state.result == "*" && result.state.termination.empty()) {
        result.state.result = "1/2-1/2";
        result.state.termination = "ply limit";
    }

    result.pgn.SetTag("Result", result.state.result);
    publish_live(result.state.result);
    return result;
}

}  // namespace ijccrl::core::game
