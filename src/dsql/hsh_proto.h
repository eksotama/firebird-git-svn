/*
 *	PROGRAM:	Dynamic SQL runtime support
 *	MODULE:		hsh_proto.h
 *	DESCRIPTION:	Prototype Header file for hsh.c
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

#ifndef DSQL_HSH_PROTO_H
#define DSQL_HSH_PROTO_H

void HSHD_fini(void);
void HSHD_finish(void *);
void HSHD_init(void);
void HSHD_insert(struct sym *);
SYM HSHD_lookup(void *, TEXT *, SSHORT, SYM_TYPE, USHORT);
void HSHD_remove(struct sym *);
void HSHD_set_flag(void *, TEXT *, SSHORT, SYM_TYPE, SSHORT);

#endif /*DSQL_HSH_PROTO_H*/
