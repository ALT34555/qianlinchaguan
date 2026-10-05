@echo off
rem Create a desktop shortcut (with icon) pointing at the main launcher .cmd.
rem The shortcut name is taken from the target file name, so this script stays
rem pure ASCII and immune to code page problems.
setlocal
title Qianlin Chaguan - Create Desktop Shortcut
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0create-shortcut.ps1" -Target "%~f0"
echo.
pause
