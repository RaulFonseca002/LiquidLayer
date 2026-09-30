cmake_minimum_required(VERSION 3.20)

project(liquid_source_tree_consumer LANGUAGES CXX)

option(LIQUID_CONSUMER_BUILD_LUA "Build the Liquid::Lua consumer" ON)
option(LIQUID_CONSUMER_BUILD_SIMULATION "Build the Liquid::Simulation consumer" ON)
option(LIQUID_CONSUMER_BUILD_AUTHORING "Build the Liquid::Authoring consumer" OFF)

if(NOT LIQUID_SOURCE_DIR)
    message(FATAL_ERROR
        "Set LIQUID_SOURCE_DIR to the root of a Liquid source checkout")
endif()

if(LIQUID_CONSUMER_BUILD_SIMULATION AND NOT LIQUID_CONSUMER_BUILD_LUA)
    message(FATAL_ERROR "The Simulation consumer requires the Lua consumer")
endif()

if(LIQUID_CONSUMER_BUILD_AUTHORING AND NOT LIQUID_CONSUMER_BUILD_SIMULATION)
    message(FATAL_ERROR "The Authoring consumer requires the Simulation consumer")
endif()

set(BUILD_TESTING OFF CACHE BOOL "Do not build Liquid's tests" FORCE)
set(LIQUID_BUILD_LUA ${LIQUID_CONSUMER_BUILD_LUA}
    CACHE BOOL "Build Liquid's Lua component" FORCE)
set(LIQUID_BUILD_SIMULATION ${LIQUID_CONSUMER_BUILD_SIMULATION}
    CACHE BOOL "Build Liquid's Simulation component" FORCE)
set(LIQUID_BUILD_AUTHORING ${LIQUID_CONSUMER_BUILD_AUTHORING}
    CACHE BOOL "Build Liquid's Authoring component" FORCE)
add_subdirectory("${LIQUID_SOURCE_DIR}" liquid)

enable_testing()
include(../consumer_targets.cmake)
