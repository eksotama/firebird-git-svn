#!/bin/sh

# This file is used both to rebuild the header file and to set the 
# environment variables on the config call

BuildVersion="$Id: writeBuildNum.sh,v 1.277 2002-04-09 23:21:24 bellardo Exp $"

BuildType=T
MajorVer=2
MinorVer=0
RevNo=0
BuildNum=306
BuildSuffix="Firebird2 Dev1"

FIREBIRD_VERSION="$MajorVer.$MinorVer.$RevNo"
PRODUCT_VER_STRING="$MajorVer.$MinorVer.$RevNo.$BuildNum"
FILE_VER_STRING="WI-$BuildType$MajorVer.$MinorVer.$RevNo.$BuildNum"
FILE_VER_NUMBER="$MajorVer, $MinorVer, $RevNo, $BuildNum"


headerFile=src/jrd/build_no.h
tempfile=gen/test.header.txt

#______________________________________________________________________________
# Routine to build a new jrd/build_no.h file. If required.

rebuildHeaderFile() {

cat > $tempfile <<eof
/*
  FILE GENERATED BY src/misc/writeBuildNum.sh 
               *** DO NOT EDIT ***
  TO CHANGE ANY INFORMATION IN HERE PLEASE
  EDIT src/misc/writeBuildNum.sh
  FORMAL BUILD NUMBER:$BuildNum 
*/

#define PRODUCT_VER_STRING "$PRODUCT_VER_STRING"
#define FILE_VER_STRING "$FILE_VER_STRING"
#define LICENSE_VER_STRING "$FILE_VER_STRING"
#define FILE_VER_NUMBER $FILE_VER_NUMBER
#define FB_MAJOR_VER "$MajorVer"
#define FB_MINOR_VER "$MinorVer"
#define FB_REV_NO "$RevNo"
#define FB_BUILD_NO "$BuildNum"
#define FB_BUILD_TYPE "$BuildType"
#define FB_BUILD_SUFFIX "$BuildSuffix"
eof
    cmp -s $headerFile tempfile
    Result=$?
    if [ $Result -lt 0 ]
       then
         echo "error compareing $tempfile and $headerFile"
    elif [ $Result -gt 0 ]
      then
      echo "updating header file $headerFile"
      cp $tempfile $headerFile
    else
      echo "files are identical"
    fi
}



if [ "$1" = "rebuildHeader" ]
  then
    rebuildHeaderFile
fi
