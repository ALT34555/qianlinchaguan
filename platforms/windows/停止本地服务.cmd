@echo off
rem Stop a launcher that was started in the background (silent-launch .vbs).
setlocal
title Qianlin Chaguan - Stop Local Service
cd /d "%~dp0..\.."

where node >nul 2>nul
if errorlevel 1 goto :nonode

node "platforms\windows\launcher.mjs" --stop
pause
exit /b 0

:nonode
echo.
echo   [ERROR] Node.js was not found on PATH.
echo   Download: https://nodejs.org/
echo.
pause
exit /b 1
