#pragma once

#include "ijccrl/core/game/GameState.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ijccrl::core::rules {

enum class TerminationReason {
    Checkmate,
    Stalemate,
    Resign,
    Timeout,
    Crash,
    Threefold,
    FiftyMove,
    TBAdjudication,
    ScoreAdjudication,
    MaxPlies,
    ManualStop
};

struct ScoreAdjudicationConfig {
    bool enabled = true;
    int score_draw_cp = 15;
    int score_draw_moves = 8;
    int score_win_cp = 700;
    int score_win_moves = 6;
    int min_depth = 12;
};

struct TablebaseConfig {
    bool enabled = true;
    std::vector<std::string> paths;
    int probe_limit_pieces = 6;
};

struct ResignConfig {
    bool enabled = true;
    int cp = 900;
    int moves = 3;
    int min_depth = 12;
};

struct ConfigLimits {
    int max_plies = 400;
    bool draw_by_repetition = false;
    ScoreAdjudicationConfig adjudication;
    TablebaseConfig tablebases;
    ResignConfig resign;
};

struct EngineInfo {
    ijccrl::core::game::GameState::EvalInfo eval;
    bool running = true;
    bool crashed = false;
    bool timeout = false;
    bool no_move = false;
};

struct EngineInfos {
    EngineInfo white;
    EngineInfo black;
};

struct ProbeInfo {
    enum class Wdl {
        Unknown,
        Win,
        Draw,
        Loss
    };

    Wdl wdl = Wdl::Unknown;
    int pieces = 0;
    bool tb_available = false;
    bool tb_used = false;
    std::string detail;
};

struct TerminationOutcome {
    bool should_end = false;
    std::string result = "*";
    TerminationReason reason = TerminationReason::ManualStop;
    std::string detail;
    bool tablebase_used = false;
};

class GameTerminator {
public:
    GameTerminator(const std::string& initial_fen,
                   const std::vector<std::string>& opening_moves,
                   const ConfigLimits& limits,
                   const TablebaseConfig& tablebases);
    ~GameTerminator();

    void ApplyMove(const std::string& move_uci);
    ProbeInfo BuildProbeInfo() const;
    TerminationOutcome ShouldEnd(const ijccrl::core::game::GameState& state,
                                 const EngineInfos& infos,
                                 const ProbeInfo& probe,
                                 bool manual_stop);

    static std::string ReasonToString(TerminationReason reason);
    static std::string TerminationTag(TerminationReason reason);

private:
    struct PositionState;
    std::unique_ptr<PositionState> position_state_;
    ConfigLimits limits_;
    TablebaseConfig tablebases_;

    int draw_score_streak_ = 0;
    int win_score_streak_white_ = 0;
    int win_score_streak_black_ = 0;
    int resign_streak_white_ = 0;
    int resign_streak_black_ = 0;
};

}  // namespace ijccrl::core::rules
