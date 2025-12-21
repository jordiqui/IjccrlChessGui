#include "ijccrl/core/game/GameRunner.h"

#include "ijccrl/core/pgn/PgnWriter.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <optional>
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
                                        const ijccrl::core::rules::ConfigLimits& termination_limits,
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

    std::optional<ijccrl::core::rules::TerminationReason> termination_reason;

    auto publish_live = [&](const std::string& outcome) {
        result.pgn.moves = result.state.moves_uci;
        result.pgn.result = outcome;
        if (!result.state.termination.empty()) {
            result.pgn.SetTag("Termination", result.state.termination);
        }
        if (!result.state.termination_detail.empty()) {
            result.pgn.termination_comment = result.state.termination_detail;
        }
        if (live_update) {
            live_update(result.pgn);
        }
    };

    const std::string position_fen = IsStartposFen(initial_fen) ? "" : initial_fen;
    ijccrl::core::rules::GameTerminator terminator(position_fen,
                                                   opening_moves,
                                                   termination_limits,
                                                   termination_limits.tablebases);
    ijccrl::core::rules::EngineInfos engine_infos;

    auto update_eval = [&](ijccrl::core::uci::UciEngine& engine,
                           Side engine_side) {
        const auto& info = engine.last_info();
        ijccrl::core::game::GameState::EvalInfo eval;
        const int multiplier = (engine_side == Side::White) ? 1 : -1;
        eval.depth = info.depth;
        if (info.has_score_mate) {
            eval.has_mate = true;
            eval.mate = info.score_mate * multiplier;
        } else if (info.has_score_cp) {
            eval.has_cp = true;
            eval.cp = info.score_cp * multiplier;
        }
        if (engine_side == Side::White) {
            result.state.last_eval_white = eval;
            engine_infos.white.eval = eval;
        } else {
            result.state.last_eval_black = eval;
            engine_infos.black.eval = eval;
        }
    };

    while (true) {
        if (stop_requested && stop_requested->load()) {
            const auto outcome = terminator.ShouldEnd(result.state,
                                                      engine_infos,
                                                      terminator.BuildProbeInfo(),
                                                      true);
            if (outcome.should_end) {
                result.state.result = outcome.result;
                result.state.termination = ijccrl::core::rules::GameTerminator::ReasonToString(outcome.reason);
                result.state.termination_detail = outcome.detail;
                result.state.tablebase_used = outcome.tablebase_used;
                termination_reason = outcome.reason;
            }
            break;
        }
        auto& engine = (result.state.side_to_move == Side::White) ? white : black;
        auto& current_info =
            (result.state.side_to_move == Side::White) ? engine_infos.white : engine_infos.black;
        current_info.no_move = false;
        current_info.timeout = false;
        current_info.crashed = false;

        if (!engine.IsRunning()) {
            current_info.crashed = true;
            const auto outcome = terminator.ShouldEnd(result.state,
                                                      engine_infos,
                                                      terminator.BuildProbeInfo(),
                                                      false);
            result.state.result = outcome.result;
            result.state.termination = ijccrl::core::rules::GameTerminator::ReasonToString(outcome.reason);
            result.state.termination_detail = outcome.detail;
            termination_reason = outcome.reason;
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
            if (!got_move) {
                current_info.timeout = engine.last_failure() ==
                                       ijccrl::core::uci::UciEngine::Failure::Timeout;
                if (!engine.IsRunning()) {
                    current_info.crashed = true;
                }
            } else {
                current_info.no_move = true;
            }
            update_eval(engine, result.state.side_to_move);
            const auto outcome = terminator.ShouldEnd(result.state,
                                                      engine_infos,
                                                      terminator.BuildProbeInfo(),
                                                      false);
            if (outcome.should_end) {
                result.state.result = outcome.result;
                result.state.termination = ijccrl::core::rules::GameTerminator::ReasonToString(outcome.reason);
                result.state.termination_detail = outcome.detail;
                result.state.tablebase_used = outcome.tablebase_used;
                termination_reason = outcome.reason;
            }
            break;
        }

        result.state.moves_uci.push_back(bestmove);
        update_eval(engine, result.state.side_to_move);
        terminator.ApplyMove(bestmove);

        if (result.state.side_to_move == Side::White) {
            result.state.wtime_ms -= movetime_ms;
            result.state.wtime_ms += result.state.winc_ms;
        } else {
            result.state.btime_ms -= movetime_ms;
            result.state.btime_ms += result.state.binc_ms;
        }

        publish_live("*");

        result.state.side_to_move =
            (result.state.side_to_move == Side::White) ? Side::Black : Side::White;

        const auto outcome = terminator.ShouldEnd(result.state,
                                                  engine_infos,
                                                  terminator.BuildProbeInfo(),
                                                  false);
        if (outcome.should_end) {
            result.state.result = outcome.result;
            result.state.termination = ijccrl::core::rules::GameTerminator::ReasonToString(outcome.reason);
            result.state.termination_detail = outcome.detail;
            result.state.tablebase_used = outcome.tablebase_used;
            termination_reason = outcome.reason;
            break;
        }
    }

    if (result.state.result == "*" && result.state.termination.empty()) {
        result.state.result = "1/2-1/2";
        result.state.termination = "ply limit";
        termination_reason = ijccrl::core::rules::TerminationReason::MaxPlies;
    }

    result.pgn.SetTag("Result", result.state.result);
    if (termination_reason.has_value()) {
        result.pgn.SetTag("Termination",
                          ijccrl::core::rules::GameTerminator::TerminationTag(*termination_reason));
        if (*termination_reason == ijccrl::core::rules::TerminationReason::ScoreAdjudication) {
            result.pgn.termination_comment = "ScoreAdjudication: " + result.state.termination_detail;
        } else if (*termination_reason == ijccrl::core::rules::TerminationReason::TBAdjudication) {
            result.pgn.termination_comment = "TBAdjudication: " + result.state.termination_detail;
        }
    }
    publish_live(result.state.result);
    return result;
}

}  // namespace ijccrl::core::game
