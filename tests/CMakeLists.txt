add_library(visionrt_python_testing STATIC ${CMAKE_CURRENT_SOURCE_DIR}/TestPythonEnvironment.cpp ${CMAKE_CURRENT_SOURCE_DIR}/TestPythonEnvironment.h)
target_link_libraries(visionrt_python_testing PUBLIC visionrt_python_bridge visionrt_python pybind11::embed)
target_include_directories(visionrt_python_testing PUBLIC "${CMAKE_SOURCE_DIR}/tests")
target_compile_definitions(visionrt_python_testing PUBLIC
    VISIONRT_TEST_PYTHON_EXECUTABLE="${Python3_EXECUTABLE}")

include(AddTest)
visionrt_define_test_groups()
find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

if(VISIONRT_BUILD_APP)
    add_library(test_dll_consumer SHARED ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/dll_consumer.cpp)
    generate_export_header(test_dll_consumer EXPORT_MACRO_NAME VISIONRT_TEST_CONSUMER_API
        EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/include/test_consumer/Export.h")
    visionrt_module_logging(test_dll_consumer test_consumer)
    target_include_directories(test_dll_consumer PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/include")
    target_link_libraries(test_dll_consumer PRIVATE visionrt_ui spdlog::spdlog)
    visionrt_contract(dll_consumer_contract visionrt_ui LABELS backend)
    target_link_libraries(dll_consumer_contract PRIVATE test_dll_consumer spdlog::spdlog Qt6::Test)
endif()

set(VISIONRT_TEST_TOOLS_PACKAGE "${CMAKE_BINARY_DIR}/tools-package" CACHE PATH "Installed tools package used by integration contracts")
set(VISIONRT_GPU_FIXTURES "${CMAKE_BINARY_DIR}/gpu-fixtures" CACHE PATH "YOLO fixtures (bus.jpg, yolov8n.wts) for the GPU contract")

visionrt_contract(version_contract visionrt_common "${PROJECT_VERSION}" "${VISIONRT_CONTRACTS_VERSION}")
visionrt_contract(value_contract visionrt_core)

visionrt_contract(tool_registry_contract visionrt_core)
visionrt_contract(definition_json_contract visionrt_core)
visionrt_contract(tools_package_contract visionrt_core)
visionrt_contract(custom_tool_store_contract visionrt_core)
add_test(NAME validator_cli_contract COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/tests/validator_cli_contract.py"
    --validator "$<TARGET_FILE:visionrt_validate_tool>" --opencv-bin "${VISIONRT_OPENCV_BIN}"
    --module "$<TARGET_FILE:test_registration_valid>" --bad-module "$<TARGET_FILE:test_plugin_legacy>")
visionrt_apply_test_environment(validator_cli_contract)
add_test(NAME package_app_contract COMMAND "${CMAKE_COMMAND}" -E env
    "VISIONTOOLS_VALIDATOR=$<TARGET_FILE:visionrt_validate_tool>"
    "CMAKE_COMMAND=${CMAKE_COMMAND}"
    "CMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
    "CMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
    "VISIONRT_BUILD_ENTRY=${CMAKE_SOURCE_DIR}/tools/build.cmd"
    "${Python3_EXECUTABLE}" -S "${CMAKE_SOURCE_DIR}/tests/test_package_app.py")
visionrt_apply_test_environment(package_app_contract)
# The release record is assembled from facts another process produced; its audit rules are pure
# Python and need neither a build nor an installation, so they run as their own quick test.
add_test(NAME release_evidence_contract COMMAND "${CMAKE_COMMAND}" -E env
    "${Python3_EXECUTABLE}" -S "${CMAKE_SOURCE_DIR}/tests/test_release_evidence.py")
# The six real nested Git+CMake builds are a separate entry: the quick package suite must not
# recurse into a compiler, and a candidate selects them explicitly with -L integration.
add_test(NAME build_metadata_integration_contract COMMAND "${CMAKE_COMMAND}" -E env
    "VISIONTOOLS_VALIDATOR=$<TARGET_FILE:visionrt_validate_tool>"
    "CMAKE_COMMAND=${CMAKE_COMMAND}"
    "CMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
    "CMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
    "VISIONRT_BUILD_ENTRY=${CMAKE_SOURCE_DIR}/tools/build.cmd"
    "${Python3_EXECUTABLE}" -S "${CMAKE_SOURCE_DIR}/tests/test_build_metadata_integration.py")
visionrt_apply_test_environment(build_metadata_integration_contract)
set_tests_properties(build_metadata_integration_contract PROPERTIES LABELS "package;integration")
add_test(NAME dependency_manifest_contract COMMAND "${CMAKE_COMMAND}" -E env
    "CMAKE_COMMAND=${CMAKE_COMMAND}" "${Python3_EXECUTABLE}" -S
    "${CMAKE_SOURCE_DIR}/tests/dependency_manifest_contract.py")
visionrt_apply_test_environment(dependency_manifest_contract)
if(WIN32)
    # The nested configure and build reuse the toolchain this project already resolved; the
    # MSVC environment itself comes from build.cmd, so no second discovery chain is needed here.
    add_test(NAME build_incremental_contract COMMAND "${CMAKE_COMMAND}" -E env
        "CMAKE_COMMAND=${CMAKE_COMMAND}" "CMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
        "CMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}" "CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
        "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/build_incremental_contract.py")
endif()
visionrt_contract(value_json_contract visionrt_core)
# The log contract is split by responsibility: text/allocation, aggregation and lifecycle.
# All three share the production logging library and the same test hooks.
foreach(log_test log_text_contract log_aggregation_contract log_lifecycle_contract log_level_contract)
    visionrt_contract(${log_test} visionrt_log_testing LABELS backend)
    target_compile_definitions(${log_test} PRIVATE VISIONRT_LOG_TEST_HOOKS=1)
    target_link_libraries(${log_test} PRIVATE Qt6::Test)
    set_tests_properties(${log_test} PROPERTIES TIMEOUT 60)
endforeach()
visionrt_contract(roi_view_benchmark VisionRT::Contracts
    --output "${CMAKE_BINARY_DIR}/evidence/roi-view-smoke.csv" LABELS perf)
visionrt_contract(python_value_contract visionrt_python_bridge)
# The probe is the only source of Python facts; its positive and negative samples run without a
# window so a rejected environment is a test result rather than a manual exercise.
visionrt_contract(python_probe_contract visionrt_python LABELS bridge)
target_compile_definitions(python_probe_contract PRIVATE
    VISIONRT_TEST_PYTHON_EXECUTABLE="${Python3_EXECUTABLE}")
visionrt_contract(python_loading_contract visionrt_python_testing)
visionrt_contract(python_adapter_contract visionrt_python_testing)
visionrt_contract(python_shutdown_contract visionrt_python_testing)
visionrt_contract(tool_discovery_contract visionrt_core "$<TARGET_FILE:visionrt_validate_tool>")
add_dependencies(tool_discovery_contract visionrt_validate_tool)
visionrt_contract(project_editor_contract visionrt_core)
visionrt_contract(logic_node_execution_test visionrt_runtime)
visionrt_contract(edge_hierarchy_contract visionrt_core)
target_link_libraries(edge_hierarchy_contract PRIVATE visionrt_runtime)
visionrt_contract(project_storage_contract visionrt_core)
visionrt_contract(project_value_contract visionrt_core)
visionrt_contract(cpu_tools_contract visionrt_core "${VISIONRT_TEST_TOOLS_PACKAGE}")
target_link_libraries(cpu_tools_contract PRIVATE ${OpenCV_LIBS})
visionrt_contract(example_metadata_contract visionrt_core "${VISIONRT_TEST_TOOLS_PACKAGE}" "${CMAKE_SOURCE_DIR}/examples")
add_executable(gpu_tools_contract ${CMAKE_CURRENT_SOURCE_DIR}/gpu_tools_contract.cpp)
target_link_libraries(gpu_tools_contract PRIVATE visionrt_runtime)
visionrt_contract(python_tools_execution_test visionrt_python_testing "${VISIONRT_TEST_TOOLS_PACKAGE}")
visionrt_contract(installed_pipeline_contract visionrt_runtime "${VISIONRT_TEST_TOOLS_PACKAGE}" LABELS installed)
target_link_libraries(installed_pipeline_contract PRIVATE visionrt_python_testing)
visionrt_contract(installed_python_author_contract visionrt_runtime "${VISIONRT_TEST_TOOLS_PACKAGE}" LABELS installed)
target_link_libraries(installed_python_author_contract PRIVATE visionrt_python_testing ${OpenCV_LIBS})
visionrt_contract(runtime_contract visionrt_runtime "${VISIONRT_TEST_TOOLS_PACKAGE}")
visionrt_contract(runtime_node_activity_contract visionrt_runtime)
visionrt_contract(node_activity_mailbox_test visionrt_runtime)
visionrt_contract(runtime_python_lazy_contract visionrt_runtime)
target_link_libraries(runtime_python_lazy_contract PRIVATE visionrt_python_testing)
visionrt_contract(runtime_diagnostics_contract visionrt_runtime)
visionrt_contract(runtime_value_contract visionrt_runtime)
visionrt_contract(result_readback_barrier_test visionrt_runtime)

if(VISIONRT_BUILD_APP)
    visionrt_contract(training_session_contract visionrt_ui)
    visionrt_contract(display_storage_contract visionrt_ui LABELS backend)
    visionrt_contract(display_conversion_benchmark visionrt_ui
        --output "${CMAKE_BINARY_DIR}/evidence/display-conversion-smoke.csv" LABELS perf)
    visionrt_contract(output_value_contract visionrt_ui LABELS backend)
    visionrt_contract(value_bridge_benchmark visionrt_ui --output "${CMAKE_BINARY_DIR}/evidence/value_bridge-smoke.csv" LABELS perf)
    target_link_libraries(value_bridge_benchmark PRIVATE visionrt_python_bridge)
    if(WIN32)
        target_link_libraries(value_bridge_benchmark PRIVATE psapi)
    endif()
    add_executable(libavoid_contract ${CMAKE_CURRENT_SOURCE_DIR}/libavoid_contract.cpp)
    target_link_libraries(libavoid_contract PRIVATE visionrt_avoid)
    add_test(NAME libavoid_contract COMMAND libavoid_contract)
    visionrt_apply_test_environment(libavoid_contract)
    visionrt_contract(wire_router_contract visionrt_ui)
    visionrt_contract(wire_routing_benchmark visionrt_ui --output "${CMAKE_BINARY_DIR}/evidence/routing-smoke.csv")
    # these scenarios execute Python, so the test states the build interpreter.
    target_link_libraries(wire_routing_benchmark PRIVATE visionrt_python_testing)
    target_link_libraries(wire_routing_benchmark PRIVATE Qt6::Test)
    target_compile_definitions(wire_routing_benchmark PRIVATE
        VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
        VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")
    visionrt_contract(viewport_contract visionrt_ui)
    visionrt_contract(viewport_lifetime_contract visionrt_ui)
    target_link_libraries(viewport_lifetime_contract PRIVATE Qt6::Test)
    visionrt_contract(ui_value_contract visionrt_ui)
    # this scenario executes Python, so the test states the build interpreter.
    target_link_libraries(ui_value_contract PRIVATE visionrt_python_testing)
    target_link_libraries(ui_value_contract PRIVATE Qt6::Test)
    visionrt_contract(ui_receiver_identity_contract visionrt_ui)
    # these scenarios execute Python, so the test states the build interpreter.
    target_link_libraries(ui_receiver_identity_contract PRIVATE visionrt_python_testing)
    target_link_libraries(ui_receiver_identity_contract PRIVATE Qt6::Test)
    target_compile_definitions(ui_receiver_identity_contract PRIVATE
        VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
        VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")
    visionrt_contract(ui_notifications_contract visionrt_ui)
    # these scenarios execute Python, so the test states the build interpreter.
    target_link_libraries(ui_notifications_contract PRIVATE visionrt_python_testing)
    target_link_libraries(ui_notifications_contract PRIVATE Qt6::Test)
    target_compile_definitions(ui_notifications_contract PRIVATE
        VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}")
    # The node list model has no window: it must not take the desktop lock or join the ui group.
    visionrt_contract(node_list_model_test visionrt_ui LABELS backend)
    target_link_libraries(node_list_model_test PRIVATE Qt6::Test)
    visionrt_contract(tool_port_list_model_test visionrt_ui LABELS backend)
    target_link_libraries(tool_port_list_model_test PRIVATE Qt6::Test)
    # What the result view may expose is decided without a window, so the visibility rule is
    # checked here instead of through a rendered frame.
    visionrt_contract(presentation_visibility_contract visionrt_ui LABELS backend)
    # The environment verdict is host-owned: one status, one reason per failure, and a check
    # that never blocks the caller. The controller is the only entry point, so this walks the
    # real settings path without opening a window.
    visionrt_contract(python_environment_contract visionrt_ui LABELS backend)
    target_link_libraries(python_environment_contract PRIVATE Qt6::Test)
    target_compile_definitions(python_environment_contract PRIVATE
        VISIONRT_TEST_PYTHON_EXECUTABLE="${Python3_EXECUTABLE}"
        VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}")
    visionrt_contract(tool_editor_controller_test visionrt_ui LABELS backend)
    target_link_libraries(tool_editor_controller_test PRIVATE Qt6::Test)
    target_compile_definitions(tool_editor_controller_test PRIVATE
        VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}")
    # One complete authoring flow through the real dialog: open, add a tool and a parameter,
    # click Save, and read the declaration the window wrote.
    visionrt_contract(tool_editor_dialog_test visionrt_ui)
    target_link_libraries(tool_editor_dialog_test PRIVATE Qt6::Test)
    target_compile_definitions(tool_editor_dialog_test PRIVATE
        VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
        VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")
    visionrt_contract(ui_unc_paths_contract visionrt_ui)
    # these scenarios execute Python, so the test states the build interpreter.
    target_link_libraries(ui_unc_paths_contract PRIVATE visionrt_python_testing)
    target_link_libraries(ui_unc_paths_contract PRIVATE Qt6::Test)
    target_compile_definitions(ui_unc_paths_contract PRIVATE
        VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
        VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    set_tests_properties(ui_unc_paths_contract PROPERTIES SKIP_RETURN_CODE 77)
    visionrt_contract(ui_completion_contract visionrt_ui)
target_link_libraries(ui_completion_contract PRIVATE visionrt_python_testing)
    target_link_libraries(ui_completion_contract PRIVATE Qt6::Test)
    target_compile_definitions(ui_completion_contract PRIVATE
        VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}")
    visionrt_contract(output_text_readback_test visionrt_ui)
    # these scenarios execute Python, so the test states the build interpreter.
    target_link_libraries(output_text_readback_test PRIVATE visionrt_python_testing)
    target_link_libraries(output_text_readback_test PRIVATE Qt6::Test)
    target_compile_definitions(output_text_readback_test PRIVATE
        VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}")
    visionrt_contract(display_reader_drain_test visionrt_ui)
    target_link_libraries(display_reader_drain_test PRIVATE Qt6::Test)
    visionrt_contract(assist_viewport_contract visionrt_ui)
    target_link_libraries(assist_viewport_contract PRIVATE Qt6::Test)
    visionrt_contract(workbench_expression_contract visionrt_ui)
    # these scenarios execute Python, so the test states the build interpreter.
    target_link_libraries(workbench_expression_contract PRIVATE visionrt_python_testing)
    target_link_libraries(workbench_expression_contract PRIVATE Qt6::Test)
    target_compile_definitions(workbench_expression_contract PRIVATE
        VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
        VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")
    # Reuse of the Python instance is a correctness property, not a measurement: keeping it out
    # of the perf group leaves that group as benchmarks only, which is what the docs promise.
    visionrt_contract(runtime_hotpath_contract visionrt_ui LABELS bridge)
    target_link_libraries(runtime_hotpath_contract PRIVATE Qt6::Test visionrt_python_testing)
    target_compile_definitions(runtime_hotpath_contract PRIVATE
        VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}")
    visionrt_contract(workbench_flows_contract visionrt_ui)
    target_link_libraries(workbench_flows_contract PRIVATE Qt6::Test)
    target_compile_definitions(workbench_flows_contract PRIVATE
        VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
        VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")
    visionrt_contract(workbench_clipboard_contract visionrt_ui)
    # these scenarios execute Python, so the test states the build interpreter.
    target_link_libraries(workbench_clipboard_contract PRIVATE visionrt_python_testing)
    target_link_libraries(workbench_clipboard_contract PRIVATE Qt6::Test)
    target_compile_definitions(workbench_clipboard_contract PRIVATE
        VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
        VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")
    visionrt_contract(workbench_status_contract visionrt_ui)
    # these scenarios execute Python, so the test states the build interpreter.
    target_link_libraries(workbench_status_contract PRIVATE visionrt_python_testing)
    target_link_libraries(workbench_status_contract PRIVATE Qt6::Test)
    target_compile_definitions(workbench_status_contract PRIVATE
        VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
        VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")
    visionrt_contract(workbench_interaction_contract visionrt_ui)
    # these scenarios execute Python, so the test states the build interpreter.
    target_link_libraries(workbench_interaction_contract PRIVATE visionrt_python_testing)
    target_link_libraries(workbench_interaction_contract PRIVATE Qt6::Test)
    target_compile_definitions(workbench_interaction_contract PRIVATE
        VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
        VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")
    set_tests_properties(workbench_interaction_contract PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    set_property(TEST workbench_interaction_contract APPEND PROPERTY ENVIRONMENT "QT_QUICK_CONTROLS_STYLE=Basic")
    visionrt_contract(workbench_assist_release_contract visionrt_ui)
    target_link_libraries(workbench_assist_release_contract PRIVATE Qt6::Test)
    visionrt_contract(workbench_assist_contract visionrt_ui)
    # these scenarios execute Python, so the test states the build interpreter.
    target_link_libraries(workbench_assist_contract PRIVATE visionrt_python_testing)
    target_link_libraries(workbench_assist_contract PRIVATE Qt6::Test)
    target_compile_definitions(workbench_assist_contract PRIVATE
        VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}")
    visionrt_contract(workbench_tooltree_contract visionrt_ui)
    # these scenarios execute Python, so the test states the build interpreter.
    target_link_libraries(workbench_tooltree_contract PRIVATE visionrt_python_testing)
    target_link_libraries(workbench_tooltree_contract PRIVATE Qt6::Test)
    target_compile_definitions(workbench_tooltree_contract PRIVATE
        VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
        VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")
    visionrt_contract(workbench_unavailable_tool_contract visionrt_ui)
    # these scenarios execute Python, so the test states the build interpreter.
    target_link_libraries(workbench_unavailable_tool_contract PRIVATE visionrt_python_testing)
    target_link_libraries(workbench_unavailable_tool_contract PRIVATE Qt6::Test)
    target_compile_definitions(workbench_unavailable_tool_contract PRIVATE
        VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
        VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")
    set_property(TEST workbench_unavailable_tool_contract APPEND PROPERTY
        ENVIRONMENT "QT_QUICK_CONTROLS_STYLE=Basic")
    visionrt_contract(workbench_publication_contract visionrt_ui)
    # these scenarios execute Python, so the test states the build interpreter.
    target_link_libraries(workbench_publication_contract PRIVATE visionrt_python_testing)
    target_link_libraries(workbench_publication_contract PRIVATE Qt6::Test)
    target_compile_definitions(workbench_publication_contract PRIVATE
        VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
        VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")
    visionrt_contract(workbench_training_contract visionrt_ui)
    target_link_libraries(workbench_training_contract PRIVATE Qt6::Test)
    target_compile_definitions(workbench_training_contract PRIVATE
        VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
        VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")
    visionrt_contract(workbench_lifecycle_contract visionrt_ui)
    visionrt_contract(workbench_exit_contract visionrt_ui)
    target_link_libraries(workbench_exit_contract PRIVATE visionrt_python_testing Qt6::Test)
    target_compile_definitions(workbench_exit_contract PRIVATE
        VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
        VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")
    # these scenarios execute Python, so the test states the build interpreter.
    target_link_libraries(workbench_lifecycle_contract PRIVATE visionrt_python_testing)
    target_link_libraries(workbench_lifecycle_contract PRIVATE Qt6::Test)
    target_compile_definitions(workbench_lifecycle_contract PRIVATE
        VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
        VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")
    visionrt_contract(ui_result_isolation_contract visionrt_ui)
    target_link_libraries(ui_result_isolation_contract PRIVATE Qt6::Test)
    target_compile_definitions(ui_result_isolation_contract PRIVATE
        VISIONRT_TEST_PYTHON_EXECUTABLE="${Python3_EXECUTABLE}"
        VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}")
    visionrt_contract(recent_projects_contract visionrt_ui)
    visionrt_contract(ui_preferences_contract visionrt_ui)
    # Example recognition, resource export and the installed-example save rule, at controller
    # level: the shipped assets are checked by example_metadata_contract instead.
    visionrt_contract(example_project_contract visionrt_ui "${VISIONRT_TEST_TOOLS_PACKAGE}")
    target_link_libraries(example_project_contract PRIVATE Qt6::Test)
    visionrt_contract(ui_storage_contract visionrt_ui)
    # this scenario executes Python, so the test states the build interpreter.
    target_link_libraries(ui_storage_contract PRIVATE visionrt_python_testing)
    target_link_libraries(ui_storage_contract PRIVATE Qt6::Test)
    if(WIN32)
        target_link_libraries(ui_storage_contract PRIVATE psapi)
    endif()
    # One runner, one CTest entry per scenario: the aggregate entry used to run the same
    # scenarios again after the focused entries had already covered them. The source list is
    # explicit so a scenario file can be named for its responsibility, not for the target.
    visionrt_contract(workbench_contract visionrt_ui NO_TEST SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/workbench_contract.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/workbench_parameters.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/workbench_parameter_states.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/flow_canvas_scenarios.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/workbench_cancellation.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/workbench_startup.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/workbench_paths.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/workbench_popups.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/workbench_roi.cpp)
    target_link_libraries(workbench_contract PRIVATE Qt6::Test visionrt_python_testing)
    add_dependencies(workbench_contract test_plugin_module)
    set(VISIONRT_WORKBENCH_SCENARIOS core roi output parameters paths canvas startup catalog popups
        output-reuse cancellation)
    foreach(_scenario IN LISTS VISIONRT_WORKBENCH_SCENARIOS)
        add_test(NAME workbench_${_scenario}_contract COMMAND workbench_contract --${_scenario}-child)
        visionrt_apply_test_environment(workbench_${_scenario}_contract)
        set_tests_properties(workbench_${_scenario}_contract PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
        set_property(TEST workbench_${_scenario}_contract APPEND PROPERTY ENVIRONMENT "QT_QUICK_CONTROLS_STYLE=Basic")
    endforeach()
    target_compile_definitions(workbench_contract PRIVATE
        VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
        VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")

    add_executable(performance_contract ${CMAKE_CURRENT_SOURCE_DIR}/performance_contract.cpp)
    target_link_libraries(performance_contract PRIVATE visionrt_ui visionrt_runtime visionrt_python_testing opencv_core Qt6::Quick)
    target_compile_definitions(performance_contract PRIVATE
        VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")
    add_test(NAME performance_contract COMMAND performance_contract --evidence-dir "${CMAKE_BINARY_DIR}/evidence")
    visionrt_apply_test_environment(performance_contract)
    if(MSVC)
        target_compile_options(performance_contract PRIVATE /UNDEBUG)
    endif()
endif()

# RT-09: 通用插件模块加载测试动态库与契约测试
add_library(test_plugin_module SHARED ${CMAKE_CURRENT_SOURCE_DIR}/test_plugin_module.cpp)
target_link_libraries(test_plugin_module PRIVATE VisionRT::Contracts)
target_compile_definitions(test_plugin_module PRIVATE VISIONRT_CONTRACTS_EXPORTS)
set_target_properties(test_plugin_module PROPERTIES PREFIX "" OUTPUT_NAME test_plugin_module)

# VR5-02: 真实传递依赖测试动态库
add_library(test_dep_a SHARED ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/test_dep_a.cpp)
set_target_properties(test_dep_a PROPERTIES PREFIX "" OUTPUT_NAME test_dep_a
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/fixtures"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/fixtures"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/fixtures")

add_library(test_dep_b SHARED ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/test_dep_b.cpp)
set_target_properties(test_dep_b PROPERTIES PREFIX "" OUTPUT_NAME test_dep_b
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/fixtures"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/fixtures"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/fixtures")

add_library(test_plugin_a SHARED ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/test_plugin_a.cpp)
target_link_libraries(test_plugin_a PRIVATE VisionRT::Contracts test_dep_a)
target_compile_definitions(test_plugin_a PRIVATE VISIONRT_CONTRACTS_EXPORTS)
set_target_properties(test_plugin_a PROPERTIES PREFIX "" OUTPUT_NAME test_plugin_a
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/fixtures"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/fixtures"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/fixtures")

add_library(test_plugin_b SHARED ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/test_plugin_b.cpp)
target_link_libraries(test_plugin_b PRIVATE VisionRT::Contracts test_dep_b)
target_compile_definitions(test_plugin_b PRIVATE VISIONRT_CONTRACTS_EXPORTS)
set_target_properties(test_plugin_b PROPERTIES PREFIX "" OUTPUT_NAME test_plugin_b
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/fixtures"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/fixtures"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/fixtures")

# Negative fixture, not dead code: it publishes an older ABI so the loader's refusal is exercised.
add_library(test_plugin_legacy SHARED ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/incompatible_abi_plugin.cpp)
target_link_libraries(test_plugin_legacy PRIVATE VisionRT::Contracts)
target_compile_definitions(test_plugin_legacy PRIVATE VISIONRT_CONTRACTS_EXPORTS)
set_target_properties(test_plugin_legacy PROPERTIES PREFIX "" OUTPUT_NAME test_plugin_legacy)

foreach(_registration_fixture IN ITEMS valid duplicate short null collision)
    add_library(test_registration_${_registration_fixture} SHARED ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/test_plugin_registration.cpp)
    target_link_libraries(test_registration_${_registration_fixture} PRIVATE VisionRT::Contracts)
    target_compile_definitions(test_registration_${_registration_fixture} PRIVATE VISIONRT_CONTRACTS_EXPORTS)
endforeach()
target_compile_definitions(test_registration_duplicate PRIVATE VISIONRT_DUPLICATE_REGISTRATION)
target_compile_definitions(test_registration_short PRIVATE VISIONRT_SHORT_MODULE)
target_compile_definitions(test_registration_null PRIVATE VISIONRT_NULL_REGISTRATION)
visionrt_contract(module_registration_contract visionrt_core
    "$<TARGET_FILE:test_registration_valid>" "$<TARGET_FILE:test_registration_duplicate>"
    "$<TARGET_FILE:test_registration_short>" "$<TARGET_FILE:test_registration_null>"
    "$<TARGET_FILE:test_registration_collision>")
add_dependencies(module_registration_contract test_registration_valid test_registration_duplicate
    test_registration_short test_registration_null test_registration_collision)

add_library(test_plugin_value_lifetime SHARED ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/test_plugin_value_lifetime.cpp)
target_link_libraries(test_plugin_value_lifetime PRIVATE VisionRT::Contracts)
target_compile_definitions(test_plugin_value_lifetime PRIVATE VISIONRT_CONTRACTS_EXPORTS)
visionrt_contract(native_value_lifetime_contract visionrt_core "$<TARGET_FILE:test_plugin_value_lifetime>")
add_dependencies(native_value_lifetime_contract test_plugin_value_lifetime)
visionrt_contract(python_array_lifetime_contract visionrt_python_bridge "$<TARGET_FILE:test_plugin_value_lifetime>")
add_dependencies(python_array_lifetime_contract test_plugin_value_lifetime)

add_executable(plugin_contract ${CMAKE_CURRENT_SOURCE_DIR}/plugin_contract.cpp)
if(VISIONRT_BUILD_APP)
    target_link_libraries(plugin_contract PRIVATE visionrt_ui)
    target_compile_definitions(plugin_contract PRIVATE VISIONRT_BUILD_APP=1)
else()
    target_link_libraries(plugin_contract PRIVATE visionrt_core visionrt_runtime visionrt_python_testing Qt6::Core Qt6::Gui)
endif()
add_dependencies(plugin_contract test_plugin_module test_plugin_a test_plugin_b test_dep_a test_dep_b test_plugin_legacy)
add_test(NAME plugin_contract COMMAND plugin_contract
    "$<TARGET_FILE:test_plugin_module>"
    "$<TARGET_FILE:test_plugin_a>"
    "$<TARGET_FILE:test_dep_a>"
    "$<TARGET_FILE:test_plugin_b>"
    "$<TARGET_FILE:test_dep_b>")
visionrt_apply_test_environment(plugin_contract)
add_test(NAME plugin_abi_contract COMMAND plugin_contract --abi-child "$<TARGET_FILE:test_plugin_legacy>")
visionrt_apply_test_environment(plugin_abi_contract)
if(WIN32)
    add_test(NAME plugin_late_runtime_contract COMMAND plugin_contract --late-runtime-child
        "$<TARGET_FILE:test_plugin_a>" "$<TARGET_FILE:test_dep_a>" "$<TARGET_FILE:test_dep_b>")
    visionrt_apply_test_environment(plugin_late_runtime_contract)
endif()
if(MSVC)
    target_compile_options(plugin_contract PRIVATE /UNDEBUG)
endif()

add_executable(example_pipelines_contract ${CMAKE_CURRENT_SOURCE_DIR}/example_pipelines.cpp)
target_link_libraries(example_pipelines_contract PRIVATE visionrt_core visionrt_runtime visionrt_python_testing Qt6::Core)
add_dependencies(example_pipelines_contract test_plugin_module test_plugin_a test_dep_a)
target_compile_definitions(example_pipelines_contract PRIVATE VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME example_pipelines_contract COMMAND example_pipelines_contract --tools-package "${VISIONRT_TEST_TOOLS_PACKAGE}" "${CMAKE_SOURCE_DIR}")
visionrt_apply_test_environment(example_pipelines_contract)
if(MSVC)
    target_compile_options(example_pipelines_contract PRIVATE /UNDEBUG)
endif()

# Package replacement is a deployment transaction, not an example pipeline: it is registered
# with the package group and builds its own temporary tools package.
visionrt_contract(package_replacement_test visionrt_runtime
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/package_replacement_test.cpp LABELS package)
target_link_libraries(package_replacement_test PRIVATE visionrt_python_testing Qt6::Core)
add_dependencies(package_replacement_test test_plugin_module)

foreach(test IN ITEMS cpu_tools_contract example_pipelines_contract gpu_tools_contract installed_pipeline_contract installed_python_author_contract module_registration_contract native_value_lifetime_contract package_replacement_test plugin_contract python_array_lifetime_contract tool_registry_contract)
    target_link_libraries(${test} PRIVATE visionrt_plugins)
endforeach()
foreach(test IN ITEMS logic_node_execution_test package_replacement_test runtime_contract)
    target_link_libraries(${test} PRIVATE visionrt_tools)
endforeach()

# Entries registered outside visionrt_contract go through the same table: one owning group per
# test, the build prerequisites ctest needs, and no property writes for tests that a
# configuration does not create (BUILD_APP=OFF has no workbench entries at all).
visionrt_group_test(plugin_contract LABELS backend REQUIRES plugin_contract)
visionrt_group_test(plugin_abi_contract LABELS backend REQUIRES plugin_contract)
if(TEST plugin_late_runtime_contract)
    visionrt_group_test(plugin_late_runtime_contract LABELS backend REQUIRES plugin_contract)
endif()
visionrt_group_test(example_pipelines_contract LABELS backend REQUIRES example_pipelines_contract)

if(VISIONRT_BUILD_APP)
    foreach(_scenario IN LISTS VISIONRT_WORKBENCH_SCENARIOS)
        visionrt_group_test(workbench_${_scenario}_contract LABELS ui REQUIRES workbench_contract)
    endforeach()
endif()
if(TEST libavoid_contract)
    visionrt_group_test(libavoid_contract LABELS ui REQUIRES libavoid_contract)
endif()
if(TEST performance_contract)
    visionrt_group_test(performance_contract LABELS perf REQUIRES performance_contract)
endif()
# The package group runs the real validator, so the validator and the two fixture modules it
# loads are build prerequisites of the group, not of the individual invocation.
visionrt_group_test(validator_cli_contract LABELS package
    REQUIRES visionrt_validate_tool test_registration_valid test_plugin_legacy)
visionrt_group_test(package_app_contract LABELS package)
visionrt_group_test(release_evidence_contract LABELS package)
visionrt_group_test(build_metadata_integration_contract LABELS package integration)
visionrt_group_test(dependency_manifest_contract LABELS package)
if(TEST build_incremental_contract)
    visionrt_group_test(build_incremental_contract LABELS package)
endif()

# SDK group: the Contracts subproject registers its own suite (VISIONRT_CONTRACTS_TESTS), labels
# it `sdk` and publishes its members; the host only adds the build entry and the group audit
# sees the real membership.
get_property(_contract_tests GLOBAL PROPERTY VISIONRT_CONTRACTS_REGISTERED_TESTS)
if(NOT _contract_tests)
    # An explicit opt-out is reported, not silently swallowed: the group audit only accepts a
    # missing group when the configuration said so.
    list(APPEND VISIONRT_DISABLED_TEST_GROUPS sdk)
endif()
foreach(_sdk_test IN LISTS _contract_tests)
    set_property(GLOBAL APPEND PROPERTY VISIONRT_GROUP_MEMBERS_sdk ${_sdk_test})
    if(TARGET ${_sdk_test})
        set_target_properties(${_sdk_test} PROPERTIES EXCLUDE_FROM_ALL TRUE)
        add_dependencies(tests-sdk ${_sdk_test})
    endif()
endforeach()

if(TARGET gpu_tools_contract)
    add_test(NAME gpu_tools_contract COMMAND gpu_tools_contract
        "${VISIONRT_TEST_TOOLS_PACKAGE}" "${CMAKE_SOURCE_DIR}/examples/04_扩展示例/03_GPU目标检测/GPU目标检测.vrt.json"
        "${VISIONRT_GPU_FIXTURES}")
    visionrt_apply_test_environment(gpu_tools_contract)
    # Without a CUDA device or the external YOLO fixtures the case is reported as not run; it
    # must never look like a pass. A real device failure still fails the test.
    set_tests_properties(gpu_tools_contract PROPERTIES LABELS "gpu" SKIP_RETURN_CODE 77)
    set_property(GLOBAL APPEND PROPERTY VISIONRT_GROUP_MEMBERS_gpu gpu_tools_contract)
    add_dependencies(tests-gpu gpu_tools_contract)

    # Converting weights rebuilds a TensorRT engine and can take tens of minutes; checking that
    # an existing engine still infers correctly is a separate, cheap capability. It shares the
    # binary but not the failure domain, and reports NOT RUN when no engine was ever built.
    add_test(NAME gpu_inference_contract COMMAND gpu_tools_contract
        "${VISIONRT_TEST_TOOLS_PACKAGE}" "${CMAKE_SOURCE_DIR}/examples/04_扩展示例/03_GPU目标检测/GPU目标检测.vrt.json"
        "${VISIONRT_GPU_FIXTURES}" --engine-only)
    visionrt_apply_test_environment(gpu_inference_contract)
    set_tests_properties(gpu_inference_contract PROPERTIES LABELS "gpu" SKIP_RETURN_CODE 77)
    set_property(GLOBAL APPEND PROPERTY VISIONRT_GROUP_MEMBERS_gpu gpu_inference_contract)
    add_dependencies(tests-gpu gpu_tools_contract)
    add_test(NAME gpu_engine_output_contract COMMAND gpu_tools_contract
        "${VISIONRT_TEST_TOOLS_PACKAGE}" "${CMAKE_SOURCE_DIR}/examples/04_扩展示例/03_GPU目标检测/GPU目标检测.vrt.json"
        "${VISIONRT_GPU_FIXTURES}" --engine-output-error)
    visionrt_apply_test_environment(gpu_engine_output_contract)
    set_tests_properties(gpu_engine_output_contract PROPERTIES LABELS "gpu" SKIP_RETURN_CODE 77)
    set_property(GLOBAL APPEND PROPERTY VISIONRT_GROUP_MEMBERS_gpu gpu_engine_output_contract)
endif()

foreach(_executable IN ITEMS plugin_contract example_pipelines_contract libavoid_contract performance_contract
        gpu_tools_contract)
    if(TARGET ${_executable})
        set_target_properties(${_executable} PROPERTIES EXCLUDE_FROM_ALL TRUE)
        add_dependencies(tests-all ${_executable})
    endif()
endforeach()

visionrt_audit_test_groups()
