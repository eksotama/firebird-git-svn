/*
 *	PROGRAM:	JRD Access Method
 *	MODULE:		err_proto.h
 *	DESCRIPTION:	Prototype header file for err.c
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

#ifndef JRD_ERR_PROTO_H
#define JRD_ERR_PROTO_H

#ifndef REQUESTER

/* Index error types */

typedef enum idx_e {
	idx_e_ok = 0,
	idx_e_duplicate,
	idx_e_keytoobig,
	idx_e_conversion,
	idx_e_foreign
} IDX_E;

BOOLEAN	ERR_post_warning(ISC_STATUS, ...);
void	ERR_assert(const TEXT*, int);
void	ERR_bugcheck(int);
void	ERR_bugcheck_msg(const TEXT*);
void	ERR_corrupt(int);
void	ERR_duplicate_error(enum idx_e, class jrd_rel*, USHORT);
void	ERR_error(int);
void	ERR_error_msg(const TEXT *);
void	ERR_post(ISC_STATUS, ...);
void	ERR_punt(void);
void	ERR_warning(ISC_STATUS, ...);
void	ERR_log(int, int, const TEXT *);

#endif /* REQUESTER */

const TEXT*		ERR_cstring(const TEXT*);
const TEXT*		ERR_string(const TEXT*, int);

#endif /* JRD_ERR_PROTO_H */
