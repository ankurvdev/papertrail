@echo off
SETLOCAL ENABLEDELAYEDEXPANSION
SETLOCAL ENABLEEXTENSIONS

set CONFIGFILE=%~dp0.config
if exist %CONFIGFILE% (
       for /f "delims== tokens=1,2" %%G in (%CONFIGFILE%) do set %%G=%%H
)

where /q python
IF ERRORLEVEL 1 (
    if "!PYTHON_PATH!" == "" (set /p PYTHON_PATH=PythonPath:)
    if not exist "!PYTHON_PATH!\python.exe" ( goto installpython )
    set PYEXE="!PYTHON_PATH!\python.exe"
) ELSE (
    SET PYEXE="python"
)

:resume

goto end

:installpython
echo "Downloading Python"
if not exist %TMP%\python.zip (
powershell -command "& { iwr https://www.python.org/ftp/python/3.8.0/python-3.8.0-embed-amd64.zip -OutFile %TMP%\python.zip }"
)
if not exist "!PYTHON_PATH!\python.exe" (
powershell -command "Add-Type -assembly \"system.io.compression.filesystem\"; [io.compression.zipfile]::ExtractToDirectory(\"%TMP%\python.zip\", \"!PYTHON_PATH!\")"
)
del %TMP%\python.zip
goto resume

:end
%PYEXE% %~dp0devel.py %*