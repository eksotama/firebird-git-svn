/*
 *	PROGRAM:	JRD Access Method
 *	MODULE:		scl_proto.h
 *	DESCRIPTION:	Prototype header file for scl.epp
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

#ifndef JRD_SCL_PROTO_H
#define JRD_SCL_PROTO_H

namespace Jrd {
	class SecurityClass;
}

struct dsc;

void SCL_check_access(const Jrd::SecurityClass*, SLONG, const TEXT*,
					  const TEXT*, USHORT, const TEXT*, const TEXT*);
void SCL_check_procedure(const dsc*, USHORT);
void SCL_check_relation(const dsc*, USHORT);
Jrd::SecurityClass* SCL_get_class(/* INOUT */ TEXT*);
int SCL_get_mask(const TEXT*, const TEXT*);
void SCL_init(bool, const TEXT*, const TEXT*, const TEXT*, const TEXT*,
	const TEXT*, Jrd::thread_db*, const bool);
void SCL_move_priv(UCHAR**, USHORT, STR*, ULONG*);
Jrd::SecurityClass* SCL_recompute_class(Jrd::thread_db*, TEXT*);
void SCL_release(Jrd::SecurityClass*);
void SCL_check_index(Jrd::thread_db*, const TEXT*, UCHAR, USHORT);

#endif // JRD_SCL_PROTO_H

