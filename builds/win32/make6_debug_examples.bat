@cd ..\..
@for /f "delims=" %%a in ('@cd') do (set ROOT_PATH=%%a)
@echo Setting root path to %ROOT_PATH%
@cd %~dp0
::Make sure that the db path is set to a style that wont break SED
for /f "tokens=*" %%a in ('@echo %ROOT_PATH:\=/%') do (set DB_PATH=%%a)

::===========
:SED
del expand_ex.sed
@echo s?\"empbuild.fdb\"?\"localhost:%DB_PATH%/gen/v5_examples/empbuild.fdb\"?g >> expand_ex.sed

@sed -f expand_ex.sed ..\..\src\v5_examples\empbuild.e > sed.tmp
@copy sed.tmp ..\..\gen\v5_examples\empbuild.e
@del sed.tmp
@copy ..\..\src\v5_examples\*.sql ..\..\gen\v5_examples\
@copy ..\..\src\v5_examples\*.inp ..\..\gen\v5_examples\

cd ..\..\gen\v5_examples
isql -i empbld.sql
gpre -r -m -n -z empbuild.e empbuild.c
del empbuild.fdb
cd ..\..\builds\win32

::===========
cd msvc6
@msdev Firebird2.dsw /MAKE "v5_examples - Win32 Debug" /OUT v5_examples.log /REBUILD
cd..

mkdir output\v5_examples
@copy ..\..\src\v5_examples\* output\v5_examples
@copy ..\..\gen\v5_examples\* output\v5_examples
@copy msvc6\debug\v5_examples\v5_examples.exe output\v5_examples\empbuild.exe

::===========
:: only to test if it works
cd output\v5_examples
empbuild.exe %DB_PATH%\builds\win32\output\v5_examples\employee.fdb
del %ROOT_PATH%\builds\win32\output\v5_examples\employee.fdb
cd ..\..
