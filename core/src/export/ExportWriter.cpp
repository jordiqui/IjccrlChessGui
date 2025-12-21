#include "ijccrl/core/export/ExportWriter.h"

#include "ijccrl/core/util/AtomicFileWriter.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace ijccrl::core::exporter {

namespace {

bool EnsureParentDir(const std::string& path) {
    const std::filesystem::path fs_path(path);
    if (!fs_path.parent_path().empty()) {
        std::filesystem::create_directories(fs_path.parent_path());
    }
    return true;
}

std::vector<ijccrl::core::stats::EngineStats> SortedByPoints(
    const std::vector<ijccrl::core::stats::EngineStats>& standings) {
    auto sorted = standings;
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        if (a.points != b.points) {
            return a.points > b.points;
        }
        return a.score_percent() > b.score_percent();
    });
    return sorted;
}

}  // namespace

bool WriteStandingsCsv(const std::string& path, const std::vector<ijccrl::core::stats::EngineStats>& standings) {
    EnsureParentDir(path);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output << "rank,name,pts,g,w,d,l,score_percent\n";
    const auto sorted = SortedByPoints(standings);
    int rank = 1;
    for (const auto& row : sorted) {
        output << rank++ << ','
               << row.name << ','
               << row.points << ','
               << row.games << ','
               << row.wins << ','
               << row.draws << ','
               << row.losses << ','
               << row.score_percent()
               << "\n";
    }
    return true;
}

bool WriteStandingsHtml(const std::string& path,
                        const std::string& event_name,
                        const std::vector<ijccrl::core::stats::EngineStats>& standings) {
    EnsureParentDir(path);
    std::ostringstream html;
    html << "<!doctype html>\n<html><head><meta charset=\"utf-8\">"
         << "<title>Standings</title>"
         << "<style>table{border-collapse:collapse;font-family:Arial,sans-serif}"
         << "th,td{border:1px solid #ccc;padding:4px 8px;text-align:left}</style>"
         << "</head><body>\n";
    html << "<h2>" << event_name << "</h2>\n";
    html << "<table>\n<thead><tr>"
         << "<th>Rank</th><th>Name</th><th>Pts</th><th>G</th><th>W</th><th>D</th><th>L</th><th>Score%</th>"
         << "</tr></thead>\n<tbody>\n";
    const auto sorted = SortedByPoints(standings);
    int rank = 1;
    for (const auto& row : sorted) {
        html << "<tr><td>" << rank++ << "</td><td>" << row.name << "</td><td>"
             << row.points << "</td><td>" << row.games << "</td><td>"
             << row.wins << "</td><td>" << row.draws << "</td><td>"
             << row.losses << "</td><td>" << row.score_percent() << "</td></tr>\n";
    }
    html << "</tbody></table>\n</body></html>\n";
    return ijccrl::core::util::AtomicFileWriter::Write(path, html.str());
}

bool WriteSummaryJson(const std::string& path,
                      const std::string& event_name,
                      const std::string& tc_desc,
                      const std::string& mode,
                      int total_games,
                      const std::vector<ijccrl::core::stats::EngineStats>& standings) {
    EnsureParentDir(path);
    nlohmann::json summary;
    summary["event"] = event_name;
    summary["tc"] = tc_desc;
    summary["mode"] = mode;
    summary["total_games"] = total_games;
    summary["top10"] = nlohmann::json::array();
    const auto sorted = SortedByPoints(standings);
    const size_t limit = std::min<size_t>(10, sorted.size());
    for (size_t i = 0; i < limit; ++i) {
        const auto& row = sorted[i];
        summary["top10"].push_back({
            {"rank", static_cast<int>(i + 1)},
            {"name", row.name},
            {"pts", row.points},
            {"g", row.games},
            {"w", row.wins},
            {"d", row.draws},
            {"l", row.losses},
            {"score_percent", row.score_percent()},
        });
    }
    return ijccrl::core::util::AtomicFileWriter::Write(path, summary.dump(2));
}

}  // namespace ijccrl::core::exporter
