if(NOT DEFINED CLI OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "CLI and WORK_DIR are required")
endif()

string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef process_nonce)
set(process_directory
    "${WORK_DIR}/simulation_cli_process_workspace_${process_nonce}")

file(MAKE_DIRECTORY "${process_directory}")
set(success_script "${process_directory}/success.lua")
set(failure_script "${process_directory}/failure.lua")

file(WRITE "${success_script}" [=[
local light = access.Light.officeLight
light.propose({ value = { brightness = 30 }, priority = "low" })
light.propose({ value = { brightness = 70 }, priority = "high", duration_ms = 5 })
]=])

file(WRITE "${failure_script}" [=[
access.Light.officeLight.propose({ value = { brightness = 80 } })
error("script failure", 0)
]=])

set(success_output [=[scenario initial_brightness=10 frame_count=2
script status=success created_intents=2 intent_ids=1,2 diagnostic=""
frame number=0 now_ms=100 completed=true phases=begin_frame,expire_intents,run_input_systems,run_behavior_systems,run_decision_systems,resolve_intents,end_frame expired_intents=0 resolution_requests=1 selected_intents=1 systems_completed=2
selection frame=0 type=Light type_id=0 component=officeLight intent_id=2 brightness=70 priority=high lifetime=until_time expires_at_ms=105
frame number=1 now_ms=105 completed=true phases=begin_frame,expire_intents,run_input_systems,run_behavior_systems,run_decision_systems,resolve_intents,end_frame expired_intents=1 resolution_requests=1 selected_intents=1 systems_completed=2
selection frame=1 type=Light type_id=0 component=officeLight intent_id=1 brightness=30 priority=low lifetime=persistent
final component=Light.officeLight brightness=10 tracking_system_runs=2 frames_completed=2 faulted=false
]=])

set(failure_output [=[scenario initial_brightness=10 frame_count=1
script status=runtime_error created_intents=0 intent_ids=- diagnostic="script failure"
frame number=0 now_ms=100 completed=true phases=begin_frame,expire_intents,run_input_systems,run_behavior_systems,run_decision_systems,resolve_intents,end_frame expired_intents=0 resolution_requests=1 selected_intents=0 systems_completed=2
final component=Light.officeLight brightness=10 tracking_system_runs=1 frames_completed=1 faulted=false
]=])

function(assert_replay name expected_status expected_output)
    foreach(iteration RANGE 1 20)
        execute_process(
            COMMAND "${CLI}" ${ARGN}
            RESULT_VARIABLE actual_status
            OUTPUT_VARIABLE actual_output
            ERROR_VARIABLE actual_error
        )

        if(NOT actual_status STREQUAL expected_status)
            message(FATAL_ERROR
                "${name} run ${iteration}: expected exit ${expected_status}, got ${actual_status}"
            )
        endif()

        if(NOT actual_output STREQUAL expected_output)
            message(FATAL_ERROR "${name} run ${iteration}: stdout did not match the golden output")
        endif()

        if(NOT actual_error STREQUAL "")
            message(FATAL_ERROR "${name} run ${iteration}: unexpected stderr: ${actual_error}")
        endif()
    endforeach()
endfunction()

assert_replay(
    success
    0
    "${success_output}"
    --initial-brightness 10
    --script "${success_script}"
    --frame-time 100
    --frame-time 105
)

assert_replay(
    bounded_error
    3
    "${failure_output}"
    --initial-brightness 10
    --script "${failure_script}"
    --frame-time 100
)

file(REMOVE_RECURSE "${process_directory}")
