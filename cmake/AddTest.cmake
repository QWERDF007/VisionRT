include_guard(GLOBAL)

# Test groups used by the aggregate build targets and the ctest label filter. Every contract test
# carries at least one label so a change can be verified without building the whole matrix.
set(VISIONRT_TEST_GROUPS backend bridge ui package sdk installed gpu perf)
# Groups whose members only exist with the desktop application.
set(VISIONRT_APP_TEST_GROUPS bridge ui)

function(visionrt_define_test_groups)
    foreach(group IN LISTS VISIONRT_TEST_GROUPS)
        if(NOT TARGET tests-${group})
            add_custom_target(tests-${group})
        endif()
    endforeach()
    if(NOT TARGET tests-all)
        add_custom_target(tests-all)
        foreach(group IN LISTS VISIONRT_TEST_GROUPS)
            add_dependencies(tests-all tests-${group})
        endforeach()
    endif()
endfunction()

# Tests write through Qt's temporary directory and the linker, both of which follow TMP/TEMP. The
# default keeps that traffic on the build drive instead of the system one, and a machine can point
# it elsewhere from the cache without touching this file.
set(VISIONRT_TEST_TEMP_DIR "${CMAKE_BINARY_DIR}/tmp" CACHE PATH "Temporary directory for test runs")
if(WIN32)
    file(MAKE_DIRECTORY "${VISIONRT_TEST_TEMP_DIR}")
endif()

function(visionrt_apply_test_environment TEST_NAME)
    if(WIN32)
        file(TO_NATIVE_PATH "${VISIONRT_TEST_TEMP_DIR}" _tmp)
        file(TO_NATIVE_PATH "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}" _p0)
        file(TO_NATIVE_PATH "${VISIONRT_QT_BIN}" _p1)
        file(TO_NATIVE_PATH "${VISIONRT_OPENCV_BIN}" _p2)
        file(TO_NATIVE_PATH "${VISIONRT_PYTHON_BIN}" _p3)
        file(TO_NATIVE_PATH "${VISIONRT_PYTHON_BIN}/Library/bin" _p4)
        file(TO_NATIVE_PATH "${INFERRT_BIN_DIR}" _p5)
        file(TO_NATIVE_PATH "${CUDAToolkit_BIN_DIR}" _p6)
        file(TO_NATIVE_PATH "${VISIONRT_PYTHON_BIN}" _pyhome)

        set_tests_properties(${TEST_NAME} PROPERTIES
            ENVIRONMENT "PATH=${_p0}\;${_p1}\;${_p2}\;${_p3}\;${_p4}\;${_p5}\;${_p6}\;$ENV{PATH};PYTHONHOME=${_pyhome};TMP=${_tmp};TEMP=${_tmp}"
        )
    endif()
endfunction()

# Registers a contract executable, its test entry and its labels. The executable is excluded from
# the default build: `tests-<group>` (or `tests-all`) is the explicit build entry, and ctest only
# runs what was built. The label defaults to the owning library so no test can be unlabelled.
# `LABELS` is a multi-value keyword and must be the last argument list; positional test arguments
# come before it.
function(visionrt_contract name library)
    cmake_parse_arguments(ARG "" "" "LABELS" ${ARGN})
    add_executable(${name} EXCLUDE_FROM_ALL tests/${name}.cpp)
    target_link_libraries(${name} PRIVATE ${library})
    if(MSVC)
        target_compile_options(${name} PRIVATE /UNDEBUG)
    else()
        target_compile_options(${name} PRIVATE -UNDEBUG)
    endif()
    add_test(NAME ${name} COMMAND ${name} ${ARG_UNPARSED_ARGUMENTS})
    visionrt_apply_test_environment(${name})
    if(NOT ARG_LABELS)
        if(library MATCHES "visionrt_ui|visionrt_viewport")
            set(ARG_LABELS bridge ui)
        else()
            set(ARG_LABELS backend)
        endif()
    endif()
    set_tests_properties(${name} PROPERTIES LABELS "${ARG_LABELS}")
    foreach(label IN LISTS ARG_LABELS)
        set_property(GLOBAL APPEND PROPERTY VISIONRT_GROUP_MEMBERS_${label} ${name})
    endforeach()
    if("ui" IN_LIST ARG_LABELS)
        # Window/focus driven cases own the desktop while they run; the lock keeps two real
        # workbench windows from fighting over activation and the clipboard.
        set_tests_properties(${name} PROPERTIES RESOURCE_LOCK visionrt-ui)
    endif()
    if(TARGET tests-all)
        add_dependencies(tests-all ${name})
        foreach(label IN LISTS ARG_LABELS)
            if(TARGET tests-${label})
                add_dependencies(tests-${label} ${name})
            endif()
        endforeach()
    endif()
endfunction()

# Labels an extra test entry created outside visionrt_contract (scenario child, script, installed
# check) and attaches the targets that have to exist before ctest can run it. The owning group is
# recorded so `visionrt_audit_test_groups` can refuse an empty filter at configure time.
function(visionrt_group_test name)
    cmake_parse_arguments(ARG "" "" "LABELS;REQUIRES" ${ARGN})
    if(NOT ARG_LABELS)
        message(FATAL_ERROR "visionrt_group_test(${name}) 缺少 LABELS")
    endif()
    if(NOT TEST ${name})
        message(FATAL_ERROR "visionrt_group_test(${name}) 指向未注册的测试")
    endif()
    set_tests_properties(${name} PROPERTIES LABELS "${ARG_LABELS}")
    if("ui" IN_LIST ARG_LABELS)
        set_tests_properties(${name} PROPERTIES RESOURCE_LOCK visionrt-ui)
    endif()
    foreach(label IN LISTS ARG_LABELS)
        set_property(GLOBAL APPEND PROPERTY VISIONRT_GROUP_MEMBERS_${label} ${name})
        foreach(executable IN LISTS ARG_REQUIRES)
            if(TARGET tests-${label} AND TARGET ${executable})
                add_dependencies(tests-${label} ${executable})
            endif()
        endforeach()
    endforeach()
endfunction()

# A group that filters to nothing is a false green: the configure step refuses it instead of
# letting `ctest -L <group>` report success after running zero tests. `ui` and `bridge` only exist
# with the desktop application, so a headless configuration reports them as disabled instead.
function(visionrt_audit_test_groups)
    foreach(group IN LISTS VISIONRT_TEST_GROUPS)
        get_property(members GLOBAL PROPERTY VISIONRT_GROUP_MEMBERS_${group})
        if(NOT members)
            if(group IN_LIST VISIONRT_APP_TEST_GROUPS AND NOT VISIONRT_BUILD_APP)
                message(STATUS "测试组 ${group}: 本配置未构建桌面应用，该组不存在（ctest -L ${group} 无意义）")
                continue()
            endif()
            if(group IN_LIST VISIONRT_DISABLED_TEST_GROUPS)
                message(STATUS "测试组 ${group}: 本配置显式关闭，未注册成员")
                continue()
            endif()
            message(FATAL_ERROR "测试组 ${group} 没有任何成员；请补充成员或删除该组")
        endif()
        list(REMOVE_DUPLICATES members)
        message(STATUS "测试组 ${group}: ${members}")
    endforeach()
endfunction()
