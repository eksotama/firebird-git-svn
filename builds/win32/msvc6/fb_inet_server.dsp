# Microsoft Developer Studio Project File - Name="fb_inet_server" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Application" 0x0101

CFG=fb_inet_server - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "fb_inet_server.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "fb_inet_server.mak" CFG="fb_inet_server - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "fb_inet_server - Win32 Debug" (based on "Win32 (x86) Application")
!MESSAGE "fb_inet_server - Win32 Release" (based on "Win32 (x86) Application")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "fb_inet_server - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir ""
# PROP BASE Intermediate_Dir ""
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "debug\fb_inet_server"
# PROP Intermediate_Dir "debug\fb_inet_server"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /YX /FD /GZ /c
# ADD CPP /nologo /MDd /Gi /GX /Zi /Od /I "../../../src/include" /D "_DEBUG" /D "_WINDOWS" /D "NOMSG" /D "WIN32_LEAN_AND_MEAN" /D "WIN32" /D "_MBCS" /D "_X86_" /D "DEV_BUILD" /FR /FD /GZ /c
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
# ADD LINK32 kernel32.lib user32.lib gdi32.lib comdlg32.lib advapi32.lib comctl32.lib wsock32.lib mpr.lib shell32.lib /nologo /stack:0x200000 /subsystem:windows /incremental:no /debug /machine:I386 /out:"debug/firebird/bin/fb_inet_server.exe" /pdbtype:sept
# SUBTRACT LINK32 /verbose /profile /pdb:none /map

!ELSEIF  "$(CFG)" == "fb_inet_server - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir ""
# PROP BASE Intermediate_Dir ""
# PROP BASE Ignore_Export_Lib 0
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "release\fb_inet_server"
# PROP Intermediate_Dir "release\fb_inet_server"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MDd /w /W0 /Gi /GX /Ot /Oi /Op /Oy /Ob2 /I "../../../src/include" /D "NDEBUG" /D "_WINDOWS" /D "NOMSG" /D "WIN32_LEAN_AND_MEAN" /D "WIN32" /D "_MBCS" /D "_X86_" /FR /FD /GZ /c
# ADD CPP /nologo /G4 /MD /Gi /GX /Ot /Oi /Op /Oy /Ob2 /I "../../../src/include" /D "NDEBUG" /D "_WINDOWS" /D "NOMSG" /D "WIN32_LEAN_AND_MEAN" /D "WIN32" /D "_MBCS" /D "_X86_" /FR /FD /GZ /c
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
# ADD LINK32 kernel32.lib user32.lib gdi32.lib comdlg32.lib advapi32.lib comctl32.lib wsock32.lib mpr.lib shell32.lib /nologo /stack:0x200000 /subsystem:windows /incremental:no /machine:I386 /out:"release/firebird/bin/fb_inet_server.exe" /pdbtype:sept
# SUBTRACT LINK32 /verbose /profile /pdb:none /map /debug

!ENDIF 

# Begin Target

# Name "fb_inet_server - Win32 Debug"
# Name "fb_inet_server - Win32 Release"
# Begin Group "UTILITIES files"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\..\generated\utilities\dba.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\utilities\ppg.cpp
# End Source File
# End Group
# Begin Group "Resource files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# Begin Source File

SOURCE=..\..\..\src\remote\caution.ico
# End Source File
# Begin Source File

SOURCE=..\..\..\src\remote\server.ico
# End Source File
# Begin Source File

SOURCE=..\..\..\src\remote\window.rc
# End Source File
# End Group
# End Target
# End Project
