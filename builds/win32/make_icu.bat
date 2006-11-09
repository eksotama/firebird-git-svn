@echo off


::Set up the compiler environment
if DEFINED VS80COMNTOOLS (
@devenv /? >nul 2>nul
@if errorlevel 9009 (call "%VS80COMNTOOLS%vsvars32.bat") else ( echo    The file: & echo      "%VS80COMNTOOLS%vsvars32.bat" & echo    has already been executed.)
) else (
if DEFINED VS71COMNTOOLS (
@devenv /? >nul 2>nul
@if errorlevel 9009 (call "%VS71COMNTOOLS%vsvars32.bat") else ( echo    The file: & echo      "%VS71COMNTOOLS%vsvars32.bat" & echo    has already been executed.)
) else (
@msdev /? >nul 2>nul
@if errorlevel 9009 (call "C:\Program Files\Microsoft Visual Studio\VC98\Bin\vcvars32.bat") else (echo MSVC6 vcvars.bat has already been executed.)
)
)
@echo.

:: Set env vars
@call setenvvar.bat

::===========
:: Read input values
@set DBG=
@set DBG_DIR=release
@set CLEAN=/build
@if "%1"=="DEBUG" ((set DBG=TRUE) && (set DBG_DIR=debug))
@if "%2"=="DEBUG" ((set DBG=TRUE) && (set DBG_DIR=debug))
@if "%1"=="CLEAN" (set CLEAN=/rebuild)
@if "%2"=="CLEAN" (set CLEAN=/rebuild)

::==========
:: MAIN

@if "%DBG%"=="" (
	call :RELEASE
) else (
	call :DEBUG
)
@goto :EOF


::===========
:RELEASE
@echo   Building release...
if "%VS_VER%"=="msvc6" (
	@msdev %ROOT_PATH%\extern\icu\source\allinone\allinone.dsw /MAKE "all - Win32 Release" %CLEAN% /OUT make_icu.log
) else (
if "%VS_VER%"=="msvc7" (
	@devenv %ROOT_PATH%\extern\icu\source\allinone\allinone.sln %CLEAN% release /OUT make_icu.log
) else (
	@devenv %ROOT_PATH%\extern\icu\source\allinone\allinone_8.sln %CLEAN% "release|%PLATFORM%" /OUT make_icu.log
)
)
if errorlevel 1 call :ERROR Release build failed
@goto :EOF


::===========
:DEBUG
@echo   Building debug...
if "%VS_VER%"=="msvc6" (
	@msdev %ROOT_PATH%\extern\icu\source\allinone\allinone.dsw /MAKE "all - Win32 Debug" %CLEAN% /OUT make_icu.log
) else (
if "%VS_VER%"=="msvc7" (
	@devenv %ROOT_PATH%\extern\icu\source\allinone\allinone.sln %CLEAN% debug /OUT make_icu.log
) else (
	@devenv %ROOT_PATH%\extern\icu\source\allinone\allinone_8.sln %CLEAN% "debug|%PLATFORM%" /OUT make_icu.log
)
)
if errorlevel 1 call :ERROR Debug build failed
@goto :EOF



:ERROR
::====
@echo.
@echo   An error occurred while running make_icu.bat -
@echo     %*
@echo.
set ERRLEV=1
cancel_script > nul 2>&1
::End of ERROR
::------------
@goto :EOF




