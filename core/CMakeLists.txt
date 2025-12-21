add_library(ijccrlcore
    src/ijccrlcore.cpp
    src/api/RunnerConfig.cpp
    src/api/RunnerService.cpp
    src/broadcast/TlcsFeedAdapter.cpp
    src/broadcast/TlcsFeedWriter.cpp
    src/broadcast/TlcsIniAdapter.cpp
    src/export/ExportWriter.cpp
    src/game/GameRunner.cpp
    src/openings/EpdParser.cpp
    src/openings/OpeningPolicy.cpp
    src/openings/PgnSuite.cpp
    src/persist/CheckpointState.cpp
    src/pgn/PgnWriter.cpp
    src/process/Process.cpp
    src/runtime/EnginePool.cpp
    src/runtime/MatchRunner.cpp
    src/rules/Termination.cpp
    src/stats/StandingsTable.cpp
    src/tournament/RoundRobinScheduler.cpp
    src/tournament/SwissScheduler.cpp
    src/uci/UciEngine.cpp
    src/util/AtomicFileWriter.cpp
)

target_include_directories(ijccrlcore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party
)

if(MSVC)
    target_compile_options(ijccrlcore PRIVATE /W4)
else()
    target_compile_options(ijccrlcore PRIVATE -Wall -Wextra -Wpedantic)
endif()
