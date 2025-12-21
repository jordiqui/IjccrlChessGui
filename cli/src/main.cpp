#include "ijccrl/core/broadcast/TlcsIniAdapter.h"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: ijccrlcli <config.json>" << '\n';
        return 1;
    }

    const std::string config_path = argv[1];
    std::cout << "[ijccrlcli] Runner config: " << config_path << '\n';
    std::cout << "[ijccrlcli] TODO: load JSON config, start tournament, and broadcast." << '\n';

    ijccrl::core::broadcast::TlcsIniAdapter adapter;
    std::cout << "[ijccrlcli] TLCS adapter ready (configure via broadcast.server_ini)." << '\n';

    return 0;
}
