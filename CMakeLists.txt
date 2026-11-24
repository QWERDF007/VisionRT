cmake_minimum_required(VERSION 3.25)
project(VisionRT VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 引入构建系统与编译器配置模块
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ConfigBuildTree)
include(ConfigCompiler)
include(ConfigVersion)

# 基础与公共依赖配置
include(ConfigDependencyDefaults)
include(ConfigOpenCV)
include(ConfigQT)
include(Config3rdparty)
include(ConfigPython)
include(ConfigQuickUI)
include(ConfigSpdlog)

# 测试开关与 enable_testing() 必须先于任何子目录：Contracts 在自己的目录里注册契约测试，
# 这里只负责把它们编入 sdk 组。
include(CTest)
set(VISIONRT_CONTRACTS_TESTS ${BUILD_TESTING} CACHE BOOL "构建 Contracts 自身的契约测试（归入 sdk 组）")
set(VISIONRT_CONTRACTS_INSTALL ON CACHE BOOL "Contracts 随宿主安装，供安装态消费者契约使用")
option(VISIONRT_RELEASE_BUILD "显式发布候选构建模式（要求干净工作区与明确 Git 提交）" OFF)

# 独立通用工具 SDK（Git 子模块）
add_subdirectory(3rdparty/VisionRTContracts)

include(3rdparty/VisionRTContracts/tools/EnumExport.cmake)
visionrt_add_enum_exporter(visionrt_enum_export tools/export_ui_enums.cpp)
target_include_directories(visionrt_enum_export PRIVATE
    src/ui/include src/runtime/include src/python/include src/log/include)

include(AddModule)
add_subdirectory(src/common)
add_subdirectory(src/log)

add_subdirectory(src/core)
add_subdirectory(src/plugins)
add_subdirectory(src/python)
add_subdirectory(src/python_bridge)
add_subdirectory(src/runtime)
add_subdirectory(src/tools)

add_executable(visionrt_validate_tool tools/visionrt_validate_tool.cpp)
target_link_libraries(visionrt_validate_tool PRIVATE visionrt_plugins)

option(VISIONRT_BUILD_APP "Build the desktop workbench" ON)
if(VISIONRT_BUILD_APP)
    add_subdirectory(src/ui)
    # 窗口程序，不是控制台程序：双击启动时不应额外打开命令行终端。命令行走文件与退出码，
    # 不依赖标准输出，所以 GUI 子系统不损失任何既有入口。
    qt_add_executable(VisionRT WIN32 src/main.cpp)
    target_link_libraries(VisionRT PRIVATE visionrt_ui quickui::quickui)
    # 桥接模块不参与链接，但必须随程序构建到同一目录，供运行期按名称加载。
    add_dependencies(VisionRT visionrt_python_bridge)

    # 构建元数据必须来自“构建完成”而不是“配置开始”，所以两份组件信息都在链接结束后生成：
    # VisionRT 用当前时刻（刚刚完成链接），Contracts 用它自己产物的最后修改时间，未重新链接的
    # 静态库因此保留上一次真实构建时刻。关于弹窗只读取这两个文件，不读编译期配置时间。
    add_custom_command(TARGET VisionRT POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DCOMPONENT=VisionRT
            -DVERSION=${PROJECT_VERSION}${PROJECT_VERSION_SUFFIX}
            -DREPO_DIR=${CMAKE_SOURCE_DIR}
            -DSOURCE_FILE=$<TARGET_FILE:VisionRT>
            -DOUTPUT=$<TARGET_FILE_DIR:VisionRT>/build_info.json
            -DRELEASE_BUILD=${VISIONRT_RELEASE_BUILD}
            -P "${CMAKE_SOURCE_DIR}/cmake/WriteBuildInfo.cmake"
        COMMAND ${CMAKE_COMMAND}
            -DCOMPONENT=VisionRTContracts
            -DVERSION=${VISIONRT_CONTRACTS_VERSION}
            -DREPO_DIR=${CMAKE_SOURCE_DIR}/3rdparty/VisionRTContracts
            -DSOURCE_FILE=$<TARGET_FILE:VisionRTContracts>
            -DOUTPUT=$<TARGET_FILE_DIR:VisionRT>/contracts-build-info.json
            -DRELEASE_BUILD=${VISIONRT_RELEASE_BUILD}
            -P "${CMAKE_SOURCE_DIR}/cmake/WriteBuildInfo.cmake"
        COMMENT "记录 VisionRT 与 VisionRTContracts 的构建完成时刻"
        VERBATIM)

    configure_file(cmake/Deploy.cmake.in "${CMAKE_BINARY_DIR}/Deploy.configured.cmake" @ONLY)
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/Deploy-$<CONFIG>.cmake"
        INPUT "${CMAKE_BINARY_DIR}/Deploy.configured.cmake")
    add_custom_target(deploy
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_BINARY_DIR}/Deploy-$<CONFIG>.cmake"
        DEPENDS VisionRT
        USES_TERMINAL)

    # 开发运行目录与安装态同构：示例工程就在可执行文件旁边，菜单在 build/bin 下同样可用。
    # 先清空再复制，改名或删除的示例不会残留在运行目录里冒充产品文件。
    add_custom_target(visionrt_examples
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/examples"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/examples" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/examples"
        COMMENT "同步示例工程到运行目录")
    add_dependencies(VisionRT visionrt_examples)
endif()

visionrt_write_module_catalog()

# 开发与本地调试动态库拷贝目标：OpenCV 取自本次配置实际链接的导入目标，Python 取自宿主解释器
set(_visionrt_runtime_dlls ${VISIONRT_OPENCV_RUNTIME_DLLS} ${PYTHON_DLLS})
if(_visionrt_runtime_dlls)
    add_custom_target(deploy_runtime_dlls ALL
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${_visionrt_runtime_dlls} "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        COMMENT "Deploying runtime DLLs to ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    )
    if(TARGET VisionRT)
        add_dependencies(deploy_runtime_dlls VisionRT)
        add_custom_command(TARGET deploy_runtime_dlls POST_BUILD
            COMMAND "${VISIONRT_WINDEPLOYQT}" "$<IF:$<CONFIG:Debug>,--debug,--release>"
                --no-translations --no-compiler-runtime
                --qmldir "${CMAKE_SOURCE_DIR}/src/ui/qml" --qmlimport "${VISIONRT_QUICKUI_QML_ROOT}"
                --dir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}" "$<TARGET_FILE:VisionRT>"
            COMMENT "Deploying Qt runtime and QML imports to the build tree")
    endif()
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

include(PrintConfig)
