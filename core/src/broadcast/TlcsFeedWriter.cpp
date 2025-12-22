#include "ijccrl/core/broadcast/TlcsFeedWriter.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace ijccrl::core::broadcast {

bool TlcsFeedWriter::Open(const std::string& feed_path) {
    feed_path_ = feed_path;
    halfmove_index_ = 0;
    fmr_ = 0;
    open_ = !feed_path_.empty();

    if (!open_) {
        return false;
    }

    const std::filesystem::path path(feed_path_);
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }
    return true;
}

void TlcsFeedWriter::WriteHeader(const ijccrl::core::api::RunnerConfig& cfg) {
    (void)cfg;
}

void TlcsFeedWriter::OnGameStart(const GameInfo& g, const std::string& initial_fen) {
    if (!open_) {
        return;
    }

    ResetFeedFile();

    std::string fen_value = initial_fen.empty() ? StartposFen() : initial_fen;
    FenParts parts;
    if (!ParseFen(fen_value, parts)) {
        parts = {};
        ParseFen(StartposFen(), parts);
    }

    halfmove_index_ = std::max(0, (parts.fullmove - 1) * 2 + (parts.stm == "b" ? 1 : 0));
    fmr_ = parts.halfmove;

    if (!g.site.empty()) {
        AppendLine("SITE " + g.site);
    } else if (!g.event.empty()) {
        AppendLine("SITE " + g.event);
    }
    AppendLine("WPLAYER " + g.white);
    AppendLine("BPLAYER " + g.black);
    AppendLine("FMR " + std::to_string(fmr_));
    AppendLine("FEN " + FormatFenPrefix(parts));
}

void TlcsFeedWriter::OnMove(const std::string& uci_move, const std::string& fen_after_move) {
    if (!open_) {
        return;
    }

    const bool white_to_move = (halfmove_index_ % 2 == 0);
    const int move_number = halfmove_index_ / 2 + 1;
    const std::string move_label =
        white_to_move ? (std::to_string(move_number) + ".") : (std::to_string(move_number) + "...");
    const std::string command = white_to_move ? "WMOVE " : "BMOVE ";
    AppendLine(command + move_label + " " + uci_move);
    halfmove_index_ += 1;

    FenParts parts;
    if (ParseFen(fen_after_move, parts)) {
        fmr_ = parts.halfmove;
        AppendLine("FMR " + std::to_string(fmr_));
        AppendLine("FEN " + FormatFenPrefix(parts));
    }
}

void TlcsFeedWriter::OnGameEnd(const GameResult& r, const std::string& final_fen) {
    if (!open_) {
        return;
    }

    FenParts parts;
    if (ParseFen(final_fen, parts)) {
        fmr_ = parts.halfmove;
        AppendLine("FMR " + std::to_string(fmr_));
        AppendLine("FEN " + FormatFenPrefix(parts));
    }

    if (!r.result.empty()) {
        AppendLine("result " + r.result);
    }
}

void TlcsFeedWriter::Flush() {
    if (!open_) {
        return;
    }
}

std::string TlcsFeedWriter::StartposFen() {
    return "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
}

std::string TlcsFeedWriter::FormatFenPrefix(const FenParts& parts) {
    return parts.board + ' ' + parts.stm + ' ' + parts.castling + ' ' + parts.ep;
}

bool TlcsFeedWriter::ParseFen(const std::string& fen, FenParts& parts) {
    std::istringstream iss(fen);
    if (!(iss >> parts.board >> parts.stm)) {
        return false;
    }
    if (!(iss >> parts.castling)) {
        parts.castling = "-";
    }
    if (!(iss >> parts.ep)) {
        parts.ep = "-";
    }
    if (!(iss >> parts.halfmove)) {
        parts.halfmove = 0;
    }
    if (!(iss >> parts.fullmove)) {
        parts.fullmove = 1;
    }
    return true;
}

void TlcsFeedWriter::AppendLine(const std::string& line) {
    std::ofstream out(feed_path_, std::ios::binary | std::ios::app);
    if (!out) {
        return;
    }
    out << line << "\r\n";
    out.flush();
    LogAppend(line);
}

void TlcsFeedWriter::ResetFeedFile() {
    std::ofstream out(feed_path_, std::ios::binary | std::ios::trunc);
    if (!out) {
        return;
    }
    out.flush();
}

void TlcsFeedWriter::LogAppend(const std::string& line) const {
    std::error_code ec;
    const auto size = std::filesystem::file_size(feed_path_, ec);
    const auto reported_size = ec ? 0U : size;
    std::cout << "[tlcs] Append: " << line << "\\r\\n"
              << " (feed_size=" << reported_size << ")" << '\n';
}

}  // namespace ijccrl::core::broadcast
