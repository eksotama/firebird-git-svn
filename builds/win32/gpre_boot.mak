
NULL=

CPP=cl.exe
RSC=rc.exe

OUTDIR=.\temp
INTDIR=.\temp

ALL : ".\gpre_boot.exe" "$(OUTDIR)\gpre_boot.bsc"

CLEAN :
	-@erase "$(INTDIR)\ada.obj"
	-@erase "$(INTDIR)\ada.sbr"
	-@erase "$(INTDIR)\c_cxx.obj"
	-@erase "$(INTDIR)\c_cxx.sbr"
	-@erase "$(INTDIR)\cmd.obj"
	-@erase "$(INTDIR)\cmd.sbr"
	-@erase "$(INTDIR)\cme.obj"
	-@erase "$(INTDIR)\cme.sbr"
	-@erase "$(INTDIR)\cmp.obj"
	-@erase "$(INTDIR)\cmp.sbr"
	-@erase "$(INTDIR)\cob.obj"
	-@erase "$(INTDIR)\cob.sbr"
	-@erase "$(INTDIR)\dls.obj"
	-@erase "$(INTDIR)\dls.sbr"
	-@erase "$(INTDIR)\dsc.obj"
	-@erase "$(INTDIR)\dsc.sbr"
	-@erase "$(INTDIR)\exp.obj"
	-@erase "$(INTDIR)\exp.sbr"
	-@erase "$(INTDIR)\fb_exception.obj"
	-@erase "$(INTDIR)\fb_exception.sbr"
	-@erase "$(INTDIR)\gds.obj"
	-@erase "$(INTDIR)\gds.sbr"
	-@erase "$(INTDIR)\gpre.obj"
	-@erase "$(INTDIR)\gpre.sbr"
	-@erase "$(INTDIR)\gpre_meta_boot.obj"
	-@erase "$(INTDIR)\gpre_meta_boot.sbr"
	-@erase "$(INTDIR)\hsh.obj"
	-@erase "$(INTDIR)\hsh.sbr"
	-@erase "$(INTDIR)\int.obj"
	-@erase "$(INTDIR)\int.sbr"
	-@erase "$(INTDIR)\int_cxx.obj"
	-@erase "$(INTDIR)\int_cxx.sbr"
	-@erase "$(INTDIR)\isc.obj"
	-@erase "$(INTDIR)\isc.sbr"
	-@erase "$(INTDIR)\isc_ipc.obj"
	-@erase "$(INTDIR)\isc_ipc.sbr"
	-@erase "$(INTDIR)\isc_sync.obj"
	-@erase "$(INTDIR)\isc_sync.sbr"
	-@erase "$(INTDIR)\jrdmet.obj"
	-@erase "$(INTDIR)\jrdmet.sbr"
	-@erase "$(INTDIR)\movg.obj"
	-@erase "$(INTDIR)\movg.sbr"
	-@erase "$(INTDIR)\msc.obj"
	-@erase "$(INTDIR)\msc.sbr"
	-@erase "$(INTDIR)\noform.obj"
	-@erase "$(INTDIR)\noform.sbr"
	-@erase "$(INTDIR)\par.obj"
	-@erase "$(INTDIR)\par.sbr"
	-@erase "$(INTDIR)\pas.obj"
	-@erase "$(INTDIR)\pas.sbr"
	-@erase "$(INTDIR)\pat.obj"
	-@erase "$(INTDIR)\pat.sbr"
	-@erase "$(INTDIR)\pretty.obj"
	-@erase "$(INTDIR)\pretty.sbr"
	-@erase "$(INTDIR)\sqe.obj"
	-@erase "$(INTDIR)\sqe.sbr"
	-@erase "$(INTDIR)\sql.obj"
	-@erase "$(INTDIR)\sql.sbr"
	-@erase "$(INTDIR)\thd.obj"
	-@erase "$(INTDIR)\thd.sbr"
	-@erase "$(INTDIR)\vc60.idb"
	-@erase "$(INTDIR)\version.res"
	-@erase "$(OUTDIR)\gpre_boot.bsc"
	-@erase ".\pre_boot.exe"

"$(OUTDIR)" :
    if not exist "$(OUTDIR)/$(NULL)" mkdir "$(OUTDIR)"

