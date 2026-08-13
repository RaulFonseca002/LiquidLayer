cmake_minimum_required(VERSION 3.20)

project(liquid_installed_package_consumer LANGUAGES CXX)

option(LIQUID_CONSUMER_BUILD_LUA "Build the Liquid::Lua consumer" ON)
option(LIQUID_CONSUMER_BUILD_SIMULATION "Build the Liquid::Simulation consumer" ON)
option(LIQUID_CONSUMER_EXPECT_CORE_ONLY
    "Fail if optional Liquid targets are visible to the Core-only consumer" OFF)

if(LIQUID_CONSUMER_BUILD_SIMULATION AND NOT LIQUID_CONSUMER_BUILD_LUA)
    message(FATAL_ERROR "The Simulation consumer requires the Lua consumer")
endif()

set(liquid_consumer_components Core)
if(LIQUID_CONSUMER_BUILD_LUA)
    list(APPEND liquid_consumer_components Lua)
endif()
if(LIQUID_CONSUMER_BUILD_SIMULATION)
    list(APPEND liquid_consumer_components Simulation)
endif()

find_package(Liquid 0.1 CONFIG REQUIRED
    COMPONENTS ${liquid_consumer_components})

get_target_property(liquid_core_include_dirs
    Liquid::Core INTERFACE_INCLUDE_DIRECTORIES)
foreach(liquid_core_include_dir IN LISTS liquid_core_include_dirs)
    foreach(liquid_internal_header IN ITEMS
        BehaviorRegistry.hpp
        ComponentRegistry.hpp
        ComponentStorage.hpp
        IntentRegistry.hpp
        SystemRegistry.hpp
        world/Coordinator.hpp
        world/WorldState.hpp)
        if(EXISTS "${liquid_core_include_dir}/liquid/${liquid_internal_header}")
            message(FATAL_ERROR
                "Liquid::Core installed internal header ${liquid_internal_header}")
        endif()
    endforeach()
endforeach()

if(LIQUID_CONSUMER_EXPECT_CORE_ONLY AND
   (TARGET Liquid::Lua OR TARGET Liquid::Simulation))
    message(FATAL_ERROR
        "Core-only package discovery exposed an optional Liquid target")
endif()

if(LIQUID_CONSUMER_EXPECT_CORE_ONLY)
    foreach(liquid_core_include_dir IN LISTS liquid_core_include_dirs)
        if(EXISTS "${liquid_core_include_dir}/liquid/scripting/LuaBehaviorRunner.hpp" OR
           EXISTS "${liquid_core_include_dir}/liquid/simulation/InMemoryAdapter.hpp")
            message(FATAL_ERROR
                "Core-only package installed optional component headers")
        endif()
    endforeach()
endif()

enable_testing()
include(../consumer_targets.cmake)
