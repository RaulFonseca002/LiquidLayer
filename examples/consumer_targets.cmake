option(LIQUID_CONSUMER_STRICT_WARNINGS
    "Compile Liquid consumers with strict warnings treated as errors" OFF)

function(liquid_consumer_apply_warnings target_name)
    if(NOT LIQUID_CONSUMER_STRICT_WARNINGS)
        return()
    endif()

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /WX)
    else()
        target_compile_options(${target_name} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Werror)
    endif()
endfunction()

add_executable(liquid_core_consumer
    "${CMAKE_CURRENT_LIST_DIR}/core_consumer/main.cpp")
target_link_libraries(liquid_core_consumer PRIVATE Liquid::Core)
liquid_consumer_apply_warnings(liquid_core_consumer)
add_test(NAME core_consumer COMMAND liquid_core_consumer)

if(LIQUID_CONSUMER_BUILD_LUA)
    add_executable(liquid_lua_consumer
        "${CMAKE_CURRENT_LIST_DIR}/lua_consumer/main.cpp")
    target_link_libraries(liquid_lua_consumer PRIVATE Liquid::Lua)
    liquid_consumer_apply_warnings(liquid_lua_consumer)
    add_test(NAME lua_consumer COMMAND liquid_lua_consumer)
endif()

if(LIQUID_CONSUMER_BUILD_SIMULATION)
    add_executable(liquid_simulation_consumer
        "${CMAKE_CURRENT_LIST_DIR}/simulation_consumer/main.cpp")
    target_link_libraries(liquid_simulation_consumer PRIVATE Liquid::Simulation)
    liquid_consumer_apply_warnings(liquid_simulation_consumer)
    add_test(NAME simulation_consumer COMMAND liquid_simulation_consumer)
endif()
