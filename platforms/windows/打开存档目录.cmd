@echo off
rem Open the folder where the game writes local save files.
setlocal
title Qianlin Chaguan - Saves Folder
pushd "%~dp0..\.."
set "SAVES=%CD%\userdata\saves"
popd

if not exist "%SAVES%" mkdir "%SAVES%"
start "" "%SAVES%"
