#include "ijccrl/core/rules/Termination.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <memory>
#include <optional>
#include <sstream>

namespace ijccrl::core::rules {

namespace {

constexpr int kBoardSize = 8;

int FileIndex(char file) {
    return file - 'a';
}

int RankIndex(char rank) {
    return rank - '1';
}

char FileChar(int file) {
    return static_cast<char>('a' + file);
}

char RankChar(int rank) {
    return static_cast<char>('1' + rank);
}

bool IsWhitePiece(char piece) {
    return std::isupper(static_cast<unsigned char>(piece)) != 0;
}

bool IsEmpty(char piece) {
    return piece == '.';
}

struct CastlingRights {
    bool white_kingside = false;
    bool white_queenside = false;
    bool black_kingside = false;
    bool black_queenside = false;
};

}  // namespace

struct GameTerminator::PositionState {
    char board[kBoardSize][kBoardSize]{};
    ijccrl::core::game::Side side_to_move = ijccrl::core::game::Side::White;
    CastlingRights castling{};
    std::string en_passant = "-";
    int halfmove_clock = 0;
    int fullmove_number = 1;
    std::unordered_map<std::string, int> repetition_counts;

    PositionState() {
        for (int rank = 0; rank < kBoardSize; ++rank) {
            for (int file = 0; file < kBoardSize; ++file) {
                board[rank][file] = '.';
            }
        }
    }

    void LoadStartpos() {
        LoadFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    }

    void LoadFen(const std::string& fen) {
        *this = PositionState();
        std::istringstream iss(fen);
        std::string placement;
        iss >> placement;
        std::string side;
        iss >> side;
        std::string castling_str;
        iss >> castling_str;
        std::string ep;
        iss >> ep;
        iss >> halfmove_clock;
        iss >> fullmove_number;

        int rank = 7;
        int file = 0;
        for (char c : placement) {
            if (c == '/') {
                rank -= 1;
                file = 0;
                continue;
            }
            if (std::isdigit(static_cast<unsigned char>(c))) {
                file += c - '0';
                continue;
            }
            if (rank >= 0 && rank < kBoardSize && file >= 0 && file < kBoardSize) {
                board[rank][file] = c;
            }
            file += 1;
        }

        side_to_move = (side == "b") ? ijccrl::core::game::Side::Black
                                     : ijccrl::core::game::Side::White;
        castling = {};
        if (castling_str.find('K') != std::string::npos) {
            castling.white_kingside = true;
        }
        if (castling_str.find('Q') != std::string::npos) {
            castling.white_queenside = true;
        }
        if (castling_str.find('k') != std::string::npos) {
            castling.black_kingside = true;
        }
        if (castling_str.find('q') != std::string::npos) {
            castling.black_queenside = true;
        }
        en_passant = ep.empty() ? "-" : ep;

        repetition_counts.clear();
        repetition_counts.emplace(PositionKey(), 1);
    }

    std::string PositionKey() const {
        std::ostringstream out;
        for (int rank = 7; rank >= 0; --rank) {
            int empty = 0;
            for (int file = 0; file < kBoardSize; ++file) {
                const char piece = board[rank][file];
                if (IsEmpty(piece)) {
                    empty += 1;
                } else {
                    if (empty > 0) {
                        out << empty;
                        empty = 0;
                    }
                    out << piece;
                }
            }
            if (empty > 0) {
                out << empty;
            }
            if (rank > 0) {
                out << '/';
            }
        }
        out << ' ' << (side_to_move == ijccrl::core::game::Side::White ? 'w' : 'b');
        out << ' ';
        std::string castling_str;
        if (castling.white_kingside) {
            castling_str.push_back('K');
        }
        if (castling.white_queenside) {
            castling_str.push_back('Q');
        }
        if (castling.black_kingside) {
            castling_str.push_back('k');
        }
        if (castling.black_queenside) {
            castling_str.push_back('q');
        }
        out << (castling_str.empty() ? "-" : castling_str);
        out << ' ' << (en_passant.empty() ? "-" : en_passant);
        return out.str();
    }

