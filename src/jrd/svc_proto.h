/*
 *	PROGRAM:	JRD Access Method
 *	MODULE:		svc_proto.h
 *	DESCRIPTION:	Prototype header file for svc.cpp
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

#ifndef JRD_SVC_PROTO_H
#define JRD_SVC_PROTO_H

class Service* SVC_attach(USHORT, const TEXT*, USHORT, const SCHAR*);
void   SVC_cleanup(class Service*);
void   SVC_detach(class Service*);
void   SVC_fprintf(class Service*, const SCHAR*, ...);
void   SVC_putc(class Service*, const UCHAR);
void   SVC_query(class Service*, USHORT, const SCHAR*, USHORT, const SCHAR*,
	USHORT, SCHAR*);
ISC_STATUS SVC_query2(class Service*, struct thread_db*, USHORT, const SCHAR*,
	USHORT, const SCHAR*, USHORT, SCHAR*);
void*  SVC_start(class Service*, USHORT, const SCHAR*);
void   SVC_finish(class Service*, USHORT);
int   SVC_read_ib_log(class Service*);
const TEXT* SVC_err_string(const TEXT*, USHORT);
int SVC_output(class Service*, const UCHAR*);

#ifdef SERVER_SHUTDOWN
typedef void (*shutdown_fct_t) (ULONG);
void SVC_shutdown_init(shutdown_fct_t, ULONG);
#endif /* SERVER_SHUTDOWN */

#endif /* JRD_SVC_PROTO_H */

