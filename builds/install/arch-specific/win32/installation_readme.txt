Firebird Database Server 2.1
==================================================


This document is a guide to installing this package of
Firebird 2.1 on the Win32 platform. These notes refer
to the installation package itself, rather than
Firebird 2.1 in general.

It is assumed that readers of this document are already
familiar Firebird 2.0. If you are evaluating Fb 2.1 as
part of a migration from Fb 1.5 you are advised to
review the Fb 2.0 documentation to understand the
changes made between 1.5 and 2.0.


Contents
--------

o Before installation
o Known installation problems
o Uninstallation
o Other Notes
o Installation from a batch file


Before installation
-------------------

It is recommended that you UNINSTALL all previous
versions of Firebird or InterBase before installing
this package. It is especially important to verify that
fbclient.dll and gds32.dll are removed from <system32>.


Known installation problems
---------------------------

o It is not (yet) possible to use the binary installer
  to install more than one instance of Firebird 2.1.

o Unfortunately, (at the time of Beta 1) the installer
  cannot detect if other versions of Firebird are
  running.

o There are known areas of overlap between the
  32-bit and 64-bit installs:

  - The Control Panel | Add or Remove Programs section
	does not indicate whether the installed version
	is 32-bit or 64-bit.

  - The service installer (instsvc) uses the same
	instance name.

o The 64-bit install does not yet provide the 32-bit
  client libraries.

These issues will be resolved in a subsequent beta
release.


Uninstallation
--------------

o It is preferred that this package be uninstalled
  correctly using the uninstallation application
  supplied. This can be called from the Control Panel.
  Alternatively it can be uninstalled by running
  unins000.exe directly from the installation
  directory.

o If Firebird is running as an application (instead of
  as a service) it is recommended that you manually
  stop the server before running the uninstaller. This
  is because the uninstaller cannot stop a running
  application. If a server is running during the
  uninstall the uninstall will complete with errors.
  You will have to delete the remnants by hand.

o Uninstallation leaves four files in the install
  directory:

  - aliases.conf
  - firebird.conf
  - firebird.log
  - security2.fdb

  This is intentional. These files are all
  potentially modifiable by users and may be required
  if Firebird is re-installed in the future. They can
  be deleted if no longer required.


Other Notes
-----------

  Firebird requires WinSock2. All Win32 platforms
  should have this, except for Win95. A test for the
  Winsock2 library is made during install. If it is
  not found the install will fail. You can visit
  this link:

	http://support.microsoft.com/default.aspx?scid=kb;EN-US;q177719

  to find out how to go about upgrading.


Installation from a batch file
------------------------------

The setup program can be run from a batch file.
Please see this document:

	installation_scripted.txt

for full details.


