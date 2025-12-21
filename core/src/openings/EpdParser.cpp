#include "ijccrl/core/openings/EpdParser.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <sstream>

namespace ijccrl::core::openings {

namespace {

std::string Trim(const std::string& value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

std::vector<std::string> SplitTokens(const std::string& value) {
    std::istringstream iss(value);
    std::vector<std::string> tokens;
    std::string token;
    while (iss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

std::string NormalizeFen(const std::string& fen) {
    auto tokens = SplitTokens(fen);
    if (tokens.size() == 4) {
        tokens.push_back("0");
        tokens.push_back("1");
    } else if (tokens.size() == 5) {
        tokens.push_back("1");
    } else if (tokens.size() > 6) {
        tokens.resize(6);
    }
    std::ostringstream out;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i > 0) {
            out << ' ';
        }
        out << tokens[i];
    }
    return out.str();
}

std::string HashLine(const std::string& line) {
    const std::hash<std::string> hasher;
    return std::to_string(hasher(line));
}

void ParseOperations(const std::string& line, Opening& opening) {
    size_t start = line.find(';');
    if (start == std::string::npos) {
        return;
    }
    while (start != std::string::npos) {
        size_t end = line.find(';', start + 1);
        std::string op = (end == std::string::npos) ? line.substr(start + 1)
                                                    : line.substr(start + 1, end - start - 1);
        op = Trim(op);
        if (op.rfind("id", 0) == 0) {
            const auto first_quote = op.find('"');
            const auto last_quote = op.find_last_of('"');
            if (first_quote != std::string::npos && last_quote != std::string::npos &&
                last_quote > first_quote) {
                opening.id = op.substr(first_quote + 1, last_quote - first_quote - 1);
            }
        } else if (op.rfind("moves", 0) == 0) {
            auto tokens = SplitTokens(op.substr(5));
            opening.moves.insert(opening.moves.end(), tokens.begin(), tokens.end());
        }
        start = end;
    }
}

}  // namespace

std::vector<Opening> EpdParser::LoadFile(const std::string& path) {
    std::vector<Opening> openings;
    std::ifstream file(path);
    if (!file) {
        return openings;
    }

    std::string line;
    while (std::getline(file, line)) {
        line = Trim(line);
        if (line.empty()) {
            continue;
        }
        if (line[0] == '#' || line[0] == ';') {
            continue;
        }

        const size_t sep = line.find(';');
        const std::string fen_part = (sep == std::string::npos) ? line : line.substr(0, sep);
        const auto fen_tokens = SplitTokens(fen_part);
        if (fen_tokens.size() < 4) {
            continue;
        }

        std::ostringstream fen_out;
        const size_t fen_fields = std::min(static_cast<size_t>(6), fen_tokens.size());
        for (size_t i = 0; i < fen_fields; ++i) {
            if (i > 0) {
                fen_out << ' ';
            }
            fen_out << fen_tokens[i];
        }

        Opening opening;
        opening.fen = NormalizeFen(fen_out.str());
        ParseOperations(line, opening);
        if (opening.id.empty()) {
            opening.id = HashLine(line);
        }
        openings.push_back(std::move(opening));
    }

    return openings;
}

}  // namespace ijccrl::core::openings
