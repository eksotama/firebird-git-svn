/*
 *	PROGRAM:	JRD Remote Interface/Server
 *      MODULE:         xnet_proto.h
 *      DESCRIPTION:    Prototpe header file for xnet.c
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
 *
 * 2003.05.01 Victor Seryodkin, Dmitry Yemanov: Completed XNET implementation
 */

#ifndef _REMOTE_XNET_PROTO_H_
#define _REMOTE_XNET_PROTO_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifdef NO_PORT
#define PORT void*
#endif

extern PORT XNET_analyze(TEXT *, USHORT *, ISC_STATUS *, TEXT *, TEXT *, USHORT);
extern PORT XNET_connect(TEXT *, struct packet *, ISC_STATUS *, USHORT);

#ifndef SUPERCLIENT
extern void XNET_srv(USHORT flag);
extern PORT XNET_reconnect(ULONG, TEXT *, ISC_STATUS *);
#endif

#ifdef __cplusplus
}	/* extern "C" */
#endif

#endif /* _REMOTE_XNET_PROTO_H */ 
