# Microsoft Developer Studio Project File - Name="isql" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Console Application" 0x0103

CFG=isql - Win32 Release
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "isql.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "isql.mak" CFG="isql - Win32 Release"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "isql - Win32 Debug" (based on "Win32 (x86) Console Application")
!MESSAGE "isql - Win32 Release" (based on "Win32 (x86) Console Application")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
RSC=rc.exe

!IF  "$(CFG)" == "isql - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir ""
# PROP BASE Intermediate_Dir ""
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "debug\isql"
# PROP Intermediate_Dir "debug\isql"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /D "_MBCS" /YX /FD /GZ /c
# ADD CPP /nologo /W3 /Gm /GX /Zi /Od /I "../../../src/include" /D "_DEBUG" /D "_CONSOLE" /D "SUPERCLIENT" /D "NOMSG" /D "WIN32_LEAN_AND_MEAN" /D "DEV_BUILD" /D "WIN32" /D "_MBCS" /D "_X86_" /FR /FD /GZ /c
# ADD BASE RSC /l 0x41d /d "_DEBUG"
# ADD RSC /l 0x41d /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /debug /machine:I386 /pdbtype:sept
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /incremental:no /debug /machine:I386 /out:"debug\firebird\bin/isql.exe" /pdbtype:sept

!ELSEIF  "$(CFG)" == "isql - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir ""
# PROP BASE Intermediate_Dir ""
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "release\isql"
# PROP Intermediate_Dir "release\isql"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MDd /W3 /Gm /GX /ZI /Od /I "../../../src/include" /D "_DEBUG" /D "_CONSOLE" /D "SUPERCLIENT" /D "NOMSG" /D "WIN32_LEAN_AND_MEAN" /D "DEV_BUILD" /D "WIN32" /D "_MBCS" /D "_X86_" /FR /FD /GZ /c
# SUBTRACT BASE CPP /YX
# ADD CPP /nologo /ML /W3 /GX /Ot /Oi /Op /Oy /Ob2 /I "../../../src/include" /D "NDEBUG" /D "_CONSOLE" /D "SUPERCLIENT" /D "NOMSG" /D "WIN32_LEAN_AND_MEAN" /D "WIN32" /D "_MBCS" /D "_X86_" /FR /FD /GZ /c
# ADD BASE RSC /l 0x41d /d "_DEBUG"
# ADD RSC /l 0x41d /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /debug /machine:I386 /pdbtype:sept
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /incremental:no /machine:I386 /out:"release\firebird\bin/isql.exe" /pdbtype:sept
# SUBTRACT LINK32 /pdb:none /debug

!ENDIF 

# Begin Target

# Name "isql - Win32 Debug"
# Name "isql - Win32 Release"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Group "epp Source Files"

# PROP Default_Filter "epp"
# Begin Source File

SOURCE=..\..\..\src\isql\extract.epp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\isql\isql.epp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\isql\show.epp
# End Source File
# End Group
# Begin Source File

SOURCE=..\..\..\generated\isql\extract.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\common\fb_exception.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\generated\isql\isql.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\isql\isql_win.cpp

!IF  "$(CFG)" == "isql - Win32 Debug"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "isql - Win32 Release"

# PROP BASE Exclude_From_Build 1
# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\..\..\generated\isql\show.cpp
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=..\..\..\src\isql\extra_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\isql\isql.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\isql\isql_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\isql\isql_res.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\isql\isql_win.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\isql\isqlw_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\isql\show_proto.h
# End Source File
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# End Group
# Begin Source File

SOURCE=..\..\..\src\isql\isql.rc

!IF  "$(CFG)" == "isql - Win32 Debug"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "isql - Win32 Release"

# PROP BASE Exclude_From_Build 1
# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# End Target
# End Project
