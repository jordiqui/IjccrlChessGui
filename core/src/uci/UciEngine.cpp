#include "ijccrl/core/uci/UciEngine.h"

#include <chrono>
#include <iostream>
#include <sstream>

namespace ijccrl::core::uci {

UciEngine::UciEngine(std::string name,
                     std::string command,
                     std::vector<std::string> args)
    : name_(std::move(name)),
      command_(std::move(command)),
      args_(std::move(args)) {}

bool UciEngine::Start(const std::string& working_dir) {
    return process_.Start(command_, args_, working_dir);
}

void UciEngine::Stop() {
    process_.WriteLine("quit");
    if (!process_.WaitForExit(500)) {
        process_.Terminate();
    }
}

bool UciEngine::UciHandshake() {
    last_failure_ = Failure::None;
    if (!process_.WriteLine("uci")) {
        last_failure_ = Failure::WriteFailed;
        return false;
    }

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(handshake_timeout_ms_);

    while (std::chrono::steady_clock::now() < deadline) {
        std::string line;
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   deadline - std::chrono::steady_clock::now())
                                   .count();
        if (!ReadLineWithTimeout(line, static_cast<int>(remaining))) {
            if (!process_.IsRunning()) {
                last_failure_ = Failure::EngineExited;
                return false;
            }
            continue;
        }

        if (line.rfind("id name ", 0) == 0) {
            id_name_ = line.substr(8);
        } else if (line.rfind("id author ", 0) == 0) {
            id_author_ = line.substr(10);
        } else if (line.rfind("option ", 0) == 0) {
            const auto name_pos = line.find("name ");
            if (name_pos != std::string::npos) {
                auto name_start = name_pos + 5;
                auto name_end = line.find(" type", name_start);
                if (name_end == std::string::npos) {
                    name_end = line.size();
                }
                const std::string option_name = line.substr(name_start, name_end - name_start);
                available_options_.emplace(option_name, line);
            }
        }

        if (line == "uciok") {
            return true;
        }
    }

    last_failure_ = Failure::HandshakeTimeout;
    return false;
}

bool UciEngine::SetOption(const std::string& name, const std::string& value) {
    std::ostringstream command;
    command << "setoption name " << name;
    if (!value.empty()) {
        command << " value " << value;
    }
    options_[name] = value;
    return process_.WriteLine(command.str());
}

bool UciEngine::IsReady() {
    last_failure_ = Failure::None;
    if (!process_.WriteLine("isready")) {
        last_failure_ = Failure::WriteFailed;
        return false;
    }
    const bool ok = WaitForToken("readyok", handshake_timeout_ms_);
    if (!ok && !process_.IsRunning()) {
        last_failure_ = Failure::EngineExited;
    }
    return ok;
}

void UciEngine::NewGame() {
    process_.WriteLine("ucinewgame");
}

void UciEngine::Position(const std::string& fen, const std::vector<std::string>& moves) {
    std::ostringstream command;
    if (fen.empty()) {
        command << "position startpos";
    } else {
        command << "position fen " << fen;
    }
    if (!moves.empty()) {
        command << " moves";
        for (const auto& move : moves) {
            command << ' ' << move;
        }
    }
    process_.WriteLine(command.str());
}

bool UciEngine::Go(int wtime_ms,
                   int btime_ms,
                   int winc_ms,
                   int binc_ms,
                   int movetime_ms,
                   int timeout_ms,
                   std::string& bestmove) {
    last_failure_ = Failure::None;
    std::ostringstream command;
    command << "go";
    command << " wtime " << wtime_ms;
    command << " btime " << btime_ms;
    command << " winc " << winc_ms;
    command << " binc " << binc_ms;
    if (movetime_ms > 0) {
        command << " movetime " << movetime_ms;
    }

    if (!process_.WriteLine(command.str())) {
        last_failure_ = Failure::WriteFailed;
        return false;
    }

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
        std::string line;
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   deadline - std::chrono::steady_clock::now())
                                   .count();
        if (!ReadLineWithTimeout(line, static_cast<int>(remaining))) {
            if (!process_.IsRunning()) {
                last_failure_ = Failure::EngineExited;
                return false;
            }
            continue;
        }

        if (line.rfind("bestmove ", 0) == 0) {
            std::istringstream iss(line);
            std::string token;
            iss >> token;  // bestmove
            iss >> bestmove;
            if (bestmove == "(none)") {
                bestmove.clear();
                last_failure_ = Failure::NoBestmove;
                return false;
            }
            return true;
        }
    }

    bestmove.clear();
    last_failure_ = Failure::Timeout;
    return false;
}

bool UciEngine::WaitForToken(const std::string& token, int timeout_ms) {
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        std::string line;
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   deadline - std::chrono::steady_clock::now())
                                   .count();
        if (!ReadLineWithTimeout(line, static_cast<int>(remaining))) {
            continue;
        }
        if (line == token) {
            return true;
        }
    }
    return false;
}

bool UciEngine::ReadLineWithTimeout(std::string& line, int timeout_ms) {
    return process_.ReadLineBlocking(line, timeout_ms);
}

}  // namespace ijccrl::core::uci
