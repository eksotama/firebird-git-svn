#!/bin/sh

# The post install script for Firebird Classic


#------------------------------------------------------------------------
# Prompt for response, store result in Answer

Answer=""

AskQuestion() {
    Test=$1
    DefaultAns=$2
    echo -n "${1}"
    Answer="$DefaultAns"
    read Answer
}


#------------------------------------------------------------------------
# add a service line in the (usually) /etc/services or /etc/inetd.conf file
# Here there are three cases, not found         => add service line,
#                             found & different => ask user to check
#                             found & same      => do nothing
#                             

replaceLineInFile() {
    FileName=$1
    newLine=$2
    oldLine=$3

    if [ -z "$oldLine" ] 
      then
        echo "$newLine" >> $FileName

    elif [ "$oldLine" != "$newLine"  ]
      then
        echo ""
        echo "--- Warning ----------------------------------------------"
        echo ""
        echo "    In file $FileName found line: "
        echo "    $oldLine"
        echo "    Which differs from the expected line:"
        echo "    $newLine"
        echo ""

#        AskQuestion "Press return to update file or ^C to abort install"

        cat $FileName | grep -v "$oldLine" > ${FileName}.tmp
        mv ${FileName}.tmp $FileName
        echo "$newLine" >> $FileName
        echo "Updated."

    fi
}

#------------------------------------------------------------------------
#  Add new user and group


addFirebirdUser() {

    testStr=`grep firebird /etc/group`

    if [ -z "$testStr" ]
      then
        groupadd -g 84 -o -r firebird
    fi

    testStr=`grep firebird /etc/passwd`
    if [ -z "$testDir" ]
      then
        useradd -o -r -m -d $IBRootDir -s /bin/bash \
            -c "Firebird Database Administrator" -g firebird -u 84 firebird 

        # >/dev/null 2>&1 
    fi
}


#------------------------------------------------------------------------
#  Delete new user and group


deleteFirebirdUser() {

    userdel firebird
 #   groupdel firebird

}


#------------------------------------------------------------------------
#  changeXinetdServiceUser
#  Change the run user of the xinetd service

changeXinetdServiceUser() {

    InitFile=/etc/xinetd.d/firebird
    if [ -f $InitFile ] 
      then
        ed -s $InitFile <<EOF
/	user	/s/=.*$/= $RunUser/g
w
q
EOF
    fi
}

#------------------------------------------------------------------------
#  Update inetd service entry
#  This just adds/replaces the service entry line

updateInetdEntry() {

    FileName=/etc/inetd.conf
    newLine="gds_db  stream  tcp     nowait.30000      $RunUser $IBBin/gds_inet_server gds_inet_server # InterBase Database Remote Server"
    oldLine=`grep "^gds_db" $FileName`

    replaceLineInFile "$FileName" "$newLine" "$oldLine"
}

#------------------------------------------------------------------------
#  Update xinetd service entry

updateXinetdEntry() {

    cp $IBRootDir/misc/firebird.xinetd /etc/xinetd.d/firebird
    changeXinetdServiceUser
}


#------------------------------------------------------------------------
#  Update inetd service entry 
#  Check to see if we have xinetd installed or plain inetd.  Install differs
#  for each of them.

updateInetdServiceEntry() {

    if [ -d /etc/xinetd.d ] 
      then
        updateXinetdEntry
    else
        updateInetdEntry
    fi

}




#------------------------------------------------------------------------
#  Unable to generate the password for the rpm, so put out a message 
#  instead


keepOrigDBAPassword() {

    DBAPasswordFile=$IBRootDir/SYSDBA.password
    
    NewPasswd='masterkey'
    echo "Firebird initial install password " > $DBAPasswordFile
    echo "for user SYSDBA is : $NewPasswd" >> $DBAPasswordFile

    echo "for install on `hostname` at time `date`" >> $DBAPasswordFile
    echo "You should change this password at the earliest oportunity" >> $DBAPasswordFile
    echo ""

    echo "(For superserver you will also want to check the password in the" >> $DBAPasswordFile
    echo "daemon init routine in the file /etc/init.d/firebird)" >> $DBAPasswordFile
    echo "" >> $DBAPasswordFile
    echo "Your should password can be changed to a more suitable one using the" >> $DBAPasswordFile
    echo "/opt/interbase/bin/changeDBAPassword.sh script" >> $DBAPasswordFile
    echo "" >> $DBAPasswordFile

    chmod u=r,go= $DBAPasswordFile


}


