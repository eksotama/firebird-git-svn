/*
 *	PROGRAM:	JRD Access Method
 *	MODULE:		all.c
 *	DESCRIPTION:	Internal block allocator
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

#ifdef SHLIB_DEFS
#define LOCAL_SHLIB_DEFS
#endif

#include "firebird.h"
#include <string.h>
#include "../jrd/ib_stdio.h"

#include "gen/codes.h"

#ifndef GATEWAY
#include "../jrd/everything.h"
#else
#include ".._gway/gway/everything.h"
#endif
#include "../jrd/all_proto.h"
#include "../jrd/err_proto.h"
#include "../jrd/gds_proto.h"
#include "../jrd/mov_proto.h"
#include "../jrd/thd_proto.h"

#ifdef SUPERSERVER

extern "C" {
extern SLONG trace_pools;
}
SLONG all_delta_alloc = 0;

#endif // SUPERSERVER


#ifdef SHLIB_DEFS
#define strlen		(*_libgds_strlen)

extern int strlen();
#endif

#define PERM_EXTEND_SIZE        (16 * 1024)
#define CACH_EXTEND_SIZE        (16 * 1024)


#ifdef DEV_BUILD
void ALL_check_memory()
{
/**************************************
 *
 *	A L L _ c h e c k _ m e m o r y
 *
 **************************************
 *
 * Functional description
 *	This routine goes through all allocated
 *	memory pools and checks the hunks of 
 *	memory allocated to make sure they are
 *	valid.  It is designed to be used for
 *	debugging purposes to find bad memory
 *	areas as soon as they are corrupted. 
 *	A call to this routine can be made from
 *	looper() to ensure that it will be regularly
 *	executed.
 *
 **************************************/

	Firebird::vector<JrdMemoryPool*>::iterator itr;

	DBB Dbb = GET_DBB;

	V4_RW_LOCK_LOCK(dbb->dbb_rw_locks + DBB_WLCK_pools, WLCK_read);

	// walk through all the pools in the database
	for (itr = Dbb->dbb_pools.begin(); itr < Dbb->dbb_pools.end(); ++itr)
	{
		JrdMemoryPool* pool = *itr;
		if (pool) {
			// walk through all the hunks in the pool
			pool->verify_pool();
		}
	}

	V4_RW_LOCK_UNLOCK(dbb->dbb_rw_locks + DBB_WLCK_pools);
}
#endif /* DEV_BUILD */


TEXT* ALL_cstring(TEXT* in_string)
{
/**************************************
 *
 *	A L L _ c s t r i n g
 *
 **************************************
 *
 * Functional description
 *	Copy a stack local string to an allocated
 *	string so it doesn't disappear before we 
 *	return to the user or where ever.
 *
 **************************************/
	TDBB tdbb;
	JrdMemoryPool *pool;
	TEXT *p, *q;
	STR string;
	ULONG length;

	tdbb = GET_THREAD_DATA;

	if (!(pool = tdbb->tdbb_default)) {
		if (tdbb->tdbb_transaction)
			pool = tdbb->tdbb_transaction->tra_pool;
		else if (tdbb->tdbb_request)
			pool = tdbb->tdbb_request->req_pool;

		/* theoretically this shouldn't happen, but just in case */

		if (!pool)
			return NULL;
	}

	length = strlen(in_string);
	string = (STR) pool->allocate(type_str, length);
/* TMN: Here we should really have the following assert */
/* assert(length <= MAX_USHORT); */
	string->str_length = (USHORT) length;

	p = (TEXT *) string->str_data;
	q = in_string;

	while (length--)
		*p++ = *q++;
	*p = 0;

	return (TEXT *) string->str_data;
}


void ALL_fini(void)
{
/**************************************
 *
 *	A L L _ f i n i
 *
 **************************************
 *
 * Functional description
 *	Get rid of everything.
 *	NOTE:  This routine does not lock any mutexes on
 *	its own behalf.  It is assumed that mutexes will
 *	have been locked before entry.
 *	Call gds__free explicitly instead of ALL_free
 *	because it references the dbb block which gets
 *	released at the top of this routine.
 *
 **************************************/
	DBB dbb;

	dbb = GET_DBB;

	/* Don't know if we even need to do this, so it is commented out */
	//delete dbb;
}


void ALL_init(void)
{
/**************************************
 *
 *	A L L _ i n i t
 *
 **************************************
 *
 * Functional description
 *	Initialize the pool system.
 *	NOTE:  This routine does not lock any mutexes on
 *	its own behalf.  It is assumed that mutexes will
 *	have been locked before entry.
 *
 **************************************/
	TDBB tdbb;
	DBB dbb;
	JrdMemoryPool* pool;

	tdbb = GET_THREAD_DATA;
	dbb = tdbb->tdbb_database;

	pool = tdbb->tdbb_default = dbb->dbb_permanent;
        dbb->dbb_permanent->setExtendSize(PERM_EXTEND_SIZE);
	dbb->dbb_pools[0] = pool;

	dbb->dbb_bufferpool = new(*pool) JrdMemoryPool(CACH_EXTEND_SIZE);
	dbb->dbb_pools[1] = dbb->dbb_bufferpool;
}

