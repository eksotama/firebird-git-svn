/*
 *	PROGRAM:	JRD Access method
 *	MODULE:		ail.h
 *	DESCRIPTION:	Prototype Header file for ail.cpp
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
 *
 * 2002.10.29 Sean Leyne - Removed obsolete "Netware" port
 *
 * 2002.10.30 Sean Leyne - Removed support for obsolete "PC_PLATFORM" define
 *
 */

#ifndef JRD_AIL_H
#define JRD_AIL_H

void	AIL_add_log(void);
void	AIL_checkpoint_finish(ISC_STATUS*, dbb*, SLONG, TEXT*,
								  SLONG, SLONG);
void	AIL_commit(SLONG);
void	AIL_disable(void);
void	AIL_drop_log(void);
void	AIL_drop_log_force(void);
void	AIL_enable(TEXT *, USHORT, UCHAR *, USHORT, SSHORT);
void	AIL_fini(void);
void	AIL_get_file_list(lls**);
void	AIL_init(TEXT*, SSHORT, win*, USHORT, sbm**);
void	AIL_init_log_page(log_info_page*, SLONG);
void	AIL_journal_tid(void);
void	AIL_process_jrn_error(SLONG);
void	AIL_put(dbb*, ISC_STATUS*, jrnh*, USHORT, UCHAR*,
					USHORT, ULONG, ULONG, ULONG *, ULONG *);
void	AIL_recover_page(SLONG, pag*);
void	AIL_set_log_options(SLONG, SSHORT, USHORT, SLONG);
void	AIL_shutdown(SCHAR *, SLONG *, SLONG *, SLONG *, SSHORT);
void	AIL_upd_cntrl_pt(TEXT *, USHORT, ULONG, ULONG, ULONG);

#endif // JRD_AIL_H

