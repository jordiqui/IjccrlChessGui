cmake_minimum_required(VERSION 3.16)

project(IjccrlChessGui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(IJCCRLGUI_QT "Build Qt GUI" ON)

add_subdirectory(core)
add_subdirectory(cli)
if(IJCCRLGUI_QT)
    add_subdirectory(gui)
endif()
