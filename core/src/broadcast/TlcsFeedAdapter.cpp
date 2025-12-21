#include "ijccrl/core/broadcast/TlcsFeedAdapter.h"

#include "ijccrl/core/util/AtomicFileWriter.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string_view>
#include <vector>

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

std::string StripQuotes(std::string_view value) {
    std::string trimmed = Trim(value);
    if (trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"') {
        return trimmed.substr(1, trimmed.size() - 2);
    }
    return trimmed;
}

}  // namespace

bool TlcsFeedAdapter::Configure(const Config& config) {
    server_ini_path_ = config.server_ini;
    feed_path_ = config.feed_path;

    if (feed_path_.empty()) {
        std::cerr << "[tlcs] TLCV feed path is not configured." << '\n';
        return false;
    }

    std::string ini_path;
    std::string ini_site;
    if (!server_ini_path_.empty()) {
        if (!ParseServerIni(server_ini_path_, ini_path, ini_site)) {
            std::cerr << "[tlcs] Failed to parse server.ini: " << server_ini_path_ << '\n';
            return false;
        }

        if (!ini_path.empty() && ini_path != feed_path_) {
            if (config.auto_write_server_ini) {
                if (!UpdateServerIniPath(server_ini_path_, feed_path_)) {
                    std::cerr << "[tlcs] Failed to update PATH in server.ini." << '\n';
                    return false;
                }
            } else {
                std::cerr << "[tlcs] PATH in server.ini does not match feed_path." << '\n';
                return false;
            }
        } else if (ini_path.empty() && config.auto_write_server_ini) {
            if (!UpdateServerIniPath(server_ini_path_, feed_path_)) {
                std::cerr << "[tlcs] Failed to write PATH in server.ini." << '\n';
                return false;
            }
        }

        site_ = ini_site;
    }

    if (!writer_.Open(feed_path_)) {
        std::cerr << "[tlcs] Failed to open feed path: " << feed_path_ << '\n';
        return false;
    }

    return true;
}

void TlcsFeedAdapter::WriteHeader(const ijccrl::core::api::RunnerConfig& cfg) {
    std::lock_guard<std::mutex> lock(mutex_);
    writer_.WriteHeader(cfg);
    writer_.Flush();
}

void TlcsFeedAdapter::OnGameStart(const GameInfo& g, const std::string& initial_fen) {
    std::lock_guard<std::mutex> lock(mutex_);
    writer_.OnGameStart(g, initial_fen);
    writer_.Flush();
}

void TlcsFeedAdapter::OnMove(const std::string& uci_move, const std::string& fen_after_move) {
    std::lock_guard<std::mutex> lock(mutex_);
    writer_.OnMove(uci_move, fen_after_move);
    writer_.Flush();
}

void TlcsFeedAdapter::OnGameEnd(const GameResult& r, const std::string& final_fen) {
    std::lock_guard<std::mutex> lock(mutex_);
    writer_.OnGameEnd(r, final_fen);
    writer_.Flush();
}

bool TlcsFeedAdapter::ParseServerIni(const std::string& config_path,
                                     std::string& path_value,
                                     std::string& site_value) {
    path_value.clear();
    site_value.clear();

    std::ifstream file(config_path);
    if (!file) {
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        const auto trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
            continue;
        }
        const auto eq_pos = trimmed.find('=');
        if (eq_pos == std::string::npos) {
            continue;
        }

        const auto key = Trim(std::string_view(trimmed).substr(0, eq_pos));
        const auto value = StripQuotes(std::string_view(trimmed).substr(eq_pos + 1));

        if (key == "PATH") {
            path_value = value;
        } else if (key == "SITE") {
            site_value = value;
        }
    }

    return true;
}

bool TlcsFeedAdapter::UpdateServerIniPath(const std::string& config_path, const std::string& feed_path) {
    std::ifstream file(config_path);
    if (!file) {
        return false;
    }

    std::vector<std::string> lines;
    std::string line;
    bool updated = false;
    while (std::getline(file, line)) {
        const auto trimmed = Trim(line);
        if (!trimmed.empty() && trimmed[0] != '#' && trimmed[0] != ';') {
            const auto eq_pos = trimmed.find('=');
            if (eq_pos != std::string::npos) {
                const auto key = Trim(std::string_view(trimmed).substr(0, eq_pos));
                if (key == "PATH") {
                    line = "PATH=" + feed_path;
                    updated = true;
                }
            }
        }
        lines.push_back(line);
    }

    if (!updated) {
        lines.push_back("PATH=" + feed_path);
    }

    std::ostringstream out;
    for (const auto& entry : lines) {
        out << entry << "\n";
    }

    return ijccrl::core::util::AtomicFileWriter::Write(config_path, out.str());
}

}  // namespace ijccrl::core::broadcast