    std::string Fen() const {
        std::ostringstream out;
        for (int rank = 7; rank >= 0; --rank) {
            int empty = 0;
            for (int file = 0; file < kBoardSize; ++file) {
                const char piece = board[rank][file];
                if (IsEmpty(piece)) {
                    empty += 1;
                } else {
                    if (empty > 0) {
                        out << empty;
                        empty = 0;
                    }
                    out << piece;
                }
            }
            if (empty > 0) {
                out << empty;
            }
            if (rank > 0) {
                out << '/';
            }
        }
        out << ' ' << (side_to_move == ijccrl::core::game::Side::White ? 'w' : 'b');
        out << ' ';
        std::string castling_str;
        if (castling.white_kingside) {
            castling_str.push_back('K');
        }
        if (castling.white_queenside) {
            castling_str.push_back('Q');
        }
        if (castling.black_kingside) {
            castling_str.push_back('k');
        }
        if (castling.black_queenside) {
            castling_str.push_back('q');
        }
        out << (castling_str.empty() ? "-" : castling_str);
        out << ' ' << (en_passant.empty() ? "-" : en_passant);
        out << ' ' << halfmove_clock;
        out << ' ' << fullmove_number;
        return out.str();
    }

    int PieceCount() const {
        int pieces = 0;
        for (int rank = 0; rank < kBoardSize; ++rank) {
            for (int file = 0; file < kBoardSize; ++file) {
                if (!IsEmpty(board[rank][file])) {
                    pieces += 1;
                }
            }
        }
        return pieces;
    }

    void ApplyMove(const std::string& move) {
        if (move.size() < 4) {
            return;
        }
        const int from_file = FileIndex(move[0]);
        const int from_rank = RankIndex(move[1]);
        const int to_file = FileIndex(move[2]);
        const int to_rank = RankIndex(move[3]);

        char moving_piece = board[from_rank][from_file];
        const char target_piece = board[to_rank][to_file];
        const bool is_capture = !IsEmpty(target_piece);
        const bool is_pawn = std::tolower(static_cast<unsigned char>(moving_piece)) == 'p';

        bool en_passant_capture = false;
        if (is_pawn && !is_capture && en_passant != "-" && en_passant.size() == 2) {
            if (en_passant[0] == move[2] && en_passant[1] == move[3]) {
                en_passant_capture = true;
                const int capture_rank = (side_to_move == ijccrl::core::game::Side::White)
                                             ? to_rank - 1
                                             : to_rank + 1;
                if (capture_rank >= 0 && capture_rank < kBoardSize) {
                    board[capture_rank][to_file] = '.';
                }
            }
        }

        board[from_rank][from_file] = '.';
        if (move.size() >= 5) {
            char promo = move[4];
            if (side_to_move == ijccrl::core::game::Side::White) {
                promo = static_cast<char>(std::toupper(static_cast<unsigned char>(promo)));
            } else {
                promo = static_cast<char>(std::tolower(static_cast<unsigned char>(promo)));
            }
            board[to_rank][to_file] = promo;
        } else {
            board[to_rank][to_file] = moving_piece;
        }

        if (std::tolower(static_cast<unsigned char>(moving_piece)) == 'k') {
            if (side_to_move == ijccrl::core::game::Side::White) {
                castling.white_kingside = false;
                castling.white_queenside = false;
                if (move == "e1g1") {
                    board[0][5] = board[0][7];
                    board[0][7] = '.';
                } else if (move == "e1c1") {
                    board[0][3] = board[0][0];
                    board[0][0] = '.';
                }
            } else {
                castling.black_kingside = false;
                castling.black_queenside = false;
                if (move == "e8g8") {
                    board[7][5] = board[7][7];
                    board[7][7] = '.';
                } else if (move == "e8c8") {
                    board[7][3] = board[7][0];
                    board[7][0] = '.';
                }
            }
        }

        if (std::tolower(static_cast<unsigned char>(moving_piece)) == 'r') {
            if (from_file == 0 && from_rank == 0) {
                castling.white_queenside = false;
            } else if (from_file == 7 && from_rank == 0) {
                castling.white_kingside = false;
            } else if (from_file == 0 && from_rank == 7) {
                castling.black_queenside = false;
            } else if (from_file == 7 && from_rank == 7) {
                castling.black_kingside = false;
            }
        }

        if (!IsEmpty(target_piece) || en_passant_capture) {
            if (to_file == 0 && to_rank == 0) {
                castling.white_queenside = false;
            } else if (to_file == 7 && to_rank == 0) {
                castling.white_kingside = false;
            } else if (to_file == 0 && to_rank == 7) {
                castling.black_queenside = false;
            } else if (to_file == 7 && to_rank == 7) {
                castling.black_kingside = false;
            }
        }

        if (is_pawn && std::abs(to_rank - from_rank) == 2) {
            const int ep_rank = (from_rank + to_rank) / 2;
            en_passant = std::string{FileChar(from_file), RankChar(ep_rank)};
        } else {
            en_passant = "-";
        }

        if (is_pawn || is_capture || en_passant_capture) {
            halfmove_clock = 0;
        } else {
            halfmove_clock += 1;
        }

        if (side_to_move == ijccrl::core::game::Side::Black) {
            fullmove_number += 1;
        }

        side_to_move = (side_to_move == ijccrl::core::game::Side::White)
                           ? ijccrl::core::game::Side::Black
                           : ijccrl::core::game::Side::White;

        const std::string key = PositionKey();
        repetition_counts[key] += 1;
    }
};

namespace {

struct TablebaseProber {
    explicit TablebaseProber(TablebaseConfig config)
        : config_(std::move(config)) {}

