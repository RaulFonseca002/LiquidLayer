cmake_minimum_required(VERSION 3.20)

project(liquid VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)

option(LIQUID_ENABLE_SANITIZERS "Build Liquid and tests with AddressSanitizer and UBSan" OFF)
option(LIQUID_ENABLE_THREAD_SANITIZER "Build Liquid and tests with ThreadSanitizer" OFF)
option(LIQUID_ENABLE_COVERAGE "Instrument Liquid Core and its tests for coverage" OFF)
option(LIQUID_ENABLE_STRICT_WARNINGS "Build Liquid and tests with strict compiler warnings" OFF)
option(LIQUID_WARNINGS_AS_ERRORS "Treat Liquid compiler warnings as errors" OFF)
option(LIQUID_BUILD_LUA "Build the optional Liquid Lua component" ON)
option(LIQUID_BUILD_SIMULATION "Build Liquid simulation library and CLI" ON)
option(LIQUID_BUILD_FUZZERS "Build decoder fuzz targets" OFF)

if(LIQUID_ENABLE_SANITIZERS AND LIQUID_ENABLE_THREAD_SANITIZER)
    message(FATAL_ERROR
        "LIQUID_ENABLE_SANITIZERS and LIQUID_ENABLE_THREAD_SANITIZER cannot be combined")
endif()

if(LIQUID_ENABLE_COVERAGE AND
   (LIQUID_ENABLE_SANITIZERS OR LIQUID_ENABLE_THREAD_SANITIZER))
    message(FATAL_ERROR
        "LIQUID_ENABLE_COVERAGE cannot be combined with a sanitizer build")
endif()

