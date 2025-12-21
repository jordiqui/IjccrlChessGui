if(NOT IJCCRLGUI_QT)
    message(STATUS "Qt GUI disabled (IJCCRLGUI_QT=OFF)")
else()
    find_package(Qt6 COMPONENTS Widgets QUIET)
    if(NOT Qt6_FOUND)
        message(STATUS "Qt6 Widgets not found; ijccrlgui will not be built.")
    else()
        set(CMAKE_AUTOMOC ON)
        set(CMAKE_AUTOUIC ON)
        set(CMAKE_AUTORCC ON)

        add_executable(ijccrlgui
            src/main.cpp
            src/MainWindow.cpp
            src/MainWindow.h
            src/TournamentWizard.cpp
            src/TournamentWizard.h
            src/Models/EnginesModel.cpp
            src/Models/EnginesModel.h
            src/Models/StandingsModel.cpp
            src/Models/StandingsModel.h
            src/Widgets/LogView.cpp
            src/Widgets/LogView.h
            src/Widgets/LiveGamePanel.cpp
            src/Widgets/LiveGamePanel.h
        )

        target_link_libraries(ijccrlgui PRIVATE ijccrlcore Qt6::Widgets)

        target_include_directories(ijccrlgui PRIVATE
            ${CMAKE_SOURCE_DIR}/core/include
        )

        if(MSVC)
            target_compile_options(ijccrlgui PRIVATE /W4)
        else()
            target_compile_options(ijccrlgui PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endif()
endif()
