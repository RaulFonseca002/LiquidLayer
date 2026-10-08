foreach(required IN ITEMS LIQUID_SOURCE_DIR LIQUID_BINARY_DIR WORK_DIR GENERATOR CXX_COMPILER)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

if(NOT BUILD_CONFIG)
    set(BUILD_CONFIG Release)
endif()

set(consumer_source_dir "${LIQUID_SOURCE_DIR}/examples/installed-package")
set(prefix "${WORK_DIR}/prefix")
set(partial_prefix "${WORK_DIR}/prefix-without-authoring")
set(core_prefix "${WORK_DIR}/prefix-core-only")

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(toolchain_args
    -G "${GENERATOR}"
    "-DCMAKE_BUILD_TYPE=${BUILD_CONFIG}"
    "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}"
    "-DCMAKE_CXX_FLAGS=${SANITIZER_FLAGS}"
    "-DCMAKE_EXE_LINKER_FLAGS=${SANITIZER_FLAGS}"
)
if(C_COMPILER)
    list(APPEND toolchain_args "-DCMAKE_C_COMPILER=${C_COMPILER}")
endif()

function(run_step name)
    execute_process(
        COMMAND ${ARGN}
        RESULT_VARIABLE status
        OUTPUT_VARIABLE output
        ERROR_VARIABLE output
    )
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${name} failed (${status}):\n${output}")
    endif()
endfunction()

# Runs a step that must fail and whose output must name the expected cause.
function(expect_failure name expected)
    execute_process(
        COMMAND ${ARGN}
        RESULT_VARIABLE status
        OUTPUT_VARIABLE output
        ERROR_VARIABLE output
    )
    if(status EQUAL 0)
        message(FATAL_ERROR "${name} unexpectedly succeeded")
    endif()
    string(FIND "${output}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "${name} failed without \"${expected}\":\n${output}")
    endif()
endfunction()

function(find_package_dir root out_var)
    file(GLOB_RECURSE config_files "${root}/*/LiquidConfig.cmake")
    list(LENGTH config_files count)
    if(NOT count EQUAL 1)
        message(FATAL_ERROR "expected one LiquidConfig.cmake under ${root}, found ${count}")
    endif()
    get_filename_component(directory "${config_files}" DIRECTORY)
    set(${out_var} "${directory}" PARENT_SCOPE)
endfunction()

# 1. The enabled package installs the Authoring export and headers, and the
#    installed Core/Lua/Simulation/Authoring consumers build and run.
run_step(install ${CMAKE_COMMAND} --install "${LIQUID_BINARY_DIR}"
    --prefix "${prefix}" --config "${BUILD_CONFIG}")
find_package_dir("${prefix}" package_dir)
if(NOT EXISTS "${package_dir}/LiquidAuthoringTargets.cmake")
    message(FATAL_ERROR "enabled package is missing ${package_dir}/LiquidAuthoringTargets.cmake")
endif()
# Every source Authoring header is required, so new headers need no list edit.
set(authoring_source_dir "${LIQUID_SOURCE_DIR}/include/liquid/authoring")
file(GLOB_RECURSE authoring_headers RELATIVE "${authoring_source_dir}"
    "${authoring_source_dir}/*.hpp")
if(NOT authoring_headers)
    message(FATAL_ERROR "no authoring headers found under ${authoring_source_dir}")
endif()
foreach(header IN LISTS authoring_headers)
    if(NOT EXISTS "${prefix}/include/liquid/authoring/${header}")
        message(FATAL_ERROR "enabled package is missing include/liquid/authoring/${header}")
    endif()
endforeach()

set(consumer_build "${WORK_DIR}/consumer")
run_step(configure_authoring_consumer ${CMAKE_COMMAND}
    -S "${consumer_source_dir}" -B "${consumer_build}" ${toolchain_args}
    "-DCMAKE_PREFIX_PATH=${prefix}"
    -DLIQUID_CONSUMER_BUILD_AUTHORING=ON
    -DLIQUID_CONSUMER_STRICT_WARNINGS=ON)
run_step(build_authoring_consumer ${CMAKE_COMMAND}
    --build "${consumer_build}" --config "${BUILD_CONFIG}" --parallel 4)
