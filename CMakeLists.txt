cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

project(AutoDodge VERSION 2.0.0)

set(CPM_SOURCE_CACHE "${CMAKE_SOURCE_DIR}/.cpm-cache")

if (NOT DEFINED ENV{GEODE_SDK})
    include(cmake/CPM.cmake)
    CPMAddPackage("gh:geode-sdk/geode#v4.20.0")
else()
    set(GEODE_SDK $ENV{GEODE_SDK})
    list(APPEND CMAKE_MODULE_PATH "${GEODE_SDK}/cmake")
    include(GeodeFile)
endif()

file(GLOB_RECURSE SOURCES "src/*.cpp")

add_library(${PROJECT_NAME} SHARED ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(${PROJECT_NAME} geode-sdk)
setup_geode_mod(${PROJECT_NAME})
