#pragma once

#include "ijccrl/core/process/Process.h"

#include <map>
#include <string>
#include <vector>

namespace ijccrl::core::uci {

class UciEngine {
public:
    enum class Failure {
        None,
        Timeout,
        EngineExited,
        WriteFailed,
        NoBestmove,
        HandshakeTimeout,
        HandshakeFailed
    };

    UciEngine(std::string name,
              std::string command,
              std::vector<std::string> args);

    void set_handshake_timeout_ms(int timeout_ms) { handshake_timeout_ms_ = timeout_ms; }

    bool Start(const std::string& working_dir);
    void Stop();

    bool UciHandshake();
    bool SetOption(const std::string& name, const std::string& value);
    bool IsReady();
    void NewGame();
    void Position(const std::string& fen, const std::vector<std::string>& moves);
    bool Go(int wtime_ms,
            int btime_ms,
            int winc_ms,
            int binc_ms,
            int movetime_ms,
            int timeout_ms,
            std::string& bestmove);

    bool IsRunning() const { return process_.IsRunning(); }
    Failure last_failure() const { return last_failure_; }
    void clear_failure() { last_failure_ = Failure::None; }
    int exit_code() const { return process_.ExitCode(); }

    const std::string& name() const { return name_; }
    const std::string& id_name() const { return id_name_; }
    const std::string& id_author() const { return id_author_; }

private:
    bool WaitForToken(const std::string& token, int timeout_ms);
    bool ReadLineWithTimeout(std::string& line, int timeout_ms);

    std::string name_;
    std::string command_;
    std::vector<std::string> args_;
    std::map<std::string, std::string> options_;
    std::map<std::string, std::string> available_options_;

    std::string id_name_;
    std::string id_author_;

    int handshake_timeout_ms_ = 10000;
    Failure last_failure_ = Failure::None;

    ijccrl::core::process::Process process_;
};

}  // namespace ijccrl::core::uci