run_step(run_authoring_consumer ${CMAKE_CTEST_COMMAND}
    --test-dir "${consumer_build}" -C "${BUILD_CONFIG}" --output-on-failure)

# Each installed Authoring header compiles alone, with the installed
# consumer's strict warnings, so none relies on an earlier include.
set(standalone_project "${WORK_DIR}/standalone-headers")
foreach(header IN LISTS authoring_headers)
    string(MAKE_C_IDENTIFIER "${header}" header_target)
    file(WRITE "${standalone_project}/headers/${header_target}.cpp"
        "#include <liquid/authoring/${header}>\n")
endforeach()
# consumer_targets.cmake supplies liquid_consumer_apply_warnings; only the
# header targets are built.
file(WRITE "${standalone_project}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.20)
project(liquid_standalone_authoring_headers LANGUAGES CXX)
find_package(Liquid 0.1 CONFIG REQUIRED COMPONENTS Core Lua Simulation Authoring)
include("${LIQUID_CONSUMER_TARGETS}")
file(GLOB header_sources "${CMAKE_CURRENT_SOURCE_DIR}/headers/*.cpp")
foreach(header_source IN LISTS header_sources)
    get_filename_component(header_target "${header_source}" NAME_WE)
    add_library(${header_target} OBJECT "${header_source}")
    target_link_libraries(${header_target} PRIVATE Liquid::Authoring)
    liquid_consumer_apply_warnings(${header_target})
endforeach()
]=])
set(standalone_build "${standalone_project}/build")
run_step(configure_standalone_headers ${CMAKE_COMMAND}
    -S "${standalone_project}" -B "${standalone_build}" ${toolchain_args}
    "-DCMAKE_PREFIX_PATH=${prefix}"
    "-DLIQUID_CONSUMER_TARGETS=${LIQUID_SOURCE_DIR}/examples/consumer_targets.cmake"
    -DLIQUID_CONSUMER_STRICT_WARNINGS=ON)
foreach(header IN LISTS authoring_headers)
    string(MAKE_C_IDENTIFIER "${header}" header_target)
    run_step("standalone_header liquid/authoring/${header}" ${CMAKE_COMMAND}
        --build "${standalone_build}" --config "${BUILD_CONFIG}" --target ${header_target})
endforeach()

# 2. A package without the Authoring export rejects COMPONENTS Authoring but
#    still serves the old components.
file(COPY "${prefix}/" DESTINATION "${partial_prefix}"
    PATTERN "LiquidAuthoringTargets*" EXCLUDE)
expect_failure(missing_authoring_export "package built without Authoring"
    ${CMAKE_COMMAND}
    -S "${consumer_source_dir}" -B "${WORK_DIR}/consumer-missing" ${toolchain_args}
    "-DCMAKE_PREFIX_PATH=${partial_prefix}"
    -DLIQUID_CONSUMER_BUILD_AUTHORING=ON)
run_step(configure_old_components_without_authoring ${CMAKE_COMMAND}
    -S "${consumer_source_dir}" -B "${WORK_DIR}/consumer-old" ${toolchain_args}
    "-DCMAKE_PREFIX_PATH=${partial_prefix}")

# 3. An unknown component fails.
set(unknown_project "${WORK_DIR}/unknown-component")
file(WRITE "${unknown_project}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.20)
project(liquid_unknown_component LANGUAGES CXX)
find_package(Liquid 0.1 CONFIG REQUIRED COMPONENTS Core Authorship)
]=])
expect_failure(unknown_component "requested component: Authorship"
    ${CMAKE_COMMAND}
    -S "${unknown_project}" -B "${unknown_project}/build" ${toolchain_args}
    "-DCMAKE_PREFIX_PATH=${prefix}")

# 4. Enabling Authoring without its dependencies fails at configure.
expect_failure(authoring_without_lua "LIQUID_BUILD_AUTHORING requires LIQUID_BUILD_LUA"
    ${CMAKE_COMMAND}
    -S "${LIQUID_SOURCE_DIR}" -B "${WORK_DIR}/no-lua" ${toolchain_args}
    -DBUILD_TESTING=OFF
    -DLIQUID_BUILD_AUTHORING=ON
    -DLIQUID_BUILD_LUA=OFF)
