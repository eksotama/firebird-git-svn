/*
 *	PROGRAM:	JRD Command Oriented Query Language
 *	MODULE:		proc_proto.h
 *	DESCRIPTION:	Prototype header file for proc.cpp
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

#ifndef QLI_PROC_PROTO_H
#define QLI_PROC_PROTO_H

void	PRO_close(dbb*, FRBRD*);
void	PRO_commit(dbb*);
void	PRO_copy_procedure(dbb*, TEXT*, dbb*, TEXT*);
void	PRO_create(dbb*, TEXT*);
int		PRO_delete_procedure(dbb*, TEXT*);
void	PRO_edit_procedure(dbb*, TEXT*);
FRBRD*	PRO_fetch_procedure(dbb*, TEXT*);
bool	PRO_get_line (FRBRD *, TEXT *, USHORT);
void	PRO_invoke(dbb*, TEXT*);
FRBRD*	PRO_open_blob(dbb*, SLONG*);
int		PRO_rename_procedure(dbb*, TEXT*, TEXT*);
void	PRO_rollback(dbb*);
void	PRO_scan(dbb*, void(*)(), void*);
void	PRO_setup(dbb*);
FRBRD*	PRO_transaction(dbb*, int);

#endif /* QLI_PROC_PROTO_H */