    ProbeInfo Probe(const GameTerminator::PositionState& position) const {
        ProbeInfo info;
        info.pieces = position.PieceCount();
        info.tb_available = config_.enabled && !config_.paths.empty();
        info.tb_used = false;

        if (!config_.enabled || config_.paths.empty() || info.pieces > config_.probe_limit_pieces) {
            info.detail = "tb disabled or above piece limit";
            return info;
        }

        info.detail = "tb backend not available";
        return info;
    }

    TablebaseConfig config_;
};

bool EvalBelow(const ijccrl::core::game::GameState::EvalInfo& eval, int threshold, int min_depth) {
    if (eval.depth < min_depth) {
        return false;
    }
    if (eval.has_mate) {
        return eval.mate < 0;
    }
    if (eval.has_cp) {
        return eval.cp <= -threshold;
    }
    return false;
}

bool EvalAbove(const ijccrl::core::game::GameState::EvalInfo& eval, int threshold, int min_depth) {
    if (eval.depth < min_depth) {
        return false;
    }
    if (eval.has_mate) {
        return eval.mate > 0;
    }
    if (eval.has_cp) {
        return eval.cp >= threshold;
    }
    return false;
}

bool EvalNearZero(const ijccrl::core::game::GameState::EvalInfo& eval, int threshold, int min_depth) {
    if (eval.depth < min_depth) {
        return false;
    }
    if (eval.has_mate) {
        return false;
    }
    if (eval.has_cp) {
        return std::abs(eval.cp) <= threshold;
    }
    return false;
}

bool HasEval(const ijccrl::core::game::GameState::EvalInfo& eval) {
    return eval.has_cp || eval.has_mate;
}

}  // namespace

GameTerminator::~GameTerminator() = default;

GameTerminator::GameTerminator(const std::string& initial_fen,
                               const std::vector<std::string>& opening_moves,
                               const ConfigLimits& limits,
                               const TablebaseConfig& tablebases)
    : position_state_(std::make_unique<GameTerminator::PositionState>()),
      limits_(limits),
      tablebases_(tablebases) {
    if (initial_fen.empty()) {
        position_state_->LoadStartpos();
    } else {
        position_state_->LoadFen(initial_fen);
    }
    for (const auto& move : opening_moves) {
        position_state_->ApplyMove(move);
    }
}

void GameTerminator::ApplyMove(const std::string& move_uci) {
    if (position_state_) {
        position_state_->ApplyMove(move_uci);
    }
}

ProbeInfo GameTerminator::BuildProbeInfo() const {
    if (!position_state_) {
        return {};
    }
    TablebaseProber prober(tablebases_);
    return prober.Probe(*position_state_);
}

TerminationOutcome GameTerminator::ShouldEnd(const ijccrl::core::game::GameState& state,
                                             const EngineInfos& infos,
                                             const ProbeInfo& probe,
                                             bool manual_stop) {
    TerminationOutcome outcome;
    if (manual_stop) {
        outcome.should_end = true;
        outcome.result = "*";
        outcome.reason = TerminationReason::ManualStop;
        outcome.detail = "manual stop";
        return outcome;
    }

    if (infos.white.crashed || infos.black.crashed) {
        outcome.should_end = true;
        outcome.reason = TerminationReason::Crash;
        if (infos.white.crashed) {
            outcome.result = "0-1";
        } else {
            outcome.result = "1-0";
        }
        outcome.detail = "engine crash";
        return outcome;
    }

    if (infos.white.timeout || infos.black.timeout) {
        outcome.should_end = true;
        outcome.reason = TerminationReason::Timeout;
        if (infos.white.timeout) {
            outcome.result = "0-1";
        } else {
            outcome.result = "1-0";
        }
        outcome.detail = "timeout";
        return outcome;
    }

    const auto no_move = (state.side_to_move == ijccrl::core::game::Side::White)
                             ? infos.white.no_move
                             : infos.black.no_move;
    if (no_move) {
        const auto& eval = (state.side_to_move == ijccrl::core::game::Side::White)
                               ? infos.white.eval
                               : infos.black.eval;
        const bool checkmate = eval.has_mate && eval.mate != 0;
        if (checkmate) {
            outcome.reason = TerminationReason::Checkmate;
            outcome.result = (state.side_to_move == ijccrl::core::game::Side::White) ? "0-1" : "1-0";
        } else {
            outcome.reason = TerminationReason::Stalemate;
            outcome.result = "1/2-1/2";
        }
        outcome.should_end = true;
        outcome.detail = "no legal moves";
        return outcome;
    }

    if (state.wtime_ms <= 0 || state.btime_ms <= 0) {
        outcome.should_end = true;
        outcome.reason = TerminationReason::Timeout;
        outcome.result = (state.wtime_ms <= 0) ? "0-1" : "1-0";
        outcome.detail = "clock flag";
        return outcome;
    }

    if (probe.tb_used) {
        outcome.should_end = true;
        outcome.reason = TerminationReason::TBAdjudication;
        outcome.tablebase_used = true;
        outcome.detail = probe.detail;
        if (probe.wdl == ProbeInfo::Wdl::Draw) {
            outcome.result = "1/2-1/2";
        } else if (probe.wdl == ProbeInfo::Wdl::Win) {
            outcome.result = "1-0";
        } else if (probe.wdl == ProbeInfo::Wdl::Loss) {
            outcome.result = "0-1";
        }
        return outcome;
    }

    if (limits_.adjudication.enabled) {
        const bool draw_ok = EvalNearZero(state.last_eval_white,
                                          limits_.adjudication.score_draw_cp,
                                          limits_.adjudication.min_depth) &&
                             EvalNearZero(state.last_eval_black,
                                          limits_.adjudication.score_draw_cp,
                                          limits_.adjudication.min_depth);
        if (draw_ok) {
            draw_score_streak_ += 1;
        } else {
            draw_score_streak_ = 0;
        }
        if (draw_score_streak_ >= limits_.adjudication.score_draw_moves) {
            outcome.should_end = true;
            outcome.reason = TerminationReason::ScoreAdjudication;
            outcome.result = "1/2-1/2";
            outcome.detail = "score draw";
            return outcome;
        }

        const bool white_win =
            EvalAbove(state.last_eval_white, limits_.adjudication.score_win_cp, limits_.adjudication.min_depth) &&
            (!HasEval(state.last_eval_black) ||
             EvalBelow(state.last_eval_black,
                       limits_.adjudication.score_win_cp,
                       limits_.adjudication.min_depth));
        const bool black_win =
            EvalBelow(state.last_eval_white, limits_.adjudication.score_win_cp, limits_.adjudication.min_depth) &&
            (!HasEval(state.last_eval_black) ||
             EvalAbove(state.last_eval_black,
                       limits_.adjudication.score_win_cp,
                       limits_.adjudication.min_depth));

        if (white_win) {
            win_score_streak_white_ += 1;
            win_score_streak_black_ = 0;
        } else if (black_win) {
            win_score_streak_black_ += 1;
            win_score_streak_white_ = 0;
        } else {
            win_score_streak_white_ = 0;
            win_score_streak_black_ = 0;
        }

        if (win_score_streak_white_ >= limits_.adjudication.score_win_moves) {
            outcome.should_end = true;
            outcome.reason = TerminationReason::ScoreAdjudication;
            outcome.result = "1-0";
            outcome.detail = "score win";
            return outcome;
        }
        if (win_score_streak_black_ >= limits_.adjudication.score_win_moves) {
            outcome.should_end = true;
            outcome.reason = TerminationReason::ScoreAdjudication;
            outcome.result = "0-1";
            outcome.detail = "score win";
            return outcome;
        }
    }

    if (limits_.resign.enabled) {
        if (EvalBelow(state.last_eval_white, limits_.resign.cp, limits_.resign.min_depth)) {
            resign_streak_white_ += 1;
        } else {
            resign_streak_white_ = 0;
        }
        if (EvalBelow(state.last_eval_black, limits_.resign.cp, limits_.resign.min_depth)) {
            resign_streak_black_ += 1;
        } else {
            resign_streak_black_ = 0;
        }
        if (resign_streak_white_ >= limits_.resign.moves) {
            outcome.should_end = true;
            outcome.reason = TerminationReason::Resign;
            outcome.result = "0-1";
            outcome.detail = "resign eval";
            return outcome;
        }
        if (resign_streak_black_ >= limits_.resign.moves) {
            outcome.should_end = true;
            outcome.reason = TerminationReason::Resign;
            outcome.result = "1-0";
            outcome.detail = "resign eval";
            return outcome;
        }
    }

    if (limits_.draw_by_repetition && position_state_) {
        const std::string key = position_state_->PositionKey();
        const auto it = position_state_->repetition_counts.find(key);
        if (it != position_state_->repetition_counts.end() && it->second >= 3) {
            outcome.should_end = true;
            outcome.reason = TerminationReason::Threefold;
            outcome.result = "1/2-1/2";
            outcome.detail = "threefold repetition";
            return outcome;
        }
    }

    if (position_state_ && position_state_->halfmove_clock >= 100) {
        outcome.should_end = true;
        outcome.reason = TerminationReason::FiftyMove;
        outcome.result = "1/2-1/2";
        outcome.detail = "fifty-move rule";
        return outcome;
    }

    if (static_cast<int>(state.moves_uci.size()) >= limits_.max_plies) {
        outcome.should_end = true;
        outcome.reason = TerminationReason::MaxPlies;
        outcome.result = "1/2-1/2";
        outcome.detail = "max plies";
        return outcome;
    }

    return outcome;
}

std::string GameTerminator::ReasonToString(TerminationReason reason) {
    switch (reason) {
        case TerminationReason::Checkmate:
            return "checkmate";
        case TerminationReason::Stalemate:
            return "stalemate";
        case TerminationReason::Resign:
            return "resign";
        case TerminationReason::Timeout:
            return "timeout";
        case TerminationReason::Crash:
            return "engine crash";
        case TerminationReason::Threefold:
            return "threefold repetition";
        case TerminationReason::FiftyMove:
            return "fifty-move";
        case TerminationReason::TBAdjudication:
            return "tablebase adjudication";
        case TerminationReason::ScoreAdjudication:
            return "score adjudication";
        case TerminationReason::MaxPlies:
            return "ply limit";
        case TerminationReason::ManualStop:
            return "manual stop";
    }
    return "unknown";
}

std::string GameTerminator::TerminationTag(TerminationReason reason) {
    switch (reason) {
        case TerminationReason::ScoreAdjudication:
        case TerminationReason::TBAdjudication:
            return "adjudication";
        case TerminationReason::ManualStop:
            return "aborted";
        case TerminationReason::Crash:
            return "forfeit";
        case TerminationReason::Timeout:
            return "time forfeit";
        case TerminationReason::Checkmate:
            return "checkmate";
        case TerminationReason::Stalemate:
            return "stalemate";
        case TerminationReason::Resign:
            return "resign";
        case TerminationReason::Threefold:
            return "threefold repetition";
        case TerminationReason::FiftyMove:
            return "fifty-move rule";
        case TerminationReason::MaxPlies:
            return "move limit";
    }
    return "unknown";
}

}  // namespace ijccrl::core::rules
