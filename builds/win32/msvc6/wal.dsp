# Microsoft Developer Studio Project File - Name="wal" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Static Library" 0x0104

CFG=wal - Win32 Release
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "wal.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "wal.mak" CFG="wal - Win32 Release"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "wal - Win32 Debug" (based on "Win32 (x86) Static Library")
!MESSAGE "wal - Win32 Release" (based on "Win32 (x86) Static Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
RSC=rc.exe

!IF  "$(CFG)" == "wal - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir ""
# PROP BASE Intermediate_Dir ""
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "debug\wal"
# PROP Intermediate_Dir "debug\wal"
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_MBCS" /D "_LIB" /YX /FD /GZ /c
# ADD CPP /nologo /MDd /W3 /Gm /GX /ZI /Od /I "../../../src/include" /D "_DEBUG" /D "_LIB" /D "SUPERSERVER" /D "WIN32" /D "_MBCS" /D "_X86_" /FD /GZ /c
# SUBTRACT CPP /YX
# ADD BASE RSC /l 0x41d /d "_DEBUG"
# ADD RSC /l 0x41d /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LIB32=link.exe -lib
# ADD BASE LIB32 /nologo
# ADD LIB32 /nologo

!ELSEIF  "$(CFG)" == "wal - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir ""
# PROP BASE Intermediate_Dir ""
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "release\wal"
# PROP Intermediate_Dir "release\wal"
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MDd /W3 /GX /Ot /Oi /Op /Oy /Ob2 /I "../../../src/include" /D "NDEBUG" /D "_LIB" /D "SUPERSERVER" /D "WIN32" /D "_MBCS" /D "_X86_" /FD /GZ /c
# SUBTRACT BASE CPP /Z<none> /YX
# ADD CPP /nologo /MDd /W3 /GX /Ot /Oi /Op /Oy /Ob2 /I "../../../src/include" /D "NDEBUG" /D "_LIB" /D "SUPERSERVER" /D "WIN32" /D "_MBCS" /D "_X86_" /FD /GZ /c
# SUBTRACT CPP /Z<none> /YX
# ADD BASE RSC /l 0x41d /d "_DEBUG"
# ADD RSC /l 0x41d /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LIB32=link.exe -lib
# ADD BASE LIB32 /nologo
# ADD LIB32 /nologo

!ENDIF 

# Begin Target

# Name "wal - Win32 Debug"
# Name "wal - Win32 Release"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=..\..\..\src\wal\wal.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\wal\wal_prnt.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\wal\walc.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\wal\walf.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\wal\walr.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\wal\walw.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\wal\wstatus.cpp
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=..\..\..\src\wal\wal.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\wal\wal_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\wal\walc_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\wal\walf_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\wal\walr_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\wal\walw_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\wal\wstat_proto.h
# End Source File
# End Group
# End Target
# End Project
