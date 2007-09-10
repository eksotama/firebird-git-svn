@echo off
set ERRLEV=0

:: Set env vars
@call setenvvar.bat

@if errorlevel 1 (call :ERROR Executing setenvvar.bat failed & goto :EOF)

:: verify that boot was run before

@if not exist %FB_GEN_DIR%\gpre_boot.exe (goto :HELP_BOOT & goto :EOF)


@call set_build_target.bat %*

::==========
:: MAIN

@echo Building %FB_OBJ_DIR%

call compile.bat %FB_ROOT_PATH%\builds\win32\%VS_VER%\Firebird2 make_all_%FB_TARGET_PLATFORM%.log
if errorlevel 1 call :ERROR build failed - see make_all_%FB_TARGET_PLATFORM%.log for details

@if "%ERRLEV%"=="1" (
  @goto :EOF
) else (
  @call :MOVE
)
@goto :EOF

::===========
:MOVE
@echo Copying files to output
@set FB_OUTPUT_DIR=%FB_ROOT_PATH%\output_%FB_TARGET_PLATFORM%
@del %FB_ROOT_PATH%\temp\%FB_OBJ_DIR%\firebird\bin\*.exp 2>nul
@del %FB_ROOT_PATH%\temp\%FB_OBJ_DIR%\firebird\bin\*.lib 2>nul
@rmdir /q /s %FB_ROOT_PATH%\output 2>nul

