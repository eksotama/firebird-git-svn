/*
 *	PROGRAM:	JRD Access Method
 *	MODULE:		pag_proto.h
 *	DESCRIPTION:	Prototype header file for pag.cpp
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

#ifndef JRD_PAG_PROTO_H
#define JRD_PAG_PROTO_H

class Database;

void	PAG_add_clump(SLONG, USHORT, USHORT, const UCHAR*, USHORT, USHORT);
USHORT	PAG_add_file(const TEXT*, SLONG);
int		PAG_add_header_entry(struct header_page*, USHORT, SSHORT, const UCHAR*);
int		PAG_replace_entry_first(struct header_page*, USHORT, SSHORT, const UCHAR*);
struct pag*	PAG_allocate(struct win *);
SLONG	PAG_attachment_id(void);
int		PAG_delete_clump_entry(SLONG, USHORT);
void	PAG_format_header(void);
void	PAG_format_log(void);
void	PAG_format_pip(void);
bool	PAG_get_clump(SLONG, USHORT, USHORT*, UCHAR*);
void	PAG_header(const TEXT*, USHORT);
void	PAG_init(void);
void	PAG_init2(USHORT);
SLONG	PAG_last_page(void);
void	PAG_release_page(SLONG, SLONG);
void	PAG_set_force_write(Database*, SSHORT);
void	PAG_set_no_reserve(Database*, USHORT);
void	PAG_set_db_readonly(Database*, bool);
void	PAG_set_db_SQL_dialect(Database*, SSHORT);
void	PAG_set_page_buffers(ULONG);
void	PAG_sweep_interval(SLONG);
int		PAG_unlicensed(void);

#endif // JRD_PAG_PROTO_H

