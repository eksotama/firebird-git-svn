#!/bin/sh

# Script to install files from the build/transport area

#    cd InterBase


# A little bit of tidying up of some odd files not in the original build
# These files will exist in the CVS tree.  
# If this is a tar.gz install rather than the result of a build then
# We assume the files have already been copied here manually.

    if  [ -z "$FirebirdInstallPrefix" ]
       then
        FirebirdInstallPrefix=%prefix%
    fi


    if  [ -z "$FirebirdBuildPrefix" ]
       then
        FirebirdBuildPrefix="."
    fi

    BuildDir=$FirebirdBuildPrefix/firebird
    DestDir=$FirebirdInstallPrefix/firebird


#  I think this is done as part of the build now.
#    if [ -z "$InteractiveInstall" ] 
#      then
#        ScriptsSrcDir=src/install/linux
#        cp $ScriptsSrcDir/misc/README $BuildDir
#    fi

# The guts of the tranfer of files to /opt and other directories

    if [ -d $DestDir ] 
      then 
        rm -rf $DestDir 
    fi 
    mkdir $DestDir 
    mkdir $DestDir/bin 
    mkdir $DestDir/examples 
    mkdir $DestDir/help 
    mkdir $DestDir/include 
    mkdir $DestDir/intl 
    mkdir $DestDir/lib 
    mkdir $DestDir/doc 
    mkdir $DestDir/UDF 
    mkdir $DestDir/misc

    cp -f $BuildDir/bin/gds_inet_server $DestDir/bin/gds_inet_server 

    cp $BuildDir/bin/gbak $DestDir/bin/gbak 
    cp $BuildDir/bin/gdef $DestDir/bin/gdef 
    cp $BuildDir/bin/gds_lock_print $DestDir/bin/gds_lock_print 
    cp $BuildDir/bin/gds_drop $DestDir/bin/gds_drop 
    cp $BuildDir/bin/gds_lock_mgr $DestDir/bin/gds_lock_mgr 
    cp $BuildDir/bin/gds_pipe $DestDir/bin/gds_pipe 
    cp $BuildDir/bin/gfix $DestDir/bin/gfix 
    cp $BuildDir/bin/gpre $DestDir/bin/gpre 
    cp $BuildDir/bin/gsec $DestDir/bin/gsec 
    cp $BuildDir/bin/gsplit $DestDir/bin/gsplit 
    cp $BuildDir/bin/gstat $DestDir/bin/gstat 
    cp $BuildDir/bin/isc4.gbak $DestDir/bin/isc4.gbak 
    cp $BuildDir/bin/isql $DestDir/bin/isql 
    cp $BuildDir/bin/qli $DestDir/bin/qli 

    cp $BuildDir/bin/CSchangeRunUser.sh $DestDir/bin
    cp $BuildDir/bin/CSrestoreRootRunUser.sh $DestDir/bin
    cp $BuildDir/bin/changeDBAPassword.sh $DestDir/bin

    cp $BuildDir/examples/v5/*.[ceh] $DestDir/examples 
    cp $BuildDir/examples/v5/*.sql $DestDir/examples 
    cp $BuildDir/examples/v5/*.gbk $DestDir/examples 
    cp $BuildDir/examples/v5/*.gdb $DestDir/examples 
    cp $BuildDir/examples/v5/makefile $DestDir/examples 
    cp $BuildDir/help/help.gbak $DestDir/help 
    cp $BuildDir/help/help.gdb $DestDir/help 
    #cp -r $BuildDir/doc $DestDir 
    cp $BuildDir/interbase.msg $DestDir/interbase.msg 
    cp $BuildDir/isc4.gdb $DestDir/isc4.gdb 
    cp $BuildDir/isc_config $DestDir/isc_config

    cp -f $BuildDir/include/gds.h /usr/include/gds.h 
    cp -f $BuildDir/include/iberror.h /usr/include/iberror.h 
    cp -f $BuildDir/include/ibase.h /usr/include/ibase.h 
    cp -f $BuildDir/include/ib_util.h /usr/include/ib_util.h 

#    cp $BuildDir/include/gds.f $DestDir/include 
#    cp $BuildDir/include/gds.hxx $DestDir/include 
    cp $BuildDir/include/*.h $DestDir/include 

    cp -f $BuildDir/lib/libgds.so /usr/lib/libgds.so.0 
    if [ -L /usr/lib/libgds.so ]
      then
        rm -f /usr/lib/libgds.so
    fi
    ln -s libgds.so.0 /usr/lib/libgds.so

#    cp -f $BuildDir/lib/gds.a /usr/lib/libgds.a 
    cp -f $BuildDir/lib/ib_util.so /usr/lib/libib_util.so 


    cp $BuildDir/intl/libgdsintl.so $DestDir/intl/
    cp $BuildDir/UDF/ib_udf.so $DestDir/UDF/

#    cp $BuildDir/README $DestDir/README

    cp $BuildDir/misc/firebird.xinetd $DestDir/misc/firebird.xinetd

