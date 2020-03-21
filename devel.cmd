@echo off
:resume
SETLOCAL ENABLEDELAYEDEXPANSION
SETLOCAL ENABLEEXTENSIONS 

set CONFIGFILE=%~dp0.config
if exist %CONFIGFILE% (
	for /f "delims== tokens=1,2" %%G in (%CONFIGFILE%) do set %%G=%%H
)

if "!DEVEL_BUILDPATH!" == "" (
	set /p DEVEL_BUILDPATH="BuildPath:"
	echo DEVEL_BUILDPATH=!DEVEL_BUILDPATH!>> %CONFIGFILE%
)

if "!DEVEL_BINPATH!" == "" (
	set /p DEVEL_BINPATH=BinPath:
	echo DEVEL_BINPATH=!DEVEL_BINPATH!>> %CONFIGFILE%
)

if not exist "!DEVEL_BINPATH!" (
	mkdir "!DEVEL_BINPATH!"
)

if not exist "!DEVEL_BUILDPATH!" (
	mkdir "!DEVEL_BUILDPATH!"
)

if not exist "!DEVEL_BINPATH!\python" ( goto installpython )

goto end

:installpython
echo "Downloading Python"
if not exist %TMP%\python.zip (
powershell -command "& { iwr https://www.python.org/ftp/python/3.8.0/python-3.8.0-embed-amd64.zip -OutFile %TMP%\python.zip }"
)
if not exist "!DEVEL_BINPATH!\python" (
powershell -command "Add-Type -assembly \"system.io.compression.filesystem\"; [io.compression.zipfile]::ExtractToDirectory(\"%TMP%\python.zip\", \"!DEVEL_BINPATH!\python\")"
)
del %TMP%\python.zip
goto resume

:end
!DEVEL_BINPATH!\python\python.exe %~dp0devel.py %*