#include "ijccrl/core/broadcast/TlcsIniAdapter.h"

#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <windows.h>
#endif

namespace ijccrl::core::broadcast {

namespace {

std::string Trim(std::string_view value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(start, end - start + 1));
}

}  // namespace

bool TlcsIniAdapter::Configure(const std::string& config_path) {
    if (!ParseServerIni(config_path)) {
        std::cerr << "[tlcs] Failed to parse server.ini: " << config_path << '\n';
        return false;
    }

    std::cout << "[tlcs] Configured TLCS adapter" << '\n'
              << "  server.ini: " << server_ini_path_ << '\n'
              << "  TOURNEYPGN: " << live_pgn_path_ << '\n'
              << "  SITE: " << site_ << '\n'
              << "  PORT: " << port_ << '\n'
              << "  ICSMODE: " << ics_mode_ << '\n'
              << "  SAVEDEBUG: " << (save_debug_ ? "1" : "0") << '\n';

    return true;
}

bool TlcsIniAdapter::PublishLivePgn(const std::string& pgn) {
    if (live_pgn_path_.empty()) {
        std::cerr << "[tlcs] TOURNEYPGN path is not configured." << '\n';
        return false;
    }

    return WriteAtomically(pgn);
}

bool TlcsIniAdapter::ParseServerIni(const std::string& config_path) {
    server_ini_path_ = config_path;

    std::ifstream file(config_path);
    if (!file) {
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        const auto eq_pos = line.find('=');
        if (eq_pos == std::string::npos) {
            continue;
        }

        const auto key = Trim(std::string_view(line).substr(0, eq_pos));
        const auto value = Trim(std::string_view(line).substr(eq_pos + 1));

        if (key == "TOURNEYPGN") {
            live_pgn_path_ = value;
        } else if (key == "SITE") {
            site_ = value;
        } else if (key == "PORT") {
            port_ = std::stoi(value);
        } else if (key == "ICSMODE") {
            ics_mode_ = std::stoi(value);
        } else if (key == "SAVEDEBUG") {
            save_debug_ = (value == "1");
        }
    }

    return !live_pgn_path_.empty();
}

bool TlcsIniAdapter::WriteAtomically(const std::string& pgn) const {
    const std::string temp_path = live_pgn_path_ + ".tmp";

    std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        std::cerr << "[tlcs] Failed to open temp file: " << temp_path << '\n';
        return false;
    }

    output.write(pgn.data(), static_cast<std::streamsize>(pgn.size()));
    output.flush();
    if (!output) {
        std::cerr << "[tlcs] Failed to write temp PGN: " << temp_path << '\n';
        return false;
    }

#ifdef _WIN32
    const BOOL moved = MoveFileExA(
        temp_path.c_str(),
        live_pgn_path_.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!moved) {
        std::cerr << "[tlcs] MoveFileEx failed for " << live_pgn_path_ << '\n';
        return false;
    }
#else
    std::remove(live_pgn_path_.c_str());
    if (std::rename(temp_path.c_str(), live_pgn_path_.c_str()) != 0) {
        std::cerr << "[tlcs] rename failed for " << live_pgn_path_ << '\n';
        return false;
    }
#endif

    return true;
}

}  // namespace ijccrl::core::broadcast
