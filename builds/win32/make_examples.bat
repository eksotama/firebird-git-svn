
@echo off

:: Set env vars
@call setenvvar.bat
@if errorlevel 1 (goto :END)

:: verify that prepare was run before
@if not exist %ROOT_PATH%\builds\win32\dbs\metadata.fdb (goto :HELP_PREP & goto :END)

:: verify that boot was run before
@if not exist %ROOT_PATH%\gen\gpre_boot.exe (goto :HELP_BOOT & goto :END)

::===========
:: Read input values
@set DBG=
@set DBG_DIR=release
@set CLEAN=/build
@if "%1"=="DEBUG" ((set DBG=TRUE) && (set DBG_DIR=debug))
@if "%2"=="DEBUG" ((set DBG=TRUE) && (set DBG_DIR=debug))
@if "%1"=="CLEAN" (set CLEAN=/REBUILD)
@if "%2"=="CLEAN" (set CLEAN=/REBUILD)

::===========
:MAIN
@call :BUILD_EMPBUILD
@if "%DBG%"=="" (call :RELEASE) else (call :DEBUG)
@call :MOVE
@call :BUILD_EMPLOYEE
@goto :END

::===========
:BUILD_EMPBUILD
@echo.
@echo Building empbuild.fdb
@copy %ROOT_PATH%\src\v5_examples\*.sql   %ROOT_PATH%\gen\v5_examples\ > nul
@copy %ROOT_PATH%\src\v5_examples\*.inp   %ROOT_PATH%\gen\v5_examples\ > nul

@echo.
@echo Creating empbuild.fdb
:: Here we must use cd because isql does not have an option to set a base directory
@cd %ROOT_PATH%\gen\v5_examples
@del empbuild.fdb 2> nul
@isql -i empbld.sql
@cd %ROOT_PATH%\builds\win32

@echo.
@echo preprocessing empbuild.e
@echo path = %DB_PATH%/gen/v5_examples
@%ROOT_PATH%\gen\gpre_static -r -m -n -z %ROOT_PATH%\src\v5_examples\empbuild.e %ROOT_PATH%\gen\v5_examples\empbuild.c -b localhost:%DB_PATH%/gen/v5_examples/
@goto :EOF

::===========
:RELEASE
@echo.
@echo Building release
if "%VS_VER%"=="msvc6" (
	@msdev %ROOT_PATH%\builds\win32\%VS_VER%\Firebird2.dsw /MAKE "v5_examples - Win32 Release" %CLEAN% /OUT v5_examples.log
) else (
	@devenv %ROOT_PATH%\builds\win32\%VS_VER%\Firebird2.sln /project v5_examples %CLEAN% /OUT v5_examples.log
)
@goto :EOF

::===========
:DEBUG
@echo.
@echo Building debug
if "%VS_VER%"=="msvc6" (
	@msdev %ROOT_PATH%\builds\win32\%VS_VER%\Firebird2.dsw /MAKE "v5_examples - Win32 Debug" %CLEAN% /OUT v5_examples.log
) else (
	@devenv %ROOT_PATH%\builds\win32\%VS_VER%\Firebird2.sln /project v5_examples debug %CLEAN% /OUT v5_examples.log
)
@goto :EOF


::===========
:MOVE
@echo.
@mkdir %ROOT_PATH%\output\v5_examples 2> nul
@echo Moving files to output directory
@copy %ROOT_PATH%\src\v5_examples\* %ROOT_PATH%\output\v5_examples > nul
@copy %ROOT_PATH%\gen\v5_examples\* %ROOT_PATH%\output\v5_examples > nul
@copy %ROOT_PATH%\builds\win32\%VS_VER%\%DBG_DIR%\v5_examples\v5_examples.exe %ROOT_PATH%\gen\v5_examples\empbuild.exe > nul
@goto :EOF

::===========
:: only to test if it works
:BUILD_EMPLOYEE
@echo.
@echo Building employee.fdb
:: Here we must use cd because isql does not have an option to set a base directory
:: and empbuild.exe uses isql
@cd %ROOT_PATH%\gen\v5_examples
@del %ROOT_PATH%\gen\v5_examples\employee.fdb
@%ROOT_PATH%\gen\v5_examples\empbuild.exe %DB_PATH%/gen/v5_examples/employee.fdb
@cd %ROOT_PATH%\builds\win32

@goto :EOF

::==============
:HELP_PREP
@echo.
@echo    You must run prepare.bat before running this script
@echo.
@goto :EOF

::==============
:HELP_BOOT
@echo.
@echo    You must run make_boot.bat before running this script
@echo.
@goto :EOF

:END