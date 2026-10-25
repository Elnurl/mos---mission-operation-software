cmake_minimum_required(VERSION 3.21)
project(mos VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Sql Network Test)

option(MOS_BUILD_TESTS "Build unit tests" ON)

add_subdirectory(src/persist)
add_subdirectory(src/bridge)
add_subdirectory(src/adapters)
add_subdirectory(src/core)
add_subdirectory(src/app)

if(MOS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
