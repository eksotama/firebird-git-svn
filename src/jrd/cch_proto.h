/*
 *	PROGRAM:	JRD Access Method
 *	MODULE:		cch_proto.h
 *	DESCRIPTION:	Prototype header file for cch.c
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

#ifndef JRD_CCH_PROTO_H
#define JRD_CCH_PROTO_H

#ifdef __cplusplus
extern "C" {
#endif

void DLL_EXPORT	CCH_do_log_shutdown(TDBB, SSHORT);
void DLL_EXPORT CCH_shutdown_database(struct dbb *);

USHORT		CCH_checksum(struct bdb *);
void		CCH_down_grade_dbb(struct dbb *);
BOOLEAN		CCH_exclusive(TDBB, USHORT, SSHORT);
BOOLEAN		CCH_exclusive_attachment(TDBB, USHORT, SSHORT);
void		CCH_expand(TDBB, ULONG);
struct pag*	CCH_fake(TDBB, struct win *, SSHORT);
struct pag*	CCH_fetch(TDBB, register struct win *, USHORT, SSHORT,
						 SSHORT, SSHORT, BOOLEAN);
SSHORT		CCH_fetch_lock(TDBB, register struct win *, USHORT, SSHORT,
						 SSHORT, SSHORT);
void		CCH_fetch_page(TDBB, register struct win *, SSHORT, BOOLEAN);
void		CCH_fini(TDBB);
void		CCH_flush(TDBB, USHORT, SLONG);
BOOLEAN		CCH_free_page(TDBB);
SLONG		CCH_get_incarnation(struct win *);
struct pag*	CCH_handoff(TDBB, register struct win *, SLONG, SSHORT,
							   SSHORT, SSHORT, SSHORT);
void		CCH_init(TDBB, ULONG);
void		CCH_journal_page(TDBB, struct win *);
void		CCH_journal_record(TDBB, struct win *, UCHAR *, USHORT, UCHAR *,
							   USHORT);
void		CCH_mark(TDBB, struct win *, USHORT);
void		CCH_mark_must_write(TDBB, struct win *);
void		CCH_must_write(struct win *);
struct lck*	CCH_page_lock(TDBB);
void		CCH_precedence(TDBB, struct win *, SLONG);
void		CCH_prefetch(struct tdbb *, SLONG *, SSHORT);
BOOLEAN		CCH_prefetch_pages(TDBB);
void		CCH_recover_shadow(TDBB, struct sbm *);
void		CCH_release(TDBB, struct win *, BOOLEAN);
void		CCH_release_and_free(struct win *);
void		CCH_release_exclusive(TDBB);
void		CCH_release_journal(TDBB, SLONG);
BOOLEAN		CCH_rollover_to_shadow(struct dbb *, struct fil *, BOOLEAN);
void		CCH_unwind(TDBB, BOOLEAN);
BOOLEAN		CCH_validate(struct win *);
BOOLEAN		CCH_write_all_shadows(TDBB, struct sdw *, struct bdb *,
									 STATUS *, USHORT, BOOLEAN);

/* macros for dealing with cache pages */

#define CCH_FETCH(tdbb, window, lock, type)		  CCH_fetch (tdbb, window, lock, type, 1, 1, 1)
#define CCH_FETCH_NO_SHADOW(tdbb, window, lock, type)		  CCH_fetch (tdbb, window, lock, type, 1, 1, 0)
#define CCH_FETCH_NO_CHECKSUM(tdbb, window, lock, type)   CCH_fetch (tdbb, window, lock, type, 0, 1, 1)
#define CCH_FETCH_TIMEOUT(tdbb, window, lock, type, latch_wait)   CCH_fetch (tdbb, window, lock, type, 0, latch_wait, 1)
#define CCH_FETCH_LOCK(tdbb, window, lock, wait, latch_wait, type) CCH_fetch_lock (tdbb, window, lock, wait, latch_wait, type)
#define CCH_FETCH_PAGE(tdbb, window, checksum, read_shadow)       CCH_fetch_page (tdbb, window, checksum, read_shadow)
#define CCH_RELEASE(tdbb, window)                         CCH_release (tdbb, window, FALSE)
#define CCH_RELEASE_TAIL(tdbb, window)                    CCH_release (tdbb, window, TRUE)
#define CCH_MARK(tdbb, window)                            CCH_mark (tdbb, window, 0)
#define CCH_MARK_SYSTEM(tdbb, window)                     CCH_mark (tdbb, window, 1)
#define CCH_HANDOFF(tdbb, window, page, lock, type)       CCH_handoff (tdbb, window, page, lock, type, 1, 0)
#define CCH_HANDOFF_TIMEOUT(tdbb, window, page, lock, type, latch_wait)   CCH_handoff (tdbb, window, page, lock, type, latch_wait, 0)
#define CCH_HANDOFF_TAIL(tdbb, window, page, lock, type)  CCH_handoff (tdbb, window, page, lock, type, 1, 1)
#define CCH_MARK_MUST_WRITE(tdbb, window)                 CCH_mark_must_write (tdbb, window)
#define CCH_PREFETCH(tdbb, pages, count)            CCH_prefetch (tdbb, pages, count)

/* Flush flags */

#define FLUSH_ALL	1			/* flush all dirty buffers in cache */
#define FLUSH_RLSE	2			/* release page locks after flush */
#define FLUSH_TRAN	4			/* flush transaction dirty buffers from dirty btree */
#define FLUSH_SWEEP	8			/* flush dirty buffers from garbage collection */
#define FLUSH_SYSTEM	16		/* flush system transaction only from dirty btree */
#define FLUSH_FINI	(FLUSH_ALL | FLUSH_RLSE)

#ifdef __cplusplus
} /* "C" */
#endif

#endif /* JRD_CCH_PROTO_H */
