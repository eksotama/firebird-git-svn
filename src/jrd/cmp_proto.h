/*
 *	PROGRAM:	JRD Access Method
 *	MODULE:		cmp_proto.h
 *	DESCRIPTION:	Prototype header file for cmp.cpp
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

#ifndef JRD_CMP_PROTO_H
#define JRD_CMP_PROTO_H

#include "../jrd/req.h"
// req.h includes exe.h => Jrd::CompilerScratch and Jrd::CompilerScratch::csb_repeat.

bool CMP_clone_is_active(const Jrd::jrd_req*);
Jrd::jrd_nod* CMP_clone_node(Jrd::thread_db*, Jrd::CompilerScratch*, Jrd::jrd_nod*);
Jrd::jrd_req* CMP_clone_request(Jrd::thread_db*, Jrd::jrd_req*, USHORT, bool);
Jrd::jrd_req* CMP_compile(USHORT, const UCHAR*, USHORT);
Jrd::jrd_req* CMP_compile2(Jrd::thread_db*, const UCHAR*, USHORT);
Jrd::CompilerScratch::csb_repeat* CMP_csb_element(Jrd::CompilerScratch*, USHORT);
void CMP_expunge_transaction(Jrd::jrd_tra*);
void CMP_decrement_prc_use_count(Jrd::thread_db*, Jrd::jrd_prc*);
Jrd::jrd_req* CMP_find_request(Jrd::thread_db*, USHORT, USHORT);
void CMP_fini(Jrd::thread_db*);
Jrd::Format* CMP_format(Jrd::thread_db*, Jrd::CompilerScratch*, USHORT);
void CMP_get_desc(Jrd::thread_db*, Jrd::CompilerScratch*, Jrd::jrd_nod*, dsc*);
Jrd::IndexLock* CMP_get_index_lock(Jrd::thread_db*, Jrd::jrd_rel*, USHORT);
SLONG CMP_impure(Jrd::CompilerScratch*, USHORT);
Jrd::jrd_req* CMP_make_request(Jrd::thread_db*, Jrd::CompilerScratch*);
void CMP_post_access(Jrd::thread_db*, Jrd::CompilerScratch*, const TEXT*, SLONG,
					 const TEXT*, const TEXT*, USHORT, const TEXT*, const TEXT*);
void CMP_post_resource(Jrd::thread_db*, Jrd::Resource**, blk*, Jrd::Resource::rsc_s, USHORT);
void CMP_release_resource(Jrd::Resource**, Jrd::Resource::rsc_s, USHORT);
void CMP_release(Jrd::thread_db*, Jrd::jrd_req*);
void CMP_shutdown_database(Jrd::thread_db*);

#endif // JRD_CMP_PROTO_H

