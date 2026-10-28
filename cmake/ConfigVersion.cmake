# 必须在 include 时执行（而不是在函数调用时），以便获取本模块文件所在路径
get_filename_component(config_version_script_path ${CMAKE_CURRENT_LIST_FILE} PATH)

include(GetGitRevisionDescription)
# 获取当前仓库的 HEAD 引用（分支 refspec）以及提交哈希（commit）
get_git_head_revision(GIT_REFSPEC REPO_BRANCH REPO_COMMIT)

# 拼接工程版本号与可选后缀（例如 -rc1/-dev 等）
set(PROJECT_VERSION "${PROJECT_VERSION}${PROJECT_VERSION_SUFFIX}")

function(configure_version target LIBPREFIX incpath VERSION_FULL)
    # 为指定 target 配置版本信息，并生成版本相关头文件。
    #
    # 入参说明：
    # - target：要应用版本配置的 CMake target（库/可执行文件）
    # - LIBPREFIX：用于生成/缓存变量名的前缀（例如 <LIBPREFIX>_VERSION_MAJOR 等）
    # - incpath：生成的头文件相对 include 根目录的路径（会写到 build/include/<incpath>/...）
    # - VERSION_FULL：完整版本字符串（例如 1.2.3、1.2.3-rc1 等）
    string(TOUPPER "${target}" TARGET)

    # 提取版本后缀（以 '-' 开头的部分）
    string(REGEX MATCH "-(.*)$" version_suffix "${VERSION_FULL}")
    set(VERSION_SUFFIX ${CMAKE_MATCH_1})

    # 提取版本数字组件（major/minor/patch[/tweak]）
    string(REGEX MATCHALL "[0-9]+" version_list "${VERSION_FULL}")
    list(GET version_list 0 VERSION_MAJOR)
    list(GET version_list 1 VERSION_MINOR)
    list(GET version_list 2 VERSION_PATCH)

    list(LENGTH version_list num_version_components)

    if(num_version_components EQUAL 3)
        set(VERSION_TWEAK 0)
    elseif(num_version_components EQUAL 4)
        list(GET version_list 3 VERSION_TWEAK)
    else()
        message(FATAL_ERROR "Version must have either 3 or 4 components")
    endif()

    # API 版本编码：major*100 + minor
    math(EXPR VERSION_API_CODE "${VERSION_MAJOR}*100 + ${VERSION_MINOR}")

    string(REPLACE "-" "_" tmp ${VERSION_FULL})
    if(DEFINED ${LIBPREFIX}_BUILD_SUFFIX AND NOT "${${LIBPREFIX}_BUILD_SUFFIX}" STREQUAL "")
        set(VERSION_BUILD "${tmp}-${${LIBPREFIX}_BUILD_SUFFIX}")
    else()
        set(VERSION_BUILD "${tmp}")
    endif()

    # 构建时间（UTC 格式：YYYY-MM-DD HH:mm:ss UTC，符合 Spec AB-03）
    string(TIMESTAMP BUILD_TIME "%Y-%m-%d %H:%M:%S UTC" UTC)

    # 采集宿主实际引用的 Contracts 元数据 (Ticket AR-02 / Spec AB-04)
    set(CONTRACTS_COMMIT "unknown")
    set(CONTRACTS_BRANCH "unknown")
    set(CONTRACTS_VERSION "0.1.0")
    if(TARGET VisionRTContracts)
        get_target_property(_cv_prop VisionRTContracts VISIONRT_CONTRACTS_VERSION)
        if(_cv_prop)
            set(CONTRACTS_VERSION "${_cv_prop}")
        endif()
    elseif(DEFINED VISIONRT_CONTRACTS_VERSION)
        set(CONTRACTS_VERSION "${VISIONRT_CONTRACTS_VERSION}")
    endif()

    find_package(Git QUIET)
    if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/3rdparty/VisionRTContracts")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/3rdparty/VisionRTContracts"
            OUTPUT_VARIABLE _c_commit
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(_c_commit)
            set(CONTRACTS_COMMIT "${_c_commit}")
        endif()
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-parse --abbrev-ref HEAD
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/3rdparty/VisionRTContracts"
            OUTPUT_VARIABLE _c_branch
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(_c_branch)
            set(CONTRACTS_BRANCH "${_c_branch}")
        endif()
    endif()
    # Contracts 的构建时刻只能来自它自己的产物，任何复用宿主时间的写法都是伪造；因此这里不再
    # 往版本头文件里写 Contracts 的构建时间，改由构建完成阶段生成的组件元数据提供。
    set(${LIBPREFIX}_CONTRACTS_VERSION "${CONTRACTS_VERSION}" CACHE INTERNAL "${TARGET} contracts version")
    set(${LIBPREFIX}_CONTRACTS_COMMIT "${CONTRACTS_COMMIT}" CACHE INTERNAL "${TARGET} contracts commit")
    set(${LIBPREFIX}_CONTRACTS_BRANCH "${CONTRACTS_BRANCH}" CACHE INTERNAL "${TARGET} contracts branch")

    # 由模板生成版本头文件
    configure_file(${config_version_script_path}/VersionDef.h.in
        ${CMAKE_CURRENT_BINARY_DIR}/include/${incpath}/VersionDef.h @ONLY ESCAPE_QUOTES)
    configure_file(${config_version_script_path}/VersionBuildTime.h.in
        ${CMAKE_CURRENT_BINARY_DIR}/include/${incpath}/detail/BuildTime.h @ONLY ESCAPE_QUOTES)
    configure_file(${config_version_script_path}/VersionUtils.h.in
        ${CMAKE_CURRENT_BINARY_DIR}/include/${incpath}/detail/VersionUtils.h @ONLY ESCAPE_QUOTES)

    # 将版本信息缓存为 INTERNAL，便于工程其他位置引用
    set(${LIBPREFIX}_VERSION_FULL ${VERSION_FULL} CACHE INTERNAL "${TARGET} full version")
    set(${LIBPREFIX}_VERSION_MAJOR ${VERSION_MAJOR} CACHE INTERNAL "${TARGET} major version")
    set(${LIBPREFIX}_VERSION_MINOR ${VERSION_MINOR} CACHE INTERNAL "${TARGET} minor version")
    set(${LIBPREFIX}_VERSION_PATCH ${VERSION_PATCH} CACHE INTERNAL "${TARGET} patch version")
    set(${LIBPREFIX}_VERSION_TWEAK ${VERSION_TWEAK} CACHE INTERNAL "${TARGET} tweak version")
    set(${LIBPREFIX}_VERSION_SUFFIX ${VERSION_SUFFIX} CACHE INTERNAL "${TARGET} version suffix")
    set(${LIBPREFIX}_VERSION_API ${VERSION_MAJOR}.${VERSION_MINOR} CACHE INTERNAL "${TARGET} API version")
    set(${LIBPREFIX}_VERSION_API_CODE ${VERSION_API_CODE} CACHE INTERNAL "${TARGET} API code")
    set(${LIBPREFIX}_VERSION_BUILD ${VERSION_BUILD} CACHE INTERNAL "${TARGET} build version")
    set(${LIBPREFIX}_BUILD_TIME ${BUILD_TIME} CACHE INTERNAL "${TARGET} build time")

    # 让 target 在构建时能找到生成的头文件（build/include）
    target_include_directories(${target}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    )

    if(DEFINED CMAKE_INSTALL_INCLUDEDIR)
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/${incpath}/VersionDef.h
                DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${incpath}
                COMPONENT dev OPTIONAL)
    endif()