expect_failure(authoring_without_simulation "LIQUID_BUILD_AUTHORING requires LIQUID_BUILD_SIMULATION"
    ${CMAKE_COMMAND}
    -S "${LIQUID_SOURCE_DIR}" -B "${WORK_DIR}/no-simulation" ${toolchain_args}
    -DBUILD_TESTING=OFF
    -DLIQUID_BUILD_AUTHORING=ON
    -DLIQUID_BUILD_SIMULATION=OFF)

# 5. The generic header install excludes authoring/: a package built without
#    Authoring installs no Authoring header or export.
set(core_build "${WORK_DIR}/core-build")
run_step(configure_core_only ${CMAKE_COMMAND}
    -S "${LIQUID_SOURCE_DIR}" -B "${core_build}" ${toolchain_args}
    -DBUILD_TESTING=OFF
    -DLIQUID_BUILD_LUA=OFF
    -DLIQUID_BUILD_SIMULATION=OFF)
run_step(build_core_only ${CMAKE_COMMAND}
    --build "${core_build}" --config "${BUILD_CONFIG}" --parallel 4)
run_step(install_core_only ${CMAKE_COMMAND} --install "${core_build}"
    --prefix "${core_prefix}" --config "${BUILD_CONFIG}")
if(EXISTS "${core_prefix}/include/liquid/authoring")
    message(FATAL_ERROR "package built without Authoring installed authoring headers")
endif()
find_package_dir("${core_prefix}" core_package_dir)
if(EXISTS "${core_package_dir}/LiquidAuthoringTargets.cmake")
    message(FATAL_ERROR "package built without Authoring installed its export")
endif()
set(core_consumer_build "${WORK_DIR}/core-consumer")
run_step(configure_core_only_consumer ${CMAKE_COMMAND}
    -S "${consumer_source_dir}" -B "${core_consumer_build}" ${toolchain_args}
    "-DCMAKE_PREFIX_PATH=${core_prefix}"
    -DLIQUID_CONSUMER_BUILD_LUA=OFF
    -DLIQUID_CONSUMER_BUILD_SIMULATION=OFF
    -DLIQUID_CONSUMER_EXPECT_CORE_ONLY=ON
    -DLIQUID_CONSUMER_STRICT_WARNINGS=ON)
run_step(build_core_only_consumer ${CMAKE_COMMAND}
    --build "${core_consumer_build}" --config "${BUILD_CONFIG}" --parallel 4)
run_step(run_core_only_consumer ${CMAKE_CTEST_COMMAND}
    --test-dir "${core_consumer_build}" -C "${BUILD_CONFIG}" --output-on-failure)
expect_failure(core_only_rejects_authoring "package built without Authoring"
    ${CMAKE_COMMAND}
    -S "${consumer_source_dir}" -B "${WORK_DIR}/core-consumer-authoring" ${toolchain_args}
    "-DCMAKE_PREFIX_PATH=${core_prefix}"
    -DLIQUID_CONSUMER_BUILD_AUTHORING=ON)

# 6. The Core-only consumer rejects any file under liquid/authoring/, not only
#    known header names.
set(planted_prefix "${WORK_DIR}/prefix-planted-authoring")
file(COPY "${core_prefix}/" DESTINATION "${planted_prefix}")
file(WRITE "${planted_prefix}/include/liquid/authoring/Planted.hpp" "")
expect_failure(core_only_rejects_any_authoring_header
    "Core-only package installed optional component headers"
    ${CMAKE_COMMAND}
    -S "${consumer_source_dir}" -B "${WORK_DIR}/core-consumer-planted" ${toolchain_args}
    "-DCMAKE_PREFIX_PATH=${planted_prefix}"
    -DLIQUID_CONSUMER_BUILD_LUA=OFF
    -DLIQUID_CONSUMER_BUILD_SIMULATION=OFF
    -DLIQUID_CONSUMER_EXPECT_CORE_ONLY=ON)

file(REMOVE_RECURSE "${WORK_DIR}")
