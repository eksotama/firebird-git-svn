/*
 *	PROGRAM:	JRD Access Method
 *	MODULE:		vio_proto.h
 *	DESCRIPTION:	Prototype header file for vio.c
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
 * 2002.10.21 Nickolay Samofatov: Added support for explicit pessimistic locks
 * 2002.10.29 Nickolay Samofatov: Added support for savepoints
 */

#ifndef JRD_VIO_PROTO_H
#define JRD_VIO_PROTO_H

void	VIO_backout(TDBB, struct rpb *, class jrd_tra *);
void	VIO_bump_count(TDBB, USHORT, struct jrd_rel *, bool);
int		VIO_chase_record_version(TDBB, struct rpb *, class Rsb *,
									class jrd_tra *, struct blk *, BOOLEAN);
#ifdef PC_ENGINE
int		VIO_check_if_updated(TDBB, struct rpb *);
#endif
void	VIO_data(TDBB, struct rpb *, struct blk *);
void	VIO_erase(TDBB, struct rpb *, class jrd_tra *);
#ifdef GARBAGE_THREAD
void	VIO_fini(TDBB);
#endif
bool	VIO_garbage_collect(TDBB, struct rpb *, class jrd_tra *);
struct rec*	VIO_gc_record(TDBB, struct jrd_rel *);
int		VIO_get(TDBB, struct rpb *, class Rsb *, class jrd_tra *, struct blk *);
int		VIO_get_current(TDBB, struct rpb *, class jrd_tra *, struct blk *, USHORT);
#ifdef GARBAGE_THREAD
void	VIO_init(TDBB);
#endif
void	VIO_merge_proc_sav_points(TDBB, class jrd_tra *, struct sav **);
BOOLEAN	VIO_writelock(TDBB, struct rpb *, class Rsb *, class jrd_tra *);
void	VIO_modify(TDBB, struct rpb *, struct rpb *, class jrd_tra *);
BOOLEAN	VIO_next_record(TDBB, struct rpb *, class Rsb *, class jrd_tra *,
							   struct blk *, BOOLEAN, BOOLEAN);
struct rec*	VIO_record(TDBB, struct rpb *, struct fmt *, JrdMemoryPool *);
void	VIO_start_save_point(TDBB, class jrd_tra *);
void	VIO_store(TDBB, struct rpb *, class jrd_tra *);
BOOLEAN	VIO_sweep(TDBB, class jrd_tra *);
void	VIO_verb_cleanup(TDBB, class jrd_tra *);
SLONG	VIO_savepoint_large(struct sav *, SLONG);

#endif // JRD_VIO_PROTO_H

