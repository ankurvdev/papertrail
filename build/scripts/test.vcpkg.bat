@echo off
SETLOCAL ENABLEDELAYEDEXPANSION
SETLOCAL ENABLEEXTENSIONS
set WORK_DIR=%1
if "!WORK_DIR!" == "" (set /p WORK_DIR=Specify work directory to use:)
python3 %~dp0../Vcpkg.py --test --root=%WORK_DIR%
