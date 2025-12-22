#include "ijccrl/core/broadcast/TlcsFeedWriter.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace ijccrl::core::broadcast {

namespace {

bool WriteFileContents(const std::string& path, const std::string& contents, bool append) {
#ifdef _WIN32
    const std::filesystem::path file_path(path);
    const DWORD disposition = append ? OPEN_ALWAYS : CREATE_ALWAYS;
    HANDLE file = CreateFileW(file_path.wstring().c_str(),
                              GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr,
                              disposition,
                              FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    if (append) {
        SetFilePointer(file, 0, nullptr, FILE_END);
    }
    bool ok = true;
    if (!contents.empty()) {
        DWORD written = 0;
        ok = WriteFile(file,
                       contents.data(),
                       static_cast<DWORD>(contents.size()),
                       &written,
                       nullptr) != 0;
    }
    FlushFileBuffers(file);
    CloseHandle(file);
    return ok;
#else
    std::ofstream out(path, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
    if (!out) {
        return false;
    }
    out << contents;
    out.flush();
    return true;
#endif
}

}  // namespace

bool TlcsFeedWriter::Open(const std::string& feed_path, Format format) {
    feed_path_ = feed_path;
    halfmove_index_ = 0;
    fmr_ = 0;
    lines_.clear();
    last_fen_.clear();
    open_ = !feed_path_.empty();
    format_ = format;

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

    std::string fen_value = initial_fen.empty() ? StartposFen() : initial_fen;
    last_fen_ = fen_value;
    if (format_ == Format::WinboardDebug) {
        AppendWinboardFen(fen_value);
        return;
    }

    ResetFeedFile();
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

    if (format_ == Format::WinboardDebug) {
        last_fen_ = fen_after_move;
        if (!last_fen_.empty()) {
            AppendWinboardFen(last_fen_);
        }
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
        last_fen_ = fen_after_move;
        fmr_ = parts.halfmove;
        AppendLine("FMR " + std::to_string(fmr_));
        AppendLine("FEN " + FormatFenPrefix(parts));
    }
}

void TlcsFeedWriter::OnGameEnd(const GameResult& r, const std::string& final_fen) {
    if (!open_) {
        return;
    }

    if (format_ == Format::WinboardDebug) {
        last_fen_ = final_fen;
        if (!last_fen_.empty()) {
            AppendWinboardFen(last_fen_);
        }
        return;
    }

    FenParts parts;
    if (ParseFen(final_fen, parts)) {
        last_fen_ = final_fen;
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
    lines_.push_back(line);
    WriteSnapshot();
}

void TlcsFeedWriter::ResetFeedFile() {
    lines_.clear();
    WriteSnapshot();
}

void TlcsFeedWriter::AppendWinboardFen(const std::string& fen) {
    const std::string line = "FEN : " + fen + "\r\n";
    if (!WriteFileContents(feed_path_, line, true)) {
        return;
    }
    LogWrite(line.size());
}

void TlcsFeedWriter::WriteSnapshot() {
    if (format_ != Format::Tlcv) {
        return;
    }

    std::size_t bytes_written = 0;
    std::ostringstream buffer;
    for (const auto& entry : lines_) {
        buffer << entry << "\r\n";
        bytes_written += entry.size() + 2;
    }
    if (!WriteFileContents(feed_path_, buffer.str(), false)) {
        return;
    }
    LogWrite(bytes_written);
}

void TlcsFeedWriter::LogWrite(std::size_t bytes_written) const {
    std::error_code ec;
    const auto size = std::filesystem::file_size(feed_path_, ec);
    const auto reported_size = ec ? 0U : size;
    std::cout << "[tlcs] Write bytes=" << bytes_written << " feed_size=" << reported_size
              << " last_fen=\"" << last_fen_ << "\"" << '\n';
}

}  // namespace ijccrl::core::broadcast