#------------------------------------------------------------------------
#  Generate new sysdba password - this routine is used only in the 
#  rpm file not in the install acript.


generateNewDBAPassword() {

    DBAPasswordFile=$IBRootDir/SYSDBA.password
    
    NewPasswd=`/usr/bin/mkpasswd -l 8`

    echo "Firebird generated password " > $DBAPasswordFile
    echo "for user SYSDBA is : $NewPasswd" >> $DBAPasswordFile
    echo "generated on `hostname` at time `date`" >> $DBAPasswordFile
    echo "(For superserver you will also want to check the password in the" >> $DBAPasswordFile
    echo "daemon init routine in the file /etc/rc.d/init.d/firebird)" >> $DBAPasswordFile
    echo "" >> $DBAPasswordFile
    echo "Your password can be changed to a more suitable one using the" >> $DBAPasswordFile
    echo "/opt/interbase/bin/changeDBAPassword.sh script" >> $DBAPasswordFile
    echo "" >> $DBAPasswordFile
    chmod u=r,go= $DBAPasswordFile

    $IBBin/gsec -user sysdba -password masterkey <<EOF
modify sysdba -pw $NewPasswd
EOF

}

#------------------------------------------------------------------------
#  Change sysdba password - this routine is interactive and is only 
#  used in the install shell script not the rpm one.


askUserForNewDBAPassword() {

    NewPasswd=""

    while [ -z "$NewPasswd" ]
      do
# If using a generated password
#         DBAPasswordFile=$IBRootDir/SYSDBA.password
#         NewPasswd=`mkpasswd -l 8`
#         echo "Password for SYSDBA on `hostname` is : $NewPasswd" > $DBAPasswordFile
#         chmod ga-rwx $DBAPasswordFile

          AskQuestion "Please enter new password for SYSDBA user: "
          NewPasswd=$Answer
          if [ ! -z "$NewPasswd" ]
            then
             $IBBin/gsec -user sysdba -password masterkey <<EOF
modify sysdba -pw $NewPasswd
EOF
              echo ""
          fi
          
      done
}


#------------------------------------------------------------------------
#  Change sysdba password - this routine is interactive and is only 
#  used in the install shell script not the rpm one.

#  On some systems the mkpasswd program doesn't appear and on others
#  there is another mkpasswd which does a different operation.  So if
#  the specific one isn't available then keep the original password.


changeDBAPassword() {

    if [ -z "$InteractiveInstall" ]
      then
        if [ -f /usr/bin/mkpasswd ]
            then
              generateNewDBAPassword
        else
              keepOrigDBAPassword
        fi
      else
        askUserForNewDBAPassword
    fi
}


#------------------------------------------------------------------------
#  fixFilePermissions
#  Change the permissions to restrict access to server programs to 
#  firebird group only.  This is MUCH better from a saftey point of 
#  view than installing as root user, even if it requires a little 
#  more work.


fixFilePermissions() {

    # Turn other access off.
    chmod -R o= $IBRootDir


    # Now fix up the mess.

    # fix up directories 
    for i in `find $IBRootDir -print`
    do
        FileName=$i
        if [ -d $FileName ]
        then
            chmod o=rx $FileName
        fi
    done


    cd $IBBin


    # set up the defaults for bin
    for i in `ls`
      do
         chmod ug=rx,o=  $i
    done

    # User can run these programs, they need to talk to server though.
    # and they cannot actually create a database.
     

    chmod a=rx isql 
    chmod a=rx qli
    
    # SUID is still needed for group direct access.  General users
    # cannot run though.
    for i in gds_lock_mgr gds_drop gds_inet_server
    do
        chmod ug=rx,o= $i
        chmod ug+s $i
    done


    cd $IBRootDir

    # Fix lock files
    for i in isc_init1 isc_lock1 isc_event1 
      do
        FileName=$i.`hostname`
        chmod ug=rw,o= $FileName
      done


    chmod ug=rw,o= interbase.log

    chmod a=r interbase.msg
    chmod ug=rw,o= help/help.gdb
    chmod ug=rw,o= isc4.gdb


    # Set a default of read all files in examples

    cd examples

    for i in `ls`
      do
         chmod a=r  $i
    done

    # make examples db's writable by group
    chmod ug=rw,o= *.gdb

    cd ..


    # make include files world readable 
    cd include

    for i in `ls`
      do
         chmod a=r  $i
    done

    cd ..

    # make intl library world readable
    cd intl

    for i in `ls`
      do
         chmod a=r  $i
    done

    cd ..
}