endfunction()

function(configure_symbol_versioning dso_target VERPREFIX input_targets)
    if(WIN32)
        return()
    endif()
    set(EXPORTS_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/exports.ldscript")
    target_link_libraries(${dso_target}
        PRIVATE
        -Wl,--version-script ${EXPORTS_OUTPUT}
    )
    set(ALL_SOURCES "")
    foreach(tgt ${input_targets})
        get_target_property(tgt_sources ${tgt} SOURCES)
        get_target_property(tgt_srcdir ${tgt} SOURCE_DIR)

        foreach(src ${tgt_sources})
            if(${src} MATCHES "^/")
                list(APPEND ALL_SOURCES ${src})
            else()
                list(APPEND ALL_SOURCES ${tgt_srcdir}/${src})
            endif()
        endforeach()
    endforeach()

    set(GEN_EXPORTS_SCRIPT "${config_version_script_path}/CreateExportsFile.cmake")
    if(EXISTS "${GEN_EXPORTS_SCRIPT}")
        add_custom_command(OUTPUT ${EXPORTS_OUTPUT}
            COMMAND ${CMAKE_COMMAND} -DSOURCES="${ALL_SOURCES}"
                                     -DVERPREFIX=${VERPREFIX}
                                     -DOUTPUT=${EXPORTS_OUTPUT}
                                     -P "${GEN_EXPORTS_SCRIPT}"
            DEPENDS ${GEN_EXPORTS_SCRIPT} ${ALL_SOURCES})

        add_custom_target(create_${dso_target}_exports_file DEPENDS ${EXPORTS_OUTPUT})
        add_dependencies(${dso_target} create_${dso_target}_exports_file)
    endif()
endfunction()
