# Microsoft Developer Studio Project File - Name="engine" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Static Library" 0x0104

CFG=engine - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "engine.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "engine.mak" CFG="engine - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "engine - Win32 Release" (based on "Win32 (x86) Static Library")
!MESSAGE "engine - Win32 Debug" (based on "Win32 (x86) Static Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
RSC=rc.exe

!IF  "$(CFG)" == "engine - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "release\engine_ss"
# PROP Intermediate_Dir "release\engine_ss"
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_MBCS" /D "_LIB" /YX /FD /c
# ADD CPP /nologo /MD /W3 /GX /Ot /Oi /Op /Oy /I "../../../src/include" /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_LIB" /D "NOMSG" /D "WIN32_LEAN_AND_MEAN" /D "_X86_" /D "SERVER_SHUTDOWN" /D "SUPERSERVER" /YX /FD /c
# ADD BASE RSC /l 0x419 /d "NDEBUG"
# ADD RSC /l 0x419 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LIB32=link.exe -lib
# ADD BASE LIB32 /nologo
# ADD LIB32 /nologo

!ELSEIF  "$(CFG)" == "engine - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "debug\engine_ss"
# PROP Intermediate_Dir "debug\engine_ss"
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_MBCS" /D "_LIB" /YX /FD /GZ /c
# ADD CPP /nologo /MDd /W3 /Gm /GX /ZI /Od /I "../../../src/include" /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_LIB" /D "NOMSG" /D "WIN32_LEAN_AND_MEAN" /D "_X86_" /D "SUPERSERVER" /D "SERVER_SHUTDOWN" /D "DEV_BUILD" /FD /GZ /c
# SUBTRACT CPP /YX
# ADD BASE RSC /l 0x419 /d "_DEBUG"
# ADD RSC /l 0x419 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LIB32=link.exe -lib
# ADD BASE LIB32 /nologo
# ADD LIB32 /nologo

!ENDIF 

# Begin Target

# Name "engine - Win32 Release"
# Name "engine - Win32 Debug"
# Begin Group "JRD files"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\..\src\jrd\ail.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\all.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\alt.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\blb.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\generated\jrd\blob_filter.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\btr.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\builtin.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\cch.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\cmp.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\cvt.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\cvt2.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\db_alias.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\generated\jrd\dfw.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\divorce.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\dls.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\generated\jrd\dpm.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\dsc.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\generated\jrd\dyn.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\generated\jrd\dyn_def.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\generated\jrd\dyn_del.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\generated\jrd\dyn_mod.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\generated\jrd\dyn_util.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\enc.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\err.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\event.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\evl.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\exe.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\ext.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\filters.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\flu.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\generated\jrd\fun.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\functions.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\gds.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\generated\jrd\grant.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\iberr.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\idx.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\inf.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\generated\jrd\ini.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\intl.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\intl_builtin.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\inuse.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\isc.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\isc_file.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\isc_ipc.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\isc_sync.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\isc_win32.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\jrd.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\jrn.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\lck.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\llio.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\log.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\generated\jrd\met.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\misc.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\mov.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\nav.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\nodebug.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\old.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\opt.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\pag.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\par.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\generated\jrd\pcmet.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\plugin_manager.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\pwd.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\rec.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\rlck.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\rng.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\rse.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\sbm.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\sch.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\generated\jrd\scl.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\sdl.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\sdw.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\shut.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\sort.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\sort_mem.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\sqz.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\status.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\svc.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\sym.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\os\win32\thd_priority.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\thd_win32.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\tpc.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\tra.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\utl.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\val.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\vio.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\why.c
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\winnt.cpp
# End Source File
# End Group
# Begin Group "Header files"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\..\src\jrd\acl.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\aif_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\ail_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\align.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\all.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\all_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\ansi.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\arg.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\atom.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\blb.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\blb_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\blf_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\blk.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\blob_filter.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\blp.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\blr.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\include\gen\blrtable.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\bookmark.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\btr.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\btr_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\build_no.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\cch.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\cch_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\cdefs.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\cmp_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\codes.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\codetext.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\common.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\constants.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\cvt2_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\cvt_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\dbg.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\dbg_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\dflt.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\dfw_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\divorce.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\dls_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\dmp_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\doserr.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\dpm_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\drq.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\dsc.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\dsc_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\dyn.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\dyn_df_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\dyn_dl_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\dyn_md_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\dyn_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\dyn_ut_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\enc_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\entry.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\err_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\event.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\event_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\everything.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\evl_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\exe.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\exe_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\ext.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\ext_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\include\fb_blk.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\fields.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\fil.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\file_params.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\filte_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\flags.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\flu.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\flu_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\fun_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\gds.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\gds.hxx
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\gds_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\gdsassert.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\gdsold.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\grant_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\ib_stdio.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\ibase.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\iberr.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\iberr_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\iberror.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\ibsetjmp.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\ids.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\idx.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\idx_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\inf.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\inf_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\ini.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\ini_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\intl.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\intl_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\intlnames.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\intlobj.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\inuse_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\irq.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\isc.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\isc_f_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\isc_i_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\isc_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\isc_s_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\isc_signal.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\jrd.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\jrd_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\jrn.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\jrn_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\kanji.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\kanji_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\lck.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\lck_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\license.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\llio.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\llio_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\lls.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\lnmdef.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\log.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\log_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\loiter.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\map_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\math.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\mblr.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\mblr_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\met.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\met_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\misc.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\misc_func_ids.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\misc_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\mov_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\mpexl.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\msg.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\msg_encode.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\include\gen\msg_facs.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\include\gen\msgs.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\names.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\nav_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\nlm_thd.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\nod.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\obj.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\ods.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\old.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\old_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\opt_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\pag.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\pag_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\par_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\pcmet_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\perf.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\perf_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\pio.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\pio_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\pragma.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\prv_m_bypass.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\pwd.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\pwd_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\quad.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\quad_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\que.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\rdb.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\rdb_codes.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\rdbcodes.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\rec_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\relations.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\req.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\rlck_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\rng.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\rng_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\rse.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\rse_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\sbm.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\sbm_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\sch_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\scl.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\scl_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\scroll_cursors.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\sdl.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\sdl_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\sdw.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\sdw_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\seg_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\shdef.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\shut_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\sort.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\sort_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\sql_code.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\sqz.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\sqz_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\svc.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\svc_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\svc_undoc.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\syidef.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\sym_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\termtype.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\thd.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\os\thd_priority.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\thd_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\time.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\tpc.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\tpc_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\tra.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\tra_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\trig.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\types.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\utl_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\val.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\val_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\vio_debug.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\vio_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\vms.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\vmslo_proto.h
# End Source File
# Begin Source File

SOURCE=..\..\..\src\jrd\why_proto.h
# End Source File
# End Group
# End Target
# End Project