#------------------------------------------------------------------------
#  fixFilePermissionsForRoot
#  This sets the file permissions up to what you need if you are
#  running the server as root user.  I hope to remove this mode
#  of running before the next version, since it's security level
#  is absolutely woeful.


fixFilePermissionsRoot() {

    # Turn other access off.
    chmod -R o= $IBRootDir

    # Now fix up the mess.

    # fix up directories 
    for i in `find $IBRootDir -print`
    do
        FileName=$i
        if [ -d $FileName ]
        then
            chmod o=rx $FileName
        fi
    done


    cd $IBBin


    # set up the defaults for bin
    for i in `ls`
      do
         chmod o=rx  $i
    done

    
    # SUID is still needed for group direct access.  General users
    # cannot run though.
    for i in gds_lock_mgr gds_drop gds_inet_server
    do
        chmod ug+s $i
    done


    cd $IBRootDir

    # Fix lock files
    for i in isc_init1 isc_lock1 isc_event1 
      do
        FileName=$i.`hostname`
        chmod a=rw $FileName
      done


    chmod a=rw interbase.log

    chmod a=r interbase.msg
    chmod a=rw help/help.gdb
    chmod a=rw isc4.gdb


    # Set a default of read all files in examples

    cd examples

    for i in `ls`
      do
         chmod a=r  $i
    done

    # make examples db's writable by group
    chmod a=rw *.gdb

    cd ..

    # make include files world readable 
    cd include

    for i in `ls`
      do
         chmod a=r  $i
    done

    cd ..

    # make intl library world readable
    cd intl

    for i in `ls`
      do
         chmod a=r  $i
    done

    cd ..
}

#------------------------------------------------------------------------
#  resetXinitdServer
#  Check for both inetd and xinetd, only one will actually be running.
#  depending upon your system.

resetInetdServer() {

    if [ -f /var/run/inetd.pid ]
      then
        kill -HUP `cat /var/run/inetd.pid`
    fi

    if [ -f /var/run/xinetd.pid ]
      then
        kill -HUP `cat /var/run/xinetd.pid`
    fi
}

#= Main Post ===============================================================

    # Make sure the links are in place 
    if [ ! -L /opt/interbase -a ! -d /opt/interbase ] 
      then 
    # Main link and... 
        ln -s $RPM_INSTALL_PREFIX/interbase /opt/interbase 
    fi 


    IBRootDir=/opt/interbase
    IBBin=$IBRootDir/bin
    RunUser=root
#    RunUser=firebird


    # Update /etc/services

    FileName=/etc/services
    newLine="gds_db          3050/tcp  # InterBase Database Remote Protocol"
    oldLine=`grep "^gds_db" $FileName`

    replaceLineInFile "$FileName" "$newLine" "$oldLine"


    # add Firebird user
    if [ $RunUser = "firebird" ]
      then
        addFirebirdUser
    fi


    # Create Lock files
    cd $IBRootDir

    for i in isc_init1 isc_lock1 isc_event1 
      do
        FileName=$i.`hostname`
        touch $FileName
      done

    # Create log
    touch interbase.log


    # Update ownership and SUID bits for programs.
    chown -R $RunUser.$RunUser $IBRootDir
    if [ "$RunUser" = "root" ]
      then
        fixFilePermissionsRoot
    else
        fixFilePermissions
    fi

    # Update the /etc/inetd.conf or xinetd entry
    updateInetdServiceEntry


    # Get inetd to reread new init files.
    resetInetdServer


    cd $IBRootDir
    # Change sysdba password
    changeDBAPassword



