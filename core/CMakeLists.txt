add_library(ijccrlcore
    src/ijccrlcore.cpp
    src/broadcast/TlcsIniAdapter.cpp
    src/game/GameRunner.cpp
    src/pgn/PgnWriter.cpp
    src/process/Process.cpp
    src/uci/UciEngine.cpp
    src/util/AtomicFileWriter.cpp
)

target_include_directories(ijccrlcore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(ijccrlcore PRIVATE /W4)
else()
    target_compile_options(ijccrlcore PRIVATE -Wall -Wextra -Wpedantic)
endif()
