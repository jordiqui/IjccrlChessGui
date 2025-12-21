#pragma once

#include "ijccrl/core/stats/StandingsTable.h"

#include <string>
#include <vector>

namespace ijccrl::core::exporter {

bool WriteStandingsCsv(const std::string& path, const std::vector<ijccrl::core::stats::EngineStats>& standings);
bool WriteStandingsHtml(const std::string& path,
                        const std::string& event_name,
                        const std::vector<ijccrl::core::stats::EngineStats>& standings);
bool WriteSummaryJson(const std::string& path,
                      const std::string& event_name,
                      const std::string& tc_desc,
                      const std::string& mode,
                      int total_games,
                      const std::vector<ijccrl::core::stats::EngineStats>& standings);

}  // namespace ijccrl::core::exporter
