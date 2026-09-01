cmake_minimum_required(VERSION 3.16)

project(macos-dock-qt6 VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Quick
    Qml
    QuickControls2
    DBus
)

find_package(LayerShellQt REQUIRED)

qt_add_executable(macos-dock-qt6
    src/main.cpp
    src/app_item.h
    src/app_item.cpp
    src/dock_manager.h
    src/dock_manager.cpp
    resources.qrc
)

target_link_libraries(macos-dock-qt6 PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Qml
    Qt6::QuickControls2
    Qt6::DBus
    LayerShellQt::Interface
)

target_include_directories(macos-dock-qt6 PRIVATE src)

install(TARGETS macos-dock-qt6 DESTINATION bin)
