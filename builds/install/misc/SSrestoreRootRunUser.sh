#!/bin/sh

# A routine to change the user that runs interbase Firebird


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
# Check for a previous install 


checkInstallUser() {

    if [ "`whoami`" != "root" ];
      then
        ehco ""
        echo "--- Warning ----------------------------------------------"
        echo ""
        echo "    You need to be 'root' user to do this change"
        echo ""
        exit
    fi
}


#------------------------------------------------------------------------
#  stop super server if it is running
#  Also will only stop firebird, since that has the init script


stopServerIfRunning() {

    checkString=`ps -efww| egrep "(fbserver|fbguard)" |grep -v grep`

    if [ ! -z "$checkString" ] 
      then
        if [ -f /etc/rc.d/init.d/firebird ]
          then
            /etc/rc.d/init.d/firebird stop
        fi
    fi
}

#------------------------------------------------------------------------
#  check if it is running


checkIfServerRunning() {

    stopServerIfRunning

# Check is server is being actively used.

    checkString=`ps -efww| egrep "(fserver|fbguard)" |grep -v grep`

    if [ ! -z "$checkString" ] 
      then
        echo "An instance of the Firebird/InterBase Super server seems to be running." 
        echo "Please quit all interbase applications and then proceed"
        exit 1 
    fi

    checkString=`ps -efww| egrep "(fb_inet_server|gds_pipe)" |grep -v grep`

    if [ ! -z "$checkString" ] 
      then
        echo "An instance of the Firebird/InterBase server seems to be running." 
        echo "Please quit all interbase applications and then proceed." 
        exit 1 
    fi



# Stop lock manager if it is the only thing running.

    for i in `ps -efww | grep "fb_lock_mgr" | grep -v "grep" | awk '{print $2}' `
     do
        kill $i
     done

}


#------------------------------------------------------------------------
#  Add new user and group


addFirebirdUser() {

    groupadd -g 84 -o -r firebird

    useradd -o -r -m -d /home/firebird -s /bin/bash \
        -c "Firebird Database Administrator" -g firebird -u 84 firebird 

# >/dev/null 2>&1 
}


#------------------------------------------------------------------------
#  Delete new user and group


deleteFirebirdUser() {

    userdel firebird
 #   groupdel firebird

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
# We really expect this to be the case.

        cat $FileName | grep -v "$oldLine" > ${FileName}.tmp
        mv ${FileName}.tmp $FileName
        echo "$newLine" >> $FileName
        echo "Updated."

    fi
}


#------------------------------------------------------------------------
#  changeInitRunUser


changeInitRunUser() {

    NewUser=$1

    InitFile=/etc/rc.d/init.d/firebird
    if [ -f $InitFile ] 
      then
        ed $InitFile <<EOF
/FBRunUser:=/s/FBRunUser:=.*\}/FBRunUser:=$NewUser\}/g
w
q
EOF
    chmod u=rwx,g=rx,o= $InitFile

    fi
}


IBRootDir=/usr/local/firebird
IBBin=$IBRootDir/bin




#RunUser=firebird
#RunGroup=firebird

# Well if you really insist, here it is ;-) - Mark.
#RunUser=interbase
#RunGroup=interbase


# If you want to root user (server run restructed to group root)
RunUser=root
RunGroup=root


checkInstallUser


echo ""
echo "Change Firebird install for $IBRootDir to uid=$RunUser gid=$RunGroup"
echo "(User or group options can be changed by editing this script)"
echo ""
AskQuestion "Press return to continue - or ^C to abort"


checkIfServerRunning


# Update ownership and SUID bits for programs.
echo "Updating $IBRootDir"


chown -R $RunUser.$RunGroup $IBRootDir

# Turn everybody option off.
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


# User can run these programs, they need to talk to server though.
# and they cannot actually create a database.

chmod o=rx isql 
chmod o=rx qli



cd $IBRootDir

touch firebird.log
chmod ug=rw,o= firebird.log


# make examples writable by group
chmod ug=rw,o= examples/*.fdb


changeInitRunUser $RunUser

# start the db server 

(cd /etc/rc.d/init.d; ./firebird start)

echo "Completed."
