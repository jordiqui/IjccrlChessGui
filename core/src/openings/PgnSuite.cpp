#include "ijccrl/core/openings/PgnSuite.h"

#include <cctype>
#include <fstream>
#include <functional>
#include <map>
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

std::string HashText(const std::string& text) {
    const std::hash<std::string> hasher;
    return std::to_string(hasher(text));
}

bool IsResultToken(const std::string& token) {
    return token == "1-0" || token == "0-1" || token == "1/2-1/2" || token == "*";
}

std::vector<std::string> ParseMoves(const std::string& movetext) {
    std::vector<std::string> moves;
    auto tokens = SplitTokens(movetext);
    for (auto& token : tokens) {
        if (IsResultToken(token)) {
            continue;
        }
        if (token.find('.') != std::string::npos) {
            continue;
        }
        moves.push_back(token);
    }
    return moves;
}

}  // namespace

std::vector<Opening> PgnSuite::LoadFile(const std::string& path) {
    std::vector<Opening> openings;
    std::ifstream file(path);
    if (!file) {
        return openings;
    }

    std::map<std::string, std::string> tags;
    std::ostringstream movetext;
    std::string line;

    auto flush_game = [&]() {
        if (tags.empty() && movetext.str().empty()) {
            return;
        }
        Opening opening;
        const auto setup_it = tags.find("SetUp");
        const auto fen_it = tags.find("FEN");
        if (setup_it != tags.end() && setup_it->second == "1" && fen_it != tags.end()) {
            opening.fen = NormalizeFen(fen_it->second);
        }
        opening.moves = ParseMoves(movetext.str());
        const auto event_it = tags.find("Event");
        const auto round_it = tags.find("Round");
        if (event_it != tags.end() && round_it != tags.end()) {
            opening.id = event_it->second + " " + round_it->second;
        }
        if (opening.id.empty()) {
            opening.id = HashText(movetext.str());
        }
        openings.push_back(std::move(opening));
        tags.clear();
        movetext.str(std::string());
        movetext.clear();
    };

    while (std::getline(file, line)) {
        line = Trim(line);
        if (line.empty()) {
            if (!movetext.str().empty()) {
                flush_game();
            }
            continue;
        }
        if (!line.empty() && line.front() == '[') {
            const auto space_pos = line.find(' ');
            const auto quote_pos = line.find('"');
            const auto last_quote = line.find_last_of('"');
            if (space_pos != std::string::npos && quote_pos != std::string::npos &&
                last_quote != std::string::npos && last_quote > quote_pos) {
                const std::string key = line.substr(1, space_pos - 1);
                const std::string value = line.substr(quote_pos + 1, last_quote - quote_pos - 1);
                tags[key] = value;
            }
            continue;
        }
        movetext << line << ' ';
    }

    flush_game();
    return openings;
}

}  // namespace ijccrl::core::openings