if(LIQUID_ENABLE_THREAD_SANITIZER AND
   NOT (CMAKE_C_COMPILER_ID MATCHES "Clang|GNU" AND
        CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU"))
    message(FATAL_ERROR
        "LIQUID_ENABLE_THREAD_SANITIZER requires GCC or Clang")
endif()

if(LIQUID_ENABLE_COVERAGE AND
   NOT (CMAKE_C_COMPILER_ID MATCHES "Clang|GNU" AND
        CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU"))
    message(FATAL_ERROR "LIQUID_ENABLE_COVERAGE requires GCC or Clang")
endif()

if(LIQUID_BUILD_SIMULATION AND NOT LIQUID_BUILD_LUA)
    message(FATAL_ERROR "LIQUID_BUILD_SIMULATION requires LIQUID_BUILD_LUA")
endif()

set(lua_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua-5.4.8")

if(LIQUID_BUILD_LUA)
add_library(liquid_lua OBJECT
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
set_target_properties(liquid_lua PROPERTIES POSITION_INDEPENDENT_CODE ON OUTPUT_NAME liquid_lua_vendor)
target_include_directories(liquid_lua SYSTEM PRIVATE ${lua_SOURCE_DIR}/src)

if(WIN32)
    target_compile_definitions(liquid_lua PRIVATE LUA_USE_WINDOWS)
elseif(APPLE)
    target_compile_definitions(liquid_lua PRIVATE LUA_USE_MACOSX)
elseif(UNIX)
    target_compile_definitions(liquid_lua PRIVATE LUA_USE_LINUX)
endif()

endif()

add_library(liquid
    src/Value.cpp
    src/effects/EffectTypes.cpp
    src/effects/Feedback.cpp
    src/effects/IdempotentDispatcher.cpp
    src/events/EventStore.cpp
    src/events/MemoryEventStore.cpp
    src/events/FileEventStore.cpp
    src/events/FileEventStoreFormat.cpp
    src/events/ValueCodec.cpp
    src/events/Replay.cpp
    src/BehaviorRegistry.cpp
    src/ComponentRegistry.cpp
    src/world/Coordinator.cpp
    src/IntentExpiration.cpp
    src/IntentRegistry.cpp
    src/runtime/Runtime.cpp
    src/runtime/RuntimeEffectsFeedback.cpp
    src/runtime/RuntimeEffectsState.cpp
    src/runtime/RuntimeEffectsRestore.cpp
    src/runtime/RuntimeEvidence.cpp
    src/SystemRegistry.cpp
    src/world/World.cpp
)

target_compile_features(liquid PUBLIC cxx_std_20)

add_library(Liquid::Core ALIAS liquid)

if(LIQUID_BUILD_LUA)
add_library(liquid_lua_component STATIC
    src/scripting/LuaBehaviorRunner.cpp
    src/scripting/LuaLifecycleSystem.cpp
)
target_compile_features(liquid_lua_component PUBLIC cxx_std_20)
target_include_directories(liquid_lua_component PRIVATE ${lua_SOURCE_DIR}/src)
target_sources(liquid_lua_component PRIVATE $<TARGET_OBJECTS:liquid_lua>)
target_link_libraries(liquid_lua_component PUBLIC liquid)
if(UNIX)
    target_link_libraries(liquid_lua_component PRIVATE ${CMAKE_DL_LIBS} m)
endif()
add_library(Liquid::Lua ALIAS liquid_lua_component)
endif()

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

function(liquid_apply_thread_sanitizer target_name)
    if(LIQUID_ENABLE_THREAD_SANITIZER AND
       (CMAKE_C_COMPILER_ID MATCHES "Clang|GNU" OR
        CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU"))
        target_compile_options(${target_name}
            PRIVATE
                $<$<COMPILE_LANGUAGE:C>:-fsanitize=thread>
                $<$<COMPILE_LANGUAGE:C>:-fno-omit-frame-pointer>
                $<$<COMPILE_LANGUAGE:CXX>:-fsanitize=thread>
                $<$<COMPILE_LANGUAGE:CXX>:-fno-omit-frame-pointer>
        )
        target_link_options(${target_name} PRIVATE -fsanitize=thread)
    endif()
endfunction()

function(liquid_apply_coverage target_name)
    if(LIQUID_ENABLE_COVERAGE AND
       (CMAKE_C_COMPILER_ID MATCHES "Clang|GNU" OR
        CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU"))
        target_compile_options(${target_name}
            PRIVATE
                $<$<COMPILE_LANGUAGE:C>:--coverage>
                $<$<COMPILE_LANGUAGE:C>:-O0>
                $<$<COMPILE_LANGUAGE:CXX>:--coverage>
                $<$<COMPILE_LANGUAGE:CXX>:-O0>
        )
        target_link_options(${target_name} PRIVATE --coverage)
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

liquid_apply_sanitizers(liquid)
liquid_apply_thread_sanitizer(liquid)
liquid_apply_coverage(liquid)
liquid_apply_warnings(liquid)
if(LIQUID_BUILD_LUA)
liquid_apply_sanitizers(liquid_lua)
liquid_apply_sanitizers(liquid_lua_component)
liquid_apply_thread_sanitizer(liquid_lua)
liquid_apply_thread_sanitizer(liquid_lua_component)
liquid_apply_warnings(liquid_lua_component)
endif()

if(LIQUID_BUILD_SIMULATION)
add_library(liquid_simulation STATIC
    apps/SimulationCli.cpp
    src/simulation/InMemoryAdapter.cpp
)

target_include_directories(liquid_simulation PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/apps>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/liquid/simulation>
)
target_link_libraries(liquid_simulation PUBLIC liquid_lua_component)
target_compile_definitions(liquid_simulation
    PRIVATE LIQUID_ENABLE_LEGACY_INTERNAL_COMPONENT_REGISTRATION)
set_target_properties(liquid_simulation PROPERTIES EXPORT_NAME Simulation OUTPUT_NAME liquid_simulation)
add_library(Liquid::Simulation ALIAS liquid_simulation)
liquid_apply_sanitizers(liquid_simulation)
liquid_apply_thread_sanitizer(liquid_simulation)
liquid_apply_warnings(liquid_simulation)

add_executable(liquid_sim_cli apps/liquid_sim_cli.cpp)

target_include_directories(liquid_sim_cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/apps)
target_link_libraries(liquid_sim_cli PRIVATE liquid_simulation)
liquid_apply_sanitizers(liquid_sim_cli)
liquid_apply_thread_sanitizer(liquid_sim_cli)
liquid_apply_warnings(liquid_sim_cli)
endif()

if(UNIX AND LIQUID_BUILD_SIMULATION)
add_library(liquid_scope_scenario STATIC
    apps/SimulationInput.cpp
    apps/SimulationScenario.cpp
)
target_include_directories(liquid_scope_scenario PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/apps)
target_link_libraries(liquid_scope_scenario PUBLIC liquid_simulation)
liquid_apply_sanitizers(liquid_scope_scenario)
liquid_apply_thread_sanitizer(liquid_scope_scenario)
liquid_apply_warnings(liquid_scope_scenario)

add_executable(liquid_sim_trace
    apps/liquid_sim_trace.cpp
    apps/SimulationTrace.cpp
)
target_include_directories(liquid_sim_trace PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/apps)
target_link_libraries(liquid_sim_trace PRIVATE liquid_scope_scenario)
liquid_apply_sanitizers(liquid_sim_trace)
liquid_apply_thread_sanitizer(liquid_sim_trace)
liquid_apply_warnings(liquid_sim_trace)
endif()

include(CTest)

if(LIQUID_ENABLE_COVERAGE AND NOT BUILD_TESTING)
    message(FATAL_ERROR "LIQUID_ENABLE_COVERAGE requires BUILD_TESTING=ON")
endif()

if(LIQUID_BUILD_FUZZERS)
    set(LIQUID_FUZZ_TARGETS
        fuzz_value_decoder
        fuzz_event_batch_decoder
        fuzz_file_event_store
    )

    set(LIQUID_USE_LIBFUZZER OFF)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        include(CheckCXXSourceCompiles)
        set(liquid_saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
        set(liquid_saved_required_link_options "${CMAKE_REQUIRED_LINK_OPTIONS}")
        set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -fsanitize=fuzzer")
        set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=fuzzer")
        check_cxx_source_compiles([[extern "C" int LLVMFuzzerTestOneInput(
            const unsigned char*, unsigned long) { return 0; }]]
            LIQUID_HAS_LIBFUZZER)
        set(CMAKE_REQUIRED_FLAGS "${liquid_saved_required_flags}")
        set(CMAKE_REQUIRED_LINK_OPTIONS "${liquid_saved_required_link_options}")
        if(LIQUID_HAS_LIBFUZZER)
            set(LIQUID_USE_LIBFUZZER ON)
        endif()
    endif()

    if(NOT LIQUID_USE_LIBFUZZER)
        message(STATUS
            "libFuzzer is unavailable for ${CMAKE_CXX_COMPILER_ID}; "
            "building standalone fuzz smoke executables")
    endif()

    foreach(fuzz_target IN LISTS LIQUID_FUZZ_TARGETS)
        add_executable(${fuzz_target} fuzz/${fuzz_target}.cpp)
        target_include_directories(${fuzz_target} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/fuzz")
        target_link_libraries(${fuzz_target} PRIVATE liquid)
        target_compile_features(${fuzz_target} PRIVATE cxx_std_20)

        if(LIQUID_USE_LIBFUZZER)
            target_compile_definitions(${fuzz_target} PRIVATE LIQUID_LIBFUZZER)
            target_compile_options(${fuzz_target} PRIVATE -fsanitize=fuzzer)
            target_link_options(${fuzz_target} PRIVATE -fsanitize=fuzzer)
        else()
            target_sources(${fuzz_target} PRIVATE fuzz/FuzzMain.cpp)
        endif()

        liquid_apply_sanitizers(${fuzz_target})
        liquid_apply_warnings(${fuzz_target})

        if(BUILD_TESTING)
            if(LIQUID_USE_LIBFUZZER)
                add_test(NAME ${fuzz_target}_smoke
                    COMMAND ${fuzz_target} -runs=1)
            else()
                add_test(NAME ${fuzz_target}_smoke COMMAND ${fuzz_target})
            endif()
            set_tests_properties(${fuzz_target}_smoke PROPERTIES TIMEOUT 30)
        endif()
    endforeach()
endif()

include(CMakePackageConfigHelpers)

set_target_properties(liquid PROPERTIES EXPORT_NAME Core OUTPUT_NAME liquid_core)
if(LIQUID_BUILD_LUA)
set_target_properties(liquid_lua_component PROPERTIES EXPORT_NAME Lua OUTPUT_NAME liquid_lua)
endif()

target_include_directories(liquid
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
if(LIQUID_BUILD_LUA)
target_include_directories(liquid_lua_component
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
endif()

set(Liquid_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/Liquid")
install(TARGETS liquid EXPORT LiquidCoreTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
if(LIQUID_BUILD_LUA)
install(TARGETS liquid_lua_component EXPORT LiquidLuaTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/liquid/scripting
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/liquid)
install(FILES third_party/lua-5.4.8/LICENSE
    DESTINATION ${CMAKE_INSTALL_DATADIR}/Liquid/third_party/lua-5.4.8)
install(EXPORT LiquidLuaTargets FILE LiquidLuaTargets.cmake NAMESPACE Liquid:: DESTINATION ${Liquid_INSTALL_CMAKEDIR})
endif()
if(LIQUID_BUILD_SIMULATION)
install(TARGETS liquid_simulation EXPORT LiquidSimulationTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/liquid/simulation
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/liquid)
install(FILES apps/SimulationCli.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/liquid/simulation)
install(EXPORT LiquidSimulationTargets FILE LiquidSimulationTargets.cmake NAMESPACE Liquid:: DESTINATION ${Liquid_INSTALL_CMAKEDIR})
endif()
install(DIRECTORY include/liquid DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    PATTERN scripting EXCLUDE
    PATTERN simulation EXCLUDE)

install(EXPORT LiquidCoreTargets FILE LiquidCoreTargets.cmake NAMESPACE Liquid:: DESTINATION ${Liquid_INSTALL_CMAKEDIR})

configure_package_config_file(
    cmake/LiquidConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/LiquidConfig.cmake"
    INSTALL_DESTINATION ${Liquid_INSTALL_CMAKEDIR}
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/LiquidConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/LiquidConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/LiquidConfigVersion.cmake"
    DESTINATION ${Liquid_INSTALL_CMAKEDIR}
)

if(BUILD_TESTING)
    set(LIQUID_CATCH2_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/catch2-3.8.1")
    add_library(liquid_catch2 STATIC
        "${LIQUID_CATCH2_DIR}/src/catch_amalgamated.cpp"
    )
    target_compile_features(liquid_catch2 PUBLIC cxx_std_20)
    target_compile_definitions(liquid_catch2 PRIVATE CATCH_AMALGAMATED_CUSTOM_MAIN)
    target_include_directories(liquid_catch2 SYSTEM PUBLIC
        "${LIQUID_CATCH2_DIR}/src"
        "${LIQUID_CATCH2_DIR}/include"
    )
    add_library(Catch2::Catch2 ALIAS liquid_catch2)
    liquid_apply_sanitizers(liquid_catch2)

    add_library(liquid_catch2_main STATIC
        "${LIQUID_CATCH2_DIR}/src/catch_main.cpp"
    )
    target_compile_features(liquid_catch2_main PUBLIC cxx_std_20)
    target_link_libraries(liquid_catch2_main PUBLIC liquid_catch2)
    add_library(Catch2::Catch2WithMain ALIAS liquid_catch2_main)
    liquid_apply_sanitizers(liquid_catch2_main)
    liquid_apply_warnings(liquid_catch2_main)

    set(LIQUID_TESTS
        test_ids
        test_value
        test_component_storage
        test_component_registry
        test_component_codec
        test_contract_edges
        test_feedback
        test_idempotent_dispatcher
        test_event_store
        test_replay
        test_behavior_registry
        test_intent_expiration
        test_intent_registry
        test_intent_resolution
        test_runtime
        test_runtime_effects
        test_world
        test_system_registry
    )
    if(LIQUID_BUILD_LUA)
        list(APPEND LIQUID_TESTS test_lua_behavior test_lua_lifecycle test_stress)
    endif()
    if(LIQUID_BUILD_SIMULATION)
        list(APPEND LIQUID_TESTS
            test_simulation_cli
            test_simulation_adapter
            test_full_loop_simulation)
    endif()

    foreach(test_name IN LISTS LIQUID_TESTS)
        add_executable(${test_name} tests/${test_name}.cpp)

        if(test_name STREQUAL "test_simulation_cli")
            target_include_directories(${test_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/apps)
            add_dependencies(${test_name} liquid_sim_cli)
        endif()

        if(test_name STREQUAL "test_event_store")
            target_compile_definitions(${test_name} PRIVATE
                LIQUID_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
        endif()

        if(test_name STREQUAL "test_simulation_cli" OR
           test_name STREQUAL "test_simulation_adapter" OR
           test_name STREQUAL "test_full_loop_simulation")
            set(liquid_test_component liquid_simulation)
        elseif(test_name STREQUAL "test_lua_behavior" OR
               test_name STREQUAL "test_lua_lifecycle" OR
               test_name STREQUAL "test_stress")
            set(liquid_test_component liquid_lua_component)
        else()
            set(liquid_test_component liquid)
        endif()
        target_link_libraries(${test_name} PRIVATE
            ${liquid_test_component}
            Catch2::Catch2WithMain
        )
        target_compile_definitions(${test_name}
            PRIVATE LIQUID_ENABLE_LEGACY_INTERNAL_COMPONENT_REGISTRATION)

        liquid_apply_sanitizers(${test_name})
        liquid_apply_thread_sanitizer(${test_name})
        liquid_apply_coverage(${test_name})
        liquid_apply_warnings(${test_name})
        string(REPLACE "test_" "" ctest_name ${test_name})
        add_test(NAME ${ctest_name} COMMAND ${test_name})

        if(ctest_name STREQUAL "simulation_cli")
            set_tests_properties(${ctest_name} PROPERTIES TIMEOUT 10)
        elseif(ctest_name STREQUAL "stress")
            set_tests_properties(${ctest_name} PROPERTIES TIMEOUT 60)
        endif()
    endforeach()

    if(LIQUID_BUILD_SIMULATION)
    add_test(
        NAME simulation_cli_process
        COMMAND ${CMAKE_COMMAND}
            "-DCLI=$<TARGET_FILE:liquid_sim_cli>"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_simulation_cli_process.cmake"
    )
    set_tests_properties(simulation_cli_process PROPERTIES TIMEOUT 10)
    endif()

    if(UNIX AND LIQUID_BUILD_SIMULATION)
        foreach(scope_test IN ITEMS
                test_simulation_scenario
                test_simulation_trace)
            add_executable(${scope_test} tests/${scope_test}.cpp)
            if(scope_test STREQUAL "test_simulation_trace")
                target_sources(${scope_test} PRIVATE apps/SimulationTrace.cpp)
                add_dependencies(${scope_test} liquid_sim_trace)
            endif()
            target_include_directories(${scope_test} PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/apps)
            target_link_libraries(${scope_test} PRIVATE
                liquid_scope_scenario Catch2::Catch2WithMain)
            target_compile_definitions(${scope_test} PRIVATE
                LIQUID_ENABLE_LEGACY_INTERNAL_COMPONENT_REGISTRATION)
            if(MSVC)
                target_compile_options(${scope_test} PRIVATE /UNDEBUG)
            else()
                target_compile_options(${scope_test} PRIVATE -UNDEBUG)
            endif()
            liquid_apply_sanitizers(${scope_test})
            liquid_apply_thread_sanitizer(${scope_test})
            liquid_apply_warnings(${scope_test})
            string(REPLACE "test_" "" scope_ctest_name ${scope_test})
            add_test(NAME ${scope_ctest_name} COMMAND ${scope_test})
        endforeach()

        # The Python bridge regressions are Linux-verified: the bridge is an
        # owner-operated Linux development instrument, and the same suite
        # hangs under the macOS kqueue selector on hosted CI. macOS still
        # compiles every Scope target and runs the C++ scenario, trace, and
        # renderer self-test coverage.
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            find_package(Python3 REQUIRED COMPONENTS Interpreter)
            add_test(
                NAME visualizer_server
                COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_visualizer_server.py")
            set_tests_properties(visualizer_server PROPERTIES TIMEOUT 30)
            add_test(
                NAME visualizer_integration
                COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_visualizer_integration.py"
                    "$<TARGET_FILE:liquid_sim_trace>")
            set_tests_properties(visualizer_integration PROPERTIES TIMEOUT 30)
        endif()

        # The renderer self-test runs headlessly when a system Node.js is
        # available; it is a dependency-free harness, not an npm dependency.
        find_program(LIQUID_NODE_EXECUTABLE NAMES node nodejs)
        if(LIQUID_NODE_EXECUTABLE)
            add_test(
                NAME visualizer_selftest
                COMMAND "${LIQUID_NODE_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_visualizer_selftest.js")
            set_tests_properties(visualizer_selftest PROPERTIES TIMEOUT 30)
        endif()
    endif()

    if(LIQUID_ENABLE_COVERAGE)
        find_program(LIQUID_GCOVR_EXECUTABLE NAMES gcovr)
        if(LIQUID_GCOVR_EXECUTABLE)
            add_custom_target(coverage
                COMMAND ${CMAKE_CTEST_COMMAND}
                    --test-dir "${CMAKE_CURRENT_BINARY_DIR}"
                    --output-on-failure
                COMMAND "${LIQUID_GCOVR_EXECUTABLE}"
                    --root "${CMAKE_CURRENT_SOURCE_DIR}"
                    --object-directory "${CMAKE_CURRENT_BINARY_DIR}"
                    --filter "${CMAKE_CURRENT_SOURCE_DIR}/include/liquid"
                    --filter "${CMAKE_CURRENT_SOURCE_DIR}/src"
                    --merge-mode-functions separate
                    --exclude-unreachable-branches
                    --exclude-throw-branches
                    --gcov-ignore-parse-errors negative_hits.warn_once_per_file
                    --fail-under-line 90
                    --fail-under-branch 80
                DEPENDS ${LIQUID_TESTS}
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                USES_TERMINAL
                COMMENT "Run tests and enforce 90% line / 80% branch coverage for Liquid Core"
            )
        else()
            message(WARNING
                "gcovr was not found; coverage instrumentation is enabled but the coverage target is unavailable")
        endif()
    endif()

endif()
