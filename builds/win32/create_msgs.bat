@echo off

@call setenvvar.bat
@if errorlevel 1 (goto :END)

@if "%1"=="msg" goto MSG

@if exist "%FB_DB_PATH%\gen\dbs\msg.fdb" del "%FB_DB_PATH%\gen\dbs\msg.fdb"

@echo create database '%FB_DB_PATH%\gen\dbs\msg.fdb'; | "%FB_ROOT_PATH%\gen\isql_embed" -q
@set FB_MSG_ISQL=@"%FB_ROOT_PATH%\gen\isql_embed" -q %FB_DB_PATH%\gen\dbs\msg.fdb -i %FB_ROOT_PATH%\src\msgs\
@%FB_MSG_ISQL%msg.sql
@%FB_MSG_ISQL%facilities.sql
@echo.
@echo loading locales
@%FB_MSG_ISQL%locales.sql
@echo loading history
@%FB_MSG_ISQL%history.sql
@echo loading messages
@%FB_MSG_ISQL%messages.sql
@echo loading symbols
@%FB_MSG_ISQL%symbols.sql
@echo loading system errors
@%FB_MSG_ISQL%system_errors.sql
@echo loading French translation
@%FB_MSG_ISQL%transmsgs.fr_FR.sql
@echo loading German translation
@%FB_MSG_ISQL%transmsgs.de_DE.sql

@if "%1"=="db" goto END

:MSG
@echo Building message file and codes header...
@%FB_ROOT_PATH%\gen\build_msg -f %FB_DB_PATH%/gen/firebird.msg -D %FB_DB_PATH%/gen/dbs/msg.fdb
@%FB_ROOT_PATH%\gen\codes %FB_ROOT_PATH%\src\include\gen %FB_ROOT_PATH%\lang_helpers

:END
