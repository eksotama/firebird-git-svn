/*
 *	PROGRAM:	JRD Access Method
 *	MODULE:		cmp_proto.h
 *	DESCRIPTION:	Prototype header file for cmp.c
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

int				CMP_clone_active(struct jrd_req*);
struct jrd_nod* CMP_clone_node(TDBB, struct Csb*, struct jrd_nod*);
struct jrd_req* CMP_clone_request(TDBB, struct jrd_req*, USHORT, BOOLEAN);
struct jrd_req* CMP_compile(USHORT, UCHAR*, USHORT);
struct jrd_req* CMP_compile2(TDBB, UCHAR*, USHORT);
struct csb_repeat* CMP_csb_element(struct Csb*, USHORT);
extern "C" void CMP_expunge_transaction(struct jrd_tra*);
void			CMP_decrement_prc_use_count(TDBB, JRD_PRC);
struct jrd_req*	CMP_find_request(TDBB, USHORT, USHORT);
void			CMP_fini(TDBB);
struct fmt*		CMP_format(TDBB, struct Csb*, USHORT);
void			CMP_get_desc(TDBB, struct Csb*, struct jrd_nod*, struct dsc*);
struct idl*		CMP_get_index_lock(TDBB, struct jrd_rel*, USHORT);
SLONG			CMP_impure(struct Csb*, USHORT);
struct jrd_req*	CMP_make_request(TDBB, struct Csb*);
int				CMP_post_access(TDBB, struct Csb*, /* INOUT */ TEXT*, SLONG,
								const TEXT*, const TEXT*, USHORT, const TEXT*,
								const TEXT*);
void			CMP_post_resource(TDBB, struct Rsc**, struct blk*,
									 enum rsc_s, USHORT);
void			CMP_release_resource(struct Rsc**, enum rsc_s, USHORT);
void			CMP_release(TDBB, struct jrd_req*);
void			CMP_shutdown_database(TDBB);

#endif // JRD_CMP_PROTO_H

