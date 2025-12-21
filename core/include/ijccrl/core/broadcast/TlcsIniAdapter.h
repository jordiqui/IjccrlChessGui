#pragma once

#include "ijccrl/core/broadcast/IBroadcastAdapter.h"

#include <string>

namespace ijccrl::core::broadcast {

class TlcsIniAdapter final : public IBroadcastAdapter {
public:
    bool Configure(const std::string& config_path) override;
    bool PublishLivePgn(const std::string& pgn) override;

    const std::string& live_pgn_path() const { return live_pgn_path_; }
    const std::string& server_ini_path() const { return server_ini_path_; }
    const std::string& site() const { return site_; }

private:
    bool WriteAtomically(const std::string& pgn) const;
    bool ParseServerIni(const std::string& config_path);

    std::string server_ini_path_{};
    std::string live_pgn_path_{};
    std::string site_{};
    int port_ = 0;
    int ics_mode_ = 0;
    bool save_debug_ = false;
};

}  // namespace ijccrl::core::broadcast
