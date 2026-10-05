@echo off
rem ===========================================================================
rem  Qianlin Chaguan - Windows local launcher (main entry / double-click this)
rem  All user-facing text is printed by Node, which handles Unicode correctly in
rem  the Windows console. This wrapper is deliberately pure ASCII so that it can
rem  never be broken by a code page mismatch.
rem ===========================================================================
setlocal
title Qianlin Chaguan - Local Launcher
cd /d "%~dp0..\.."

where node >nul 2>nul
if errorlevel 1 goto :nonode

if not "%~1"=="" goto :service
call npm run desktop
goto :result
:service
node "platforms\windows\launcher.mjs" %*
:result
set "CODE=%ERRORLEVEL%"
if not "%CODE%"=="0" goto :failed
exit /b 0

:nonode
echo.
echo   [ERROR] Node.js was not found on PATH.
echo.
echo   This launcher needs Node.js 22 LTS or newer.
echo   Download: https://nodejs.org/
echo.
pause
exit /b 1

:failed
echo.
echo   [ERROR] The launcher exited with code %CODE%.
echo   The message above explains what happened.
echo.
pause
exit /b %CODE%
