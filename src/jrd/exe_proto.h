/*
 *	PROGRAM:	JRD Access Method
 *	MODULE:		exe_proto.h
 *	DESCRIPTION:	Prototype header file for exe.cpp
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

#ifndef JRD_EXE_PROTO_H
#define JRD_EXE_PROTO_H

class jrd_req;

void EXE_assignment(thread_db*, class jrd_nod*);
jrd_req* EXE_find_request(thread_db*, jrd_req *, bool);
void EXE_receive(thread_db*, jrd_req*, USHORT, USHORT, UCHAR*);
void EXE_send(thread_db*, jrd_req*, USHORT, USHORT, UCHAR *);
void EXE_start(thread_db*, jrd_req*, class jrd_tra *);
void EXE_unwind(thread_db*, jrd_req*);
#ifdef SCROLLABLE_CURSORS
void EXE_seek(thread_db*, jrd_req*, USHORT, ULONG);
#endif

#ifdef PC_ENGINE
bool EXE_crack(thread_db*, Rsb*, USHORT);
void EXE_mark_crack(thread_db*, Rsb*, USHORT);
#endif


#endif // JRD_EXE_PROTO_H

