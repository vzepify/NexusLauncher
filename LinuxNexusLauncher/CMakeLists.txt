cmake_minimum_required(VERSION 3.21)
project(NexusLauncherLinux VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Network)

qt_add_executable(NexusLauncher
    src/main.cpp
    src/mainwindow.h
    src/mainwindow.cpp
    src/discordpresence.h
    src/discordpresence.cpp
    src/updater.h
    src/updater.cpp
    resources/resources.qrc
)

target_link_libraries(NexusLauncher PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network
)

if(UNIX AND NOT APPLE)
    target_compile_definitions(NexusLauncher PRIVATE NEXUS_LINUX=1)
    set_target_properties(NexusLauncher PROPERTIES
        OUTPUT_NAME "NexusLauncher"
        BUILD_RPATH "$ORIGIN"
    )
endif()

install(TARGETS NexusLauncher RUNTIME DESTINATION bin)
