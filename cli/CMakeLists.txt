add_executable(ijccrlcli
    src/main.cpp
)

target_link_libraries(ijccrlcli PRIVATE ijccrlcore)

target_include_directories(ijccrlcli PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
)

if(MSVC)
    target_compile_options(ijccrlcli PRIVATE /W4)
else()
    target_compile_options(ijccrlcli PRIVATE -Wall -Wextra -Wpedantic)
endif()
