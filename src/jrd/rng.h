/*
 *	PROGRAM:	JRD Access Method
 *	MODULE:		rng.h
 *	DESCRIPTION:	Refresh Range Definitions
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

#ifndef JRD_RNG_H
#define JRD_RNG_H

/* refresh range block used to store info about a particular
   set of records in a refresh range */

namespace Jrd {
class Attachment;
class vec;

class RefreshRange : public pool_alloc_rpt<SCHAR, type_rng>
{
    public:
	RefreshRange*	rng_next;			/* next in list of ranges being created */
	Attachment*		rng_attachment;		/* attachment that owns range */
	RefreshRange*	rng_lck_next;		/* next in list of ranges interested in a lock */
	vec*		rng_relation_locks;	/* relation locks */
	vec*		rng_relation_trans;	/* relation transactions */
	vec*		rng_record_locks;	/* record locks */
	vec*		rng_page_locks;		/* page locks */
	vec*		rng_transaction_locks;	/* transaction locks */
	USHORT rng_relations;		/* count of relations in range */
	USHORT rng_records;			/* count of records in range */
	USHORT rng_pages;			/* count of index pages in range */
	USHORT rng_transactions;	/* count of uncommitted transactions in range */
	USHORT rng_number;			/* range number */
	USHORT rng_flags;			/* see flags below */
	USHORT rng_event_length;	/* length of event name */
	UCHAR rng_event[1];			/* event name to post */
};

const USHORT RNG_posted	= 1;			/* range has already been posted */

const int RANGE_NAME_LENGTH	= 31;	/* max. length of range name for the event */

} //namespace Jrd

#endif // JRD_RNG_H

