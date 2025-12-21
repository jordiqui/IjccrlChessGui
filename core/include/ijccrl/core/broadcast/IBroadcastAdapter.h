#pragma once

#include <string>

namespace ijccrl::core::broadcast {

class IBroadcastAdapter {
public:
    virtual ~IBroadcastAdapter() = default;
    virtual bool Configure(const std::string& config_path) = 0;
    virtual bool PublishLivePgn(const std::string& pgn) = 0;
};

}  // namespace ijccrl::core::broadcast