CPP_PROJ=/nologo /MD /W3 /GX /Ot /Oi /Oy /Ob2 /I "../../src/include" /D "NDEBUG" /D "_CONSOLE" /D "SUPERCLIENT" /D "WIN32" /D "_MBCS" /D "_X86_" /D "WIN32_LEAN_AND_MEAN" /FR"$(INTDIR)\\" /Fo"$(INTDIR)\\" /Fd"$(INTDIR)\\" /FD /GZ /c 
BSC32=bscmake.exe
BSC32_FLAGS=/nologo /o"$(OUTDIR)\gpre_boot.bsc" 
BSC32_SBRS= \
	"$(INTDIR)\ada.sbr" \
	"$(INTDIR)\c_cxx.sbr" \
	"$(INTDIR)\cmd.sbr" \
	"$(INTDIR)\cme.sbr" \
	"$(INTDIR)\cmp.sbr" \
	"$(INTDIR)\cob.sbr" \
	"$(INTDIR)\exp.sbr" \
	"$(INTDIR)\fb_exception.sbr" \
	"$(INTDIR)\gpre.sbr" \
	"$(INTDIR)\gpre_meta_boot.sbr" \
	"$(INTDIR)\hsh.sbr" \
	"$(INTDIR)\int.sbr" \
	"$(INTDIR)\int_cxx.sbr" \
	"$(INTDIR)\jrdmet.sbr" \
	"$(INTDIR)\movg.sbr" \
	"$(INTDIR)\msc.sbr" \
	"$(INTDIR)\noform.sbr" \
	"$(INTDIR)\par.sbr" \
	"$(INTDIR)\pas.sbr" \
	"$(INTDIR)\pat.sbr" \
	"$(INTDIR)\pretty.sbr" \
	"$(INTDIR)\sqe.sbr" \
	"$(INTDIR)\sql.sbr" \
	"$(INTDIR)\dls.sbr" \
	"$(INTDIR)\dsc.sbr" \
	"$(INTDIR)\gds.sbr" \
	"$(INTDIR)\isc.sbr" \
	"$(INTDIR)\isc_ipc.sbr" \
	"$(INTDIR)\isc_sync.sbr" \
	"$(INTDIR)\thd.sbr"

"$(OUTDIR)\gpre_boot.bsc" : "$(OUTDIR)" $(BSC32_SBRS)
    $(BSC32) @<<
  $(BSC32_FLAGS) $(BSC32_SBRS)
<<

LINK32=link.exe
LINK32_FLAGS=kernel32.lib user32.lib gdi32.lib shell32.lib comctl32.lib advapi32.lib ws2_32.lib mpr.lib version.lib /nologo /subsystem:console /incremental:no /pdb:"$(OUTDIR)\gpre_boot.pdb" /machine:I386 /out:"./gpre_boot.exe" /pdbtype:sept 
LINK32_OBJS= \
	"$(INTDIR)\ada.obj" \
	"$(INTDIR)\c_cxx.obj" \
	"$(INTDIR)\cmd.obj" \
	"$(INTDIR)\cme.obj" \
	"$(INTDIR)\cmp.obj" \
	"$(INTDIR)\cob.obj" \
	"$(INTDIR)\exp.obj" \
	"$(INTDIR)\fb_exception.obj" \
	"$(INTDIR)\gpre.obj" \
	"$(INTDIR)\gpre_meta_boot.obj" \
	"$(INTDIR)\hsh.obj" \
	"$(INTDIR)\int.obj" \
	"$(INTDIR)\int_cxx.obj" \
	"$(INTDIR)\jrdmet.obj" \
	"$(INTDIR)\movg.obj" \
	"$(INTDIR)\msc.obj" \
	"$(INTDIR)\noform.obj" \
	"$(INTDIR)\par.obj" \
	"$(INTDIR)\pas.obj" \
	"$(INTDIR)\pat.obj" \
	"$(INTDIR)\pretty.obj" \
	"$(INTDIR)\sqe.obj" \
	"$(INTDIR)\sql.obj" \
	"$(INTDIR)\dls.obj" \
	"$(INTDIR)\dsc.obj" \
	"$(INTDIR)\gds.obj" \
	"$(INTDIR)\isc.obj" \
	"$(INTDIR)\isc_ipc.obj" \
	"$(INTDIR)\isc_sync.obj" \
	"$(INTDIR)\thd.obj" \
	"$(INTDIR)\version.res" \
	".\temp\common.lib"

".\gpre_boot.exe" : "$(OUTDIR)" $(DEF_FILE) $(LINK32_OBJS)
    $(LINK32) @<<
  $(LINK32_FLAGS) $(LINK32_OBJS)
