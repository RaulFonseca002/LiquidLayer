cmake_minimum_required(VERSION 3.20)

project(liquid LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(LIQUID_ENABLE_SANITIZERS "Build Liquid and tests with AddressSanitizer and UBSan" OFF)
option(LIQUID_ENABLE_STRICT_WARNINGS "Build Liquid and tests with strict compiler warnings" OFF)
option(LIQUID_WARNINGS_AS_ERRORS "Treat Liquid compiler warnings as errors" OFF)

include(FetchContent)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

FetchContent_Declare(
    lua
    URL https://www.lua.org/ftp/lua-5.4.8.tar.gz
    URL_HASH SHA256=4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae
)
FetchContent_MakeAvailable(lua)

add_library(liquid_lua STATIC
    ${lua_SOURCE_DIR}/src/lapi.c
    ${lua_SOURCE_DIR}/src/lauxlib.c
    ${lua_SOURCE_DIR}/src/lbaselib.c
    ${lua_SOURCE_DIR}/src/lcode.c
    ${lua_SOURCE_DIR}/src/lcorolib.c
    ${lua_SOURCE_DIR}/src/lctype.c
    ${lua_SOURCE_DIR}/src/ldblib.c
    ${lua_SOURCE_DIR}/src/ldebug.c
    ${lua_SOURCE_DIR}/src/ldo.c
    ${lua_SOURCE_DIR}/src/ldump.c
    ${lua_SOURCE_DIR}/src/lfunc.c
    ${lua_SOURCE_DIR}/src/lgc.c
    ${lua_SOURCE_DIR}/src/linit.c
    ${lua_SOURCE_DIR}/src/liolib.c
    ${lua_SOURCE_DIR}/src/llex.c
    ${lua_SOURCE_DIR}/src/lmathlib.c
    ${lua_SOURCE_DIR}/src/lmem.c
    ${lua_SOURCE_DIR}/src/loadlib.c
    ${lua_SOURCE_DIR}/src/lobject.c
    ${lua_SOURCE_DIR}/src/lopcodes.c
    ${lua_SOURCE_DIR}/src/loslib.c
    ${lua_SOURCE_DIR}/src/lparser.c
    ${lua_SOURCE_DIR}/src/lstate.c
    ${lua_SOURCE_DIR}/src/lstring.c
    ${lua_SOURCE_DIR}/src/lstrlib.c
    ${lua_SOURCE_DIR}/src/ltable.c
    ${lua_SOURCE_DIR}/src/ltablib.c
    ${lua_SOURCE_DIR}/src/ltm.c
    ${lua_SOURCE_DIR}/src/lundump.c
    ${lua_SOURCE_DIR}/src/lutf8lib.c
    ${lua_SOURCE_DIR}/src/lvm.c
    ${lua_SOURCE_DIR}/src/lzio.c
)

target_compile_features(liquid_lua PRIVATE c_std_99)
set_target_properties(liquid_lua PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(liquid_lua SYSTEM PUBLIC ${lua_SOURCE_DIR}/src)

if(WIN32)
    target_compile_definitions(liquid_lua PRIVATE LUA_USE_WINDOWS)
elseif(APPLE)
    target_compile_definitions(liquid_lua PRIVATE LUA_USE_MACOSX)
elseif(UNIX)
    target_compile_definitions(liquid_lua PRIVATE LUA_USE_LINUX)
endif()

if(UNIX)
    target_link_libraries(liquid_lua PUBLIC ${CMAKE_DL_LIBS} m)
endif()

add_library(liquid
    src/BehaviorRegistry.cpp
    src/ComponentRegistry.cpp
    src/world/Coordinator.cpp
    src/IntentExpiration.cpp
    src/IntentRegistry.cpp
    src/Runtime.cpp
    src/scripting/LuaBehaviorRunner.cpp
    src/SystemRegistry.cpp
    src/world/World.cpp
)

target_compile_features(liquid PUBLIC cxx_std_20)

target_include_directories(liquid
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(liquid PRIVATE liquid_lua)

function(liquid_apply_sanitizers target_name)
    if(LIQUID_ENABLE_SANITIZERS AND
       (CMAKE_C_COMPILER_ID MATCHES "Clang|GNU" OR
        CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU"))
        target_compile_options(${target_name}
            PRIVATE
                $<$<COMPILE_LANGUAGE:C>:-fsanitize=address,undefined>
                $<$<COMPILE_LANGUAGE:C>:-fno-omit-frame-pointer>
                $<$<COMPILE_LANGUAGE:CXX>:-fsanitize=address,undefined>
                $<$<COMPILE_LANGUAGE:CXX>:-fno-omit-frame-pointer>
        )
        target_link_options(${target_name}
            PRIVATE
                -fsanitize=address,undefined
        )
    endif()
endfunction()

function(liquid_apply_warnings target_name)
    if(NOT LIQUID_ENABLE_STRICT_WARNINGS)
        return()
    endif()

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4)

        if(LIQUID_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target_name}
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Wconversion
        )

        if(LIQUID_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE -Werror)
        endif()
    endif()
endfunction()

liquid_apply_sanitizers(liquid_lua)
liquid_apply_sanitizers(liquid)
liquid_apply_warnings(liquid)

include(CTest)

if(BUILD_TESTING)
    set(LIQUID_TESTS
        test_ids
        test_component_storage
        test_component_registry
        test_behavior_registry
        test_intent_expiration
        test_intent_registry
        test_intent_resolution
        test_runtime
        test_lua_behavior
        test_world
        test_system_registry
        test_stress
    )

    foreach(test_name IN LISTS LIQUID_TESTS)
        add_executable(${test_name} tests/${test_name}.cpp)
        target_link_libraries(${test_name} PRIVATE liquid)

        # assert is the current test harness, so checks must remain active in
        # optimized configurations where CMake otherwise defines NDEBUG.
        if(MSVC)
            target_compile_options(${test_name} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${test_name} PRIVATE -UNDEBUG)
        endif()

        liquid_apply_sanitizers(${test_name})
        liquid_apply_warnings(${test_name})
        string(REPLACE "test_" "" ctest_name ${test_name})
        add_test(NAME ${ctest_name} COMMAND ${test_name})
    endforeach()

endif()
