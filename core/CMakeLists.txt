add_library(ijccrlcore
    src/ijccrlcore.cpp
    src/broadcast/TlcsIniAdapter.cpp
)

target_include_directories(ijccrlcore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(ijccrlcore PRIVATE /W4)
else()
    target_compile_options(ijccrlcore PRIVATE -Wall -Wextra -Wpedantic)
endif()
