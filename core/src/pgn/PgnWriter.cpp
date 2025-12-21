#include "ijccrl/core/pgn/PgnWriter.h"

#include <sstream>

namespace ijccrl::core::pgn {

std::string PgnWriter::Render(const PgnGame& game) {
    std::ostringstream out;
    for (const auto& tag : game.tags) {
        out << '[' << tag.key << " \"" << tag.value << "\"]\n";
    }
    out << "\n";

    for (std::size_t i = 0; i < game.moves.size(); ++i) {
        if (i % 2 == 0) {
            out << (i / 2 + 1) << ". ";
        }
        out << game.moves[i] << ' ';
    }
    if (!game.termination_comment.empty()) {
        out << '{' << game.termination_comment << "} ";
    }
    out << game.result;
    out << "\n";
    return out.str();
}

}  // namespace ijccrl::core::pgn
