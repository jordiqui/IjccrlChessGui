#pragma once

#include "ijccrl/core/broadcast/TlcsFeedWriter.h"

#include <mutex>
#include <string>

namespace ijccrl::core::broadcast {

class TlcsFeedAdapter {
public:
    struct Config {
        std::string server_ini;
        std::string feed_path;
        bool auto_write_server_ini = false;
        bool force_update_path = true;
    };

    bool Configure(const Config& config);

    void WriteHeader(const ijccrl::core::api::RunnerConfig& cfg);
    void OnGameStart(const GameInfo& g, const std::string& initial_fen);
    void OnMove(const std::string& uci_move, const std::string& fen_after_move);
    void OnGameEnd(const GameResult& r, const std::string& final_fen);

    const std::string& site() const { return site_; }

private:
    bool ParseServerIni(const std::string& config_path, std::string& path_value, std::string& site_value);
    bool UpdateServerIniPath(const std::string& config_path, const std::string& feed_path);

    std::mutex mutex_{};
    TlcsFeedWriter writer_{};
    std::string server_ini_path_{};
    std::string feed_path_{};
    std::string site_{};
};

}  // namespace ijccrl::core::broadcast
