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

namespace Jrd {
	class thread_db;
	class Database;
	class PageNumber;
	class PageSpace;
	struct win;
}
namespace Ods {
	struct pag;
	struct header_page;
}

void	PAG_add_clump(SLONG, USHORT, USHORT, const UCHAR*, USHORT, USHORT);
USHORT	PAG_add_file(const TEXT*, SLONG);
int		PAG_add_header_entry(Ods::header_page*, USHORT, SSHORT, const UCHAR*);
void	PAG_attach_temp_pages(Jrd::thread_db*, USHORT pageSpaceID);
int		PAG_replace_entry_first(Ods::header_page*, USHORT, SSHORT, const UCHAR*);
Ods::pag*	PAG_allocate(Jrd::win *);
SLONG	PAG_attachment_id(Jrd::thread_db*);
int		PAG_delete_clump_entry(SLONG, USHORT);
void	PAG_format_header(void);
void	PAG_format_log(void);
void	PAG_format_pip(Jrd::thread_db*, Jrd::PageSpace& pageSpace);
bool	PAG_get_clump(SLONG, USHORT, USHORT*, UCHAR*);
void	PAG_header(const TEXT*, USHORT, bool);
void	PAG_init(void);
void	PAG_init2(USHORT);
SLONG	PAG_last_page(void);
void	PAG_release_page(const Jrd::PageNumber&, const Jrd::PageNumber&);
void	PAG_set_force_write(Jrd::Database*, SSHORT);
void	PAG_set_no_reserve(Jrd::Database*, USHORT);
void	PAG_set_db_readonly(Jrd::Database*, bool);
void	PAG_set_db_SQL_dialect(Jrd::Database*, SSHORT);
void	PAG_set_page_buffers(ULONG);
void	PAG_sweep_interval(SLONG);
//int		PAG_unlicensed(void);

#endif // JRD_PAG_PROTO_H