void JrdMemoryPool::ALL_push(BLK object, register LLS * stack)
{
/**************************************
 *
 *	A L L _ p u s h
 *
 **************************************
 *
 * Functional description
 *	Push an object on an LLS stack.
 *
 **************************************/
	TDBB tdbb;
	register LLS node;
	JrdMemoryPool* pool;

	tdbb = GET_THREAD_DATA;

	pool = tdbb->tdbb_default;
	node = pool->lls_cache.newBlock();
	node->lls_object = object;
	node->lls_next = *stack;
	*stack = node;
}


BLK JrdMemoryPool::ALL_pop(register LLS *stack)
{
/**************************************
 *
 *	A L L _ p o p
 *
 **************************************
 *
 * Functional description
 *	Pop an object off a linked list stack.  Save the node for
 *	further use.
 *
 **************************************/
	register LLS node;
	JrdMemoryPool* pool;
	BLK object;

	node = *stack;
	*stack = node->lls_next;
	object = node->lls_object;

	pool = (JrdMemoryPool*)MemoryPool::blk_pool(node);
	pool->lls_cache.returnBlock(node);

	return object;
}


#ifdef SUPERSERVER
void ALL_print_memory_pool_info(IB_FILE* fptr, DBB databases)
{
#if 0
/***********************************************************
 *
 *	A L L _ p r i n t _ m e m o r y _ p o o l _ i n f o
 *
 ***********************************************************
 *
 * Functional description
 *	Print the different block types allocated in the pool.
 *	Walk the dbb's to print out pool info of every database
 *
 **************************************/
	DBB dbb;
	STR string;
	VEC vector;
	HNK hnk;
	ATT att;
	int i, j, k, col;

	ib_fprintf(fptr, "\n\tALL_xx block types\n");
	ib_fprintf(fptr, "\t------------------");
	for (i = 0, col = 0; i < types->type_MAX; i++) {
		if (types->all_block_type_count[i]) {
			if (col % 5 == 0)
				ib_fprintf(fptr, "\n\t");
			ib_fprintf(fptr, "%s = %d  ", types->ALL_types[i],
					   types->all_block_type_count[i]);
			++col;
		}
	}
	ib_fprintf(fptr, "\n");

	// TMN: Note. Evil code.
	for (i = 0, dbb = databases; dbb; dbb = dbb->dbb_next, ++i);
	ib_fprintf(fptr, "\tNo of dbbs = %d\n", i);

	for (k = 1, dbb = databases; dbb; dbb = dbb->dbb_next, ++k)
	{
		string = dbb->dbb_filename;
		ib_fprintf(fptr, "\n\t dbb%d -> %s\n", k, string->str_data);
		vector = (VEC) dbb->dbb_pools;
		j = 0;
		for (pool_vec_type::iterator itr = dbb->dbb_pools.begin();
			itr != dbb->dbb_pools.end(); ++itr)
		{
			PLB myPool = *itr;
			if (myPool) {
				++j;
			}
		}
		ib_fprintf(fptr, "\t    %s has %d pools", string->str_data, j);
		for (j = 0, att = dbb->dbb_attachments; att; att = att->att_next)
		{
			j++;
		}
		ib_fprintf(fptr, " and %d attachment(s)", j);
		for (i = 0; i < (int) vector->vec_count; i++)
		{
			PLB myPool = (PLB) vector->vec_object[i];
			if (!myPool) {
				continue;
			}
			ib_fprintf(fptr, "\n\t    Pool %d", myPool->plb_pool_id);

			// Count # of hunks
			for (j = 0, hnk = myPool->plb_hunks; hnk; hnk = hnk->hnk_next) {
				++j;
			}
			if (j) {
				ib_fprintf(fptr, " has %d hunks", j);
			}
			j = 0;

			// Count # of "huge" hunks
			for (hnk = myPool->plb_huge_hunks; hnk; hnk = hnk->hnk_next)
			{
				++j;
			}
			if (j) {
				ib_fprintf(fptr, " and %d huge_hunks", j);
			}
			ib_fprintf(fptr, " Extend size is %d", myPool->plb_extend_size);
			for (j = 0, col = 0; j < types->type_MAX; j++)
			{
				if (myPool->plb_blk_type_count[j])
				{
					if (col % 5 == 0)
					{
						ib_fprintf(fptr, "\n\t    ");
					}
					ib_fprintf(fptr, "%s = %d  ", types->ALL_types[j],
							   myPool->plb_blk_type_count[j]);
					++col;
				}
			}
		}
	}
#endif	// 0
}
#endif	// SUPERSERVER

