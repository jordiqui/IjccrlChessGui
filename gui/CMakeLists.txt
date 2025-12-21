add_executable(ijccrlgui
    src/main.cpp
)

target_link_libraries(ijccrlgui PRIVATE ijccrlcore)

target_include_directories(ijccrlgui PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
)

if(MSVC)
    target_compile_options(ijccrlgui PRIVATE /W4)
else()
    target_compile_options(ijccrlgui PRIVATE -Wall -Wextra -Wpedantic)
endif()
