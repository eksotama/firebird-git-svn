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
/*#include "../jrd/jrd.h"
#include "../jrd/btr.h"*/
#include "../include/fb_types.h"

/* Index error types */

typedef enum idx_e {
    idx_e_ok = 0,
    idx_e_duplicate,
    idx_e_keytoobig,
    idx_e_nullunique,
    idx_e_conversion,
    idx_e_foreign
} IDX_E;

#define BUGCHECK(number)        ERR_bugcheck (number)
#define CORRUPT(number)         ERR_corrupt (number)
#define IBERROR(number)         ERR_error (number)

#ifdef __cplusplus
extern "C" {
#endif

BOOLEAN DLL_EXPORT ERR_post_warning(STATUS, ...);
void ERR_assert(const TEXT*, int);
void DLL_EXPORT ERR_bugcheck(int);
void DLL_EXPORT ERR_bugcheck_msg(const TEXT*);
void DLL_EXPORT ERR_corrupt(int);
void DLL_EXPORT ERR_duplicate_error(enum idx_e, struct jrd_rel*, USHORT);
void DLL_EXPORT ERR_error(int);
void DLL_EXPORT ERR_error_msg(const TEXT *);
void DLL_EXPORT ERR_post(STATUS, ...);
void DLL_EXPORT ERR_punt(void);
void DLL_EXPORT ERR_warning(STATUS, ...);
void DLL_EXPORT ERR_log(int, int, const TEXT *);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* REQUESTER */

#ifdef __cplusplus
extern "C" {
#endif

const TEXT* DLL_EXPORT ERR_cstring(const TEXT*);
const TEXT* DLL_EXPORT ERR_string(const TEXT*, int);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* JRD_ERR_PROTO_H */
