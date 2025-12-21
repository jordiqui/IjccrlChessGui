#pragma once

#include "ijccrl/core/api/RunnerConfig.h"

#include <string>
#include <vector>

namespace ijccrl::core::broadcast {

struct GameInfo {
    std::string white;
    std::string black;
    std::string event;
    std::string site;
    std::string round;
};

struct GameResult {
    std::string result;
    std::string termination;
};

class TlcsFeedWriter {
public:
    bool Open(const std::string& feed_path);

    void WriteHeader(const ijccrl::core::api::RunnerConfig& cfg);
    void OnGameStart(const GameInfo& g, const std::string& initial_fen);
    void OnMove(const std::string& uci_move, const std::string& fen_after_move);
    void OnGameEnd(const GameResult& r, const std::string& final_fen);
    void Flush();

    const std::string& feed_path() const { return feed_path_; }

private:
    struct FenParts {
        std::string board;
        std::string stm;
        std::string castling;
        std::string ep;
        int halfmove = 0;
        int fullmove = 1;
    };

    static std::string StartposFen();
    static std::string FormatFenPrefix(const FenParts& parts);
    static bool ParseFen(const std::string& fen, FenParts& parts);

    void AppendLine(const std::string& line);

    std::string feed_path_;
    std::vector<std::string> lines_;
    int halfmove_index_ = 0;
    int fmr_ = 0;
    bool open_ = false;
};

}  // namespace ijccrl::core::broadcast
