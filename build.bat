@echo off
setlocal
cd /d "%~dp0"
set "PROJECT_DIR=%CD%"
set "OUTPUT_EXE=%PROJECT_DIR%\qianlinchaguan.exe"
set "BUILD_DIR=%PROJECT_DIR%\obj"
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

call :find_vcvars
if not defined VCVARS (
    echo [Build] Visual Studio C++ toolchain not found.
    exit /b 2
)
call "%VCVARS%" >nul 2>&1
if errorlevel 1 exit /b %ERRORLEVEL%

echo [Build] Compiling resources...
rc.exe /nologo /fo "%BUILD_DIR%\app.res" "%PROJECT_DIR%\app.rc"
if errorlevel 1 (
    echo [Build] RESOURCE FAILED
    exit /b 1
)

echo [Build] Collecting sources...
type nul > "%BUILD_DIR%\sources.rsp"
for /r "%PROJECT_DIR%\src" %%F in (*.c) do echo "%%F">>"%BUILD_DIR%\sources.rsp"
(
    echo user32.lib
    echo gdi32.lib
    echo opengl32.lib
    echo ole32.lib
    echo windowscodecs.lib
    echo uuid.lib
    echo advapi32.lib
    echo imm32.lib
) >> "%BUILD_DIR%\sources.rsp"

echo [Build] Compiling release...
pushd "%BUILD_DIR%"
cl.exe /nologo /O2 /fp:precise /std:c11 /utf-8 /W3 /wd4244 /wd4305 /wd4996 /MT /I"%PROJECT_DIR%\src" /D_CRT_SECURE_NO_WARNINGS @sources.rsp "%BUILD_DIR%\app.res" /Fe:"%OUTPUT_EXE%" /link /IMPLIB:"%BUILD_DIR%\qianlinchaguan.lib"
set "BUILD_EXIT=%ERRORLEVEL%"
popd
if not "%BUILD_EXIT%"=="0" (
    echo [Build] FAILED with error code %BUILD_EXIT%
    exit /b %BUILD_EXIT%
)
echo [Build] SUCCESS: %OUTPUT_EXE%
exit /b 0

:find_vcvars
set "VCVARS="
for %%E in (Community Professional Enterprise BuildTools) do (
    for %%V in (18 2022 2019) do (
        if not defined VCVARS if exist "C:\Program Files\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=C:\Program Files\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars64.bat"
    )
)
exit /b 0
