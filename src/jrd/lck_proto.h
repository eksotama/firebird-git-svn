/*
 *	PROGRAM:	JRD Access Method
 *	MODULE:		lck_proto.h
 *	DESCRIPTION:	Prototype header file for lck.cpp
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

#ifndef JRD_LCK_PROTO_H
#define JRD_LCK_PROTO_H

#include "../jrd/lck.h"

void	LCK_assert(TDBB, lck*);
bool	LCK_convert(TDBB, lck*, USHORT, SSHORT);
int		LCK_convert_non_blocking(TDBB, lck*, USHORT, SSHORT);
int		LCK_convert_opt(TDBB, lck*, USHORT);
int		LCK_downgrade(TDBB, lck*);
void	LCK_fini(TDBB, enum lck_owner_t);
SLONG	LCK_get_owner_handle(TDBB, enum lck_t);
void	LCK_init(TDBB, enum lck_owner_t);
int		LCK_lock(TDBB, lck*, USHORT, SSHORT);
int		LCK_lock_non_blocking(TDBB, lck*, USHORT, SSHORT);
int		LCK_lock_opt(TDBB, lck*, USHORT, SSHORT);
SLONG	LCK_query_data(lck*, enum lck_t, USHORT);
SLONG	LCK_read_data(lck*);
void	LCK_release(TDBB, lck*);
void	LCK_re_post(lck*);
void	LCK_write_data(lck*, SLONG);
void	LCK_ast_inhibit();
void	LCK_ast_enable();

#endif // JRD_LCK_PROTO_H