@mkdir %FB_OUTPUT_DIR% 2>nul
@mkdir %FB_OUTPUT_DIR%\bin 2>nul
@mkdir %FB_OUTPUT_DIR%\intl 2>nul
@mkdir %FB_OUTPUT_DIR%\udf 2>nul
@mkdir %FB_OUTPUT_DIR%\help 2>nul
@mkdir %FB_OUTPUT_DIR%\doc 2>nul
@mkdir %FB_OUTPUT_DIR%\include 2>nul
@mkdir %FB_OUTPUT_DIR%\lib 2>nul
@mkdir %FB_OUTPUT_DIR%\system32 2>nul
for %%v in ( icuuc30 icuuc30d icudt30 icuin30 icuin30d  ) do (
@copy %FB_ICU_SOURCE_BIN%\%%v.dll %FB_OUTPUT_DIR%\bin >nul 2>&1
)
) else (


@copy %FB_ROOT_PATH%\temp\%FB_OBJ_DIR%\firebird\bin\* %FB_OUTPUT_DIR%\bin >nul
@copy %FB_ROOT_PATH%\temp\%FB_OBJ_DIR%\firebird\intl\* %FB_OUTPUT_DIR%\intl >nul
@copy %FB_ROOT_PATH%\temp\%FB_OBJ_DIR%\firebird\udf\* %FB_OUTPUT_DIR%\udf >nul
@copy %FB_ROOT_PATH%\temp\%FB_OBJ_DIR%\firebird\system32\* %FB_OUTPUT_DIR%\system32 >nul
@copy %FB_ROOT_PATH%\temp\%FB_OBJ_DIR%\fbclient\fbclient.lib %FB_OUTPUT_DIR%\lib\fbclient_ms.lib >nul
:: Firebird.conf, etc
@copy %FB_GEN_DIR%\firebird.msg %FB_OUTPUT_DIR% > nul
@copy %FB_ROOT_PATH%\builds\install\misc\firebird.conf %FB_OUTPUT_DIR% >nul
@copy %FB_ROOT_PATH%\builds\install\misc\fbintl.conf %FB_OUTPUT_DIR%\intl >nul
:: DATABASES
@copy %FB_GEN_DIR%\dbs\SECURITY2.FDB %FB_OUTPUT_DIR%\security2.fdb >nul
@copy %FB_GEN_DIR%\dbs\HELP.fdb %FB_OUTPUT_DIR%\help\help.fdb >nul
::@copy %FB_GEN_DIR%\firebird.msg %FB_OUTPUT_DIR%\firebird.msg >nul
@copy %FB_ROOT_PATH%\builds\misc\security.gbak %FB_OUTPUT_DIR%\security2.fbk > nul
:: DOCS
::@copy %FB_ROOT_PATH%\ChangeLog %FB_OUTPUT_DIR%\doc\ChangeLog.txt >nul
::@copy %FB_ROOT_PATH%\doc\WhatsNew %FB_OUTPUT_DIR%\doc\WhatsNew.txt >nul

:: HEADERS
:: Don't use this ibase.h unless you have to - we build it better in BuildExecutableInstall.bat
:: This variation doesn't clean up the license templates, and processes the component files in
:: a different order to that used in the production version. However, this version doesn't
:: have a dependancy upon sed while the production one does.
echo #pragma message("Non-production version of ibase.h.") > %FB_OUTPUT_DIR%\include\ibase.tmp
echo #pragma message("Using raw, unprocessed concatenation of header files.") >> %FB_OUTPUT_DIR%\include\ibase.tmp
type %FB_ROOT_PATH%\src\misc\ibase_header.txt >> %FB_OUTPUT_DIR%\include\ibase.tmp
type %FB_ROOT_PATH%\src\include\types_pub.h >> %FB_OUTPUT_DIR%\include\ibase.tmp
type %FB_ROOT_PATH%\src\jrd\dsc_pub.h >> %FB_OUTPUT_DIR%\include\ibase.tmp
type %FB_ROOT_PATH%\src\dsql\sqlda_pub.h >> %FB_OUTPUT_DIR%\include\ibase.tmp
type %FB_ROOT_PATH%\src\jrd\ibase.h >> %FB_OUTPUT_DIR%\include\ibase.tmp
type %FB_ROOT_PATH%\src\jrd\inf_pub.h >> %FB_OUTPUT_DIR%\include\ibase.tmp
type %FB_ROOT_PATH%\src\include\consts_pub.h >> %FB_OUTPUT_DIR%\include\ibase.tmp
type %FB_ROOT_PATH%\src\jrd\blr.h >> %FB_OUTPUT_DIR%\include\ibase.tmp
type %FB_ROOT_PATH%\src\include\gen\iberror.h >> %FB_OUTPUT_DIR%\include\ibase.tmp
sed -f %FB_ROOT_PATH%\src\misc\headers.sed < %FB_OUTPUT_DIR%\include\ibase.tmp > %FB_OUTPUT_DIR%\include\ibase.h
del %FB_OUTPUT_DIR%\include\ibase.tmp > nul

::Copy additional headers
copy %FB_ROOT_PATH%\src\extlib\ib_util.h %FB_OUTPUT_DIR%\include > nul
copy %FB_ROOT_PATH%\src\jrd\perf.h %FB_OUTPUT_DIR%\include >nul
::This is in ibase.h so why make a separate copy?
::copy %FB_ROOT_PATH%\src\jrd\blr.h %FB_OUTPUT_DIR%\include > nul
copy %FB_ROOT_PATH%\src\include\gen\iberror.h %FB_OUTPUT_DIR%\include > nul

:: UDF
copy %FB_ROOT_PATH%\src\extlib\ib_udf.sql %FB_OUTPUT_DIR%\udf > nul
copy %FB_ROOT_PATH%\src\extlib\fbudf\fbudf.sql %FB_OUTPUT_DIR%\udf > nul
:: Examples
@copy %FB_INSTALL_SCRIPTS%\install_super.bat %FB_OUTPUT_DIR%\bin >nul
@copy %FB_INSTALL_SCRIPTS%\install_classic.bat %FB_OUTPUT_DIR%\bin >nul
@copy %FB_INSTALL_SCRIPTS%\uninstall.bat %FB_OUTPUT_DIR%\bin >nul

@goto :EOF

::==============
:HELP_BOOT
@echo.
@echo    You must run make_boot.bat before running this script
@echo.
@goto :EOF

:ERROR
::====
@echo.
@echo   An error occurred while running make_all.bat -
@echo     %*
@echo.
set ERRLEV=1
cancel_script > nul 2>&1
::End of ERROR
::------------
@goto :EOF




