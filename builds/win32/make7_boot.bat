

@echo off

:CHECK_ENV
@if /I "%DB_PATH%"=="" (goto :HELP & goto :EOF) else (@goto :MAIN)

::===========
:MAIN
@echo.
@del ..\..\src\include\gen\autoconfig.h 2> nul
@copy ..\..\src\include\gen\autoconfig_msvc.h ..\..\src\include\gen\autoconfig.h > nul
@echo Creating directories
@cd ..\..
@rmdir gen 2>nul
@mkdir gen\alice
@mkdir gen\burp
@mkdir gen\dsql
@mkdir gen\dudley
@mkdir gen\gpre
@mkdir gen\isql
@mkdir gen\jrd
@mkdir gen\msgs
@mkdir gen\qli
@mkdir gen\utilities
@mkdir gen\v5_examples
@cd builds\win32

::provisional, copy all fdbs to the same directory to be used from preprocess
copy dbs\metadata.fdb %ROOT_PATH%\gen\yachts.lnk > nul
copy dbs\jrd\security.fdb %ROOT_PATH%\gen\security.fdb > nul
copy dbs\msgs\msg.fdb %ROOT_PATH%\gen\msg.fdb > nul
copy dbs\qli\help.fdb %ROOT_PATH%\gen\help.fdb > nul

@echo Preprocessing files required to do a boot-build...
@cd msvc7
:: @msdev Firebird2Boot.dsw /MAKE "common_static - Win32 Release" "gpre_boot - Win32 Release"  /REBUILD /OUT boot1.log
@devenv Firebird2Boot.sln /clean /OUT boot1.log
@cd ..
@del gpre_boot.exe 2> nul
@move msvc7\release\firebird\bin\gpre_boot.exe .
@call preprocess.bat BOOT

@echo.
@echo Making necessary boot-build executables...
@cd msvc7
:: @msdev Firebird2Boot.dsw /MAKE "fbclient_static - Win32 Release" "gpre_static - Win32 Release"  /REBUILD /OUT boot2.log
@devenv Firebird2Boot.sln /clean /OUT boot2.log
@cd ..
@del gpre_static.exe 2> nul
@move msvc7\release\firebird\bin\gpre_static.exe .

@echo Preprocessing the entire source tree...
@call preprocess.bat
@echo Building message file and codes header...
@cd msvc7
:: @msdev Firebird2Boot.dsw /MAKE "build_msg - Win32 Release" "codes - Win32 Release"  /REBUILD /OUT boot3.log
@devenv Firebird2Boot.sln /clean /OUT boot3.log
@cd ..
@del build_msg.exe 2> nul
@move msvc7\release\build_msg\build_msg.exe .
@del codes.exe 2> nul
@move msvc7\release\codes\codes.exe .
@build_msg -D %DB_PATH%/gen/msg.fdb
@codes %ROOT_PATH%\src\include\gen
@echo Building BLR Table
@call blrtable.bat
@goto :NEXT_STEP

::==============
:NEXT_STEP
@echo.
@echo    You may now run make6_all.bat [DEBUG] [CLEAN]
@echo.
@goto :EOF

::==============
:HELP
@echo.
@echo    You must run prepare.bat before running this script
@echo.
@goto :EOF


::==============
:END