<<

.c{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cpp{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cxx{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.c{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cpp{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cxx{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

RSC_PROJ=/l 0x41d /fo"$(INTDIR)\version.res"

SOURCE=..\..\src\gpre\ada.cpp

"$(INTDIR)\ada.obj"	"$(INTDIR)\ada.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=..\..\src\gpre\c_cxx.cpp

"$(INTDIR)\c_cxx.obj"	"$(INTDIR)\c_cxx.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=..\..\src\gpre\cmd.cpp

"$(INTDIR)\cmd.obj"	"$(INTDIR)\cmd.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=..\..\src\gpre\cme.cpp

"$(INTDIR)\cme.obj"	"$(INTDIR)\cme.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=..\..\src\gpre\cmp.cpp

"$(INTDIR)\cmp.obj"	"$(INTDIR)\cmp.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=..\..\src\gpre\cob.cpp

"$(INTDIR)\cob.obj"	"$(INTDIR)\cob.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=..\..\src\gpre\exp.cpp

"$(INTDIR)\exp.obj"	"$(INTDIR)\exp.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=..\..\src\common\fb_exception.cpp

"$(INTDIR)\fb_exception.obj"	"$(INTDIR)\fb_exception.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=..\..\src\gpre\gpre.cpp

"$(INTDIR)\gpre.obj"	"$(INTDIR)\gpre.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=..\..\src\gpre\gpre_meta_boot.cpp

"$(INTDIR)\gpre_meta_boot.obj"	"$(INTDIR)\gpre_meta_boot.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=..\..\src\gpre\hsh.cpp

"$(INTDIR)\hsh.obj"	"$(INTDIR)\hsh.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=..\..\src\gpre\int.cpp

"$(INTDIR)\int.obj"	"$(INTDIR)\int.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=..\..\src\gpre\int_cxx.cpp

"$(INTDIR)\int_cxx.obj"	"$(INTDIR)\int_cxx.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=..\..\src\gpre\jrdmet.cpp

"$(INTDIR)\jrdmet.obj"	"$(INTDIR)\jrdmet.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=..\..\src\gpre\movg.cpp

"$(INTDIR)\movg.obj"	"$(INTDIR)\movg.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=..\..\src\gpre\msc.cpp

"$(INTDIR)\msc.obj"	"$(INTDIR)\msc.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=..\..\src\gpre\noform.cpp

"$(INTDIR)\noform.obj"	"$(INTDIR)\noform.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=..\..\src\gpre\par.cpp

"$(INTDIR)\par.obj"	"$(INTDIR)\par.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=..\..\src\gpre\pas.cpp

"$(INTDIR)\pas.obj"	"$(INTDIR)\pas.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=..\..\src\gpre\pat.cpp

"$(INTDIR)\pat.obj"	"$(INTDIR)\pat.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=..\..\src\gpre\pretty.cpp

"$(INTDIR)\pretty.obj"	"$(INTDIR)\pretty.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=..\..\src\gpre\sqe.cpp

"$(INTDIR)\sqe.obj"	"$(INTDIR)\sqe.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=..\..\src\gpre\sql.cpp

"$(INTDIR)\sql.obj"	"$(INTDIR)\sql.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=..\..\src\jrd\dls.cpp

"$(INTDIR)\dls.obj"	"$(INTDIR)\dls.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=..\..\src\jrd\dsc.cpp

"$(INTDIR)\dsc.obj"	"$(INTDIR)\dsc.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=..\..\src\jrd\gds.cpp

"$(INTDIR)\gds.obj"	"$(INTDIR)\gds.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=..\..\src\jrd\isc.cpp

"$(INTDIR)\isc.obj"	"$(INTDIR)\isc.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=..\..\src\jrd\isc_ipc.cpp

"$(INTDIR)\isc_ipc.obj"	"$(INTDIR)\isc_ipc.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=..\..\src\jrd\isc_sync.cpp

"$(INTDIR)\isc_sync.obj"	"$(INTDIR)\isc_sync.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=..\..\src\jrd\thd.cpp

"$(INTDIR)\thd.obj"	"$(INTDIR)\thd.sbr" : $(SOURCE) "$(INTDIR)"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=..\..\src\jrd\version.rc

"$(INTDIR)\version.res" : $(SOURCE) "$(INTDIR)"
	$(RSC) /l 0x41d /fo"$(INTDIR)\version.res" $(SOURCE)

