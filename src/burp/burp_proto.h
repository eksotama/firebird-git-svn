/*
 *	PROGRAM:	JRD Backup and Restore program  
 *	MODULE:		burp_proto.h
 *	DESCRIPTION:	Prototype header file for burp.c
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

#ifndef BURP_BURP_PROTO_H
#define BURP_BURP_PROTO_H

#ifdef SUPERSERVER
int BURP_main(SVC service);
#endif

void	BURP_abort(void);
void	BURP_svc_error(USHORT, USHORT, const void *, USHORT, const void *,
						USHORT, const void *, USHORT, const void *, USHORT, const void *);
void	BURP_error(USHORT, const void*, const void*, const void*, const void*, const void*);
void	BURP_print_status(const ISC_STATUS*);
void	BURP_error_redirect(const ISC_STATUS*, USHORT, const void*, const void*);
void	BURP_msg_partial(USHORT, const void*, const void*, const void*,
						const void*, const void*);
void	BURP_msg_put(USHORT, const void*, const void*, const void*,
					const void*, const void*);
void	BURP_msg_get(USHORT, TEXT*, const void*, const void*, const void*,
					const void*, const void*);
void	BURP_output_version(const TEXT *, TEXT *);
void	BURP_print(USHORT, const void*, const void*, const void*, const void*, const void*);
void	BURP_print_warning(const ISC_STATUS*);
void	BURP_verbose(USHORT, const void*, const void*, const void*, const void*, const void*);

#endif	//  BURP_BURP_PROTO_H

