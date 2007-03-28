# Microsoft Developer Studio Project File - Name="fbserver" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Application" 0x0101

CFG=fbserver - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "fbserver.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "fbserver.mak" CFG="fbserver - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "fbserver - Win32 Debug" (based on "Win32 (x86) Application")
!MESSAGE "fbserver - Win32 Release" (based on "Win32 (x86) Application")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "fbserver - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir ""
# PROP BASE Intermediate_Dir ""
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "..\..\..\temp\debug\fbserver"
# PROP Intermediate_Dir "..\..\..\temp\debug\fbserver"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /YX /FD /GZ /c
# ADD CPP /nologo /MDd /W3 /Gi /GX /Zi /Od /I "../../../src/include" /I "../../../src/include/gen" /D "_DEBUG" /D "_WINDOWS" /D "SUPERSERVER" /D "WIN32" /D "_MBCS" /D "_X86_" /D "DEV_BUILD" /FR /FD /GZ /c
# SUBTRACT CPP /WX
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x41d /d "_DEBUG"
# ADD RSC /l 0x41d /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:windows /debug /machine:I386 /pdbtype:sept
# ADD LINK32 kernel32.lib user32.lib gdi32.lib shell32.lib comctl32.lib advapi32.lib ws2_32.lib mpr.lib version.lib ole32.lib icuuc.lib icuin.lib /nologo /stack:0x200000 /subsystem:windows /incremental:no /debug /machine:I386 /out:"..\..\..\temp\debug\firebird/bin/fbserver.exe" /pdbtype:sept /libpath:../../../extern/icu/lib
# SUBTRACT LINK32 /pdb:none

!ELSEIF  "$(CFG)" == "fbserver - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir ""
# PROP BASE Intermediate_Dir ""
# PROP BASE Ignore_Export_Lib 0
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "..\..\..\temp\release\fbserver"
# PROP Intermediate_Dir "..\..\..\temp\release\fbserver"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MDd /W3 /Gi /GX /Ot /Oi /Op /Oy /Ob2 /I "../../../src/include" /I "../../../src/include/gen" /D "NDEBUG" /D "_WINDOWS" /D "SUPERSERVER" /D "NOMSG" /D "WIN32" /D "_MBCS" /D "_X86_" /FR /FD /GZ /c
# ADD CPP /nologo /G4 /MD /W3 /Gi /GX /Ot /Og /Oi /Op /Oy /Ob1 /I "../../../src/include" /I "../../../src/include/gen" /D "NDEBUG" /D "_WINDOWS" /D "SUPERSERVER" /D "WIN32" /D "_MBCS" /D "_X86_" /FR /FD /EHc- /c
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x41d /d "_DEBUG"
# ADD RSC /l 0x41d /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib comdlg32.lib advapi32.lib comctl32.lib wsock32.lib mpr.lib shell32.lib /nologo /subsystem:windows /incremental:no /machine:I386 /pdbtype:sept
# SUBTRACT BASE LINK32 /verbose /profile /pdb:none /map /debug
# ADD LINK32 kernel32.lib user32.lib gdi32.lib shell32.lib comctl32.lib advapi32.lib ws2_32.lib mpr.lib version.lib ole32.lib icuuc.lib icuin.lib /nologo /stack:0x200000 /subsystem:windows /incremental:no /machine:I386 /out:"..\..\..\temp\release\firebird/bin/fbserver.exe" /pdbtype:sept /libpath:../../../extern/icu/lib
# SUBTRACT LINK32 /pdb:none

!ENDIF 

# Begin Target

# Name "fbserver - Win32 Debug"
# Name "fbserver - Win32 Release"
# Begin Group "UTILITIES files"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\..\gen\utilities\gstat\dba.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\utilities\gstat\ppg.cpp
# End Source File
# End Group
# Begin Group "Resource files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# Begin Source File

SOURCE=..\..\..\src\remote\os\win32\caution.ico
# End Source File
# Begin Source File

SOURCE=..\..\..\src\remote\os\win32\server.ico
# End Source File
# Begin Source File

SOURCE=..\..\..\src\remote\os\win32\window.rc
# End Source File
# End Group
# Begin Group "COMMON files"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\..\src\common\classes\BaseStream.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\common\classes\MsgPrint.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\common\classes\SafeArg.cpp
# End Source File
# End Group
# End Target
# End Project
