/*
 *	PROGRAM:	Windows NT installation utilities
 *	MODULE:		install.h
 *	DESCRIPTION:	Defines for Windows NT installation utilities
 *
 * The contents of this file are subject to the Interbase Public
 * License Version 1.0 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy
 * of the License at http://www.Inprise.com/IPL.html
 *
 * Software distributed under the License is distributed on an
 * "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express
 * or implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * The Original Code was created by Inprise Corporation
 * and its predecessors. Portions created by Inprise Corporation are
 * Copyright (C) Inprise Corporation.
 *
 * All Rights Reserved.
 * Contributor(s): ______________________________________.
 */

#ifndef _UTILITIES_INSTALL_NT_H_
#define _UTILITIES_INSTALL_NT_H_

#if (defined SUPERCLIENT || defined SUPERSERVER)
#define REMOTE_SERVICE		"InterBaseServer"
#define REMOTE_DISPLAY_NAME	"Firebird Server"
#define REMOTE_EXECUTABLE	"bin\\ibserver"
#define ISCGUARD_SERVICE	"InterBaseGuardian"
#define ISCGUARD_DISPLAY_NAME "Firebird Guardian Service"
#define ISCGUARD_EXECUTABLE	"bin\\ibguard"
#define GUARDIAN_MUTEX      "InterBaseGuardianMutex"
/* Starting with 128 the service prams are user defined */
#define SERVICE_CREATE_GUARDIAN_MUTEX 128
#else
#define REMOTE_SERVICE		"InterBaseRemoteService"
#define REMOTE_DISPLAY_NAME	"Firebird Remote Service"
#define REMOTE_EXECUTABLE	"bin\\ibremote"
#endif
#define REMOTE_DEPENDENCIES	"Tcpip\0\0"

#define COMMAND_NONE		0
#define COMMAND_INSTALL		1
#define COMMAND_REMOVE		2
#define COMMAND_START		3
#define COMMAND_STOP		4
#define COMMAND_CONFIG		5

#define STARTUP_DEMAND		0
#define STARTUP_AUTO		1

#define NO_GUARDIAN			0
#define USE_GUARDIAN		1

#define DEFAULT_CLIENT		0
#define NORMAL_PRIORITY		1
#define HIGH_PRIORITY		2

#define IB_SERVICE_ALREADY_DEFINED	100
#define IB_SERVICE_RUNNING		101

#define IB_GUARDIAN_ALREADY_DEFINED  102
#define IB_GUARDIAN_RUNNING		103

#endif /* _UTILITIES_INSTALL_NT_H_ */
