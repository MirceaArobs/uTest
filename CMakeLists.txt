cmake_minimum_required(VERSION 3.16)
project(uTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 COMPONENTS Widgets QUIET)

if (Qt6_FOUND)
    set(QT_WIDGETS_LIB Qt6::Widgets)
else()
    find_package(Qt5 REQUIRED COMPONENTS Widgets)
    set(QT_WIDGETS_LIB Qt5::Widgets)
endif()

add_executable(uTest main.cpp)
target_link_libraries(uTest PRIVATE ${QT_WIDGETS_LIB})

