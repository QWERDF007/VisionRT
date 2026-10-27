@echo off
setlocal
chcp 65001 >nul

set "PROJECT_ROOT=%~dp0.."
pushd "%PROJECT_ROOT%"

if defined VSCMD_VER goto :VSCMD_INITIALIZED

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%VSWHERE%" (
    echo [ERROR] Visual Studio Installer vswhere.exe not found.
    popd
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do call "%%i\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 (
    echo [ERROR] Failed to initialize Visual Studio VC environment.
    popd
    exit /b 1
)

:VSCMD_INITIALIZED
setlocal enabledelayedexpansion

set "FIRST_ARG=%~1"

if "%FIRST_ARG%"=="help" goto :SHOW_HELP
if "%FIRST_ARG%"=="-h" goto :SHOW_HELP
if "%FIRST_ARG%"=="--help" goto :SHOW_HELP
if "%FIRST_ARG%"=="/?" goto :SHOW_HELP

if "%FIRST_ARG%"=="clean" goto :DO_BUILD
if "%FIRST_ARG%"=="test" goto :DO_BUILD
if "%FIRST_ARG%"=="" goto :DO_BUILD

set "CHAR1=%FIRST_ARG:~0,1%"
if "%CHAR1%"=="-" goto :DO_BUILD

popd
%*
exit /b %errorlevel%

:DO_BUILD
set "CONFIG=Release"
set "BUILD_DIR=build"
set "DO_CLEAN=0"
set "DO_TEST=0"
set "DO_PACKAGE=0"
set "PACKAGE_DIR="

:PARSE_ARGS
if "%~1"=="" goto :START_BUILD
if /i "%~1"=="clean" (
    set "DO_CLEAN=1"
    shift
    goto :PARSE_ARGS
)
if /i "%~1"=="--clean" (
    set "DO_CLEAN=1"
    shift
    goto :PARSE_ARGS
)
if /i "%~1"=="test" (
    set "DO_TEST=1"
    shift
    goto :PARSE_ARGS
)
if /i "%~1"=="--test" (
    set "DO_TEST=1"
    shift
    goto :PARSE_ARGS
)
if /i "%~1"=="--group" (
    set "DO_GROUP=%~2"
    set "DO_TEST=1"
    shift
    shift
    goto :PARSE_ARGS
)
if /i "%~1"=="--config" (
    set "CONFIG=%~2"
    shift
    shift
    goto :PARSE_ARGS
)
if /i "%~1"=="--debug" (
    set "CONFIG=Debug"
    shift
    goto :PARSE_ARGS
)
if /i "%~1"=="--release" (
    set "CONFIG=Release"
    shift
    goto :PARSE_ARGS
)
if /i "%~1"=="--build-dir" (
    set "BUILD_DIR=%~2"
    shift
    shift
    goto :PARSE_ARGS
)
if /i "%~1"=="--package" (
    set "DO_PACKAGE=1"
    if not "%~2"=="" (
        set "CHAR_TEST=%~2"
        if not "!CHAR_TEST:~0,1!"=="-" (
            set "PACKAGE_DIR=%~2"
            shift
        )
    )
    shift
    goto :PARSE_ARGS
)
echo [WARN] Unknown option: %~1
shift
goto :PARSE_ARGS

:START_BUILD
rem Ninja 定位：优先 PATH，其次 Visual Studio 自带（避免机器相关硬编码）
where ninja >nul 2>nul
if errorlevel 1 (
    if exist "%VSINSTALLDIR%Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" (
        set "PATH=%VSINSTALLDIR%Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
    ) else (
        echo [WARN] ninja not found on PATH or in Visual Studio; CMake configure will likely fail.
    )
)

if "%DO_CLEAN%"=="1" (
    echo [INFO] Cleaning build directory: %BUILD_DIR% ...
    if exist "%BUILD_DIR%" (
        cmake --build "%BUILD_DIR%" --target clean 2>nul
        if errorlevel 1 (
            rmdir /s /q "%BUILD_DIR%" 2>nul
        )
    )
    if "%DO_TEST%"=="0" if "%DO_PACKAGE%"=="0" (
        popd
        exit /b 0
    )
)

if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo [INFO] Configuring project with CMake for %CONFIG%...
    cmake -B "%BUILD_DIR%" -G "Ninja" -DCMAKE_BUILD_TYPE=%CONFIG%
    if errorlevel 1 (
        echo [ERROR] CMake configuration failed.
        popd
        exit /b 1
    )
)

echo [INFO] Building project [%CONFIG%]...
cmake --build "%BUILD_DIR%" --config %CONFIG%
if errorlevel 1 (
    echo [ERROR] Build failed.
    popd
    exit /b %errorlevel%
)

if "%DO_TEST%"=="1" (
    if defined DO_GROUP (
        echo [INFO] Building test group %DO_GROUP%...
        cmake --build "%BUILD_DIR%" --config %CONFIG% --target tests-%DO_GROUP%
    ) else (
        echo [INFO] Building all test groups...
        cmake --build "%BUILD_DIR%" --config %CONFIG% --target tests-all
    )
    if errorlevel 1 (
        echo [ERROR] Test build failed.
        popd
        exit /b %errorlevel%
    )
    echo [INFO] Running tests...
    if defined DO_GROUP (
        ctest --test-dir "%BUILD_DIR%" -C %CONFIG% -L "^%DO_GROUP%$" --output-on-failure
    ) else (
        ctest --test-dir "%BUILD_DIR%" -C %CONFIG% --output-on-failure
    )
    if errorlevel 1 (
        echo [ERROR] Tests failed.
        popd
        exit /b %errorlevel%
    )
)

if "%DO_PACKAGE%"=="1" (
    echo [INFO] Packaging application...
    if defined PACKAGE_DIR (
        python tools\package_app.py --build-dir "%BUILD_DIR%" --config %CONFIG% --validator "%BUILD_DIR%\bin\visionrt_validate_tool.exe" --install-dir "%PACKAGE_DIR%"
    ) else (
        python tools\package_app.py --build-dir "%BUILD_DIR%" --config %CONFIG% --validator "%BUILD_DIR%\bin\visionrt_validate_tool.exe"
    )
    if errorlevel 1 (
        echo [ERROR] Packaging failed.
        popd
        exit /b %errorlevel%
    )
)

popd
exit /b 0

:SHOW_HELP
echo VisionRT Build Script (tools\build.cmd)
echo.
echo Usage:
echo   tools\build.cmd [options]
echo   tools\build.cmd ^<command^> [args...]
echo.
echo Build Options:
echo   --config ^<Release^|Debug^>   Specify build configuration (default: Release)
echo   --debug                     Build Debug configuration
echo   --release                   Build Release configuration
echo   --clean                     Clean build directory
echo   --test                      Build every test group and run ctest
echo   --group ^<name^>              Build and run one group: backend/bridge/ui/package/sdk/installed/gpu/perf
echo   --package [dir]             Package application using tools\package_app.py
echo   --build-dir ^<dir^>          Specify CMake build directory (default: build)
echo   -h, --help                  Show this help message
echo.
echo Command Proxy Mode:
echo   Any other command will be executed within the initialized MSVC x64 environment:
echo   tools\build.cmd cmake --build build --config Release
echo   tools\build.cmd ctest --test-dir build --output-on-failure
echo   tools\build.cmd python tools\package_app.py --install-dir F:\tmp\VisionRT
echo.
popd
exit /b 0
