/*
 *	PROGRAM:	JRD Access Method
 *	MODULE:		rng_proto.h
 *	DESCRIPTION:	Prototype header file for rng.cpp
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

#ifndef JRD_RNG_PROTO_H
#define JRD_RNG_PROTO_H

#ifdef PC_ENGINE

class jrd_req;
struct record_param;
class jrd_nod;
class rng;

void RNG_add_page(ULONG);
void RNG_add_record(record_param*);
jrd_nod* RNG_add_relation(jrd_nod*);
void RNG_add_uncommitted_record(record_param*);
struct dsc *RNG_begin(jrd_nod*, struct vlu *);
jrd_nod *RNG_delete(jrd_nod*);
void RNG_delete_ranges(jrd_req *);
jrd_nod* RNG_end(jrd_nod*);
void RNG_release_locks(rng*);
void RNG_release_ranges(jrd_req *);
void RNG_shutdown_attachment(class Attachment*);
#endif

#endif // JRD_RNG_PROTO_H

