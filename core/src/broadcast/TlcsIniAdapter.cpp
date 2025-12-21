#include "ijccrl/core/broadcast/TlcsIniAdapter.h"

#include "ijccrl/core/util/AtomicFileWriter.h"

#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

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

    std::cout << "[tlcs] TLCS ini loaded: TOURNEYPGN=\"" << live_pgn_path_ << "\" "
              << "SITE=\"" << site_ << "\" "
              << "PORT=" << port_ << " "
              << "ICSMODE=" << ics_mode_ << '\n';

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
        const auto trimmed = Trim(line);
        if (trimmed.empty()) {
            continue;
        }
        if (trimmed[0] == '#' || trimmed[0] == ';') {
            continue;
        }
        const auto eq_pos = trimmed.find('=');
        if (eq_pos == std::string::npos) {
            continue;
        }

        const auto key = Trim(std::string_view(trimmed).substr(0, eq_pos));
        const auto value = Trim(std::string_view(trimmed).substr(eq_pos + 1));

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
    return ijccrl::core::util::AtomicFileWriter::Write(live_pgn_path_, pgn);
}

}  // namespace ijccrl::core::broadcast
