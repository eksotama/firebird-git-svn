/*
 *	PROGRAM:	JRD Access Method
 *	MODULE:		btr.cpp
 *	DESCRIPTION:	B-tree management code
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
 * 2002.10.30 Sean Leyne - Removed support for obsolete "PC_PLATFORM" define
 *
 */

#include "firebird.h"
#include <string.h>
#include <stdlib.h>
#include "memory_routines.h"
#include "../common/classes/vector.h"
#include "../jrd/ib_stdio.h"
#include "../jrd/jrd.h"
#include "../jrd/ods.h"
#include "../jrd/val.h"
#include "../jrd/btr.h"
#include "../jrd/btn.h"
#include "../jrd/req.h"
#include "../jrd/tra.h"
#include "../jrd/intl.h"
#include "gen/iberror.h"
#include "../jrd/common.h"
#include "../jrd/lck.h"
#include "../jrd/cch.h"
#include "../jrd/sbm.h"
#include "../jrd/sort.h"
#include "../jrd/gdsassert.h"
#include "../jrd/all_proto.h"
#include "../jrd/btr_proto.h"
#include "../jrd/cch_proto.h"
#include "../jrd/dpm_proto.h"
#include "../jrd/err_proto.h"
#include "../jrd/evl_proto.h"
#include "../jrd/gds_proto.h"
#include "../jrd/intl_proto.h"
#include "../jrd/jrd_proto.h"
#include "../jrd/met_proto.h"
#include "../jrd/mov_proto.h"
#include "../jrd/nav_proto.h"
#include "../jrd/dbg_proto.h"
#include "../jrd/pag_proto.h"
#include "../jrd/pcmet_proto.h"
#include "../jrd/sbm_proto.h"
#include "../jrd/sort_proto.h"
#include "../jrd/thd_proto.h"
#include "../jrd/tra_proto.h"


/*********************************************
      eliminate this conversion - kk
#ifdef VMS
extern double	MTH$CVT_G_D();
#endif
**********************************************/

#define MAX_LEVELS	16

inline void MOVE_BYTE(UCHAR*& x_from, UCHAR*& x_to)
{
	*x_to++ = *x_from++;
}

#define OVERSIZE	(MAX_PAGE_SIZE + BTN_PAGE_SIZE + MAX_KEY + sizeof (SLONG) - 1) / sizeof (SLONG)

// END_LEVEL (-1) is choosen here as a unknown/none value, because it's 
// already reserved as END_LEVEL marker for page number and record number.
#define NO_VALUE END_LEVEL
// A split page will never have the number 0, because that's the value
// of the main page.
#define NO_SPLIT 0

// Thresholds for determing of a page should be garbage collected
// Garbage collect if page size is below GARBAGE_COLLECTION_THRESHOLD
#define GARBAGE_COLLECTION_BELOW_THRESHOLD	(dbb->dbb_page_size / 4)
// Garbage collect only if new merged page will 
// be lower as GARBAGE_COLLECTION_NEW_PAGE_MAX_THRESHOLD
// 256 is the old maximum possible key_length.
#define GARBAGE_COLLECTION_NEW_PAGE_MAX_THRESHOLD	((dbb->dbb_page_size - 256))

struct INT64_KEY {
	double d_part;
	SSHORT s_part;
};

// I assume this wasn't done sizeof(INT64_KEY) on purpose, since alignment might affect it.
#define INT64_KEY_LENGTH	(sizeof (double) + sizeof (SSHORT))

static const double pow10[] =
{
	1.e00, 1.e01, 1.e02, 1.e03, 1.e04, 1.e05, 1.e06, 1.e07, 1.e08, 1.e09,
	1.e10, 1.e11, 1.e12, 1.e13, 1.e14, 1.e15, 1.e16, 1.e17, 1.e18, 1.e19,
	1.e20, 1.e21, 1.e22, 1.e23, 1.e24, 1.e25, 1.e26, 1.e27, 1.e28, 1.e29,
	1.e30, 1.e31, 1.e32, 1.e33, 1.e34, 1.e35, 1.e36
};

#define powerof10(s) ((s) <= 0 ? pow10[-(s)] : 1./pow10[-(s)])

static const struct {			/* Used in make_int64_key() */
	UINT64 limit;
	SINT64 factor;
	SSHORT scale_change;
} int64_scale_control[] =
{
	{
	QUADCONST(922337203685470000), QUADCONST(1), 0}, {
	QUADCONST(92233720368547000), QUADCONST(10), 1}, {
	QUADCONST(9223372036854700), QUADCONST(100), 2}, {
	QUADCONST(922337203685470), QUADCONST(1000), 3}, {
	QUADCONST(92233720368548), QUADCONST(10000), 4}, {
	QUADCONST(9223372036855), QUADCONST(100000), 5}, {
	QUADCONST(922337203686), QUADCONST(1000000), 6}, {
	QUADCONST(92233720369), QUADCONST(10000000), 7}, {
	QUADCONST(9223372035), QUADCONST(100000000), 8}, {
	QUADCONST(922337204), QUADCONST(1000000000), 9}, {
	QUADCONST(92233721), QUADCONST(10000000000), 10}, {
	QUADCONST(9223373), QUADCONST(100000000000), 11}, {
	QUADCONST(922338), QUADCONST(1000000000000), 12}, {
	QUADCONST(92234), QUADCONST(10000000000000), 13}, {
	QUADCONST(9224), QUADCONST(100000000000000), 14}, {
	QUADCONST(923), QUADCONST(1000000000000000), 15}, {
	QUADCONST(93), QUADCONST(10000000000000000), 16}, {
	QUADCONST(10), QUADCONST(100000000000000000), 17}, {
	QUADCONST(1), QUADCONST(1000000000000000000), 18}, {
	QUADCONST(0), QUADCONST(0), 0}
};

/* The first four entries in the array int64_scale_control[] ends with the
 * limit having 0's in the end. This is to inhibit any rounding off that
 * DOUBLE precision can introduce. DOUBLE can easily store upto 92233720368547
 * uniquely. Values after this tend to round off to the upper limit during
 * division. Hence the ending with 0's so that values will be bunched together
 * in the same limit range and scale control for INT64 index KEY calculation.
 * 
 * This part was changed as a fix for bug 10267. - bsriram 04-Mar-1999 
 */

/* enumerate the possible outcomes of deleting a node */

typedef enum contents {
	contents_empty = 0,
	contents_single,
	contents_below_threshold,
	contents_above_threshold
} CONTENTS;

static SLONG add_node(TDBB, WIN *, IIB *, KEY *, SLONG *, SLONG *, SLONG *);
static void complement_key(KEY *);
static void compress(TDBB, DSC *, KEY *, USHORT, bool, bool, USHORT);
static USHORT compress_root(TDBB, index_root_page*);
static void copy_key(const KEY*, KEY*);
static CONTENTS delete_node(TDBB, WIN *, UCHAR *);
static void delete_tree(TDBB, USHORT, USHORT, SLONG, SLONG);
static DSC *eval(TDBB, jrd_nod*, DSC *, bool *);
static SLONG fast_load(TDBB, jrd_rel*, IDX *, USHORT, SCB, SelectivityList&);
static index_root_page* fetch_root(TDBB, WIN *, jrd_rel*);
static UCHAR *find_node_start_point(btree_page*, KEY *, UCHAR *, USHORT *, bool, bool, bool = false, SLONG = NO_VALUE);
static UCHAR* find_area_start_point(btree_page*, const KEY*, UCHAR *, USHORT *, bool, bool, SLONG = NO_VALUE);
static SLONG find_page(btree_page*, const KEY*, UCHAR, SLONG = NO_VALUE, bool = false);
static CONTENTS garbage_collect(TDBB, WIN *, SLONG);
static void generate_jump_nodes(TDBB, btree_page*, jumpNodeList*, USHORT, USHORT*, USHORT*, USHORT*);
static SLONG insert_node(TDBB, WIN *, IIB *, KEY *, SLONG *, SLONG *, SLONG *);
static INT64_KEY make_int64_key(SINT64, SSHORT);
#ifdef DEBUG_INDEXKEY
static void print_int64_key(SINT64, SSHORT, INT64_KEY);
#endif
static CONTENTS remove_node(TDBB, IIB *, WIN *);
static CONTENTS remove_leaf_node(TDBB, IIB *, WIN *);
static bool scan(TDBB, UCHAR *, SBM *, USHORT, USHORT, KEY *, USHORT, SCHAR);
static void update_selectivity(index_root_page*, USHORT, const SelectivityList&);

USHORT BTR_all(TDBB    tdbb,
			   jrd_rel* relation,
			   IDX**   start_buffer,
			   IDX**   csb_idx,
			   STR*    csb_idx_allocation,
			   SLONG*  idx_size)
{
/**************************************
 *
 *	B T R _ a l l
 *
 **************************************
 *
 * Functional description
 *	Return descriptions of all indices for relation.  If there isn't
 *	a known index root, assume we were called during optimization
 *	and return no indices.
 *
 **************************************/
	SET_TDBB(tdbb);
	DBB dbb = tdbb->tdbb_database;
	CHECK_DBB(dbb);
	WIN window(-1);

	IDX* buffer = *start_buffer;
	index_root_page* root = fetch_root(tdbb, &window, relation);
	if (!root) {
		return 0;
	}

	if ((SLONG) (root->irt_count * sizeof(IDX)) > *idx_size) {
		const SLONG size = (sizeof(IDX) * dbb->dbb_max_idx) + ALIGNMENT;
		str* new_buffer = FB_NEW_RPT(*dbb->dbb_permanent, size) str();
		*csb_idx_allocation = new_buffer;
		buffer = *start_buffer =
			(IDX *) FB_ALIGN((U_IPTR) new_buffer->str_data, ALIGNMENT);
		*idx_size = size - ALIGNMENT;
	}
	USHORT count = 0;
	for (USHORT i = 0; i < root->irt_count; i++) {
		if (BTR_description(relation, root, buffer, i)) {
			count++;
			buffer = NEXT_IDX(buffer->idx_rpt, buffer->idx_count);
		}
	}
	*csb_idx = *start_buffer;
	*idx_size = *idx_size - ((UCHAR*)buffer - (UCHAR*) *start_buffer);
	*start_buffer = buffer;

	CCH_RELEASE(tdbb, &window);
	return count;
}


void BTR_create(TDBB tdbb,
				jrd_rel* relation,
				IDX * idx,
				USHORT key_length,
				SCB sort_handle,
				SelectivityList& selectivity)
{
/**************************************
 *
 *	B T R _ c r e a t e
 *
 **************************************
 *
 * Functional description
 *	Create a new index.
 *
 **************************************/

	SET_TDBB(tdbb);
	DBB dbb = tdbb->tdbb_database;
	CHECK_DBB(dbb);

	// Now that the index id has been checked out, create the index.
	idx->idx_root = fast_load(tdbb, relation, idx, key_length, 
		sort_handle, selectivity);

	// Index is created.  Go back to the index root page and update it to
	// point to the index.
	WIN window(relation->rel_index_root);
	index_root_page* root = 
		(index_root_page*) CCH_FETCH(tdbb, &window, LCK_write, pag_root);
	CCH_MARK(tdbb, &window);
	root->irt_rpt[idx->idx_id].irt_root = idx->idx_root;
	root->irt_rpt[idx->idx_id].irt_flags &= ~irt_in_progress;
	update_selectivity(root, idx->idx_id, selectivity);

	CCH_RELEASE(tdbb, &window);
}


void BTR_delete_index(TDBB tdbb, WIN * window, USHORT id)
{
/**************************************
 *
 *	B T R _ d e l e t e _ i n d e x
 *
 **************************************
 *
 * Functional description
 *	Delete an index if it exists.
 *
 **************************************/
	SET_TDBB(tdbb);
	DBB dbb = tdbb->tdbb_database;
	CHECK_DBB(dbb);

	// Get index descriptor.  If index doesn't exist, just leave.
	index_root_page* root = (index_root_page*) window->win_buffer;

	if (id >= root->irt_count) {
		CCH_RELEASE(tdbb, window);
	}
	else {
		index_root_page::irt_repeat* irt_desc = root->irt_rpt + id;
		CCH_MARK(tdbb, window);
		const SLONG next = irt_desc->irt_root;

		// remove the pointer to the top-level index page before we delete it
		irt_desc->irt_root = 0;
		irt_desc->irt_flags = 0;
		const SLONG prior = window->win_page;
		const USHORT relation_id = root->irt_relation;

		CCH_RELEASE(tdbb, window);
		delete_tree(tdbb, relation_id, id, next, prior);
	}
}


bool BTR_description(jrd_rel* relation, index_root_page* root, IDX * idx, SSHORT id)
{
/**************************************
 *
 *	B T R _ d e s c r i p t i o n
 *
 **************************************
 *
 * Functional description
 *	See if index exists, and if so, pick up its description.
 *  Index id's must fit in a short - formerly a UCHAR.
 *
 **************************************/
	DBB dbb = GET_DBB;
	
	if (id >= root->irt_count) {
		return false;
	}

	index_root_page::irt_repeat* irt_desc = &root->irt_rpt[id];

	if (irt_desc->irt_root == 0) {
		return false;
	}

	//fb_assert(id <= MAX_USHORT);
	idx->idx_id = (USHORT) id;
	idx->idx_root = irt_desc->irt_root;
	idx->idx_count = irt_desc->irt_keys;
	idx->idx_flags = irt_desc->irt_flags;
	idx->idx_runtime_flags = 0;
	idx->idx_foreign_primaries = NULL;
	idx->idx_foreign_relations = NULL;
	idx->idx_foreign_indexes = NULL;
	idx->idx_primary_relation = 0;
	idx->idx_primary_index = 0;
	idx->idx_expression = NULL;
	idx->idx_expression_request = NULL;

	// pick up field ids and type descriptions for each of the fields
	const UCHAR* ptr = (UCHAR*) root + irt_desc->irt_desc;
	idx::idx_repeat* idx_desc = idx->idx_rpt;
	for (int i = 0; i < idx->idx_count; i++, idx_desc++) {
		const irtd* key_descriptor = (irtd*) ptr;
		idx_desc->idx_field = key_descriptor->irtd_field;
		idx_desc->idx_itype = key_descriptor->irtd_itype;
		// dimitr: adjust the ODS stuff accurately
		if (dbb->dbb_ods_version >= ODS_VERSION11) {
			idx_desc->idx_selectivity = key_descriptor->irtd_selectivity;
			ptr += sizeof(irtd);
		}
		else {
			idx_desc->idx_selectivity = irt_desc->irt_stuff.irt_selectivity;
			ptr += sizeof(irtd_ods10);
		}
	}
	idx->idx_selectivity = irt_desc->irt_stuff.irt_selectivity;

#ifdef EXPRESSION_INDICES
	if (idx->idx_flags & idx_expressn) {
		PCMET_lookup_index(relation, idx);
	}
#endif

	return true;
}


void BTR_evaluate(TDBB tdbb, IRB retrieval, SBM * bitmap)
{
/**************************************
 *
 *	B T R _ e v a l u a t e
 *
 **************************************
 *
 * Functional description
 *	Do an index scan and return a bitmap 
 * 	of all candidate record numbers.
 *
 **************************************/
	SET_TDBB(tdbb);
	SBM_reset(bitmap);

	IDX idx;
	WIN window(-1);
	KEY lower, upper;
	btree_page* page = BTR_find_page(tdbb, retrieval, &window, &idx, &lower, &upper, false);

	// If there is a starting descriptor, search down index to starting position.
	// This may involve sibling buckets if splits are in progress.  If there 
	// isn't a starting descriptor, walk down the left side of the index.
	USHORT prefix;
	UCHAR *pointer;
	if (retrieval->irb_lower_count) {
		while (!(pointer = find_node_start_point(page, &lower, 0, &prefix,
					idx.idx_flags & idx_descending, 
					(retrieval->irb_generic & (irb_starting | irb_partial)))))
		{
			page = (btree_page*) CCH_HANDOFF(tdbb, &window, page->btr_sibling,
				LCK_read, pag_index);
		}

		// Compute the number of matching characters in lower and upper bounds
		if (retrieval->irb_upper_count) {
			prefix = BTreeNode::computePrefix(upper.key_data, upper.key_length,
				lower.key_data, lower.key_length);
		}
	}
	else {
		pointer = BTreeNode::getPointerFirstNode(page);
		prefix = 0;
	}

	SCHAR flags = page->btr_header.pag_flags;
	// if there is an upper bound, scan the index pages looking for it
	if (retrieval->irb_upper_count)	{
		while (scan(tdbb, pointer, bitmap, (idx.idx_count - retrieval->irb_upper_count), 
				prefix, &upper, (USHORT) (retrieval->irb_generic &
				(irb_partial | irb_descending | irb_starting | irb_equality)),
				flags))
		{
			page = (btree_page*) CCH_HANDOFF(tdbb, &window, page->btr_sibling,
				LCK_read, pag_index);
			pointer = BTreeNode::getPointerFirstNode(page);
			prefix = 0;
		}
	}
	else {
		// if there isn't an upper bound, just walk the index to the end of the level
		IndexNode node;
		pointer = BTreeNode::readNode(&node, pointer, flags, true);
		while (true) {

			if (BTreeNode::isEndLevel(&node, true)) {
				break;
			}

			if (!BTreeNode::isEndBucket(&node, true)) {
				SBM_set(tdbb, bitmap, node.recordNumber);
				pointer = BTreeNode::readNode(&node, pointer, flags, true);
				continue;
			}

			page = (btree_page*) CCH_HANDOFF(tdbb, &window, page->btr_sibling,
				LCK_read, pag_index);
			pointer = BTreeNode::getPointerFirstNode(page);
			pointer = BTreeNode::readNode(&node, pointer, flags, true);
		}
	}

	CCH_RELEASE(tdbb, &window);
}


UCHAR *BTR_find_leaf(btree_page* bucket, KEY *key, UCHAR *value,
					 USHORT *return_value, int descending, bool retrieval)
{
/**************************************
 *
 *	B T R _ f i n d _ l e a f
 *
 **************************************
 *
 * Functional description
 *	Locate and return a pointer to the insertion point.
 *	If the key doesn't belong in this bucket, return NULL.
 *	A flag indicates the index is descending.
 *
 **************************************/
	return find_node_start_point(bucket, key, value, return_value, (descending > 0), retrieval);
}


btree_page* BTR_find_page(TDBB tdbb,
				  IRB retrieval,
				  WIN * window,
				  IDX * idx, KEY * lower, KEY * upper, bool backwards)
{
/**************************************
 *
 *	B T R _ f i n d _ p a g e
 *
 **************************************
 *
 * Functional description
 *	Initialize for an index retrieval.
 *
 **************************************/

	SET_TDBB(tdbb);

	// Generate keys before we get any pages locked to avoid unwind
	// problems --  if we already have a key, assume that we 
	// are looking for an equality
	if (retrieval->irb_key) {
		copy_key(retrieval->irb_key, lower);
		copy_key(retrieval->irb_key, upper);
	}
	else {
		if (retrieval->irb_upper_count) {
			BTR_make_key(tdbb, retrieval->irb_upper_count,
						 retrieval->irb_value +
						 retrieval->irb_desc.idx_count,
						 &retrieval->irb_desc, upper,
						 (USHORT) (retrieval->irb_generic & irb_starting));
		}

		if (retrieval->irb_lower_count) {
			BTR_make_key(tdbb, retrieval->irb_lower_count,
						 retrieval->irb_value,
						 &retrieval->irb_desc, lower,
						 (USHORT) (retrieval->irb_generic & irb_starting));
		}
	}

	window->win_page = retrieval->irb_relation->rel_index_root;
	index_root_page* rpage = (index_root_page*) CCH_FETCH(tdbb, window, LCK_read, pag_root);

	if (!BTR_description
		(retrieval->irb_relation, rpage, idx, retrieval->irb_index))
	{
		CCH_RELEASE(tdbb, window);
		IBERROR(260);	// msg 260 index unexpectedly deleted
	}

	btree_page* page = (btree_page*) CCH_HANDOFF(tdbb, window, idx->idx_root,
		LCK_read, pag_index);

	// If there is a starting descriptor, search down index to starting position.
	// This may involve sibling buckets if splits are in progress.  If there 
	// isn't a starting descriptor, walk down the left side of the index (right
	// side if we are going backwards).
	SLONG number;
	if ((!backwards && retrieval->irb_lower_count) ||
		(backwards && retrieval->irb_upper_count))
	{
		while (page->btr_level > 0) {
			while (true) {
				number = find_page(page, backwards ? upper : lower, idx->idx_flags,
					NO_VALUE, (retrieval->irb_generic & (irb_starting | irb_partial)));
				if (number != END_BUCKET) {
					page = (btree_page*) CCH_HANDOFF(tdbb, window, number,
						LCK_read, pag_index);
					break;
				}

				page = (btree_page*) CCH_HANDOFF(tdbb, window, page->btr_sibling,
					LCK_read, pag_index);
			}
		}
	}
	else {
		IndexNode node;
		while (page->btr_level > 0) {
			UCHAR* pointer;
#ifdef SCROLLABLE_CURSORS
			if (backwards) {
				pointer = BTR_last_node(page, NAV_expand_index(window, 0), 0);
			{
			else
#endif
			{
				pointer = BTreeNode::getPointerFirstNode(page);
			}

			BTreeNode::readNode(&node, pointer, page->btr_header.pag_flags, false);
			page = (btree_page*) CCH_HANDOFF(tdbb, window, node.pageNumber,
				LCK_read, pag_index);

			// make sure that we are actually on the last page on this
			// level when scanning in the backward direction
			if (backwards) {
				while (page->btr_sibling) {
					page = (btree_page*) CCH_HANDOFF(tdbb, window, page->btr_sibling,
						LCK_read, pag_index);
				}
			}

		}
	}

	return page;
}


void BTR_insert(TDBB tdbb, WIN * root_window, IIB * insertion)
{
/**************************************
 *
 *	B T R _ i n s e r t
 *
 **************************************
 *
 * Functional description
 *	Insert a node into an index.
 *
 **************************************/

	SET_TDBB(tdbb);
	DBB dbb = tdbb->tdbb_database;

	IDX* idx = insertion->iib_descriptor;
	WIN window(idx->idx_root);
	btree_page* bucket = (btree_page*) CCH_FETCH(tdbb, &window, LCK_read, pag_index);

	if (bucket->btr_level == 0) {
		CCH_RELEASE(tdbb, &window);
		CCH_FETCH(tdbb, &window, LCK_write, pag_index);
	}
	CCH_RELEASE(tdbb, root_window);

	KEY key;
	SLONG recordNumber = 0;
	SLONG split_page = add_node(tdbb, &window, insertion, &key, 
		&recordNumber, NULL, NULL);
	if (split_page == NO_SPLIT) {
		return;
	}

	// The top of the index has split.  We need to make a new level and
	// update the index root page.  Oh boy.
	index_root_page* root = (index_root_page*) CCH_FETCH(tdbb, root_window, LCK_write, pag_root);

	window.win_page = root->irt_rpt[idx->idx_id].irt_root;
	bucket = (btree_page*) CCH_FETCH(tdbb, &window, LCK_write, pag_index);

	// the original page was marked as not garbage-collectable, but 
	// since it is the root page it won't be garbage-collected anyway, 
	// so go ahead and mark it as garbage-collectable now.
	CCH_MARK(tdbb, &window);
	bucket->btr_header.pag_flags &= ~btr_dont_gc;

	WIN new_window(split_page);
	btree_page* new_bucket = (btree_page*) CCH_FETCH(tdbb, &new_window, LCK_read, pag_index);

	if (bucket->btr_level != new_bucket->btr_level) {
		CCH_RELEASE(tdbb, &new_window);
		CCH_RELEASE(tdbb, &window);
		CORRUPT(204);	// msg 204 index inconsistent
	}

	CCH_RELEASE(tdbb, &new_window);
	CCH_RELEASE(tdbb, &window);

	if ((bucket->btr_level + 1) > MAX_LEVELS) {
		// Maximum level depth reached.
		// AB: !! NEW ERROR MESSAGE ? !!
		CORRUPT(204);	// msg 204 index inconsistent
	}

	// Allocate and format new bucket, this will always be a non-leaf page
	const SCHAR flags = bucket->btr_header.pag_flags;
	new_bucket = (btree_page*) DPM_allocate(tdbb, &new_window);
	CCH_precedence(tdbb, &new_window, window.win_page);
	new_bucket->btr_header.pag_type = pag_index;
	new_bucket->btr_relation = bucket->btr_relation;
	new_bucket->btr_level = bucket->btr_level + 1;
	new_bucket->btr_id = bucket->btr_id;
	new_bucket->btr_header.pag_flags |= (flags & BTR_FLAG_COPY_MASK);

	UCHAR *pointer;
	bool useJumpInfo = (bucket->btr_header.pag_flags & btr_jump_info);
	if (useJumpInfo) {
		IndexJumpInfo jumpInfo;
		// First get jumpinfo from the level deeper, because we need 
		// to know jumpAreaSize and keyLength.
		BTreeNode::getPointerFirstNode(bucket, &jumpInfo);
		// Write uncomplete jumpinfo, so we can set the firstNodeOffset
		// to the correct position.
		jumpInfo.jumpers = 0;
		pointer = BTreeNode::writeJumpInfo(new_bucket, &jumpInfo);
		// Finally write correct jumpinfo.
		jumpInfo.firstNodeOffset = (pointer - (UCHAR*)new_bucket);
		pointer = BTreeNode::writeJumpInfo(new_bucket, &jumpInfo);
	}
	else {
		pointer = BTreeNode::getPointerFirstNode(new_bucket);
	}

	// Set up first node as degenerate, but pointing to first bucket on
	// next level.
	IndexNode node;
	node.pageNumber = window.win_page;
	node.recordNumber = 0; // First record-number of level must be zero
	node.prefix = 0;
	node.length = 0;
	pointer = BTreeNode::writeNode(&node, pointer, flags, false);

	// Move in the split node
	node.pageNumber = split_page;
	node.recordNumber = recordNumber;
	node.prefix = 0;
	node.length = key.key_length;
	node.data = key.key_data;
	pointer = BTreeNode::writeNode(&node, pointer, flags, false);

	// mark end of level
	BTreeNode::setEndLevel(&node, false);
	pointer = BTreeNode::writeNode(&node, pointer, flags, false);

	// Calculate length of bucket
	new_bucket->btr_length = pointer - (UCHAR*)new_bucket;

	// update the root page to point to the new top-level page, 
	// and make sure the new page has higher precedence so that 
	// it will be written out first--this will make sure that the 
	// root page doesn't point into space
	CCH_RELEASE(tdbb, &new_window);
	CCH_precedence(tdbb, root_window, new_window.win_page);
	CCH_MARK(tdbb, root_window);
	root->irt_rpt[idx->idx_id].irt_root = new_window.win_page;

	CCH_RELEASE(tdbb, root_window);

}


IDX_E BTR_key(TDBB tdbb, jrd_rel* relation, REC record, IDX * idx, KEY * key, idx_null_state * null_state)
{
/**************************************
 *
 *	B T R _ k e y
 *
 **************************************
 *
 * Functional description
 *	Compute a key from an record and an index descriptor. 
 *	Note that compound keys are expanded by 25%.  If this
 *	changes, both BTR_key_length and GDEF exe.e have to
 *	change.
 *
 **************************************/
	KEY temp;
	DSC desc;
	DSC* desc_ptr;
	SSHORT stuff_count;
	int missing_unique_segments = 0;

	SET_TDBB(tdbb);
	DBB dbb = tdbb->tdbb_database;
	CHECK_DBB(dbb);

	IDX_E result = idx_e_ok;
	idx::idx_repeat* tail = idx->idx_rpt;

	try {

		// Special case single segment indices

		if (idx->idx_count == 1) {
			bool isNull;
#ifdef EXPRESSION_INDICES
			// for expression indices, compute the value of the expression
			if (idx->idx_expression) {
				jrd_req* current_request = tdbb->tdbb_request;
				tdbb->tdbb_request = idx->idx_expression_request;
				tdbb->tdbb_request->req_rpb[0].rpb_record = record;

				if (!(desc_ptr = EVL_expr(tdbb, idx->idx_expression))) {
					desc_ptr = &idx->idx_expression_desc;
				}

				isNull = ((tdbb->tdbb_request->req_flags & req_null) == req_null);
				tdbb->tdbb_request = current_request;
			}
			else
#endif
			{
				desc_ptr = &desc;
				// In order to "map a null to a default" value (in EVL_field()), 
				// the relation block is referenced. 
				// Reference: Bug 10116, 10424 
				//
				isNull = !EVL_field(relation, record, tail->idx_field, desc_ptr);
			}

			if (isNull && (idx->idx_flags & idx_unique)) {
				missing_unique_segments++;
			}

			compress(tdbb, desc_ptr, key, tail->idx_itype, isNull,
				(idx->idx_flags & idx_descending), (USHORT) FALSE);
		}
		else {
			UCHAR* p = key->key_data;
			stuff_count = 0;
			for (USHORT n = 0; n < idx->idx_count; n++, tail++) {
				for (; stuff_count; --stuff_count) {
					*p++ = 0;
				}

				desc_ptr = &desc;
				// In order to "map a null to a default" value (in EVL_field()), 
				// the relation block is referenced. 
				// Reference: Bug 10116, 10424 
				const bool isNull =
					!EVL_field(relation, record, tail->idx_field, desc_ptr);
				if (isNull && (idx->idx_flags & idx_unique)) {
					missing_unique_segments++;
				}

				compress(tdbb, desc_ptr, &temp, tail->idx_itype, isNull,
					(idx->idx_flags & idx_descending), (USHORT) FALSE);

				const UCHAR* q = temp.key_data;
				for (USHORT l = temp.key_length; l; --l, --stuff_count)	{
					if (stuff_count == 0) {
						*p++ = idx->idx_count - n;
						stuff_count = STUFF_COUNT;
					}
					*p++ = *q++;
				}
			}
			key->key_length = (p - key->key_data);
		}

		if (key->key_length >= MAX_KEY_LIMIT) {
			result = idx_e_keytoobig;
		}

		if (idx->idx_flags & idx_descending) {
			complement_key(key);
		}

		if (null_state) {
			*null_state = !missing_unique_segments ? idx_nulls_none :
				(missing_unique_segments == idx->idx_count) ? 
				idx_nulls_all : idx_nulls_some;
		}

		return result;

	}	// try
	catch(const std::exception&) {
		key->key_length = 0;
		return idx_e_conversion;
	}
}


USHORT BTR_key_length(jrd_rel* relation, IDX * idx)
{
/**************************************
 *
 *	B T R _ k e y _ l e n g t h
 *
 **************************************
 *
 * Functional description
 *	Compute the maximum key length for an index.
 *
 **************************************/
	USHORT length;

	TDBB tdbb = GET_THREAD_DATA;

	const fmt* format = MET_current(tdbb, relation);
	idx::idx_repeat* tail = idx->idx_rpt;

	// If there is only a single key, the computation is straightforward.
	if (idx->idx_count == 1) {
		switch (tail->idx_itype)
		{
		case idx_numeric:
		case idx_timestamp1:
			return sizeof(double);

		case idx_sql_time:
			return sizeof(ULONG);

		case idx_sql_date:
			return sizeof(SLONG);

		case idx_timestamp2:
			return sizeof(SINT64);

		case idx_numeric2:
			return INT64_KEY_LENGTH;
		}

#ifdef EXPRESSION_INDICES
		if (idx->idx_expression) {
			length = idx->idx_expression_desc.dsc_length;
			if (idx->idx_expression_desc.dsc_dtype == dtype_varying) {
				length = length - sizeof(SSHORT);
			}
		}
		else
#endif
		{
			length = format->fmt_desc[tail->idx_field].dsc_length;
			if (format->fmt_desc[tail->idx_field].dsc_dtype == dtype_varying) {
				length = length - sizeof(SSHORT);
			}
		}

		if (tail->idx_itype >= idx_first_intl_string) {
			return INTL_key_length(tdbb, tail->idx_itype, length);
		}
		else {
			return length;
		}
	}

	// Compute length of key for segmented indices.
	USHORT key_length = 0;

	for (USHORT n = 0; n < idx->idx_count; n++, tail++) {
		switch (tail->idx_itype)
		{
		case idx_numeric:
		case idx_timestamp1:
			length = sizeof(double);
			break;
		case idx_sql_time:
			length = sizeof(ULONG);
			break;
		case idx_sql_date:
			length = sizeof(ULONG);
			break;
		case idx_timestamp2:
			length = sizeof(SINT64);
			break;
		case idx_numeric2:
			length = INT64_KEY_LENGTH;
			break;
		default:
			length = format->fmt_desc[tail->idx_field].dsc_length;
			if (format->fmt_desc[tail->idx_field].dsc_dtype == dtype_varying) {
				length -= sizeof(SSHORT);
			}
			if (tail->idx_itype >= idx_first_intl_string) {
				length = INTL_key_length(tdbb, tail->idx_itype, length);
			}
			break;
		}
		key_length += ((length + STUFF_COUNT - 1) / STUFF_COUNT) * (STUFF_COUNT + 1);
	}

	return key_length;
}


#ifdef SCROLLABLE_CURSORS
UCHAR *BTR_last_node(btree_page* page, jrd_exp* expanded_page, BTX * expanded_node)
{
/**************************************
 *
 *	B T R _ l a s t _ n o d e
 *
 **************************************
 *
 * Functional description                               
 *	Find the last node on a page.  Used when walking
 *	down the right side of an index tree.  
 *
 **************************************/

	// the last expanded node is always at the end of the page 
	// minus the size of a BTX, since there is always an extra
	// BTX node with zero-length tail at the end of the page
	BTX enode = (BTX) ((UCHAR*)expanded_page + expanded_page->exp_length - BTX_SIZE);

	// starting at the end of the page, find the
	// first node that is not an end marker
	UCHAR *pointer = ((UCHAR*)page + page->btr_length);
	SCHAR flags = page->btr_header.pag_flags;
	IndexNode node;
	while (true) {
		pointer = BTR_previousNode(&node, pointer, flags, &enode);
		if (node.recordNumber != END_BUCKET && 
			node.recordNumber != END_LEVEL) {
			if (expanded_node) {
				*expanded_node = enode;
			}
			return node.nodePointer;
		}
	}
}
#endif


#ifdef SCROLLABLE_CURSORS
btree_page* BTR_left_handoff(TDBB tdbb, WIN * window, btree_page* page, SSHORT lock_level)
{
/**************************************
 *
 *	B T R _ l e f t _ h a n d o f f
 *
 **************************************
 *
 * Functional description
 *	Handoff a btree page to the left.  This is more difficult than a 
 *	right handoff because we have to traverse pages without handing 
 *	off locks.  (A lock handoff to the left while someone was handing
 *	off to the right could result in deadlock.)
 *
 **************************************/

	SET_TDBB(tdbb);
	DBB dbb = tdbb->tdbb_database;
	CHECK_DBB(dbb);

	SLONG original_page = window->win_page;
	SLONG left_sibling = page->btr_left_sibling;

	CCH_RELEASE(tdbb, window);
	window->win_page = left_sibling;
	page = (btree_page*) CCH_FETCH(tdbb, window, lock_level, pag_index);

	if ((sibling = page->btr_sibling) == original_page) {
		return page;
	}

	// Since we are not handing off pages, a page could split before we get to it.
	// To detect this case, fetch the left sibling pointer and then handoff right
	// sibling pointers until we reach the page to the left of the page passed 
	// to us.

	SLONG sibling;
	while (sibling != original_page) {
		page = (btree_page*) CCH_HANDOFF(tdbb, window, page->btr_sibling,
			lock_level, pag_index);
		sibling = page->btr_sibling;
	}
	WIN fix_win;
	fix_win.win_page = original_page;
	fix_win.win_flags = 0;
	btree_page* fix_page = (btree_page*) CCH_FETCH(tdbb, &fix_win, LCK_write, pag_index);

	// if someone else already fixed it, just return
	if (fix_page->btr_left_sibling == window->win_page) {
		CCH_RELEASE(tdbb, &fix_win);
		return page;
	}

	CCH_MARK(tdbb, &fix_win);
	fix_page->btr_left_sibling = window->win_page;

	CCH_RELEASE(tdbb, &fix_win);

	return page;
}
#endif


USHORT BTR_lookup(TDBB tdbb, jrd_rel* relation, USHORT id, IDX * buffer)
{
/**************************************
 *
 *	B T R _ l o o k u p
 *
 **************************************
 *
 * Functional description
 *	Return a description of the specified index.
 *
 **************************************/
	SET_TDBB(tdbb);
	WIN window(-1);

	index_root_page* root = fetch_root(tdbb, &window, relation);
	if (!root) {
		return FB_FAILURE;
	}

	if ((id >= root->irt_count)
		|| !BTR_description(relation, root, buffer, id)) 
	{
		CCH_RELEASE(tdbb, &window);
		return FB_FAILURE;
	}
	CCH_RELEASE(tdbb, &window);
	return FB_SUCCESS;
}


void BTR_make_key(TDBB tdbb,
				  USHORT count,
				  jrd_nod** exprs, IDX * idx, KEY * key, USHORT fuzzy)
{
/**************************************
 *
 *	B T R _ m a k e _ k e y
 *
 **************************************
 *
 * Functional description
 *	Construct a (possibly) compound search key given a key count,
 *	a vector of value expressions, and a place to put the key.
 *
 **************************************/
	DSC *desc, temp_desc;
	KEY temp;
	bool isNull;

	SET_TDBB(tdbb);
	DBB dbb = tdbb->tdbb_database;

	fb_assert(count > 0);
	fb_assert(idx != NULL);
	fb_assert(exprs != NULL);
	fb_assert(key != NULL);

	idx::idx_repeat* tail = idx->idx_rpt;

	// If the index is a single segment index, don't sweat the compound
	// stuff.
	if (idx->idx_count == 1) {
		desc = eval(tdbb, *exprs, &temp_desc, &isNull);
		compress(tdbb, desc, key, tail->idx_itype, isNull,
			(idx->idx_flags & idx_descending), fuzzy);
	}
	else {
		// Make a compound key
		UCHAR* p = key->key_data;
		SSHORT stuff_count = 0;

		for (USHORT n = 0; n < count; n++, tail++) {
			for (; stuff_count; --stuff_count) {
				*p++ = 0;
			}
			desc = eval(tdbb, *exprs++, &temp_desc, &isNull);
			compress(tdbb, desc, &temp, tail->idx_itype, isNull,
				(idx->idx_flags & idx_descending),
				(USHORT) ((n == count - 1) ? fuzzy : FALSE));
			const UCHAR* q = temp.key_data;
			for (USHORT l = temp.key_length; l; --l, --stuff_count)
			{
				if (stuff_count == 0) {
					*p++ = idx->idx_count - n;
					stuff_count = STUFF_COUNT;
				}
				*p++ = *q++;
			}
		}
		key->key_length = (p - key->key_data);
	}

	if (idx->idx_flags & idx_descending) {
		complement_key(key);
	}
}


bool BTR_next_index(TDBB tdbb,
					   jrd_rel* relation, jrd_tra* transaction, IDX * idx, WIN * window)
{
/**************************************
 *
 *	B T R _ n e x t _ i n d e x
 *
 **************************************
 *
 * Functional description
 *	Get next index for relation.  Index ids
 *  recently change from UCHAR to SHORT
 *
 **************************************/
	SET_TDBB(tdbb);

	SSHORT id;
	if ((USHORT)idx->idx_id == (USHORT) -1) {
		id = 0;
		window->win_bdb = NULL;
	}
	else {
		id = idx->idx_id + 1;
	}

	index_root_page* root;
	if (window->win_bdb) {
		root = (index_root_page*) window->win_buffer;
	}
	else if (!(root = fetch_root(tdbb, window, relation))) {
		return false;
	}

	for (; id < root->irt_count; ++id) {
		const index_root_page::irt_repeat* irt_desc = root->irt_rpt + id;
		if (!irt_desc->irt_root &&
			(irt_desc->irt_flags & irt_in_progress) && transaction) 
		{
			const SLONG trans = irt_desc->irt_stuff.irt_transaction;
			CCH_RELEASE(tdbb, window);
			const int trans_state = TRA_wait(tdbb, transaction, trans, true);
			if ((trans_state == tra_dead) || (trans_state == tra_committed))
			{
				// clean up this left-over index
				root = (index_root_page*) CCH_FETCH(tdbb, window, LCK_write, pag_root);
				irt_desc = root->irt_rpt + id;
				if (!irt_desc->irt_root &&
					irt_desc->irt_stuff.irt_transaction == trans &&
					(irt_desc->irt_flags & irt_in_progress))
				{
					BTR_delete_index(tdbb, window, id);
				}
				else {
					CCH_RELEASE(tdbb, window);
				}
				root = (index_root_page*) CCH_FETCH(tdbb, window, LCK_read, pag_root);
				continue;
			}
			else {
				root = (index_root_page*) CCH_FETCH(tdbb, window, LCK_read, pag_root);
			}
		}
		if (BTR_description(relation, root, idx, id)) {
			return true;
		}
	}

	CCH_RELEASE(tdbb, window);

	return false;
}


UCHAR *BTR_nextNode(IndexNode * node, UCHAR * pointer, 
					SCHAR flags,  BTX * expanded_node)
{
/**************************************
 *
 *	B T R _ n e x t N o d e
 *
 **************************************
 *
 * Functional description                               
 *	Find the next node on both the index page
 *	and its associated expanded buffer.
 *
 **************************************/

	pointer = BTreeNode::readNode(node, pointer, flags, true);

	if (*expanded_node) {
		*expanded_node = (BTX) ((UCHAR*) (*expanded_node)->btx_data + 
			node->prefix + node->length);
	}

	return pointer;
}


UCHAR *BTR_previousNode(IndexNode * node, UCHAR * pointer,
					SCHAR flags,  BTX * expanded_node)
{
/**************************************
 *
 *	B T R _ p r e v i o u s N o d e
 *
 **************************************
 *
 * Functional description                               
 *	Find the previous node on a page.  Used when walking
 *	an index backwards.  
 *
 **************************************/

	pointer = (pointer - (*expanded_node)->btx_btr_previous_length);

	*expanded_node = (BTX) ((UCHAR*) *expanded_node - (*expanded_node)->btx_previous_length);

	return pointer;
}


void BTR_remove(TDBB tdbb, WIN * root_window, IIB * insertion)
{
/**************************************
 *
 *	B T R _ r e m o v e
 *
 **************************************
 *
 * Functional description
 *	Remove an index node from a b-tree.  
 *	If the node doesn't exist, don't get overly excited.
 *
 **************************************/

	DBB dbb = tdbb->tdbb_database;
	IDX* idx = insertion->iib_descriptor;
	WIN window(idx->idx_root);
	btree_page* page = (btree_page*) CCH_FETCH(tdbb, &window, LCK_read, pag_index);

	// If the page is level 0, re-fetch it for write
	const UCHAR level = page->btr_level;
	if (level == 0) {
		CCH_RELEASE(tdbb, &window);
		CCH_FETCH(tdbb, &window, LCK_write, pag_index);
	}

	// remove the node from the index tree via recursive descent
	CONTENTS result = remove_node(tdbb, insertion, &window);

	// if the root page points at only one lower page, remove this 
	// level to prevent the tree from being deeper than necessary-- 
	// do this only if the level is greater than 1 to prevent 
	// excessive thrashing in the case where a small table is 
	// constantly being loaded and deleted.
	if ((result == contents_single) && (level > 1)) {
		// we must first release the windows to obtain the root for write 
		// without getting deadlocked

		CCH_RELEASE(tdbb, &window);
		CCH_RELEASE(tdbb, root_window);

		index_root_page* root = (index_root_page*) CCH_FETCH(tdbb, root_window, LCK_write, pag_root);
		page = (btree_page*) CCH_FETCH(tdbb, &window, LCK_write, pag_index);

		// get the page number of the child, and check to make sure 
		// the page still has only one node on it
		UCHAR *pointer = BTreeNode::getPointerFirstNode(page);
		SCHAR flags = page->btr_header.pag_flags;
		IndexNode pageNode;
		pointer = BTreeNode::readNode(&pageNode, pointer, flags, false);

		const SLONG number = pageNode.pageNumber;
		pointer = BTreeNode::readNode(&pageNode, pointer, flags, false);
		if (!(BTreeNode::isEndBucket(&pageNode, false) ||
			BTreeNode::isEndLevel(&pageNode, false))) 
		{
			CCH_RELEASE(tdbb, &window);
			CCH_RELEASE(tdbb, root_window);
			return;
		}

		CCH_MARK(tdbb, root_window);
		root->irt_rpt[idx->idx_id].irt_root = number;

		// release the pages, and place the page formerly at the top level
		// on the free list, making sure the root page is written out first 
		// so that we're not pointing to a released page
		CCH_RELEASE(tdbb, root_window);
		CCH_RELEASE(tdbb, &window);
		PAG_release_page(window.win_page, root_window->win_page);
	}

	if (window.win_bdb) {
		CCH_RELEASE(tdbb, &window);
	}
	if (root_window->win_bdb) {
		CCH_RELEASE(tdbb, root_window);
	}

}


void BTR_reserve_slot(TDBB tdbb, jrd_rel* relation, jrd_tra* transaction, IDX * idx)
{
/**************************************
 *
 *	B T R _ r e s e r v e _ s l o t
 *
 **************************************
 *
 * Functional description
 *	Reserve a slot on an index root page 
 *	in preparation to index creation.
 *
 **************************************/

	SET_TDBB(tdbb);
	DBB dbb = tdbb->tdbb_database;
	CHECK_DBB(dbb);

	// Get root page, assign an index id, and store the index descriptor.
	// Leave the root pointer null for the time being.
	WIN window(relation->rel_index_root);
	index_root_page* root = (index_root_page*) CCH_FETCH(tdbb, &window, LCK_write, pag_root);
	CCH_MARK(tdbb, &window);

	// check that we create no more indexes than will fit on a single root page
	if (root->irt_count > dbb->dbb_max_idx) {
		CCH_RELEASE(tdbb, &window);
		ERR_post(isc_no_meta_update, isc_arg_gds, isc_max_idx,
				 isc_arg_number, (SLONG) dbb->dbb_max_idx, 0);
	}
	// Scan the index page looking for the high water mark of the descriptions and,
	// perhaps, an empty index slot

	UCHAR *desc;
	USHORT l, space;
	index_root_page::irt_repeat * root_idx, *end, *slot;
	bool maybe_no_room = false;
	
retry:
	// dimitr: irtd_selectivity member of IRTD is introduced in ODS11
	if (dbb->dbb_ods_version < ODS_VERSION11)
		l = idx->idx_count * sizeof(irtd_ods10);
	else
		l = idx->idx_count * sizeof(irtd);

	space = dbb->dbb_page_size;
	slot = NULL;

	for (root_idx = root->irt_rpt, end = root_idx + root->irt_count;
		 root_idx < end; root_idx++) 
	{
		if (root_idx->irt_root || (root_idx->irt_flags & irt_in_progress)) {
			space = MIN(space, root_idx->irt_desc);
		}
		if (!root_idx->irt_root && !slot
			&& !(root_idx->irt_flags & irt_in_progress)) 
		{
			slot = root_idx;
		}
	}

	space -= l;
	desc = (UCHAR*)root + space;

	// Verify that there is enough room on the Index root page.
	if (desc < (UCHAR *) (end + 1)) {
		// Not enough room:  Attempt to compress the index root page and try again.
		// If this is the second try already, then there really is no more room.
		if (maybe_no_room) {
			CCH_RELEASE(tdbb, &window);
			ERR_post(isc_no_meta_update, isc_arg_gds,
					 isc_index_root_page_full, 0);
		}
		compress_root(tdbb, root);
		maybe_no_room = true;
		goto retry;
	}

	// If we didn't pick up an empty slot, allocate a new one
	if (!slot) {
		slot = end;
		root->irt_count++;
	}

	idx->idx_id = slot - root->irt_rpt;
	slot->irt_desc = space;
	fb_assert(idx->idx_count <= MAX_UCHAR);
	slot->irt_keys = (UCHAR) idx->idx_count;
	slot->irt_flags = idx->idx_flags | irt_in_progress;

	if (transaction) {
		slot->irt_stuff.irt_transaction = transaction->tra_number;
	}

	slot->irt_root = 0;

	if (dbb->dbb_ods_version < ODS_VERSION11) {
		for (USHORT i=0; i<idx->idx_count; i++) {
			irtd_ods10 temp;
			temp.irtd_field = idx->idx_rpt[i].idx_field;
			temp.irtd_itype = idx->idx_rpt[i].idx_itype;
			memcpy(desc, &temp, sizeof(temp));
			desc += sizeof(temp);
		}
	}
	else {
		// Exploit the fact idx_repeat structure matches ODS IRTD one
		memcpy(desc, idx->idx_rpt, l);
	}

	CCH_RELEASE(tdbb, &window);
}


void BTR_selectivity(TDBB tdbb, jrd_rel* relation, USHORT id, SelectivityList& selectivity)
{
/**************************************
 *
 *	B T R _ s e l e c t i v i t y
 *
 **************************************
 *
 * Functional description
 *	Update index selectivity on the fly.
 *	Note that index leaf pages are walked
 *	without visiting data pages. Thus the
 *	effects of uncommitted transactions
 *	will be included in the calculation.
 *
 **************************************/

	SET_TDBB(tdbb);
	WIN window(-1);

	index_root_page* root = fetch_root(tdbb, &window, relation);
	if (!root) {
		return;
	}

	SLONG page;
	if (id >= root->irt_count || !(page = root->irt_rpt[id].irt_root)) {
		CCH_RELEASE(tdbb, &window);
		return;
	}
	window.win_flags = WIN_large_scan;
	window.win_scans = 1;
	btree_page* bucket = (btree_page*) CCH_HANDOFF(tdbb, &window, page, LCK_read, pag_index);
	SCHAR flags = bucket->btr_header.pag_flags;

	// go down the left side of the index to leaf level
	UCHAR* pointer = BTreeNode::getPointerFirstNode(bucket);
	while (bucket->btr_level) {
		IndexNode pageNode;
		BTreeNode::readNode(&pageNode, pointer, flags, false);
		bucket = (btree_page*) CCH_HANDOFF(tdbb, &window, pageNode.pageNumber, LCK_read, pag_index);
		pointer = BTreeNode::getPointerFirstNode(bucket);
		flags = bucket->btr_header.pag_flags;
		page = pageNode.pageNumber;
	}

	bool dup;
	SLONG nodes = 0;
	SLONG duplicates = 0;
	KEY key;
	key.key_length = 0;
	SSHORT l; 
	bool firstNode = true;
	const USHORT segments = root->irt_rpt[id].irt_keys;

	SSHORT count, stuff_count, pos, i;
	Firebird::HalfStaticArray<ULONG, 4> duplicatesList(tdbb->tdbb_default);
	duplicatesList.grow(segments);
	memset(duplicatesList.begin(), 0, segments * sizeof(ULONG));

	DBB dbb = tdbb->tdbb_database;

	// go through all the leaf nodes and count them; 
	// also count how many of them are duplicates
	IndexNode node;
	while (page) {
		pointer = BTreeNode::readNode(&node, pointer, flags, true);
		while (true) {
			if (BTreeNode::isEndBucket(&node, true) ||
				BTreeNode::isEndLevel(&node, true) ) 
			{
				break;
			}
			++nodes;
			l = node.length + node.prefix;

			if (segments > 1 && !firstNode) 
			{

				// Initialize variables for segment duplicate check.
				// count holds the current checking segment (starting by
				// the maximum segment number to 1).
				const UCHAR* p1 = key.key_data;
				const UCHAR* const p1_end = p1 + key.key_length;
				const UCHAR* p2 = node.data;
				const UCHAR* const p2_end = p2 + node.length;
				if (node.prefix == 0) {
					count = *p2; 
					pos = 0;
					stuff_count = 0;
				}
				else {
					pos = node.prefix;
					// find the segment number were we're starting.
					i = (pos / (STUFF_COUNT + 1)) * (STUFF_COUNT + 1);
					if (i == pos) {
						// We _should_ pick number from data if available
						count = *p2;
					}
					else {
						count = *(p1 + i);
					}
					// update stuff_count to the current position.
					stuff_count = STUFF_COUNT + 1 - (pos - i);
					p1 += pos;
				}

				//Look for duplicates in the segments
				while ((p1 < p1_end) && (p2 < p2_end)) {
					if (stuff_count == 0) {
						if (*p1 != *p2) {
							// We're done
							break;
						}
						count = *p2;
						p1++;
						p2++;
						stuff_count = STUFF_COUNT;
					}
					if (*p1 != *p2) {
						//We're done
						break;
					}
					p1++;
					p2++;
					stuff_count--;
				}

				if ((p1 == p1_end) && (p2 == p2_end)) {
					count = 0; // All segments are duplicates
				}
				for (i = count + 1; i <= segments; i++) {
					duplicatesList[segments - i]++;
				}

			}

			// figure out if this is a duplicate
			if (node.nodePointer == BTreeNode::getPointerFirstNode(bucket)) {
				dup = BTreeNode::keyEquality(key.key_length, key.key_data, &node);
			}
			else {
				dup = (!node.length && (l == key.key_length));
			}
			if (dup && !firstNode) {
				++duplicates;
			}
			if (firstNode) {
				firstNode = false;
			}

			// keep the key value current for comparison with the next key
			key.key_length = l;
			l = node.length;
			if (l) {
				UCHAR* p = key.key_data + node.prefix;
				const UCHAR* q = node.data;
				do {
					*p++ = *q++;
				} while (--l);
			}
			pointer = BTreeNode::readNode(&node, pointer, flags, true);
		}

		if (node.recordNumber == END_LEVEL || !(page = bucket->btr_sibling))
		{
			break;
		}
		bucket = (btree_page*) CCH_HANDOFF_TAIL(tdbb, &window, page, LCK_read, pag_index);
		pointer = BTreeNode::getPointerFirstNode(bucket);
		flags = bucket->btr_header.pag_flags;
	}

	CCH_RELEASE_TAIL(tdbb, &window);

	// calculate the selectivity 
	selectivity.grow(segments);
	if (segments > 1) {
		for (i = 0; i < segments; i++) {
			selectivity[i] = 
				(float) ((nodes) ? 1.0 / (float) (nodes - duplicatesList[i]) : 0.0);
		}	
	}
	else {
		selectivity[0] = (float) ((nodes) ? 1.0 / (float) (nodes - duplicates) : 0.0);
	}

	// Store the selectivity on the root page
	window.win_page = relation->rel_index_root;
	window.win_flags = 0;
	root = (index_root_page*) CCH_FETCH(tdbb, &window, LCK_write, pag_root);
	CCH_MARK(tdbb, &window);
	update_selectivity(root, id, selectivity);
	CCH_RELEASE(tdbb, &window);
}


static SLONG add_node(TDBB tdbb,
					  WIN * window,
					  IIB * insertion,
					  KEY * new_key,
					  SLONG * new_record_number,
					  SLONG * original_page, 
					  SLONG * sibling_page)
{
/**************************************
 *
 *	a d d _ n o d e
 *
 **************************************
 *
 * Functional description
 *	Insert a node in an index.  This recurses to the leaf level.
 *	If a split occurs, return the new index page number and its
 *	leading string.
 *
 **************************************/

	SET_TDBB(tdbb);
	SLONG split;
	btree_page* bucket = (btree_page*) window->win_buffer;

	// For leaf level guys, loop thru the leaf buckets until insertion
	// point is found (should be instant)
	if (bucket->btr_level == 0) {
		while (true) {
			split = insert_node(tdbb, window, insertion, new_key,
				new_record_number, original_page, sibling_page);
			if (split != NO_VALUE) {
				return split;
			}
			else {
				bucket = (btree_page*) CCH_HANDOFF(tdbb, window,
					bucket->btr_sibling, LCK_write, pag_index);
			}
		}
	}

	// If we're above the leaf level, find the appropriate node in the chain of sibling pages.
	// Hold on to this position while we recurse down to the next level, in case there's a 
	// split at the lower level, in which case we need to insert the new page at this level.
	SLONG page;
	while (true) {
		page = find_page(bucket, insertion->iib_key, 
			insertion->iib_descriptor->idx_flags, insertion->iib_number);
		if (page != END_BUCKET) {
			break;
		}
		bucket = (btree_page*) CCH_HANDOFF(tdbb, window, bucket->btr_sibling,
			LCK_read, pag_index);
	}

	// Fetch the page at the next level down.  If the next level is leaf level, 
	// fetch for write since we know we are going to write to the page (most likely).
	SLONG index = window->win_page;
	CCH_HANDOFF(tdbb, window, page,
		(SSHORT) ((bucket->btr_level == 1) ? LCK_write : LCK_read), pag_index);

	// now recursively try to insert the node at the next level down
	IIB propagate;
	split = add_node(tdbb, window, insertion, new_key, new_record_number, 
		&page, &propagate.iib_sibling);
	if (split == NO_SPLIT) {
		return NO_SPLIT;
	}

	// The page at the lower level split, so we need to insert a pointer 
	// to the new page to the page at this level.
	window->win_page = index;
	bucket = (btree_page*) CCH_FETCH(tdbb, window, LCK_write, pag_index);

	propagate.iib_number = split;
	propagate.iib_descriptor = insertion->iib_descriptor;
	propagate.iib_relation = insertion->iib_relation;
	propagate.iib_duplicates = NULL;
	propagate.iib_key = new_key;

	// now loop through the sibling pages trying to find the appropriate 
	// place to put the pointer to the lower level page--remember that the 
	// page we were on could have split while we weren't looking
	SLONG original_page2; 
	SLONG sibling_page2;
	while (true) {
		split = insert_node(tdbb, window, &propagate, new_key,
			new_record_number, &original_page2, &sibling_page2);

		if (split != NO_VALUE) {
			break;
		}
		else {
			bucket = (btree_page*) CCH_HANDOFF(tdbb, window, bucket->btr_sibling,
				LCK_write, pag_index);
		}
	}

	// the split page on the lower level has been propogated, so we can go back to 
	// the page it was split from, and mark it as garbage-collectable now
	window->win_page = page;
	bucket = (btree_page*) CCH_FETCH(tdbb, window, LCK_write, pag_index);
	CCH_MARK(tdbb, window);
	bucket->btr_header.pag_flags &= ~btr_dont_gc;
	CCH_RELEASE(tdbb, window);

	if (original_page) {
		*original_page = original_page2;
	}
	if (sibling_page) {
		*sibling_page = sibling_page2;
	}
	return split;
}


static void complement_key(KEY * key)
{
/**************************************
 *
 *	c o m p l e m e n t _ k e y
 *
 **************************************
 *
 * Functional description
 *	Negate a key for descending index.
 *
 **************************************/
	UCHAR* p = key->key_data;
	for (const UCHAR* const end = p + key->key_length; p < end; p++) {
		*p ^= -1;
	}
}


static void compress(TDBB tdbb,
					 DSC * desc,
					 KEY * key,
					 USHORT itype,
					 bool isNull, bool descending, USHORT fuzzy)
{
/**************************************
 *
 *	c o m p r e s s
 *
 **************************************
 *
 * Functional description
 *	Compress a data value into an index key.
 *
 **************************************/
	USHORT length;
	UCHAR pad, *ptr;
	union {
		INT64_KEY temp_int64_key;
		double temp_double;
		ULONG temp_ulong;
		SLONG temp_slong;
		SINT64 temp_sint64;
		UCHAR temp_char[sizeof(INT64_KEY)];
	} temp;
	USHORT temp_copy_length;
	bool temp_is_negative = false;
	bool int64_key_op = false;

	SET_TDBB(tdbb);
	DBB dbb = tdbb->tdbb_database;
	CHECK_DBB(dbb);

	UCHAR* p = key->key_data;

	if (isNull && dbb->dbb_ods_version >= ODS_VERSION7) {
		pad = 0;
		// AB: NULL should be threated as lowest value possible.
		//     Therefore don't complement pad when we have an
		//     ascending index.
		if (dbb->dbb_ods_version < ODS_VERSION11) {
			if (!descending) { 
				pad ^= -1;
			}
		}
		else {
			if (!descending) {
				key->key_length = 0;
				return;
			}
		}
		
		switch (itype)
		{
		case idx_numeric:
		case idx_timestamp1:
			length = sizeof(double);
			break;
		case idx_sql_time:
			length = sizeof(ULONG);
			break;
		case idx_sql_date:
			length = sizeof(SLONG);
			break;
		case idx_timestamp2:
			length = sizeof(SINT64);
			break;
		case idx_numeric2:
			length = INT64_KEY_LENGTH;
			break;
		default:
			length = desc->dsc_length;
			if (desc->dsc_dtype == dtype_varying) {
				length -= sizeof(SSHORT);
			}
			if (itype >= idx_first_intl_string) {
				length = INTL_key_length(tdbb, itype, length);
			}
			break;
		}
		length = (length > sizeof(key->key_data)) ? sizeof(key->key_data) : length;
		while (length--) {
			*p++ = pad;
		}
		key->key_length = (p - key->key_data);
		return;
	}


	if (itype == idx_string ||
		itype == idx_byte_array ||
		itype == idx_metadata || itype >= idx_first_intl_string) 
	{
		UCHAR temp1[MAX_KEY];
		pad = (itype == idx_string) ? ' ' : 0;

		if (isNull) {
			length = 0;
		}
		else if (itype >= idx_first_intl_string || itype == idx_metadata) {
			DSC to;

			// convert to an international byte array
			to.dsc_dtype = dtype_text;
			to.dsc_flags = 0;
			to.dsc_sub_type = 0;
			to.dsc_scale = 0;
			to.dsc_ttype = ttype_sort_key;
			to.dsc_length = sizeof(temp1);
			ptr = to.dsc_address = temp1;
			length = INTL_string_to_key(tdbb, itype, desc, &to, fuzzy);
		}
		else {
			USHORT ttype;
			length = MOV_get_string_ptr(desc, &ttype, &ptr, (vary*) temp1, MAX_KEY);
		}

		if (length) {
			if (length > sizeof(key->key_data)) {
				length = sizeof(key->key_data);
			}
			do {
				*p++ = *ptr++;
			} while (--length);
		}
		else {
			*p++ = pad;
		}
		while (p > key->key_data) {
			if (*--p != pad) {
				break;
			}
		}
		key->key_length = p + 1 - key->key_data;

		return;
	}

	// The index is numeric.  
	//   For idx_numeric...
	//	 Convert the value to a double precision number, 
	//   then zap it to compare in a byte-wise order. 
	// For idx_numeric2...
	//   Convert the value to a INT64_KEY struct,
	//   then zap it to compare in a byte-wise order. 

	temp_copy_length = sizeof(double);
	if (isNull) {
		memset(&temp, 0, sizeof(temp));
	}
	if (itype == idx_timestamp1) {
		temp.temp_double = MOV_date_to_double(desc);
		temp_is_negative = (temp.temp_double < 0);
#ifdef DEBUG_INDEXKEY
		ib_fprintf(ib_stderr, "TIMESTAMP1 %lf ", temp.temp_double);
#endif
	}
	else if (itype == idx_timestamp2) {
		GDS_TIMESTAMP timestamp;
		timestamp = MOV_get_timestamp(desc);
#define SECONDS_PER_DAY	((ULONG) 24 * 60 * 60)
		temp.temp_sint64 = ((SINT64) (timestamp.timestamp_date) *
			(SINT64) (SECONDS_PER_DAY * ISC_TIME_SECONDS_PRECISION)) +
			(SINT64) (timestamp.timestamp_time);
		temp_copy_length = sizeof(SINT64);
		temp_is_negative = (temp.temp_sint64 < 0);
#ifdef DEBUG_INDEXKEY
		ib_fprintf(ib_stderr, "TIMESTAMP2: %d:%u ",
				   ((SLONG *) desc->dsc_address)[0],
				   ((ULONG *) desc->dsc_address)[1]);
		ib_fprintf(ib_stderr, "TIMESTAMP2: %20" QUADFORMAT "d ",
				   temp.temp_sint64);
#endif
	}
	else if (itype == idx_sql_date) {
		temp.temp_slong = MOV_get_sql_date(desc);
		temp_copy_length = sizeof(SLONG);
		temp_is_negative = (temp.temp_slong < 0);
#ifdef DEBUG_INDEXKEY
		ib_fprintf(ib_stderr, "DATE %d ", temp.temp_slong);
#endif
	}
	else if (itype == idx_sql_time) {
		temp.temp_ulong = MOV_get_sql_time(desc);
		temp_copy_length = sizeof(ULONG);
		temp_is_negative = false;
#ifdef DEBUG_INDEXKEY
		ib_fprintf(ib_stderr, "TIME %u ", temp.temp_ulong);
#endif
	}
	else if (itype == idx_numeric2) {
		int64_key_op = true;
		temp.temp_int64_key =
			make_int64_key(MOV_get_int64(desc, desc->dsc_scale), desc->dsc_scale);
		temp_copy_length = sizeof(temp.temp_int64_key.d_part);
		temp_is_negative = (temp.temp_int64_key.d_part < 0);
#ifdef DEBUG_INDEXKEY
		print_int64_key(*(SINT64 *) desc->dsc_address,
			desc->dsc_scale, temp.temp_int64_key);
#endif
	}
	else if (desc->dsc_dtype == dtype_timestamp) {
		// This is the same as the pre v6 behavior.  Basically, the
		// customer has created a NUMERIC index, and is probing into that
		// index using a TIMESTAMP value.  
		// eg:  WHERE anInteger = TIMESTAMP '1998-9-16'
		temp.temp_double = MOV_date_to_double(desc);
		temp_is_negative = (temp.temp_double < 0);
#ifdef DEBUG_INDEXKEY
		ib_fprintf(ib_stderr, "TIMESTAMP1 special %lg ", temp.temp_double);
#endif
	}
	else {
		temp.temp_double = MOV_get_double(desc);
		temp_is_negative = (temp.temp_double < 0);
#ifdef DEBUG_INDEXKEY
		ib_fprintf(ib_stderr, "NUMERIC %lg ", temp.temp_double);
#endif
	}

#ifdef IEEE

	const UCHAR* q;

#ifndef WORDS_BIGENDIAN
	// For little-endian machines, reverse the order of bytes for the key
	// Copy the first set of bytes into key_data
	for (q = temp.temp_char + temp_copy_length, length =
		 temp_copy_length; length; --length)
	{
		*p++ = *--q;
	}

	// Copy the next 2 bytes into key_data, if key is of an int64 type
	if (int64_key_op) {
		for (q = temp.temp_char + sizeof(double) + sizeof(SSHORT),
			 length = sizeof(SSHORT); length; --length)
		{
			*p++ = *--q;
		}
	}
#else
	// For big-endian machines, copy the bytes as laid down
	// Copy the first set of bytes into key_data
	for (q = temp.temp_char, length = temp_copy_length; length; --length) {
		*p++ = *q++;
	}

	// Copy the next 2 bytes into key_data, if key is of an int64 type
	if (int64_key_op) {
		for (q = temp.temp_char + sizeof(double),
			 length = sizeof(SSHORT); length; --length)
		{
			*p++ = *q++;
		}
	}
#endif /* !WORDS_BIGENDIAN */


#else /* IEEE */


	// The conversion from G_FLOAT to D_FLOAT made below was removed because
	// it prevented users from entering otherwise valid numbers into a field
	// which was in an index.   A D_FLOAT has the sign and 7 of 8 exponent
	// bits in the first byte and the remaining exponent bit plus the first
	// 7 bits of the mantissa in the second byte.   For G_FLOATS, the sign
	// and 7 of 11 exponent bits go into the first byte, with the remaining
	// 4 exponent bits going into the second byte, with the first 4 bits of
	// the mantissa.   Why this conversion was done is unknown, but it is
	// of limited utility, being useful for reducing the compressed field
	// length only for those values which have 0 for the last 6 bytes and
	// a nonzero value for the 5-7 bits of the mantissa.


	//***************************************************************
	//#ifdef VMS
	//temp.temp_double = MTH$CVT_G_D (&temp.temp_double);
	//#endif
	//***************************************************************

	*p++ = temp.temp_char[1];
	*p++ = temp.temp_char[0];
	*p++ = temp.temp_char[3];
	*p++ = temp.temp_char[2];
	*p++ = temp.temp_char[5];
	*p++ = temp.temp_char[4];
	*p++ = temp.temp_char[7];
	*p++ = temp.temp_char[6];

#error compile_time_failure:
#error Code needs to be written in the non - IEEE floating point case
#error to handle the following:
#error 	a) idx_sql_date, idx_sql_time, idx_timestamp2 b) idx_numeric2

#endif /* IEEE */

	// Test the sign of the double precision number.  Just to be sure, don't
	// rely on the byte comparison being signed.  If the number is negative,
	// complement the whole thing.  Otherwise just zap the sign bit.
	if (temp_is_negative) {
		((SSHORT *) key->key_data)[0] = -((SSHORT *) key->key_data)[0] - 1;
		((SSHORT *) key->key_data)[1] = -((SSHORT *) key->key_data)[1] - 1;
		((SSHORT *) key->key_data)[2] = -((SSHORT *) key->key_data)[2] - 1;
		((SSHORT *) key->key_data)[3] = -((SSHORT *) key->key_data)[3] - 1;
	}
	else {
		key->key_data[0] ^= 1 << 7;
	}

	// Complement the s_part for an int64 key.
	// If we just flip the sign bit, which is equivalent to adding 32768, the
	// short part will unsigned-compare correctly.
	if (int64_key_op) {
		key->key_data[8] ^= 1 << 7;
	}

	// Finally, chop off trailing binary zeros
	for (p = &key->key_data[(!int64_key_op) ?
		temp_copy_length - 1 : INT64_KEY_LENGTH - 1];
		p > key->key_data; --p) 
	{
		if (*p) {
			break;
		}
	}

	key->key_length = (p - key->key_data) + 1;
#ifdef DEBUG_INDEXKEY
	{
		USHORT i;
		ib_fprintf(ib_stderr, "KEY: length: %d Bytes: ", key->key_length);
		for (i = 0; i < key->key_length; i++)
			ib_fprintf(ib_stderr, "%02x ", key->key_data[i]);
		ib_fprintf(ib_stderr, "\n");
	}
#endif
}


static USHORT compress_root(TDBB tdbb, index_root_page* page)
{
/**************************************
 *
 *	c o m p r e s s _ r o o t
 *
 **************************************
 *
 * Functional description
 *	Compress an index root page.
 *
 **************************************/
	SET_TDBB(tdbb);
	DBB dbb = tdbb->tdbb_database;
	CHECK_DBB(dbb);

	UCHAR* const temp =
		(UCHAR*)tdbb->tdbb_default->allocate((SLONG) dbb->dbb_page_size, 0
#ifdef DEBUG_GDS_ALLOC
	  ,__FILE__,__LINE__
#endif
	);
	memcpy(temp, page, dbb->dbb_page_size);
	UCHAR* p = temp + dbb->dbb_page_size;

	index_root_page::irt_repeat* root_idx = page->irt_rpt;
	for (const index_root_page::irt_repeat* const end = root_idx + page->irt_count;
		 root_idx < end; root_idx++)
	{
		if (root_idx->irt_root) {
			USHORT len;
			if (dbb->dbb_ods_version < ODS_VERSION11)
				len = root_idx->irt_keys * sizeof(irtd_ods10);
			else
				len = root_idx->irt_keys * sizeof(irtd);

			p -= len;
			memcpy(p, (SCHAR*)page + root_idx->irt_desc, len);
			root_idx->irt_desc = p - temp;
		}
	}
	const USHORT l = p - temp;
	tdbb->tdbb_default->deallocate(temp);

	return l;
}


static void copy_key(const KEY* in, KEY* out)
{
/**************************************
 *
 *	c o p y _ k e y
 *
 **************************************
 *
 * Functional description
 *	Copy a key.
 *
 **************************************/

	out->key_length = in->key_length;
	memcpy(out->key_data, in->key_data, in->key_length);
}


static CONTENTS delete_node(TDBB tdbb, WIN *window, UCHAR *pointer)
{
/**************************************
 *
 *	d e l e t e _ n o d e
 *
 **************************************
 *
 * Functional description
 *	Delete a node from a page and return whether it 
 *	empty, if there is a single node on it, or if it 
 * 	is above or below the threshold for garbage collection. 
 *
 **************************************/

	SET_TDBB(tdbb);
	DBB dbb = tdbb->tdbb_database;
	CHECK_DBB(dbb);

	btree_page* page = (btree_page*) window->win_buffer;

	CCH_MARK(tdbb, window);
	//USHORT nodeOffset = pointer - (UCHAR*)page;
	//USHORT delta = BTR_delete_node(tdbb, page, nodeOffset);

	SCHAR flags = page->btr_header.pag_flags;
	bool leafPage = (page->btr_level == 0);
	bool useJumpInfo = (flags & btr_jump_info);
	SLONG nodeOffset = pointer - (UCHAR*)page;

	// Read node that need to be removed
	IndexNode removingNode;
	UCHAR* localPointer = BTreeNode::readNode(&removingNode, pointer, flags, leafPage);
	USHORT offsetDeletePoint = (pointer - (UCHAR*)page);

	// Read the next node after the removing node
	IndexNode nextNode;
	localPointer = BTreeNode::readNode(&nextNode, localPointer, flags, leafPage);
	USHORT offsetNextPoint = (localPointer - (UCHAR*)page);

	// Save data in tempKey so we can rebuild from it
	USHORT newNextPrefix = nextNode.prefix;
	USHORT newNextLength = 0;
	USHORT length = MAX(removingNode.length + removingNode.prefix, 
		nextNode.length + nextNode.prefix);
	UCHAR* tempData = FB_NEW(*tdbb->tdbb_default) UCHAR[length];
	length = 0;
	if (nextNode.prefix > removingNode.prefix) {
		// The next node uses data from the node that is going to
		// be removed so save it.
		length = nextNode.prefix - removingNode.prefix;
		newNextPrefix -= length;
		newNextLength += length;
		memcpy(tempData, removingNode.data, length);
	}
	memcpy(tempData + length, nextNode.data, nextNode.length);
	newNextLength += nextNode.length;

	// Update the page prefix total.
	page->btr_prefix_total -= (removingNode.prefix + (nextNode.prefix - newNextPrefix));

	// Update the next node so we are ready to save it.	
	nextNode.prefix = newNextPrefix;
	nextNode.length = newNextLength;
	nextNode.data = tempData;
	pointer = BTreeNode::writeNode(&nextNode, pointer, flags, leafPage);
	delete tempData;

	// Compute length of rest of bucket and move it down.
	length = page->btr_length - (localPointer - (UCHAR*) page);
	if (length) {
		// Could be overlapping buffers. 
		// Use MEMMOVE macro which is memmove() in most platforms, instead 
		// of MOVE_FAST which is memcpy() in most platforms. 
		// memmove() is guaranteed to work non-destructivly on overlapping buffers. 
		memmove(pointer, localPointer, length);
		pointer += length;
		localPointer += length;
	}

	// Set page size and get delta
	USHORT delta = page->btr_length;
	page->btr_length = pointer - (UCHAR*) page;
	delta -= page->btr_length;

	if (useJumpInfo) {
		// We use a fast approach here.
		// Only update offsets pointing after the deleted node and 
		// remove jump nodes pointing to the deleted node or node
		// next to the deleted one.
		jumpNodeList* jumpNodes = FB_NEW(*tdbb->tdbb_default) 
			jumpNodeList(tdbb->tdbb_default);

		IndexJumpInfo jumpInfo;
		pointer = BTreeNode::getPointerFirstNode(page, &jumpInfo);

		bool rebuild = false;
		USHORT n = jumpInfo.jumpers;
		IndexJumpNode jumpNode, delJumpNode;
		while (n) {
			pointer = BTreeNode::readJumpNode(&jumpNode, pointer, flags);
			// Jump nodes pointing to the deleted node are removed.
			if ((jumpNode.offset < offsetDeletePoint) || 
				(jumpNode.offset > offsetNextPoint)) 
			{
				IndexJumpNode newJumpNode;
				if (rebuild && jumpNode.prefix > delJumpNode.prefix) {
					// This node has prefix against a removing jump node
					USHORT addLength = jumpNode.prefix - delJumpNode.prefix;
					newJumpNode.prefix = jumpNode.prefix - addLength;
					newJumpNode.length = jumpNode.length + addLength;
					newJumpNode.offset = jumpNode.offset;
					if (jumpNode.offset > offsetDeletePoint) {
						newJumpNode.offset -= delta;
					}
					newJumpNode.data = FB_NEW(*tdbb->tdbb_default) UCHAR[newJumpNode.length];
					memcpy(newJumpNode.data, delJumpNode.data, addLength);					
					memcpy(newJumpNode.data + addLength, jumpNode.data, jumpNode.length);
				}
				else {
					newJumpNode.prefix = jumpNode.prefix;
					newJumpNode.length = jumpNode.length;
					newJumpNode.offset = jumpNode.offset;
					if (jumpNode.offset > offsetDeletePoint) {
						newJumpNode.offset -= delta;
					}
					newJumpNode.data = FB_NEW(*tdbb->tdbb_default) UCHAR[newJumpNode.length];
					memcpy(newJumpNode.data, jumpNode.data, newJumpNode.length);
				}
				jumpNodes->add(newJumpNode);
				rebuild = false;
			}
			else {
				delJumpNode = jumpNode;
				rebuild = true;
			}
			n--;
		}

		// Update jump information.
		jumpInfo.jumpers = jumpNodes->getCount();
		pointer = BTreeNode::writeJumpInfo(page, &jumpInfo);
		// Write jump nodes.
		IndexJumpNode* walkJumpNode = jumpNodes->begin();
		for (int i = 0; i < jumpNodes->getCount(); i++) {
			pointer = BTreeNode::writeJumpNode(&walkJumpNode[i], pointer, flags);
			if (walkJumpNode[i].data) {
				delete walkJumpNode[i].data;
			}
		}
		jumpNodes->clear();
		delete jumpNodes;
	}

	// check to see if the page is now empty
	IndexNode node;
	pointer = BTreeNode::getPointerFirstNode(page);
	//bool leafPage = (page->btr_level == 0);
	//SCHAR flags = page->btr_header.pag_flags;
	pointer = BTreeNode::readNode(&node, pointer, flags, leafPage);
	if (BTreeNode::isEndBucket(&node, leafPage) ||
		BTreeNode::isEndLevel(&node, leafPage)) 
	{
		return contents_empty;
	}

	// check to see if there is just one node
	pointer = BTreeNode::readNode(&node, pointer, flags, leafPage);
	if (BTreeNode::isEndBucket(&node, leafPage) ||
		BTreeNode::isEndLevel(&node, leafPage)) 
	{
		return contents_single;
	}

	// check to see if the size of the page is below the garbage collection threshold, 
	// meaning below the size at which it should be merged with its left sibling if possible.
	if (page->btr_length < GARBAGE_COLLECTION_BELOW_THRESHOLD) {
		return contents_below_threshold;
	}

	return contents_above_threshold;
}


static void delete_tree(TDBB tdbb,
						USHORT rel_id, USHORT idx_id, SLONG next, SLONG prior)
{
/**************************************
 *
 *	d e l e t e _ t r e e
 *
 **************************************
 *
 * Functional description
 *	Release index pages back to free list.
 *
 **************************************/

	SET_TDBB(tdbb);
	WIN window(-1);
	window.win_flags = WIN_large_scan;
	window.win_scans = 1;

	SLONG down = next;
	// Delete the index tree from the top down.
	while (next) {
		window.win_page = next;
		btree_page* page = (btree_page*) CCH_FETCH(tdbb, &window, LCK_write, 0);

		// do a little defensive programming--if any of these conditions 
		// are true we have a damaged pointer, so just stop deleting. At
		// the same time, allow updates of indexes with id > 255 even though
		// the page header uses a byte for its index id.  This requires relaxing
		// the check slightly introducing a risk that we'll pick up a page belonging
		// to some other index that is ours +/- (256*n).  On the whole, unlikely.
		if (page->btr_header.pag_type != pag_index ||
			page->btr_id != (UCHAR)(idx_id % 256) || page->btr_relation != rel_id) {
			CCH_RELEASE(tdbb, &window);
			return;
		}

		// if we are at the beginning of a non-leaf level, position 
		// "down" to the beginning of the next level down
		if (next == down) {
			if (page->btr_level) {
				UCHAR *pointer = BTreeNode::getPointerFirstNode(page);
				IndexNode pageNode;
				BTreeNode::readNode(&pageNode, pointer, 
					page->btr_header.pag_flags, false);
				down = pageNode.pageNumber;
			}
			else {
				down = 0;
			}
		}

		// go through all the sibling pages on this level and release them 
		next = page->btr_sibling;
		CCH_RELEASE_TAIL(tdbb, &window);
		PAG_release_page(window.win_page, prior);
		prior = window.win_page;

		// if we are at end of level, go down to the next level
		if (!next) {
			next = down;
		}
	}
}


static DSC *eval(TDBB tdbb, jrd_nod* node, DSC * temp, bool *isNull)
{
/**************************************
 *
 *	e v a l
 *
 **************************************
 *
 * Functional description
 *	Evaluate an expression returning a descriptor, and
 *	a flag to indicate a null value.  
 *
 **************************************/
	SET_TDBB(tdbb);

	dsc* desc = EVL_expr(tdbb, node);
	*isNull = false;

	if (desc && !(tdbb->tdbb_request->req_flags & req_null)) {
		return desc;
	}
	else {
		*isNull = true;
	}

	temp->dsc_dtype = dtype_text;
	temp->dsc_flags = 0;
	temp->dsc_sub_type = 0;
	temp->dsc_scale = 0;
	temp->dsc_length = 1;
	temp->dsc_ttype = ttype_ascii;
	temp->dsc_address = (UCHAR*) " ";

	return temp;
}


static SLONG fast_load(TDBB tdbb,
					   jrd_rel* relation,
					   IDX * idx,
					   USHORT key_length,
					   SCB sort_handle,
					   SelectivityList& selectivity)
{
/**************************************
 *
 *	f a s t _ l o a d
 *
 **************************************
 *
 * Functional description
 *	Do a fast load.  The indices have already been passed into sort, and
 *	are ripe for the plucking.  This beast is complicated, but, I hope,
 *	comprehendable.
 *
 **************************************/
	
 	KEY keys[MAX_LEVELS];
	btree_page* buckets[MAX_LEVELS];
	win_for_array windows[MAX_LEVELS];
	ULONG split_pages[MAX_LEVELS];
	SLONG split_record_numbers[MAX_LEVELS];
	UCHAR* pointers[MAX_LEVELS];
	UCHAR* newAreaPointers[MAX_LEVELS];
	USHORT totalJumpSize[MAX_LEVELS];

	SET_TDBB(tdbb);
	DBB dbb = tdbb->tdbb_database;
	CHECK_DBB(dbb);

	// leaf-page and pointer-page size limits, we always need to 
	// leave room for the END_LEVEL node.
	const USHORT lp_fill_limit = dbb->dbb_page_size - BTN_LEAF_SIZE;
	const USHORT pp_fill_limit = dbb->dbb_page_size - BTN_PAGE_SIZE;
	USHORT flags = 0;
	if (idx->idx_flags & idx_descending) {
		flags |= btr_descending;
	}
	if (dbb->dbb_ods_version >= ODS_VERSION11) {
		flags |= btr_all_record_number;
	}
	if (key_length > 255) {
		flags |= btr_large_keys;
	}

	// Jump information initialization
	bool useJumpInfo = (dbb->dbb_ods_version >= ODS_VERSION11);

	typedef Firebird::vector<jumpNodeList*> jumpNodeListContainer;
	jumpNodeListContainer* jumpNodes = FB_NEW(*tdbb->tdbb_default) 
		jumpNodeListContainer(*tdbb->tdbb_default);
	jumpNodes->push_back(FB_NEW(*tdbb->tdbb_default) jumpNodeList(tdbb->tdbb_default));

	keyList* jumpKeys = FB_NEW(*tdbb->tdbb_default) keyList(*tdbb->tdbb_default);
	jumpKeys->push_back(FB_NEW(*tdbb->tdbb_default) dynKey);
	(*jumpKeys)[0]->keyData = FB_NEW(*tdbb->tdbb_default) UCHAR[key_length];

	IndexJumpInfo jumpInfo;
	jumpInfo.jumpAreaSize = 0;
	jumpInfo.jumpers = 0;
	jumpInfo.keyLength = key_length;

	if (useJumpInfo) {
		// AB: Let's try to determine to size between the jumps to speed up
		// index search. Of course the size depends on the key_length. The
		// bigger the key, the less jumps we can make. (Although we must
		// not forget that mostly the keys are compressed and much smaller
		// than the maximum possible key!).
		// These values can easily change without effect on previous created
		// indices, cause this value is stored on each page.
		// Remember, the lower the value how more jumpkeys are generated and
		// how faster jumpkeys are recalculated on insert.
		if (key_length <= 8) {
		  jumpInfo.jumpAreaSize = 384;
		}
		else if (key_length <= 64) {
		  jumpInfo.jumpAreaSize = 512;
		}
		else if (key_length <= 128) {
		  jumpInfo.jumpAreaSize = 768;
		}
		else if (key_length <= 255) {
		  jumpInfo.jumpAreaSize = 1024;
		}
		else {
		  jumpInfo.jumpAreaSize = 2048;
		}

		// If our half page_size is smaller as the jump_size then jump_size isn't
		// needfull at all.
		if ((dbb->dbb_page_size / 2) < jumpInfo.jumpAreaSize) {
			jumpInfo.jumpAreaSize = 0;
		}
		useJumpInfo = (jumpInfo.jumpAreaSize > 0);
		if (useJumpInfo) {
			flags |= btr_jump_info;
		}
	}

	// Allocate and format the first leaf level bucket.  Awkwardly,
	// the bucket header has room for only a byte of index id and that's
	// part of the ODS.  So, for now, we'll just record the first byte
	// of the id and hope for the best.  Index buckets are (almost) always
	// located through the index structure (dmp being an exception used 
	// only for debug) so the id is actually redundant.
	btree_page* bucket = (btree_page*) DPM_allocate(tdbb, &windows[0]);
	bucket->btr_header.pag_type = pag_index;
	bucket->btr_relation = relation->rel_id;
	bucket->btr_id = (UCHAR)(idx->idx_id % 256);
	bucket->btr_level = 0;
	bucket->btr_length = BTR_SIZE;
	bucket->btr_header.pag_flags |= flags;

	UCHAR* pointer;
	if (useJumpInfo) {
		pointer = BTreeNode::writeJumpInfo(bucket, &jumpInfo);
		jumpInfo.firstNodeOffset = (USHORT)(pointer - (UCHAR*)bucket);
		jumpInfo.jumpers = 0;
		pointer = BTreeNode::writeJumpInfo(bucket, &jumpInfo);
		bucket->btr_length = jumpInfo.firstNodeOffset;
		newAreaPointers[0] = pointer + jumpInfo.firstNodeOffset;
	}
	else {
		pointer = BTreeNode::getPointerFirstNode(bucket);
	}

	tdbb->tdbb_flags |= TDBB_no_cache_unwind;

	buckets[0] = bucket;
	buckets[1] = NULL;
	keys[0].key_length = 0;

	WIN* window = 0;
	bool error = false;
	ULONG count = 0;
	ULONG duplicates = 0;
	const USHORT segments = idx->idx_count;
	SSHORT segment, stuff_count, pos, i;
	Firebird::HalfStaticArray<ULONG, 4> duplicatesList(tdbb->tdbb_default);
	duplicatesList.grow(segments);
	memset(duplicatesList.begin(), 0, segments * sizeof(ULONG));

	try {

		// If there's an error during index construction, fall
		// thru to release the last index bucket at each level
		// of the index. This will prepare for a single attempt
		// to deallocate the index pages for reuse.
		IndexNode newNode;
		IndexNode previousNode;
		// pointer holds the "main" pointer for inserting new nodes.

		ISR isr;
		win_for_array split_window;
		KEY split_key, temp_key;
		key* key;
		dynKey* jumpKey = (*jumpKeys)[0];
		jumpNodeList* leafJumpNodes = (*jumpNodes)[0];
		bool duplicate = false;
		USHORT level, prefix;
		UCHAR* record;
		totalJumpSize[0] = 0;
		USHORT headerSize = (pointer - (UCHAR*)bucket);

		UCHAR* levelPointer;
		IndexNode levelNode;
		IndexNode tempNode;
		jumpKey->keyLength = 0;

		while (!error) {
			// Get the next record in sorted order.

			SORT_get(tdbb->tdbb_status_vector, sort_handle,
				  (ULONG **) & record // TMN: cast
#ifdef SCROLLABLE_CURSORS
				 , RSE_get_forward
#endif
			);

			if (!record) {
				break;
			}
			isr = (ISR) (record + key_length);
			count++;
			
			// restore previous values
			bucket = buckets[0];
			split_pages[0] = 0;
			key = &keys[0];

			// Compute the prefix as the length in common with the previous record's key.
			prefix = BTreeNode::computePrefix(key->key_data, key->key_length,
				record, isr->isr_key_length);

			// set node values
			newNode.prefix = prefix;
			newNode.length = isr->isr_key_length - prefix;
			newNode.recordNumber = isr->isr_record_number;
			newNode.data = record + prefix;

			// If the length of the new node will cause us to overflow the bucket, 
			// form a new bucket.
			if (bucket->btr_length + totalJumpSize[0] +
				BTreeNode::getNodeSize(&newNode, flags) > lp_fill_limit) 
			{
				// mark the end of the previous page
				SLONG lastRecordNumber = previousNode.recordNumber;
				previousNode.recordNumber = END_BUCKET;
				BTreeNode::writeNode(&previousNode, previousNode.nodePointer, flags, true, false);

				if (useJumpInfo && totalJumpSize[0]) {
					// Slide down current nodes;
					// CVC: Warning, this may overlap. It seems better to use
					// memmove or to ensure manually that totalJumpSize[0] > l
					// Also, "sliding down" here is moving contents higher in memory.
					const USHORT l = bucket->btr_length - headerSize;
					UCHAR* p = (UCHAR*)bucket + headerSize;
					memmove(p + totalJumpSize[0], p, l);

					// Update JumpInfo
					jumpInfo.firstNodeOffset = headerSize + totalJumpSize[0];
					if (leafJumpNodes->getCount() > 255) {
						BUGCHECK(205);	// msg 205 index bucket overfilled
					}
					jumpInfo.jumpers = (UCHAR)leafJumpNodes->getCount();
					pointer = BTreeNode::writeJumpInfo(bucket, &jumpInfo);

					// Write jumpnodes on page.
					pointer = (UCHAR*)bucket + headerSize;
					IndexJumpNode* walkJumpNode = leafJumpNodes->begin();
					for (int i = 0; i < leafJumpNodes->getCount(); i++) {
						// Update offset position first.
						walkJumpNode[i].offset += totalJumpSize[0];
						pointer = BTreeNode::writeJumpNode(&walkJumpNode[i], pointer, flags);
					}
					bucket->btr_length += totalJumpSize[0];
				}

				// Allocate new bucket.
				btree_page* split = (btree_page*) DPM_allocate(tdbb, &split_window);
				bucket->btr_sibling = split_window.win_page;
				split->btr_left_sibling = windows[0].win_page;
				split->btr_header.pag_type = pag_index;
				split->btr_relation = bucket->btr_relation;
				split->btr_level = bucket->btr_level;
				split->btr_id = bucket->btr_id;
				split->btr_header.pag_flags |= flags;

				if (useJumpInfo) {
					pointer = BTreeNode::writeJumpInfo(split, &jumpInfo);
					jumpInfo.firstNodeOffset = (USHORT)(pointer - (UCHAR*)split);
					jumpInfo.jumpers = 0;
					pointer = BTreeNode::writeJumpInfo(split, &jumpInfo);
					// Reset position and size for generating jumpnode
					newAreaPointers[0] = pointer + jumpInfo.jumpAreaSize;
					totalJumpSize[0] = 0;
					jumpKey->keyLength = 0;
				}
				else {
					pointer = BTreeNode::getPointerFirstNode(split);
				}

				// store the first node on the split page
				IndexNode splitNode;
				splitNode.prefix = 0;
				splitNode.recordNumber = lastRecordNumber;
				splitNode.data = key->key_data;
				splitNode.length = key->key_length;
				pointer = BTreeNode::writeNode(&splitNode, pointer, flags, true);

				// save the page number of the previous page and release it
				split_pages[0] = windows[0].win_page;
				split_record_numbers[0] = splitNode.recordNumber;
				CCH_RELEASE(tdbb, &windows[0]);

				// set up the new page as the "current" page
				windows[0] = split_window;
				buckets[0] = bucket = split;

				// save the first key on page as the page to be propogated
				copy_key(key, &split_key);

				if (useJumpInfo) {
					// Clear jumplist.
					IndexJumpNode* walkJumpNode = leafJumpNodes->begin();
					for (int i = 0; i < leafJumpNodes->getCount(); i++) {
						if (walkJumpNode[i].data) {
							delete walkJumpNode[i].data;
						}
					}
					leafJumpNodes->clear();
				}

			}

			// Insert the new node in the current bucket
			bucket->btr_prefix_total += prefix;
			pointer = BTreeNode::writeNode(&newNode, pointer, flags, true);
			previousNode = newNode;

			// if we have a compound-index calculate duplicates per segment.
			if (segments > 1 && count > 1) 
			{
				// Initialize variables for segment duplicate check.
				// count holds the current checking segment (starting by
				// the maximum segment number to 1).
				const UCHAR* p1 = key->key_data;
				const UCHAR* const p1_end = p1 + key->key_length;
				const UCHAR* p2 = newNode.data;
				const UCHAR* const p2_end = p2 + newNode.length;
				if (newNode.prefix == 0) {
					segment = *p2;
					pos = 0;
					stuff_count = 0;
				}
				else {
					pos = newNode.prefix;
					// find the segment number were we're starting.
					i = (pos / (STUFF_COUNT + 1)) * (STUFF_COUNT + 1);
					if (i == pos) {
						// We _should_ pick number from data if available
						segment = *p2;
					}
					else {
						segment = *(p1 + i);
					}
					// update stuff_count to the current position.
					stuff_count = STUFF_COUNT + 1 - (pos - i);
					p1 += pos;
				}

				//Look for duplicates in the segments
				while ((p1 < p1_end) && (p2 < p2_end)) {
					if (stuff_count == 0) {
						if (*p1 != *p2) {
							// We're done
							break;
						}
						segment = *p2;
						p1++;
						p2++;
						stuff_count = STUFF_COUNT;
					}
					if (*p1 != *p2) {
						//We're done
						break;
					}
					p1++;
					p2++;
					stuff_count--;
				}

				if ((p1 == p1_end) && (p2 == p2_end)) {
					segment = 0; // All segments are duplicates
				}
				for (i = segment + 1; i <= segments; i++) {
					duplicatesList[segments - i]++;
				}

			}

			// check if this is a duplicate node
			duplicate = (!newNode.length && prefix == key->key_length);
			if (duplicate && (count > 1)) {
				++duplicates;
			}

			// Update the length of the page.
			bucket->btr_length = pointer - (UCHAR*) bucket;
			if (bucket->btr_length > dbb->dbb_page_size) {
				BUGCHECK(205);		// msg 205 index bucket overfilled
			}

			// Remember the last key inserted to compress the next one.
			key->key_length = isr->isr_key_length;
			memcpy(key->key_data, record, key->key_length);

			if (useJumpInfo && (newAreaPointers[0] < pointer) &&
				(bucket->btr_length + totalJumpSize[0] + newNode.prefix + 6 < lp_fill_limit)) 
			{
				// Create a jumpnode
				IndexJumpNode jumpNode;
				jumpNode.prefix = BTreeNode::computePrefix(jumpKey->keyData,
					jumpKey->keyLength, key->key_data, newNode.prefix);
				jumpNode.length = newNode.prefix - jumpNode.prefix;
				jumpNode.offset = (newNode.nodePointer - (UCHAR*)bucket);
				jumpNode.data = FB_NEW(*tdbb->tdbb_default) UCHAR[jumpNode.length];
				memcpy(jumpNode.data, key->key_data + jumpNode.prefix, jumpNode.length);
				// Push node on end in list
				leafJumpNodes->add(jumpNode);
				// Store new data in jumpKey, so a new jump node can calculate prefix
				memcpy(jumpKey->keyData + jumpNode.prefix, jumpNode.data, jumpNode.length);
				jumpKey->keyLength = jumpNode.length + jumpNode.prefix;
				// Set new position for generating jumpnode
				newAreaPointers[0] += jumpInfo.jumpAreaSize;
				totalJumpSize[0] += BTreeNode::getJumpNodeSize(&jumpNode, flags);
			}

			// If there wasn't a split, we're done.  If there was, propagate the
			// split upward
			for (level = 1; split_pages[level - 1]; level++) {
				// initialize the current pointers for this level
				window = &windows[level];
				key = &keys[level];
				split_pages[level] = 0;
				levelPointer = pointers[level];

				// If there isn't already a bucket at this level, make one.  Remember to 
				// shorten the index id to a byte
				if (!(bucket = buckets[level])) {
					buckets[level + 1] = NULL;
					buckets[level] = bucket = (btree_page*) DPM_allocate(tdbb, window);
					bucket->btr_header.pag_type = pag_index;
					bucket->btr_relation = relation->rel_id;
					bucket->btr_id = (UCHAR)(idx->idx_id % 256);
					fb_assert(level <= MAX_UCHAR);
					bucket->btr_level = (UCHAR) level;
					bucket->btr_header.pag_flags |= flags;

					// since this is the beginning of the level, we propagate the lower-level
					// page with a "degenerate" zero-length node indicating that this page holds 
					// any key value less than the next node

					if (useJumpInfo) {
						levelPointer = BTreeNode::writeJumpInfo(bucket, &jumpInfo);
						jumpInfo.firstNodeOffset = (USHORT)(levelPointer - (UCHAR*)bucket);
						jumpInfo.jumpers = 0;
						levelPointer = BTreeNode::writeJumpInfo(bucket, &jumpInfo);
					}
					else {
						levelPointer = BTreeNode::getPointerFirstNode(bucket);
					}

					levelNode.prefix = 0;
					levelNode.length = 0;
					levelNode.pageNumber = split_pages[level - 1];
					levelNode.recordNumber = 0; // First record-number of level must be zero
					levelPointer = BTreeNode::writeNode(&levelNode, levelPointer, flags, false);
					bucket->btr_length = levelPointer - (UCHAR*) bucket;
					key->key_length = 0;

					// Initialize jumpNodes variables for new level
					jumpNodes->push_back(FB_NEW(*tdbb->tdbb_default) 
						jumpNodeList(tdbb->tdbb_default));
					jumpKeys->push_back(FB_NEW(*tdbb->tdbb_default) dynKey);
					(*jumpKeys)[level]->keyLength = 0;
					(*jumpKeys)[level]->keyData = 
						FB_NEW(*tdbb->tdbb_default) UCHAR[key_length];
					totalJumpSize[level] = 0;
					newAreaPointers[level] = levelPointer + jumpInfo.jumpAreaSize;
				}

				dynKey* pageJumpKey = (*jumpKeys)[level];
				jumpNodeList* pageJumpNodes = (*jumpNodes)[level];

				// Compute the prefix in preparation of insertion
				prefix = BTreeNode::computePrefix(key->key_data, key->key_length,
					split_key.key_data, split_key.key_length);

				// Remember the last key inserted to compress the next one.
				copy_key(&split_key, &temp_key);

				// Save current node if we need to split.
				tempNode = levelNode;
				// Set new node values.
				levelNode.prefix = prefix;
				levelNode.length = temp_key.key_length - prefix;
				levelNode.data = temp_key.key_data + prefix;
				levelNode.pageNumber = windows[level - 1].win_page;
				levelNode.recordNumber = split_record_numbers[level - 1];

				// See if the new node fits in the current bucket.  
				// If not, split the bucket.
				if (bucket->btr_length + totalJumpSize[level] +
					BTreeNode::getNodeSize(&levelNode, flags, false) > pp_fill_limit) 
				{
					// mark the end of the page; note that the end_bucket marker must 
					// contain info about the first node on the next page
					SLONG lastPageNumber = tempNode.pageNumber;
					BTreeNode::setEndBucket(&tempNode, false);
					BTreeNode::writeNode(&tempNode, tempNode.nodePointer, flags, false, false);

					if (useJumpInfo && totalJumpSize[level]) {
						// Slide down current nodes;
						// CVC: Warning, this may overlap. It seems better to use
						// memmove or to ensure manually that totalJumpSize[0] > l
						// Also, "sliding down" here is moving contents higher in memory.
						const USHORT l = bucket->btr_length - headerSize;
						UCHAR* p = (UCHAR*)bucket + headerSize;
						memmove(p + totalJumpSize[level], p, l);

						// Update JumpInfo
						jumpInfo.firstNodeOffset = headerSize + totalJumpSize[level];
						if (pageJumpNodes->getCount() > 255) {
							BUGCHECK(205);	// msg 205 index bucket overfilled
						}
						jumpInfo.jumpers = (UCHAR)pageJumpNodes->getCount();
						levelPointer = BTreeNode::writeJumpInfo(bucket, &jumpInfo);

						// Write jumpnodes on page.
						levelPointer = (UCHAR*)bucket + headerSize;
						IndexJumpNode* walkJumpNode = pageJumpNodes->begin();
						for (int i = 0; i < pageJumpNodes->getCount(); i++) {
							// Update offset position first.
							walkJumpNode[i].offset += totalJumpSize[level];
							levelPointer = BTreeNode::writeJumpNode(&walkJumpNode[i], 
								levelPointer, flags);
						}
						bucket->btr_length += totalJumpSize[level];
					}

					btree_page* split = (btree_page*) DPM_allocate(tdbb, &split_window);
					bucket->btr_sibling = split_window.win_page;
					split->btr_left_sibling = window->win_page;
					split->btr_header.pag_type = pag_index;
					split->btr_relation = bucket->btr_relation;
					split->btr_level = bucket->btr_level;
					split->btr_id = bucket->btr_id;
					split->btr_header.pag_flags |= flags;

					if (useJumpInfo) {
						levelPointer = BTreeNode::writeJumpInfo(split, &jumpInfo);
						jumpInfo.firstNodeOffset = (USHORT)(levelPointer - (UCHAR*)split);
						jumpInfo.jumpers = 0;
						levelPointer = BTreeNode::writeJumpInfo(split, &jumpInfo);
						// Reset position and size for generating jumpnode
						newAreaPointers[level] = levelPointer + jumpInfo.jumpAreaSize;
						totalJumpSize[level] = 0;
						pageJumpKey->keyLength = 0;
					}
					else {
						levelPointer = BTreeNode::getPointerFirstNode(split);
					}

					// insert the new node in the new bucket
					IndexNode splitNode;
					splitNode.prefix = 0;
					splitNode.length = key->key_length;
					splitNode.pageNumber = lastPageNumber;
					splitNode.recordNumber = tempNode.recordNumber;
					splitNode.data = key->key_data;
					levelPointer = BTreeNode::writeNode(&splitNode, levelPointer, flags, false);

					// indicate to propagate the page we just split from
					split_pages[level] = window->win_page;
					split_record_numbers[level] = splitNode.recordNumber;
					CCH_RELEASE(tdbb, window);

					// and make the new page the current page
					*window = split_window;
					buckets[level] = bucket = split;
					copy_key(key, &split_key);

					if (useJumpInfo) {
						// Clear jumplist.
						IndexJumpNode* walkJumpNode = pageJumpNodes->begin();
						for (int i = 0; i < pageJumpNodes->getCount(); i++) {
							if (walkJumpNode[i].data) {
								delete walkJumpNode[i].data;
							}
						}
						pageJumpNodes->clear();
					}

				}

				// Now propagate up the lower-level bucket by storing a "pointer" to it.
				bucket->btr_prefix_total += prefix;
				levelPointer = BTreeNode::writeNode(&levelNode, levelPointer, flags, false);

				if (useJumpInfo && (newAreaPointers[level] < levelPointer) &&
					(bucket->btr_length + totalJumpSize[level] + 
					levelNode.prefix + 6 < pp_fill_limit)) 
				{
					// Create a jumpnode
					IndexJumpNode jumpNode;
					jumpNode.prefix = BTreeNode::computePrefix(pageJumpKey->keyData,
						pageJumpKey->keyLength, temp_key.key_data, levelNode.prefix);
					jumpNode.length = levelNode.prefix - jumpNode.prefix;
					jumpNode.offset = (levelNode.nodePointer - (UCHAR*)bucket);
					jumpNode.data = FB_NEW(*tdbb->tdbb_default) UCHAR[jumpNode.length];
					memcpy(jumpNode.data, temp_key.key_data + jumpNode.prefix, 
						jumpNode.length);
					// Push node on end in list
					pageJumpNodes->add(jumpNode);
					// Store new data in jumpKey, so a new jump node can calculate prefix
					memcpy(pageJumpKey->keyData + jumpNode.prefix, jumpNode.data, 
						jumpNode.length);
					pageJumpKey->keyLength = jumpNode.length + jumpNode.prefix;
					// Set new position for generating jumpnode
					newAreaPointers[level] += jumpInfo.jumpAreaSize;
					totalJumpSize[level] += BTreeNode::getJumpNodeSize(&jumpNode, flags);
				}

				// Now restore the current key value and save this node as the 
				// current node on this level; also calculate the new page length.
				copy_key(&temp_key, key);
				pointers[level] = levelPointer;
				bucket->btr_length = levelPointer - (UCHAR*) bucket;				
				if (bucket->btr_length > dbb->dbb_page_size) {
					BUGCHECK(205);	// msg 205 index bucket overfilled
				}
			}

#ifdef SUPERSERVER
			if (--tdbb->tdbb_quantum < 0 && !tdbb->tdbb_inhibit) {
				error = JRD_reschedule(tdbb, 0, false);
			}
#endif
		}

		// To finish up, put an end of level marker on the last bucket 
		// of each level.
		for (level = 0; (bucket = buckets[level]); level++) {
			// retain the top level window for returning to the calling routine
			bool leafPage = (bucket->btr_level == 0);
			window = &windows[level];

			// store the end of level marker
			pointer = (UCHAR*)bucket + bucket->btr_length;
			BTreeNode::setEndLevel(&levelNode, leafPage);
			pointer = BTreeNode::writeNode(&levelNode, pointer, flags, leafPage);

			// and update the final page length
			bucket->btr_length = pointer - (UCHAR*)bucket;

			// Store jump nodes on page if needed.
			jumpNodeList* pageJumpNodes = (*jumpNodes)[level];
			if (useJumpInfo && totalJumpSize[level]) {
				// Slide down current nodes;
				// CVC: Warning, this may overlap. It seems better to use
				// memmove or to ensure manually that totalJumpSize[0] > l
				// Also, "sliding down" here is moving contents higher in memory.
				const USHORT l = bucket->btr_length - headerSize;
				UCHAR* p = (UCHAR*)bucket + headerSize;
				memmove(p + totalJumpSize[level], p, l);

				// Update JumpInfo
				jumpInfo.firstNodeOffset = headerSize + totalJumpSize[level];
				if (pageJumpNodes->getCount() > 255) {
					BUGCHECK(205);	// msg 205 index bucket overfilled
				}
				jumpInfo.jumpers = (UCHAR)pageJumpNodes->getCount();
				pointer = BTreeNode::writeJumpInfo(bucket, &jumpInfo);

				// Write jumpnodes on page.
				IndexJumpNode* walkJumpNode = pageJumpNodes->begin();
				for (int i = 0; i < pageJumpNodes->getCount(); i++) {
					// Update offset position first.
					walkJumpNode[i].offset += totalJumpSize[level];
					pointer = BTreeNode::writeJumpNode(&walkJumpNode[i], pointer, flags);
				}

				bucket->btr_length += totalJumpSize[level];
			}

			if (bucket->btr_length > dbb->dbb_page_size) {
				BUGCHECK(205);	// msg 205 index bucket overfilled
			}

			CCH_RELEASE(tdbb, &windows[level]);
		}

		// Finally clean up dynamic memory used.
		for (jumpNodeListContainer::iterator itr = jumpNodes->begin(); 
			itr < jumpNodes->end(); ++itr) 
		{
			jumpNodeList* freeJumpNodes = *itr;
			IndexJumpNode* walkJumpNode = freeJumpNodes->begin();
			for (int i = 0; i < freeJumpNodes->getCount(); i++) {
				if (walkJumpNode[i].data) {
					delete walkJumpNode[i].data;
				}
			}
			freeJumpNodes->clear();
			delete freeJumpNodes;
		}
		delete jumpNodes;
		for (keyList::iterator itr3 = jumpKeys->begin(); 
			itr3 < jumpKeys->end(); ++itr3) 
		{
			delete (*itr3)->keyData;
			delete (*itr3);
		}
		delete jumpKeys;


	}	// try
	catch (const std::exception&) {
		error = true;
	}

	tdbb->tdbb_flags &= ~TDBB_no_cache_unwind;

	// do some final housekeeping
	SORT_fini(sort_handle, tdbb->tdbb_attachment);

	// If index flush fails, try to delete the index tree.
	// If the index delete fails, just go ahead and punt.
	try {

		if (error) {
			delete_tree(tdbb, relation->rel_id, idx->idx_id, window->win_page, 0);
			ERR_punt();
		}

		CCH_flush(tdbb, (USHORT) FLUSH_ALL, 0);

		// Calculate selectivity, also per segment when newer ODS
		selectivity.grow(segments);
		if (segments > 1) {
			for (i = 0; i < segments; i++) {
				selectivity[i] = 
					(float) ((count) ? 1.0 / (float) (count - duplicatesList[i]) : 0.0);
			}	
		}
		else {
			selectivity[0] = 
				(float) ((count) ? (1.0 / (float) (count - duplicates)) : 0.0);
		}

		return window->win_page;

	}	// try
	catch(const std::exception&) {
		if (!error) {
			error = true;
		}
		else {
			ERR_punt();
		}
	}
	return -1L; // lint
}


static index_root_page* fetch_root(TDBB tdbb, WIN * window, jrd_rel* relation)
{
/**************************************
 *
 *	f e t c h _ r o o t
 *
 **************************************
 *
 * Functional description
 *	Return descriptions of all indices for relation.  If there isn't
 *	a known index root, assume we were called during optimization
 *	and return no indices.
 *
 **************************************/
	SET_TDBB(tdbb);

	if ((window->win_page = relation->rel_index_root) == 0) {
		if (relation->rel_id == 0) {
			return NULL;
		}
		else {
			DPM_scan_pages(tdbb);
			window->win_page = relation->rel_index_root;
		}
	}

	return (index_root_page*) CCH_FETCH(tdbb, window, LCK_read, pag_root);
}


static UCHAR *find_node_start_point(btree_page* bucket, KEY * key, UCHAR * value,
						   USHORT * return_value, bool descending, 
						   bool retrieval, bool pointer_by_marker,
						   SLONG find_record_number)
{
/**************************************
 *
 *	f i n d _ n o d e _ s t a r t _ p o i n t 
 *
 **************************************
 *
 * Functional description
 *	Locate and return a pointer to the insertion point.
 *	If the key doesn't belong in this bucket, return NULL.
 *	A flag indicates the index is descending.
 *
 **************************************/

	const SCHAR flags = bucket->btr_header.pag_flags;
	USHORT prefix = 0;
	const UCHAR* const key_end = key->key_data + key->key_length;
	if (!(flags & btr_all_record_number)) {
		find_record_number = NO_VALUE;
	}
	bool firstPass = true;
	bool leafPage = (bucket->btr_level == 0);

	// Find point where we can start search.
	UCHAR* pointer;
	if (flags & btr_jump_info) {
		pointer = find_area_start_point(bucket, key, value,
			&prefix, descending, retrieval, find_record_number);
	}
	else {
		pointer = BTreeNode::getPointerFirstNode(bucket);
	}
	UCHAR* p = key->key_data + prefix;


	if (flags & btr_large_keys) {
		IndexNode node;
		pointer = BTreeNode::readNode(&node, pointer, flags, leafPage);

		// If this is an non-leaf bucket of a descending index, the dummy node on the
		// front will trip us up.  NOTE: This code may be apocryphal.  I don't see 
		// anywhere that a dummy node is stored for a descending index.  - deej
		//
		// AB: This node ("dummy" node) is inserted on every first page in a level.
		// Because it's length and prefix is 0 a descending index would see it 
		// always as the first matching node. 
		if (!leafPage && descending && 
			(node.nodePointer == BTreeNode::getPointerFirstNode(bucket)) && 
			(node.length == 0)) 
		{
			pointer = BTreeNode::readNode(&node, pointer, flags, leafPage);
		}

		while (true) {
			// Pick up data from node
			if (value && node.length) {
				UCHAR* r = value + node.prefix;
				memcpy(r, node.data, node.length);
			}

			// If the record number is -1, the node is the last in the level
			// and, by definition, is the insertion point.  Otherwise, if the
			// prefix of the current node is less than the running prefix, the 
			// node must have a value greater than the key, so it is the insertion
			// point.
			if (BTreeNode::isEndLevel(&node, leafPage) || node.prefix < prefix) {
				if (return_value) {
					*return_value = prefix;
				}
				return node.nodePointer;
			}

			// If the node prefix is greater than current prefix , it must be less 
			// than the key, so we can skip it.  If it has zero length, then
			// it is a duplicate, and can also be skipped.
			if (node.prefix == prefix) {
				const UCHAR* q = node.data;
				const UCHAR* const nodeEnd = q + node.length;
				if (descending) {
					while (true) {
						if (q == nodeEnd || (retrieval && p == key_end)) {
							goto done1;
						}
						else if (p == key_end || *p > *q) {
							break;
						}
						else if (*p++ < *q++) {
							goto done1;
						}
					}
				}
				else if (node.length > 0 || firstPass) {
					firstPass = false;
					while (true) {
						if (p == key_end) {
							goto done1;
						}
						else if (q == nodeEnd || *p > *q) {
							break;
						}
						else if (*p++ < *q++) {
							goto done1;
						}
					}
				}
				prefix = (USHORT)(p - key->key_data);
			}

			if (BTreeNode::isEndBucket(&node, leafPage)) {
				if (pointer_by_marker) {
					goto done1;
				}
				else {
					return NULL;
				}
			}
			pointer = BTreeNode::readNode(&node, pointer, flags, leafPage);
		}

	  done1:
		if (return_value) {
			*return_value = prefix;
		}
		return node.nodePointer;
	}
	else {
		// Uses fastest approach when possible. 
		register btn* node;
		SLONG number;

		node = (btn*)pointer;

		// If this is an non-leaf bucket of a descending index, the dummy node on the
		// front will trip us up.  NOTE: This code may be apocryphal.  I don't see 
		// anywhere that a dummy node is stored for a descending index.  - deej
		//
		// AB: This node ("dummy" node) is inserted on every first page in a level.
		// Because it's length and prefix is 0 a descending index would see it 
		// always as the first matching node. 
		if (!leafPage && descending && 
			(pointer == BTreeNode::getPointerFirstNode(bucket)) && 
			(node->btn_length == 0)) 
		{
			if (flags & btr_all_record_number) {
				node = NEXT_NODE_RECNR(node);
			}
			else {
				node = NEXT_NODE(node);
			}
		}

		while (true) {
			// Pick up data from node

			if (value && node->btn_length) {
				UCHAR* r = value + node->btn_prefix;
				memcpy(r, node->btn_data, node->btn_length);
			}

			// If the page/record number is -1, the node is the last in the level
			// and, by definition, is the insertion point.  Otherwise, if the
			// prefix of the current node is less than the running prefix, the 
			// node must have a value greater than the key, so it is the insertion
			// point. 
			number = get_long(node->btn_number);

			if (number == END_LEVEL || node->btn_prefix < prefix) {
				if (return_value) {
					*return_value = prefix;
				}
				return (UCHAR*)node;
			}

			// If the node prefix is greater than current prefix , it must be less 
			// than the key, so we can skip it.  If it has zero length, then
			// it is a duplicate, and can also be skipped.
			if (node->btn_prefix == prefix) {
				const UCHAR* q = node->btn_data;
				const UCHAR* const nodeEnd = q + node->btn_length;
				if (descending) {
					while (true) {
						if (q == nodeEnd || retrieval && p == key_end) {
							goto done2;
						}
						else if (p == key_end || *p > *q) {
							break;
						}
						else if (*p++ < *q++) {
							goto done2;
						}
					}
				}
				else if (node->btn_length > 0 || firstPass) {
					firstPass = false;
					while (true) {
						if (p == key_end) {
							goto done2;
						}
						else if (q == nodeEnd || *p > *q) {
							break;
						}
						else if (*p++ < *q++) {
							goto done2;
						}
					}
				}
				prefix = (USHORT)(p - key->key_data);
			}
			if (number == END_BUCKET) {
				if (pointer_by_marker) {
					goto done2;
				}
				else {
					return NULL;
				}
			}

			// Get next node
			if (!leafPage && (flags & btr_all_record_number)) {
				node = NEXT_NODE_RECNR(node);
			}
			else {
				node = NEXT_NODE(node);
			}
		}

	  done2:
		if (return_value) {
			*return_value = prefix;
		}
		return (UCHAR*)node;
	}  
}


static UCHAR* find_area_start_point(btree_page* bucket, const KEY* key, UCHAR * value,
									USHORT * return_prefix, bool descending,
									bool retrieval, SLONG find_record_number)
{
/**************************************
 *
 *	f i n d _ a r e a _ s t a r t _ p o i n t
 *
 **************************************
 *
 * Functional description
 *	Locate and return a pointer to a start area.
 *	The starting nodes for a area are 
 *  defined with jump nodes. A jump node
 *  contains the prefix information for
 *  a node at a specific offset.
 *
 **************************************/
	SCHAR flags = bucket->btr_header.pag_flags;
	UCHAR *pointer;
	USHORT prefix = 0;
	if (flags & btr_jump_info) {
		if (!(flags & btr_all_record_number)) {
			find_record_number = NO_VALUE;
		}
		bool useFindRecordNumber = (find_record_number != NO_VALUE);
		bool leafPage = (bucket->btr_level == 0);
		UCHAR *q, *nodeEnd;
		const UCHAR* keyPointer = key->key_data;
		const UCHAR* const keyEnd = keyPointer + key->key_length;
		IndexJumpInfo jumpInfo;
		IndexJumpNode jumpNode, prevJumpNode;
		IndexNode node;

		// Retrieve jump information.
		pointer = BTreeNode::getPointerFirstNode(bucket, &jumpInfo);
		USHORT n = jumpInfo.jumpers;
		KEY jumpKey; 

		// Set begin of page as default.
		prevJumpNode.offset = jumpInfo.firstNodeOffset; 
		prevJumpNode.prefix = 0;
		prevJumpNode.length = 0;
		jumpKey.key_length = 0;
		USHORT testPrefix = 0;
		bool done;
		while (n) {
			pointer = BTreeNode::readJumpNode(&jumpNode, pointer, flags);
			BTreeNode::readNode(&node, (UCHAR*)bucket + jumpNode.offset, flags, leafPage);

			// jumpKey will hold complete data off referenced node 
			q = jumpKey.key_data + jumpNode.prefix;
			memcpy(q, jumpNode.data, jumpNode.length);
			q = jumpKey.key_data + node.prefix;
			memcpy(q, node.data, node.length);
			jumpKey.key_length = node.prefix + node.length;

			keyPointer = key->key_data + jumpNode.prefix;
			q = jumpKey.key_data + jumpNode.prefix;
			nodeEnd = jumpKey.key_data + jumpKey.key_length;
			done = false;

			if ((jumpNode.prefix <= testPrefix) && descending) {
				while (true) {
					if (q == nodeEnd) {
						done = true;
						// Check if this is a exact match or a duplicate.
						// If the node is pointing to its end and the length is 
						// the same as the key then we have found a exact match.
						// Now start walking between the jump nodes until we
						// found a node reference that's not equal anymore 
						// or the record number is higher then the one we need.
						if (useFindRecordNumber && (keyPointer == keyEnd)) 
						{
							n--;
							while (n) {
								if (find_record_number < node.recordNumber) {
									// If the record number from leaf is higer
									// then we should be in our previous area.
									break;
								}
								// Calculate new prefix to return right prefix.
								prefix = jumpNode.length + jumpNode.prefix;

								prevJumpNode = jumpNode;
								pointer = BTreeNode::readJumpNode(&jumpNode, pointer, flags);
								BTreeNode::readNode(&node, (UCHAR*)bucket + 
									jumpNode.offset, flags, leafPage);

								if (node.length != 0 ||
									node.prefix != prevJumpNode.prefix + prevJumpNode.length ||
									jumpNode.prefix != prevJumpNode.prefix + prevJumpNode.length ||
									BTreeNode::isEndBucket(&node, leafPage) ||
									BTreeNode::isEndLevel(&node, leafPage))
								{
									break;
								}
								n--;
							}
						}
						break;
					}
					else if (retrieval && keyPointer == keyEnd) {
						done = true;
						break;
					}
					else if (keyPointer == keyEnd) {
						// End of key reached
						break;
					} 
					else if (*keyPointer > *q) {
						// Our key is bigger so check next node.
						break;
					}
					else if (*keyPointer++ < *q++) {
						done = true;
						break;
					}
				}
				testPrefix = (USHORT)(keyPointer - key->key_data);
			}
			else if (jumpNode.prefix <= testPrefix) {
				while (true) {
					if (keyPointer == keyEnd) {
						// Reached end of our key we're searching for.
						done = true;
						// Check if this is a exact match or a duplicate
						// If the node is pointing to its end and the length is 
						// the same as the key then we have found a exact match.
						// Now start walking between the jump nodes until we
						// found a node reference that's not equal anymore 
						// or the record number is higher then the one we need.
						if (useFindRecordNumber && q == nodeEnd) {
							n--;
							while (n) {
								if (find_record_number < node.recordNumber) {
									// If the record number from leaf is higer
									// then we should be in our previous area.
									break;
								}
								// Calculate new prefix to return right prefix.
								prefix = jumpNode.length + jumpNode.prefix;

								prevJumpNode = jumpNode;
								pointer = BTreeNode::readJumpNode(&jumpNode, pointer, flags);
								BTreeNode::readNode(&node, (UCHAR*)bucket + 
									jumpNode.offset, flags, leafPage);

								if (node.length != 0 ||
									node.prefix != prevJumpNode.prefix + prevJumpNode.length ||
									jumpNode.prefix != prevJumpNode.prefix + prevJumpNode.length ||
									BTreeNode::isEndBucket(&node, leafPage) ||
									BTreeNode::isEndLevel(&node, leafPage))
								{
									break;
								}
								n--;
							}
						}
						break;
					} 
					else if (q == nodeEnd) {
						// End of node data reached
						break;
					}
					else if (*keyPointer > *q) {
						// Our key is bigger so check next node.
						break;
					}
					else if (*keyPointer++ < *q++) {
						done = true;
						break;
					}
				}
				testPrefix = (USHORT)(keyPointer - key->key_data);
			}
			if (done) {
				// We're done, go out of main loop.
				break;
			}
			else {
				prefix = MIN(jumpNode.length + jumpNode.prefix, testPrefix);
				if (value && (jumpNode.length + jumpNode.prefix)) {
					// Copy prefix data from referenced node to value
					UCHAR *r = value;
					memcpy(r, jumpKey.key_data, jumpNode.length + jumpNode.prefix);
				}
				prevJumpNode = jumpNode;
			}
			n--;
		}

		// Set return pointer
		pointer = (UCHAR*)bucket + prevJumpNode.offset;
	}
	else {
		pointer = BTreeNode::getPointerFirstNode(bucket);
	}
	if (return_prefix) {
		*return_prefix = prefix;
	}
	return pointer;
}


static SLONG find_page(btree_page* bucket, const KEY* key, UCHAR idx_flags, SLONG find_record_number,
					   bool retrieval)
{
/**************************************
 *
 *	f i n d _ p a g e
 *
 **************************************
 *
 * Functional description
 *	Find a page number in an index level.  Return either the 
 *	node equal to the key or the last node less than the key.
 *	Note that this routine can be called only for non-leaf 
 *	pages, because it assumes the first node on page is 
 *	a degenerate, zero-length node.
 *
 **************************************/

	const SCHAR flags = bucket->btr_header.pag_flags;
	bool leafPage = (bucket->btr_level == 0);
	bool firstPass = true;
	bool descending = (idx_flags & idx_descending);
	bool allRecordNumber = (flags & btr_all_record_number);

	if (!allRecordNumber) {
		find_record_number = NO_VALUE;
	}

//	UCHAR* p;			// pointer on key
//	UCHAR* q;			// pointer on processing node
//	UCHAR* keyEnd;		// pointer on end of key
//	UCHAR* nodeEnd;		// pointer on end of processing node
	UCHAR* pointer;		// pointer where to start reading next node
	USHORT prefix = 0;	// last computed prefix against processed node

	pointer = find_area_start_point(bucket, key, 0, &prefix,
		descending, retrieval, find_record_number);

	if (flags & btr_large_keys) {
		IndexNode node;
		pointer = BTreeNode::readNode(&node, pointer, flags, leafPage);

		if (BTreeNode::isEndBucket(&node, leafPage) ||
			BTreeNode::isEndLevel(&node, leafPage))
		{
			pointer = BTreeNode::getPointerFirstNode(bucket);
			pointer = BTreeNode::readNode(&node, pointer, flags, leafPage);
		}

		if (BTreeNode::isEndLevel(&node, leafPage)) {
			BUGCHECK(206);	// msg 206 exceeded index level
		}

		SLONG previousNumber = node.pageNumber;
		if (node.nodePointer == BTreeNode::getPointerFirstNode(bucket)) {
			prefix = 0;
			// Handle degenerating node, always generated at first
			// page in a level. 
			if ((node.prefix == 0) && (node.length == 0)) {
				// Compute common prefix of key and first node
				previousNumber = node.pageNumber;
				pointer = BTreeNode::readNode(&node, pointer, flags, leafPage);
			}
		}		
		const UCHAR* p = key->key_data + prefix; // pointer on key
		const UCHAR* const keyEnd = key->key_data + key->key_length; // pointer on end of key

		while (true) {

			// If the page/record number is -1, the node is the last in the level
			// and, by definition, is the target node.  Otherwise, if the
			// prefix of the current node is less than the running prefix, its
			// node must have a value greater than the key, which is the fb_insertion
			// point.
			if (BTreeNode::isEndLevel(&node, leafPage) || node.prefix < prefix) {
				return previousNumber;
			}

			// If the node prefix is greater than current prefix , it must be less 
			// than the key, so we can skip it.  If it has zero length, then
			// it is a duplicate, and can also be skipped.
			const UCHAR* q = node.data; // pointer on processing node
			const UCHAR* const nodeEnd = q + node.length; // pointer on end of processing node
			if (node.prefix == prefix) {
				if (descending) {
					// Descending indexes
					while (true) {
						// Check for exact match and if we need to do
						// record number matching.
						if (q == nodeEnd || p == keyEnd) {
							if (find_record_number != NO_VALUE && 
								q == nodeEnd && p == keyEnd) 
							{
								return BTreeNode::findPageInDuplicates(bucket, 
									node.nodePointer, previousNumber, find_record_number);
							}
							else {
								return previousNumber;
							}
						}
						else if (*p > *q) {
							break;
						}
						else if (*p++ < *q++) {
							return previousNumber;
						}
					}
				}
				else if (node.length > 0 || firstPass) {
					firstPass = false;
					// Ascending index
					while (true) {
						if (p == keyEnd) {
							// Check for exact match and if we need to do
							// record number matching.
							if (find_record_number != NO_VALUE && 
								 q == nodeEnd) 
							{
								return BTreeNode::findPageInDuplicates(bucket, 
									node.nodePointer, previousNumber, find_record_number);
							}
							else {
								return previousNumber;
							}
						}
						else if (q == nodeEnd || *p > *q) {
							break;
						}
						else if (*p++ < *q++) {
							return previousNumber;
						}
					}
				}
			}
			prefix = p - key->key_data;

			// If this is the end of bucket, return node.  Somebody else can
			// deal with this
			if (BTreeNode::isEndBucket(&node, leafPage)) {
				return node.pageNumber;
			}

			previousNumber = node.pageNumber;
			pointer = BTreeNode::readNode(&node, pointer, flags, leafPage);
		}
	}
	else {
		// Uses fastest approach when possible.
		// Use direct struct to memory location which is faster then
		// processing readNode from BTreeNode, this is only possible
		// for small keys (key_length < 255)
		btn* node;
		btn* prior;

		prior = node = (btn*)pointer;
		SLONG number = get_long(node->btn_number);
		if (number == END_LEVEL || number == END_BUCKET) {
			pointer = BTreeNode::getPointerFirstNode(bucket);
			node = (btn*)pointer;
		}

		number = get_long(node->btn_number);
		if (number == END_LEVEL) {
			BUGCHECK(206);	// msg 206 exceeded index level
		}

		if (pointer == BTreeNode::getPointerFirstNode(bucket)) {
			prefix = 0;
			// Handle degenerating node, always generated at first
			// page in a level. 
			if ((node->btn_prefix == 0) && (node->btn_length == 0)) {
				// Compute common prefix of key and first node
				prior = node;
				if (flags & btr_all_record_number) {
					node = NEXT_NODE_RECNR(node);
				}
				else {
					node = NEXT_NODE(node);
				}
			}
		}
		const UCHAR* p = key->key_data + prefix;
		const UCHAR* const keyEnd = key->key_data + key->key_length;

		while (true) {

			number = get_long(node->btn_number);

			// If the page/record number is -1, the node is the last in the level
			// and, by definition, is the target node.  Otherwise, if the
			// prefix of the current node is less than the running prefix, its
			// node must have a value greater than the key, which is the insertion
			// point.
			if (number == END_LEVEL || node->btn_prefix < prefix) {
				return get_long(prior->btn_number);
			}

			// If the node prefix is greater than current prefix , it must be less 
			// than the key, so we can skip it.  If it has zero length, then
			// it is a duplicate, and can also be skipped.
			const UCHAR* q = node->btn_data;
			const UCHAR* const nodeEnd = q + node->btn_length;
			if (node->btn_prefix == prefix) {
				if (descending) {
					while (true) {
						if (q == nodeEnd || p == keyEnd) {
							if (find_record_number != NO_VALUE && 
								q == nodeEnd && p == keyEnd) 
							{
								return BTreeNode::findPageInDuplicates(bucket, 
									(UCHAR*)node, get_long(prior->btn_number), 
									find_record_number);
							}
							else {
								return get_long(prior->btn_number);
							}
						}
						else if (*p > *q) {
							break;
						}
						else if (*p++ < *q++) {
							return get_long(prior->btn_number);
						}
					}
				}
				else if (node->btn_length > 0 || firstPass) {
					firstPass = false;
					// Ascending index
					while (true) {
						if (p == keyEnd) {
							// Check for exact match and if we need to do
							// record number matching.
							if (find_record_number != NO_VALUE && 
								 q == nodeEnd) 
							{
								return BTreeNode::findPageInDuplicates(bucket, 
									(UCHAR*)node, get_long(prior->btn_number), 
									find_record_number);
							}
							else {
								return get_long(prior->btn_number);
							}
						}
						else if (q == nodeEnd || *p > *q) {
							break;
						}
						else if (*p++ < *q++) {
							return get_long(prior->btn_number);
						}
					}
				}
			}
			prefix = (USHORT)(p - key->key_data);

			// If this is the end of bucket, return node.  Somebody else can
			// deal with this
			if (number == END_BUCKET) {
				return get_long(node->btn_number);
			}

			prior = node;
			if (flags & btr_all_record_number) {
				node = NEXT_NODE_RECNR(node);
			}
			else {
				node = NEXT_NODE(node);
			}

		}
	}

	// NOTREACHED
	return -1;	// superfluous return to shut lint up
}


static CONTENTS garbage_collect(TDBB tdbb, WIN * window, SLONG parent_number)
{
/**************************************
 *
 *	g a r b a g e _ c o l l e c t
 *
 **************************************
 *
 * Functional description
 *	Garbage collect an index page.  This requires 
 * 	care so that we don't step on other processes 
 * 	that might be traversing the tree forwards, 
 *	backwards, or top to bottom.  We must also 
 *	keep in mind that someone might be adding a node 
 *	at the same time we are deleting.  Therefore we 
 *	must lock all the pages involved to prevent 
 *	such operations while we are garbage collecting.
 *
 **************************************/

	SET_TDBB(tdbb);
	DBB dbb = tdbb->tdbb_database;
	CHECK_DBB(dbb);

	btree_page* gc_page = (btree_page*) window->win_buffer;
	CONTENTS result = contents_above_threshold;

	// check to see if the page was marked not to be garbage collected
	if (gc_page->btr_header.pag_flags & btr_dont_gc) {
		CCH_RELEASE(tdbb, window);
		return contents_above_threshold;
	}

	// record the left sibling now since this is the only way to 
	// get to it quickly; don't worry if it's not accurate now or 
	// is changed after we release the page, since we will fetch 
	// it in a fault-tolerant way anyway.
	const SLONG left_number = gc_page->btr_left_sibling;

	// if the left sibling is blank, that indicates we are the leftmost page, 
	// so don't garbage-collect the page; do this for several reasons:
	//   1.  The leftmost page needs a degenerate zero length node as its first node 
	//       (for a non-leaf, non-top-level page).
	//   2.  The parent page would need to be fixed up to have a degenerate node 
	//       pointing to the right sibling. 
	//   3.  If we remove all pages on the level, we would need to re-add it next 
	//       time a record is inserted, so why constantly garbage-collect and re-create 
	//       this page?

	if (!left_number) {
		CCH_RELEASE(tdbb, window);
		return contents_above_threshold;
	}

	// record some facts for later validation
	USHORT relation_number = gc_page->btr_relation;
	UCHAR index_id = gc_page->btr_id;
	UCHAR index_level = gc_page->btr_level;

	// we must release the page we are attempting to garbage collect; 
	// this is necessary to avoid deadlocks when we fetch the parent page
	CCH_RELEASE(tdbb, window);

	// fetch the parent page, but we have to be careful, because it could have 
	// been garbage-collected when we released it--make checks so that we know it 
	// is the parent page; there is a minute possibility that it could have been 
	// released and reused already as another page on this level, but if so, it 
	// won't really matter because we won't find the node on it
	WIN parent_window(parent_number);
	btree_page* parent_page = (btree_page*) CCH_FETCH(tdbb, &parent_window, LCK_write, pag_undefined);
	if ((parent_page->btr_header.pag_type != pag_index)
		|| (parent_page->btr_relation != relation_number)
		|| (parent_page->btr_id != (UCHAR)(index_id % 256))
		|| (parent_page->btr_level != index_level + 1)) 
	{
		CCH_RELEASE(tdbb, &parent_window);
		return contents_above_threshold;
	}

	// find the left sibling page by going one page to the left, 
	// but if it does not recognize us as its right sibling, keep 
	// going to the right until we find the page that is our real 
	// left sibling
	WIN left_window(left_number);
	btree_page* left_page = (btree_page*) CCH_FETCH(tdbb, &left_window, LCK_write, pag_index);
	while (left_page->btr_sibling != window->win_page) {
#ifdef DEBUG_BTR
		CCH_RELEASE(tdbb, &parent_window);
		CCH_RELEASE(tdbb, &left_window);
		CORRUPT(204);	// msg 204 index inconsistent
#endif
		// If someone garbage collects the index page before we can, it
		// won't be found by traversing the right sibling chain. This means
		// scanning index pages until the end-of-level bucket is hit.
		if (!left_page->btr_sibling) {
			CCH_RELEASE(tdbb, &parent_window);
			CCH_RELEASE(tdbb, &left_window);
			return contents_above_threshold;
		}
		left_page = (btree_page*) CCH_HANDOFF(tdbb, &left_window,
			left_page->btr_sibling, LCK_write, pag_index);
	}

	// now refetch the original page and make sure it is still 
	// below the threshold for garbage collection.
	gc_page = (btree_page*) CCH_FETCH(tdbb, window, LCK_write, pag_index);
	if ((gc_page->btr_length >= GARBAGE_COLLECTION_BELOW_THRESHOLD)
		|| (gc_page->btr_header.pag_flags & btr_dont_gc)) 
	{
		CCH_RELEASE(tdbb, &parent_window);
		CCH_RELEASE(tdbb, &left_window);
		CCH_RELEASE(tdbb, window);
		return contents_above_threshold;
	}

	// fetch the right sibling page
	btree_page* right_page = NULL;
	WIN right_window(gc_page->btr_sibling);
	if (right_window.win_page) {
		// right_window.win_flags = 0; redundant, made by the constructor
		right_page = (btree_page*) CCH_FETCH(tdbb, &right_window, LCK_write, pag_index);

		if (right_page->btr_left_sibling != window->win_page) {
			CCH_RELEASE(tdbb, &parent_window);
			if (left_page) {
				CCH_RELEASE(tdbb, &left_window);
			}
			CCH_RELEASE(tdbb, window);
			CCH_RELEASE(tdbb, &right_window);
#ifdef DEBUG_BTR
			CORRUPT(204);	// msg 204 index inconsistent
#endif
			return contents_above_threshold;
		}
	}

	const SCHAR flags = gc_page->btr_header.pag_flags;
	// Check if flags are valid.
	if ((parent_page->btr_header.pag_flags & BTR_FLAG_COPY_MASK) !=
		(flags & BTR_FLAG_COPY_MASK))
	{
		CORRUPT(204);	// msg 204 index inconsistent
	}

	// Find the node on the parent's level--the parent page could 
	// have split while we didn't have it locked
	UCHAR *parentPointer = BTreeNode::getPointerFirstNode(parent_page);
	IndexNode parentNode;
	while (true) {
		parentPointer = BTreeNode::readNode(&parentNode, parentPointer, flags, false);
		if (BTreeNode::isEndBucket(&parentNode, false)) {
			parent_page = (btree_page*) CCH_HANDOFF(tdbb, &parent_window,
				parent_page->btr_sibling, LCK_write, pag_index);
			parentPointer = BTreeNode::getPointerFirstNode(parent_page);
			continue;
		}

		if (parentNode.pageNumber == window->win_page || 
			BTreeNode::isEndLevel(&parentNode, false)) 
		{
			break;
		}

	}

	// we should always find the node, but just in case we don't, bow out gracefully
	if (BTreeNode::isEndLevel(&parentNode, false)) {
		CCH_RELEASE(tdbb, &left_window);
		if (right_page) {
			CCH_RELEASE(tdbb, &right_window);
		}
		CCH_RELEASE(tdbb, &parent_window);
		CCH_RELEASE(tdbb, window);
#ifdef DEBUG_BTR
		CORRUPT(204);	// msg 204 index inconsistent
#endif
		return contents_above_threshold;
	}


	// Fix for ARINC database corruption bug: in most cases we update the END_BUCKET 
	// marker of the left sibling page to contain the END_BUCKET of the garbage-collected 
	// page.  However, when this page is the first page on its parent, then the left 
	// sibling page is the last page on its parent.  That means if we update its END_BUCKET 
	// marker, its bucket of values will extend past that of its parent, causing trouble 
	// down the line.  
   
	// So we never garbage-collect a page which is the first one on its parent.  This page 
	// will have to wait until the parent page gets collapsed with the page to its left, 
	// in which case this page itself will then be garbage-collectable.  Since there are 
	// no more keys on this page, it will not be garbage-collected itself.  When the page 
	// to the right falls below the threshold for garbage collection, it will be merged with 
	// this page.
	if (parentNode.nodePointer == BTreeNode::getPointerFirstNode(parent_page)) {
		CCH_RELEASE(tdbb, &left_window);
		if (right_page) {
			CCH_RELEASE(tdbb, &right_window);
		}
		CCH_RELEASE(tdbb, &parent_window);
		CCH_RELEASE(tdbb, window);
		return contents_above_threshold;
	}

	// find the last node on the left sibling and save its key value
	// Check if flags are valid.
	if ((left_page->btr_header.pag_flags & BTR_FLAG_COPY_MASK) !=
		(flags & BTR_FLAG_COPY_MASK))
	{
		CORRUPT(204);	// msg 204 index inconsistent
	}
	bool useJumpInfo = (flags & btr_jump_info);
	bool leafPage = (gc_page->btr_level == 0);
	UCHAR* leftPointer;
	KEY lastKey;

	leftPointer = BTreeNode::getPointerFirstNode(left_page);
	lastKey.key_length = 0;

	IndexNode leftNode;
	if (useJumpInfo) {
		IndexJumpInfo leftJumpInfo;
		UCHAR* pointer;
		pointer = BTreeNode::getPointerFirstNode(left_page, &leftJumpInfo);

		// Walk trough node jumpers.
		USHORT n = leftJumpInfo.jumpers;
		IndexJumpNode jumpNode;
		while (n) {
			pointer = BTreeNode::readJumpNode(&jumpNode, pointer, flags);
			BTreeNode::readNode(&leftNode, 
				(UCHAR*)left_page + jumpNode.offset, flags, leafPage);

			if (!(BTreeNode::isEndBucket(&leftNode, leafPage) ||
				BTreeNode::isEndLevel(&leftNode, leafPage))) 
			{
				if (jumpNode.length) {
					memcpy(lastKey.key_data + jumpNode.prefix, jumpNode.data,
						jumpNode.length);
				}
				leftPointer = (UCHAR*)left_page + jumpNode.offset;
				lastKey.key_length = jumpNode.prefix + jumpNode.length;
			}
			else {
				break;
			}
			n--;
		}
	}
	while (true) {
		leftPointer = BTreeNode::readNode(&leftNode, leftPointer, flags, leafPage);
		// If it isn't a recordnumber were done
		if (BTreeNode::isEndBucket(&leftNode, leafPage) ||
			BTreeNode::isEndLevel(&leftNode, leafPage)) 
		{
			break;
		}
		// Save data
		if (leftNode.length) {
			UCHAR* p = lastKey.key_data + leftNode.prefix;
			memcpy(p, leftNode.data, leftNode.length);
			lastKey.key_length = leftNode.prefix + leftNode.length;
		}
	}
	leftPointer = leftNode.nodePointer;

	// see if there's enough space on the left page to move all the nodes to it
	// and leave some extra space for expansion (at least one key length)
	SCHAR gcFlags = gc_page->btr_header.pag_flags;
	UCHAR* gcPointer = BTreeNode::getPointerFirstNode(gc_page);
	IndexNode gcNode;
	BTreeNode::readNode(&gcNode, gcPointer, gcFlags, leafPage);
	USHORT prefix = BTreeNode::computePrefix(lastKey.key_data, lastKey.key_length,
		gcNode.data, gcNode.length);
	if (useJumpInfo) {
		// Get pointer for calculating gcSize (including jump nodes).
		IndexJumpInfo leftJumpInfo;
		gcPointer = BTreeNode::getPointerFirstNode(gc_page, &leftJumpInfo);
	}
	USHORT gcSize = gc_page->btr_length - (gcPointer - (UCHAR*)(gc_page));
	USHORT leftAssumedSize = left_page->btr_length + gcSize - prefix;

	// If the new page will be larger then the thresholds don't gc.
	//GARBAGE_COLLECTION_NEW_PAGE_MAX_THRESHOLD
	USHORT max_threshold = GARBAGE_COLLECTION_NEW_PAGE_MAX_THRESHOLD;
	//USHORT max_threshold = dbb->dbb_page_size - 50;
	if (leftAssumedSize > max_threshold) {
		CCH_RELEASE(tdbb, &parent_window);
		CCH_RELEASE(tdbb, &left_window);
		CCH_RELEASE(tdbb, window);
		if (right_page) {
			CCH_RELEASE(tdbb, &right_window);
		}
		return contents_above_threshold;
	}

	if (useJumpInfo) {
		// First copy left page to scratch page.
		SLONG scratchPage[OVERSIZE];
		btree_page* newBucket = (btree_page*) scratchPage;

		IndexJumpInfo jumpInfo;
		UCHAR* pointer = BTreeNode::getPointerFirstNode(left_page, &jumpInfo);
		USHORT headerSize = (pointer - (UCHAR*)left_page);
		USHORT jumpersOriginalSize = jumpInfo.firstNodeOffset - headerSize;

		// Copy header and data
		memcpy(newBucket, (UCHAR*)left_page, headerSize);
		memcpy((UCHAR*)newBucket + headerSize, 
			(UCHAR*)left_page + jumpInfo.firstNodeOffset, 
			left_page->btr_length - jumpInfo.firstNodeOffset);

		// Update leftPointer to scratch page. 
		leftPointer = (UCHAR*)newBucket + (leftPointer - (UCHAR*)left_page) - 
			jumpersOriginalSize;
		SCHAR flags = newBucket->btr_header.pag_flags;
		gcPointer = BTreeNode::getPointerFirstNode(gc_page);
		//
		BTreeNode::readNode(&leftNode, leftPointer, flags, leafPage);
		// Calculate the total amount of compression on page as the combined 
		// totals of the two pages, plus the compression of the first node 
		// on the g-c'ed page, minus the prefix of the END_BUCKET node to 
		// be deleted.
		newBucket->btr_prefix_total += 
			gc_page->btr_prefix_total + prefix - leftNode.prefix;

		// Get first node from gc-page.
		gcPointer = BTreeNode::readNode(&gcNode, gcPointer, gcFlags, leafPage);

		// Write first node with prefix compression on left page.
		leftNode.prefix = prefix;
		leftNode.length = gcNode.length - prefix;
		leftNode.recordNumber = gcNode.recordNumber;
		leftNode.pageNumber = gcNode.pageNumber;
		leftNode.data = gcNode.data + prefix;
		leftPointer = BTreeNode::writeNode(&leftNode, leftPointer, flags, leafPage);

		// Update page-size.
		newBucket->btr_length = (leftPointer - (UCHAR*)newBucket);
		// copy over the remainder of the page to be garbage-collected.
		const USHORT l = gc_page->btr_length - (gcPointer - (UCHAR*)(gc_page));
		memcpy(leftPointer, gcPointer, l);
		// update page size.
		newBucket->btr_length += l;
		
		// Generate new jump nodes.
		jumpNodeList* jumpNodes = FB_NEW(*tdbb->tdbb_default) jumpNodeList(tdbb->tdbb_default);
		USHORT jumpersNewSize = 0;
		// Update jump information on scratch page, so generate_jump_nodes
		// can deal with it.
		jumpInfo.firstNodeOffset = headerSize;
		jumpInfo.jumpers = 0;
		BTreeNode::writeJumpInfo(newBucket, &jumpInfo);
		generate_jump_nodes(tdbb, newBucket, jumpNodes, 0, &jumpersNewSize, NULL, NULL);
		
		// Now we know exact how big our updated left page is, so check size 
		// again to be sure it all will fit.
		// If the new page will be larger then the page size don't gc ofcourse.
		if (newBucket->btr_length + jumpersNewSize > dbb->dbb_page_size) {
			CCH_RELEASE(tdbb, &parent_window);
			CCH_RELEASE(tdbb, &left_window);
			CCH_RELEASE(tdbb, window);
			if (right_page) {
				CCH_RELEASE(tdbb, &right_window);
			}
			IndexJumpNode* walkJumpNode = jumpNodes->begin();
			for (int i = 0; i < jumpNodes->getCount(); i++) {
				if (walkJumpNode[i].data) {
					delete walkJumpNode[i].data;
				}
			}
			jumpNodes->clear();
			delete jumpNodes;
			return contents_above_threshold;
		}

		// Update the parent first.  If the parent is not written out first, 
		// we will be pointing to a page which is not in the doubly linked 
		// sibling list, and therefore navigation back and forth won't work.
		// AB: Parent is always a index pointer page.
		result = delete_node(tdbb, &parent_window, parentNode.nodePointer);
		CCH_RELEASE(tdbb, &parent_window);

		// Update the right sibling page next, since it does not really 
		// matter that the left sibling pointer points to the page directly 
		// to the left, only that it point to some page to the left.
		// Set up the precedence so that the parent will be written first.
		if (right_page) {
			if (parent_page) {
				CCH_precedence(tdbb, &right_window, parent_window.win_page);
			}
			CCH_MARK(tdbb, &right_window);
			right_page->btr_left_sibling = left_window.win_page;

			CCH_RELEASE(tdbb, &right_window);
		}

		// Now update the left sibling, effectively removing the garbage-collected page 
		// from the tree.  Set the precedence so the right sibling will be written first.
		if (right_page) {
			CCH_precedence(tdbb, &left_window, right_window.win_page);
		}
		else if (parent_page) {
			CCH_precedence(tdbb, &left_window, parent_window.win_page);
		}

		CCH_MARK(tdbb, &left_window);

		if (right_page) {
			left_page->btr_sibling = right_window.win_page;
		}
		else {
			left_page->btr_sibling = 0;
		}

		// Finally write all data to left page.
		jumpInfo.firstNodeOffset = headerSize + jumpersNewSize;
		jumpInfo.jumpers = jumpNodes->getCount();
		pointer = BTreeNode::writeJumpInfo(left_page, &jumpInfo);
		// Write jump nodes.
		IndexJumpNode* walkJumpNode = jumpNodes->begin();
		for (int i = 0; i < jumpNodes->getCount(); i++) {
			// Update offset to real position with new jump nodes.
			walkJumpNode[i].offset += jumpersNewSize;
			pointer = BTreeNode::writeJumpNode(&walkJumpNode[i], pointer, flags);
			if (walkJumpNode[i].data) {
				delete walkJumpNode[i].data;
			}
		}
		// Copy data.
		memcpy(pointer, (UCHAR*)newBucket + headerSize, 
			newBucket->btr_length - headerSize);
		// Update page header information.
		left_page->btr_prefix_total = newBucket->btr_prefix_total;
		left_page->btr_length = newBucket->btr_length + jumpersNewSize;

		jumpNodes->clear();
		delete jumpNodes;
	}
	else {
		// Now begin updating the pages.  We must write them out in such 
		// a way as to maintain on-disk integrity at all times.  That means 
		// not having pointers to released pages, and not leaving things in 
		// an inconsistent state for navigation through the pages.

		// Update the parent first.  If the parent is not written out first, 
		// we will be pointing to a page which is not in the doubly linked 
		// sibling list, and therefore navigation back and forth won't work.
		// AB: Parent is always a index pointer page.
		result = delete_node(tdbb, &parent_window, parentNode.nodePointer);
		CCH_RELEASE(tdbb, &parent_window);

		// Update the right sibling page next, since it does not really 
		// matter that the left sibling pointer points to the page directly 
		// to the left, only that it point to some page to the left.
		// Set up the precedence so that the parent will be written first.
		if (right_page) {
			if (parent_page) {
				CCH_precedence(tdbb, &right_window, parent_window.win_page);
			}
			CCH_MARK(tdbb, &right_window);
			right_page->btr_left_sibling = left_window.win_page;

			CCH_RELEASE(tdbb, &right_window);
		}

		// Now update the left sibling, effectively removing the garbage-collected page 
		// from the tree.  Set the precedence so the right sibling will be written first.
		if (right_page) {
			CCH_precedence(tdbb, &left_window, right_window.win_page);
		}
		else if (parent_page) {
			CCH_precedence(tdbb, &left_window, parent_window.win_page);
		}

		CCH_MARK(tdbb, &left_window);

		if (right_page) {
			left_page->btr_sibling = right_window.win_page;
		}
		else {
			left_page->btr_sibling = 0;
		}

		gcPointer = BTreeNode::getPointerFirstNode(gc_page);
		BTreeNode::readNode(&leftNode, leftPointer, flags, leafPage);
		// Calculate the total amount of compression on page as the combined totals 
		// of the two pages, plus the compression of the first node on the g-c'ed page,
		// minus the prefix of the END_BUCKET node to be deleted.
		left_page->btr_prefix_total += 
			gc_page->btr_prefix_total + prefix - leftNode.prefix;

		// Get first node from gc-page.
		gcPointer = BTreeNode::readNode(&gcNode, gcPointer, gcFlags, leafPage);

		// Write first node with prefix compression on left page.
		leftNode.prefix = prefix;
		leftNode.length = gcNode.length - prefix;
		leftNode.recordNumber = gcNode.recordNumber;
		leftNode.pageNumber = gcNode.pageNumber;
		leftNode.data = gcNode.data + prefix;
		leftPointer = BTreeNode::writeNode(&leftNode, leftPointer, flags, leafPage);

		// copy over the remainder of the page to be garbage-collected
		const USHORT l = gc_page->btr_length - (gcPointer - (UCHAR*)(gc_page));
		memcpy(leftPointer, gcPointer, l);
		leftPointer += l;
		// update page size
		left_page->btr_length = leftPointer - (UCHAR*)(left_page);
	}

#ifdef DEBUG_BTR
	if (left_page->btr_length > dbb->dbb_page_size) {
		CCH_RELEASE(tdbb, &left_window);
		CCH_RELEASE(tdbb, window);
		CORRUPT(204);	// msg 204 index inconsistent
		return contents_above_threshold;
	}
#endif

	CCH_RELEASE(tdbb, &left_window);

	// finally, release the page, and indicate that we should write the 
	// previous page out before we write the TIP page out
	CCH_RELEASE(tdbb, window);
	PAG_release_page(window->win_page, left_page ? left_window.win_page :
		right_page ? right_window.win_page : parent_window.win_page);

	// if the parent page needs to be garbage collected, that means we need to 
	// re-fetch the parent and check to see whether it is still garbage-collectable; 
	// make sure that the page is still a btree page in this index and in this level--
	// there is a miniscule chance that it was already reallocated as another page 
	// on this level which is already below the threshold, in which case it doesn't 
	// hurt anything to garbage-collect it anyway
	if (result != contents_above_threshold) {
		window->win_page = parent_window.win_page;
		parent_page = (btree_page*) CCH_FETCH(tdbb, window, LCK_write, pag_undefined);

		if ((parent_page->btr_header.pag_type != pag_index)
			|| (parent_page->btr_relation != relation_number)
			|| (parent_page->btr_id != index_id)
			|| (parent_page->btr_level != index_level + 1)) 
		{
			CCH_RELEASE(tdbb, window);
			return contents_above_threshold;
		}

		// check whether it is empty
		parentPointer = BTreeNode::getPointerFirstNode(parent_page);
		IndexNode parentNode;
		parentPointer = BTreeNode::readNode(&parentNode, parentPointer, flags, false);
		if (BTreeNode::isEndBucket(&parentNode, false) ||
			BTreeNode::isEndLevel(&parentNode, false)) 
		{
			return contents_empty;
		}

		// check whether there is just one node
		parentPointer = BTreeNode::readNode(&parentNode, parentPointer, flags, false);
		if (BTreeNode::isEndBucket(&parentNode, false) ||
			BTreeNode::isEndLevel(&parentNode, false)) 
		{
			return contents_single;
		}

		// check to see if the size of the page is below the garbage collection threshold
		if (parent_page->btr_length < GARBAGE_COLLECTION_BELOW_THRESHOLD) {
			return contents_below_threshold;
		}

		// the page must have risen above the threshold; release the window since 
		// someone else added a node while the page was released
		CCH_RELEASE(tdbb, window);
		return contents_above_threshold;
	}

	return result;
}


static void generate_jump_nodes(TDBB tdbb, btree_page* page, jumpNodeList* jumpNodes,
								USHORT excludeOffset, USHORT* jumpersSize,  
								USHORT* splitIndex, USHORT* splitPrefix)
{
/**************************************
 *
 *	g e n e r a t e _ j u m p _ n o d e s
 *
 **************************************
 *
 * Functional description
 *
 **************************************/

	SET_TDBB(tdbb);
	DBB dbb = tdbb->tdbb_database;
	fb_assert(page);
	fb_assert(jumpNodes);
	fb_assert(jumpersSize);

	IndexJumpInfo jumpInfo;
	BTreeNode::getPointerFirstNode(page, &jumpInfo);
	SCHAR flags = page->btr_header.pag_flags;
	bool leafPage = (page->btr_level == 0);

	*jumpersSize = 0;
	UCHAR* pointer = (UCHAR*)page + jumpInfo.firstNodeOffset;

	key jumpKey, currentKey;
	UCHAR* jumpData = jumpKey.key_data;
	USHORT jumpLength = 0;
	UCHAR* currentData = currentKey.key_data;

	if (splitIndex) {
		*splitIndex = 0;
	}
	if (splitPrefix) {
		*splitPrefix = 0;
	}

	const UCHAR* newAreaPosition = pointer + jumpInfo.jumpAreaSize;
	const UCHAR* const endpoint = ((UCHAR*)page + page->btr_length);
	const UCHAR* const halfpoint = ((UCHAR*)page + (dbb->dbb_page_size / 2));
	const UCHAR* const excludePointer = ((UCHAR*)page + excludeOffset);
	IndexJumpNode jumpNode;

	if (flags & btr_large_keys) {
		IndexNode node;
		while (pointer < endpoint) {
			pointer = BTreeNode::readNode(&node, pointer, flags, leafPage);
			if (BTreeNode::isEndBucket(&node, leafPage) ||
				BTreeNode::isEndLevel(&node, leafPage))
			{
				break;
			}
			if (node.length) {
				UCHAR* q = currentData + node.prefix;
				memcpy(q, node.data, node.length);
			}
					
			if (splitIndex && splitPrefix && !*splitIndex) {
				*splitPrefix += node.prefix;
			}

			if ((node.nodePointer > newAreaPosition) && 
				(node.nodePointer != excludePointer)) 
			{
				// Create a jumpnode, but it may not point to the new
				// insert pointer or any MARKER else we make split 
				// more difficult then needed.
				jumpNode.offset = (node.nodePointer - (UCHAR*)page);
				jumpNode.prefix = BTreeNode::computePrefix(jumpData, jumpLength, 
					currentData, node.prefix);
				jumpNode.length = node.prefix - jumpNode.prefix;
				if (jumpNode.length) {
					jumpNode.data = FB_NEW(*tdbb->tdbb_default) UCHAR[jumpNode.length];
					const UCHAR* const q = currentData + jumpNode.prefix;
					memcpy(jumpNode.data, q, jumpNode.length);
				}
				else {
					jumpNode.data = NULL;
				}
				// Push node on end in list
				jumpNodes->add(jumpNode);
				// Store new data in jumpKey, so a new jump node can calculate prefix
				memcpy(jumpData + jumpNode.prefix, jumpNode.data, jumpNode.length);
				jumpLength = jumpNode.length + jumpNode.prefix;
				// Check if this could be our split point (if we need to split)
				if (splitIndex && !*splitIndex && (pointer > halfpoint)) {
					*splitIndex = jumpNodes->getCount();
				}
				// Set new position for generating jumpnode
				newAreaPosition += jumpInfo.jumpAreaSize;
				*jumpersSize += BTreeNode::getJumpNodeSize(&jumpNode, flags);
			}
		}
	}
	else {
		while (pointer < endpoint) {

			btn* node = (btn*)pointer;
			if (!leafPage && (flags & btr_all_record_number)) {
				pointer = (UCHAR*)NEXT_NODE_RECNR(node);
			}
			else {
				pointer = (UCHAR*)NEXT_NODE(node);
			}
			if (node->btn_length) {
				UCHAR* q = currentData + node->btn_prefix;
				memcpy(q, node->btn_data, node->btn_length);
			}
					
			if (splitIndex && splitPrefix && !*splitIndex) {
				*splitPrefix += node->btn_prefix;
			}

			if (((UCHAR*)node > newAreaPosition) && 
				(get_long(node->btn_number) >= 0) &&
				((UCHAR*)node != excludePointer)) 
			{
				// Create a jumpnode, but it may not point to the new
				// insert pointer or any MARKER else we make split 
				// more difficult then needed.
				jumpNode.offset = ((UCHAR*)node - (UCHAR*)page);
				jumpNode.prefix = BTreeNode::computePrefix(jumpData, jumpLength, 
					currentData, node->btn_prefix);
				jumpNode.length = node->btn_prefix - jumpNode.prefix;
				if (jumpNode.length) {
					jumpNode.data = FB_NEW(*tdbb->tdbb_default) UCHAR[jumpNode.length];
					const UCHAR* const q = currentData + jumpNode.prefix;
					memcpy(jumpNode.data, q, jumpNode.length);
				}
				else {
					jumpNode.data = NULL;
				}
				// Push node on end in list
				jumpNodes->add(jumpNode);
				// Store new data in jumpKey, so a new jump node can calculate prefix
				memcpy(jumpData + jumpNode.prefix, jumpNode.data, jumpNode.length);
				jumpLength = jumpNode.length + jumpNode.prefix;
				// Check if this could be our split point (if we need to split)
				if (splitIndex && !*splitIndex && (pointer > halfpoint)) {
					*splitIndex = jumpNodes->getCount();
				}
				// Set new position for generating jumpnode
				newAreaPosition += jumpInfo.jumpAreaSize;
				*jumpersSize += BTreeNode::getJumpNodeSize(&jumpNode, flags);
			}

		}
	}
}


static SLONG insert_node(TDBB tdbb,
						 WIN * window,
						 IIB * insertion,
						 KEY * new_key,
						 SLONG * new_record_number,
						 SLONG * original_page, 
						 SLONG * sibling_page)
{
/**************************************
 *
 *	i n s e r t _ n o d e
 *
 **************************************
 *
 * Functional description
 *	Insert a node in a index leaf page.
 *  If this isn't the right bucket, return NO_VALUE.  
 *  If it splits, return the split page number and 
 *	leading string.  This is the workhorse for add_node.
 *
 **************************************/

	SET_TDBB(tdbb);
	DBB dbb = tdbb->tdbb_database;
	CHECK_DBB(dbb);

	// find the insertion point for the specified key
	btree_page* bucket = (btree_page*) window->win_buffer;
	const SCHAR flags = bucket->btr_header.pag_flags;
	KEY* key = insertion->iib_key;

	bool unique = (insertion->iib_descriptor->idx_flags & idx_unique);
	bool leafPage = (bucket->btr_level == 0);
	bool allRecordNumber = (flags & btr_all_record_number);
	USHORT prefix = 0;
	SLONG newRecordNumber;
	if (leafPage) {
		newRecordNumber = insertion->iib_number;
	}
	else {
		newRecordNumber = *new_record_number;
	}
	UCHAR* pointer = find_node_start_point(bucket, key, 0, &prefix,
		insertion->iib_descriptor->idx_flags & idx_descending, 
		false, allRecordNumber, newRecordNumber);
	if (!pointer) {
		return NO_VALUE;
	}

	IndexNode beforeInsertNode;
	pointer = BTreeNode::readNode(&beforeInsertNode, pointer, flags, leafPage);

	// loop through the equivalent nodes until the correct insertion 
	// point is found; for leaf level this will be the first node
	USHORT newPrefix, newLength;
	USHORT nodeOffset, l;
	while (true) {
		nodeOffset = (USHORT) (beforeInsertNode.nodePointer - (UCHAR*) bucket);
		newPrefix = beforeInsertNode.prefix;
		newLength = beforeInsertNode.length;

		// update the newPrefix and newLength against the node (key) that will
		// be inserted before it.
		const UCHAR* p = key->key_data + newPrefix;
		const UCHAR* q = beforeInsertNode.data;
		l = MIN(key->key_length - newPrefix, newLength);
		while (l) {
			if (*p++ != *q++) {
				break;
			}
			--newLength;
			newPrefix++;
			l--;
		}

		// check if the inserted node has the same value as the next node
		if (newPrefix != key->key_length ||
			newPrefix != beforeInsertNode.length + beforeInsertNode.prefix) {
			break;
		}
		else {
			// We have a equal node, so find the correct insertion point.
			if (BTreeNode::isEndBucket(&beforeInsertNode, leafPage)) {
				if (allRecordNumber) {
					break;
				}
				else {
					return NO_VALUE;
				}
			}
			if (BTreeNode::isEndLevel(&beforeInsertNode, leafPage)) {
				break;
			}
			if (leafPage && unique) {
				// Save the duplicate so the main caller can validate them.
				SBM_set(tdbb, &insertion->iib_duplicates, 
					beforeInsertNode.recordNumber);
			}
			// AB: Never insert a duplicate node with the same record number.
			// This would lead to nodes which will never be deleted.
			//if (leafPage && (newRecordNumber == beforeInsertNode.recordNumber)) {
				// AB: It seems this is not enough, because on mass duplicate
				// update to many nodes are deleted, possible staying and
				// going are wrong checked before BTR_remove is called.
			//	CCH_RELEASE(tdbb, window);
			//	return 0;
			//} 
			//else 
			if (allRecordNumber) {
				// if recordnumber is higher we need to insert before it.
				if (newRecordNumber <= beforeInsertNode.recordNumber) {
					break;
				}
			}
			else if (!unique) {
				break;
			}
			prefix = newPrefix;
			pointer = BTreeNode::readNode(&beforeInsertNode, pointer, flags, leafPage);
		}
	}

	USHORT beforeInsertOriginalSize = 
		BTreeNode::getNodeSize(&beforeInsertNode, flags, leafPage);
	USHORT orginalPrefix = beforeInsertNode.prefix;

	// Update the values for the next node after our new node.
	// First, store needed data for beforeInsertNode into tempData.
	UCHAR* tempData = FB_NEW(*tdbb->tdbb_default) UCHAR[newLength];
	const UCHAR* p = beforeInsertNode.data + newPrefix - beforeInsertNode.prefix;
	memcpy(tempData, p, newLength);

	beforeInsertNode.prefix = newPrefix;
	beforeInsertNode.length = newLength;
	USHORT beforeInsertSize = 
		BTreeNode::getNodeSize(&beforeInsertNode, flags, leafPage);

	// Set values for our new node.
	IndexNode newNode;
	newNode.prefix = prefix;
	newNode.length = key->key_length - prefix; 
	newNode.data = key->key_data + prefix;
	newNode.recordNumber = newRecordNumber;
	if (!leafPage) {
		newNode.pageNumber = insertion->iib_number;
	}

	// Compute the delta between current and new page.
	USHORT delta = BTreeNode::getNodeSize(&newNode, flags, leafPage) +
		beforeInsertSize - beforeInsertOriginalSize;

	// Copy data up to insert point to scratch page.
	SLONG scratchPage[OVERSIZE];
	memcpy(scratchPage, bucket, nodeOffset);
	btree_page* newBucket = (btree_page*) scratchPage;

	// Set pointer of new node to right place.
	pointer = ((UCHAR*)newBucket + nodeOffset);
	// Insert the new node.
	pointer = BTreeNode::writeNode(&newNode, pointer, flags, leafPage);
	newBucket->btr_prefix_total += prefix - orginalPrefix;

	// Recompress and rebuild the next node.
	beforeInsertNode.data = tempData;
	pointer = BTreeNode::writeNode(&beforeInsertNode, pointer, flags, leafPage);
	newBucket->btr_prefix_total += newPrefix;
	delete tempData;

	// Copy remaining data to scratch page.
	if ((nodeOffset + beforeInsertOriginalSize) < bucket->btr_length) {
		memcpy(pointer, (UCHAR*)bucket + nodeOffset + beforeInsertOriginalSize, 
			bucket->btr_length - (nodeOffset + beforeInsertOriginalSize));
	} 

	// Update bucket size.
	newBucket->btr_length += delta;

	// figure out whether this node was inserted at the end of the page
	bool endOfPage = BTreeNode::isEndBucket(&beforeInsertNode, leafPage) ||
		BTreeNode::isEndLevel(&beforeInsertNode, leafPage);

	// Initialize variables needed for generating jump information
	bool useJumpInfo = (flags & btr_jump_info);
	bool fragmentedOffset = false;
	USHORT jumpersOriginalSize = 0;
	USHORT jumpersNewSize = 0;
	USHORT headerSize = 0;
	USHORT newPrefixTotalBySplit = 0;
	USHORT splitJumpNodeIndex = 0;
	IndexJumpInfo jumpInfo;
	jumpNodeList* jumpNodes = FB_NEW(*tdbb->tdbb_default) jumpNodeList(tdbb->tdbb_default);

	USHORT ensureEndInsert = 0;
	if (endOfPage) {
		// If we're adding a node at the end we don't want that a page 
		// splits in the middle, but at the end. We can never be sure
		// that this will happen, but at least give it a bigger chance.
		ensureEndInsert = 6 + key->key_length;
	}
		
	if (useJumpInfo) {
		// Get the total size of the jump nodes currently in use.
		pointer = BTreeNode::getPointerFirstNode(newBucket, &jumpInfo);
		headerSize = (pointer - (UCHAR*)newBucket);
		jumpersOriginalSize = jumpInfo.firstNodeOffset - headerSize;

		// Allow some fragmentation, 10% below or above actual point.
		jumpersNewSize = jumpersOriginalSize;
		USHORT n = jumpInfo.jumpers;
		USHORT index = 1;
		USHORT minOffset, maxOffset;
		USHORT fragmentedThreshold = (jumpInfo.jumpAreaSize / 5);
		IndexJumpNode jumpNode;
		while (n) {
			pointer = BTreeNode::readJumpNode(&jumpNode, pointer, flags);
			if (jumpNode.offset == nodeOffset) {
				fragmentedOffset = true;
				break;
			}
			if (jumpNode.offset > nodeOffset) {
				jumpNode.offset += delta;
			}
			minOffset = headerSize + jumpersOriginalSize + 
				(index * jumpInfo.jumpAreaSize) - fragmentedThreshold;
			if (jumpNode.offset < minOffset) {
				fragmentedOffset = true;
				break;
			}
			maxOffset =  headerSize + jumpersOriginalSize + 
				(index * jumpInfo.jumpAreaSize) + fragmentedThreshold;
			if (jumpNode.offset > maxOffset) {
				fragmentedOffset = true;
				break;
			}
			jumpNodes->add(jumpNode);
			index++;
			n--;
		}
		// Rebuild jump nodes if new node is inserted after last
		// jump node offset + jumpAreaSize.
		if (nodeOffset >= (headerSize + jumpersOriginalSize + 
			((jumpInfo.jumpers + 1) * jumpInfo.jumpAreaSize)))
		{
			fragmentedOffset = true;
		}
		// Rebuild jump nodes if we gona split.
		if (newBucket->btr_length + ensureEndInsert > dbb->dbb_page_size) {
			fragmentedOffset = true;
		}

		if (fragmentedOffset) {
			// Clean up any previous nodes.
			jumpNodes->clear();
			// Generate new jump nodes.			
			generate_jump_nodes(tdbb, newBucket, jumpNodes, 
				(USHORT)(newNode.nodePointer - (UCHAR*)newBucket),
				&jumpersNewSize, &splitJumpNodeIndex, &newPrefixTotalBySplit);
		}
	}

	// If the bucket still fits on a page, we're almost done.
	if (newBucket->btr_length + ensureEndInsert +
		jumpersNewSize - jumpersOriginalSize <= dbb->dbb_page_size) 
	{
		// if we are a pointer page, make sure that the page we are 
		// pointing to gets written before we do for on-disk integrity
		if (!leafPage) {
			CCH_precedence(tdbb, window, insertion->iib_number);
		}
		// Mark page as dirty.
		CCH_MARK(tdbb, window);

		if (useJumpInfo) {
			// Put all data back into bucket (= window->win_buffer).

			// Write jump information header.
			jumpInfo.firstNodeOffset = headerSize + jumpersNewSize;
			jumpInfo.jumpers = jumpNodes->getCount();
			pointer = BTreeNode::writeJumpInfo(bucket, &jumpInfo);

			// Write jump nodes.
			IndexJumpNode* walkJumpNode = jumpNodes->begin();
			for (int i = 0; i < jumpNodes->getCount(); i++) {
				// Update offset to real position with new jump nodes.
				walkJumpNode[i].offset += jumpersNewSize - jumpersOriginalSize;
				pointer = BTreeNode::writeJumpNode(&walkJumpNode[i], pointer, flags);
				if (fragmentedOffset) {
					if (walkJumpNode[i].data) {
						delete walkJumpNode[i].data;
					}
				}
			}		
			pointer = (UCHAR*)bucket + jumpInfo.firstNodeOffset;
			// Copy data block.
			memcpy(pointer, (UCHAR*)newBucket + headerSize + jumpersOriginalSize, 
				newBucket->btr_length - (headerSize + jumpersOriginalSize));
			
			// Update header information.
			bucket->btr_prefix_total = newBucket->btr_prefix_total;
			bucket->btr_length = newBucket->btr_length + jumpersNewSize - jumpersOriginalSize;
		} 
		else {
			// Copy temp-buffer data to window buffer.
			memcpy(window->win_buffer, newBucket, newBucket->btr_length);
		}

		CCH_RELEASE(tdbb, window);

		jumpNodes->clear();
		delete jumpNodes;

		return NO_SPLIT;
	}

	// We've a bucket split in progress.  We need to determine the split point.
	// Set it halfway through the page, unless we are at the end of the page, 
	// in which case put only the new node on the new page.  This will ensure 
	// that pages get filled in the case of a monotonically increasing key. 
	// Make sure that the original page has room, in case the END_BUCKET marker 
	// is now longer because it is pointing at the new node.
	//
	// Note! : newBucket contains still old jump nodes and info.
	SLONG prefix_total = 0;
	UCHAR *splitpoint = NULL;
	USHORT jumpersSplitSize = 0;
	IndexNode node;
	if (useJumpInfo && splitJumpNodeIndex) {
		// Get pointer after new inserted node.
		splitpoint = BTreeNode::readNode(&node, newNode.nodePointer, flags, leafPage);
		if (endOfPage && ((splitpoint + jumpersNewSize - jumpersOriginalSize) <= 
			(UCHAR*)newBucket + dbb->dbb_page_size))
		{
			// Copy data from inserted key and this key will we the END_BUCKET marker
			// as the first key on the next page.
			p = key->key_data;
			UCHAR* q = new_key->key_data;
			l = new_key->key_length = key->key_length;
			memcpy(q, p, l);
			prefix_total = newBucket->btr_prefix_total - beforeInsertNode.prefix;
			splitJumpNodeIndex = 0;
		}
		else {
			jumpersNewSize = 0;

			// splitJumpNodeIndex should always be 1 or higher
			if (splitJumpNodeIndex < 1) {
				BUGCHECK(205);	// msg 205 index bucket overfilled
			}

			// First get prefix data from jump node.
			USHORT index = 1;
			IndexJumpNode* jn;
			IndexJumpNode* walkJumpNode = jumpNodes->begin();
			int i;
			for (i = 0; i < jumpNodes->getCount(); i++, index++) {
				UCHAR* q = new_key->key_data + walkJumpNode[i].prefix;
				memcpy(q, walkJumpNode[i].data, walkJumpNode[i].length);
				if (index == splitJumpNodeIndex) {
					jn = &walkJumpNode[i];
					break;
				}
			}

			// Get data from node.
			splitpoint = (UCHAR*)newBucket + jn->offset;
			splitpoint = BTreeNode::readNode(&node, splitpoint, flags, leafPage);
			UCHAR* q = new_key->key_data + node.prefix;
			memcpy(q, node.data, node.length);
			new_key->key_length = node.prefix + node.length;
			prefix_total = newPrefixTotalBySplit;

			// Rebuild first jumpnode on splitpage
			index = 1;
			walkJumpNode = jumpNodes->begin();
			for (i = 0; i < jumpNodes->getCount(); i++, index++) {
				if (index > splitJumpNodeIndex) {
					const USHORT length = walkJumpNode[i].prefix + walkJumpNode[i].length;
					UCHAR *newData = FB_NEW(*tdbb->tdbb_default) UCHAR[length];
					memcpy(newData, new_key->key_data, walkJumpNode[i].prefix);
					memcpy(newData + walkJumpNode[i].prefix, walkJumpNode[i].data, 
						walkJumpNode[i].length);
					if (walkJumpNode[i].data) {
						delete walkJumpNode[i].data;
					}
					walkJumpNode[i].prefix = 0;
					walkJumpNode[i].length = length;
					walkJumpNode[i].data = newData;
					break;
				}
			}

			// Initalize new offsets for original page and split page.
			index = 1;
			walkJumpNode = jumpNodes->begin();
			for (i = 0; i < jumpNodes->getCount(); i++, index++) {
				// The jump node where the split is done isn't included anymore!
				if (index < splitJumpNodeIndex) {
					jumpersNewSize += BTreeNode::getJumpNodeSize(&walkJumpNode[i], flags);
				}
				else if (index > splitJumpNodeIndex) {
					jumpersSplitSize += BTreeNode::getJumpNodeSize(&walkJumpNode[i], flags);
				}
			}
		}
	}
	else {
		const UCHAR* midpoint = NULL;
		splitpoint = BTreeNode::readNode(&newNode, newNode.nodePointer, flags, leafPage);
		if (endOfPage && ((UCHAR*) splitpoint <= (UCHAR*)newBucket + dbb->dbb_page_size))
		{
			midpoint = splitpoint;
		}
		else {
			midpoint = (UCHAR*)newBucket + ((dbb->dbb_page_size - 
				(BTreeNode::getPointerFirstNode(newBucket) - (UCHAR*)newBucket)) / 2);
		}
		// Start from the begin of the nodes
		splitpoint = BTreeNode::getPointerFirstNode(newBucket);
		// Copy the bucket up to the midpoint, restructing the full midpoint key
		while (splitpoint < midpoint) {
			splitpoint = BTreeNode::readNode(&node, splitpoint, flags, leafPage);
			prefix_total += node.prefix;
			UCHAR* q = new_key->key_data + node.prefix;
			new_key->key_length = node.prefix + node.length;
			memcpy(q, node.data, node.length);
		}
	}

	// Allocate and format the overflow page
	WIN split_window(-1);
	btree_page* split = (btree_page*) DPM_allocate(tdbb, &split_window);

	// if we're a pointer page, make sure the child page is written first
	if (!leafPage) {
		if (newNode.nodePointer < splitpoint) {
			CCH_precedence(tdbb, window, insertion->iib_number);
		}
		else {
			CCH_precedence(tdbb, &split_window, insertion->iib_number);
		}
	}

	// format the new page to look like the old page
	SLONG right_sibling = bucket->btr_sibling;
	split->btr_header.pag_type = bucket->btr_header.pag_type;
	split->btr_relation = bucket->btr_relation;
	split->btr_id = bucket->btr_id;
	split->btr_level = bucket->btr_level;
	split->btr_sibling = right_sibling;
	split->btr_left_sibling = window->win_page;
	split->btr_header.pag_flags |= (flags & BTR_FLAG_COPY_MASK);

	// Format the first node on the overflow page
	newNode.prefix = 0;
	newNode.pageNumber = node.pageNumber;
	newNode.recordNumber = node.recordNumber;
	// Return first record number on split page to caller. 
	*new_record_number = newNode.recordNumber;
	newNode.length = new_key->key_length;
	newNode.data = new_key->key_data;
	USHORT firstSplitNodeSize = BTreeNode::getNodeSize(&newNode, flags, leafPage);

	// Format the first node on the overflow page
	if (useJumpInfo) {
		IndexJumpInfo splitJumpInfo;
		splitJumpInfo.firstNodeOffset = headerSize + jumpersSplitSize;
		splitJumpInfo.jumpAreaSize = jumpInfo.jumpAreaSize;
		splitJumpInfo.keyLength = jumpInfo.keyLength;
		if (splitJumpNodeIndex > 0) {
			splitJumpInfo.jumpers = jumpNodes->getCount() - splitJumpNodeIndex;
		}
		else {
			splitJumpInfo.jumpers = 0;
		}
		pointer = BTreeNode::writeJumpInfo(split, &splitJumpInfo);
		if (splitJumpNodeIndex > 0) {
			// Write jump nodes to split page.
			USHORT index = 1;
			// Calculate size that's between header and splitpoint.
			USHORT splitOffset = (splitpoint - (UCHAR*)newBucket);
			IndexJumpNode* walkJumpNode = jumpNodes->begin();
			for (int i = 0; i < jumpNodes->getCount(); i++, index++) {
				if (index > splitJumpNodeIndex) {
					// Update offset to correct position.
					walkJumpNode[i].offset = walkJumpNode[i].offset - splitOffset + 
						splitJumpInfo.firstNodeOffset + firstSplitNodeSize;
					pointer = BTreeNode::writeJumpNode(&walkJumpNode[i], pointer, flags);
				}
			}
		}
		pointer = (UCHAR*)split + splitJumpInfo.firstNodeOffset;
	}
	else {
		pointer = BTreeNode::getPointerFirstNode(split);
	}

	pointer = BTreeNode::writeNode(&newNode, pointer, flags, leafPage);

	// Copy down the remaining data from scratch page.
	l = newBucket->btr_length - (splitpoint - (UCHAR*)newBucket);
	memcpy(pointer, splitpoint, l);
	split->btr_length = ((pointer + l) - (UCHAR*)split);

	// the sum of the prefixes on the split page is the previous total minus
	// the prefixes found on the original page; the sum of the prefixes on the 
	// original page must exclude the split node
	split->btr_prefix_total = newBucket->btr_prefix_total - prefix_total;
	SLONG split_page = split_window.win_page;

	CCH_RELEASE(tdbb, &split_window);
	CCH_precedence(tdbb, window, split_window.win_page);
	CCH_mark_must_write(tdbb, window);

	// The split bucket is still residing in the scratch page. Copy it
	// back to the original buffer.  After cleaning up the last node,
	// we're done!

	// mark the end of the page; note that the end_bucket marker must 
	// contain info about the first node on the next page. So we don't
	// overwrite the existing data.
	BTreeNode::setEndBucket(&node, leafPage);
	pointer = BTreeNode::writeNode(&node, node.nodePointer, flags, leafPage, false);
	newBucket->btr_length = pointer - (UCHAR*)newBucket;

	if (useJumpInfo) {
		// Write jump information.
		jumpInfo.firstNodeOffset = headerSize + jumpersNewSize;
		if (splitJumpNodeIndex > 0) {
			jumpInfo.jumpers = splitJumpNodeIndex - 1;
		}
		else {
			jumpInfo.jumpers = jumpNodes->getCount();
		}
		pointer = BTreeNode::writeJumpInfo(bucket, &jumpInfo);

		// Write jump nodes.
		USHORT index = 1;
		IndexJumpNode* walkJumpNode = jumpNodes->begin();
		for (int i = 0; i < jumpNodes->getCount(); i++, index++) {
			if (index <= jumpInfo.jumpers) {
				// Update offset to correct position.
				walkJumpNode[i].offset = walkJumpNode[i].offset + 
					jumpersNewSize - jumpersOriginalSize;
				pointer = BTreeNode::writeJumpNode(&walkJumpNode[i], pointer, flags);
			}
		}
		pointer = (UCHAR*)bucket + jumpInfo.firstNodeOffset;

		memcpy(pointer, (UCHAR*)newBucket + headerSize + jumpersOriginalSize, 
			newBucket->btr_length - (headerSize + jumpersOriginalSize));
		bucket->btr_length = newBucket->btr_length + jumpersNewSize - jumpersOriginalSize;

		if (fragmentedOffset) {
			IndexJumpNode* walkJumpNode = jumpNodes->begin();
			for (int i = 0; i < jumpNodes->getCount(); i++, index++) {
				if (walkJumpNode[i].data) {
					delete walkJumpNode[i].data;
				}
			}
		}
	}
	else {
		memcpy(window->win_buffer, newBucket, newBucket->btr_length);
	}

	// Update page information.
	bucket->btr_sibling = split_window.win_page;
	bucket->btr_prefix_total = prefix_total;
	// mark the bucket as non garbage-collectable until we can propagate
	// the split page up to the parent; otherwise its possible that the 
	// split page we just created will be lost.
	bucket->btr_header.pag_flags |= btr_dont_gc;

	if (original_page) {
		*original_page = window->win_page;
	}

	// now we need to go to the right sibling page and update its 
	// left sibling pointer to point to the newly split page
	if (right_sibling) {
		bucket = (btree_page*) CCH_HANDOFF(tdbb, window, right_sibling, LCK_write, pag_index);
		CCH_MARK(tdbb, window);
		bucket->btr_left_sibling = split_window.win_page;
	}
	CCH_RELEASE(tdbb, window);

	// return the page number of the right sibling page
	if (sibling_page) {
		*sibling_page = right_sibling;
	}

	jumpNodes->clear();
	delete jumpNodes;

	return split_page;
}


static INT64_KEY make_int64_key(SINT64 q, SSHORT scale)
{
/**************************************
 *
 *	m a k e _ i n t 6 4 _ k e y
 *
 **************************************
 *
 * Functional description
 *	Make an Index key for a 64-bit Integer value.
 *
 **************************************/

	// Following structure declared above in the modules global section
	//
	// static const struct {
	//     UINT64 limit;		--- if abs(q) is >= this, ...
	//     SINT64 factor;		--- then multiply by this, ...
	//     SSHORT scale_change;	--- and add this to the scale.
	// } int64_scale_control[];
	//

	// Before converting the scaled int64 to a double, multiply it by the
	// largest power of 10 which will NOT cause an overflow, and adjust
	// the scale accordingly.  This ensures that two different
	// representations of the same value, entered at times when the
	// declared scale of the column was different, actually wind up
	// being mapped to the same key.
 
	int n = 0;
	UINT64 uq = (UINT64) ((q >= 0) ? q : -q);	// absolute value
	while (uq < int64_scale_control[n].limit) {
		n++;
	}
	q *= int64_scale_control[n].factor;
	scale -= int64_scale_control[n].scale_change;

	INT64_KEY key;
	key.d_part = ((double) (q / 10000)) / powerof10(scale);
	key.s_part = (SSHORT) (q % 10000);

	return key;
}


#ifdef DEBUG_INDEXKEY
static void print_int64_key(SINT64 value, SSHORT scale, INT64_KEY key)
{
/**************************************
 *
 *	p r i n t _ i n t 6 4 _ k e y
 *
 **************************************
 *
 * Functional description
 *	Debugging function to print a key created out of an int64 
 *	quantify.
 *
 **************************************/
	ib_fprintf(ib_stderr,
			   "%20" QUADFORMAT
			   "d  %4d  %.15e  %6d  ", value, scale, key.d_part, key.s_part);

	const UCHAR* p = (UCHAR*) &key;
	for (int n = 10; n--; n > 0) {
		ib_fprintf(ib_stderr, "%02x ", *p++);
	}

	ib_fprintf(ib_stderr, "\n");
	return;
}
#endif /* DEBUG_INDEXKEY */


static CONTENTS remove_node(TDBB tdbb, IIB * insertion, WIN * window)
{
/**************************************
 *
 *	r e m o v e _ n o d e
 *
 **************************************
 *
 * Functional description
 *	Remove an index node from a b-tree, 
 * 	recursing down through the levels in case 
 * 	we need to garbage collect pages.
 *
 **************************************/

	SET_TDBB(tdbb);
	DBB dbb = tdbb->tdbb_database;
	IDX* idx = insertion->iib_descriptor;
	btree_page* page = (btree_page*) window->win_buffer;

	// if we are on a leaf page, remove the leaf node
	if (page->btr_level == 0) {
		return remove_leaf_node(tdbb, insertion, window);
	}

	while (true) {
		const SLONG number =
			find_page(page, insertion->iib_key, idx->idx_flags, insertion->iib_number);

		// we should always find the node, but let's make sure
		if (number == END_LEVEL) {
			CCH_RELEASE(tdbb, window);
#ifdef DEBUG_BTR
			CORRUPT(204);	// msg 204 index inconsistent
#endif
			return contents_above_threshold;
		}

		// recurse to the next level down; if we are about to fetch a 
		// level 0 page, make sure we fetch it for write
		if (number != END_BUCKET) {

			// handoff down to the next level, retaining the parent page number
			const SLONG parent_number = window->win_page;
			page = (btree_page*) CCH_HANDOFF(tdbb, window, number, (SSHORT)
				((page->btr_level == 1) ? LCK_write : LCK_read), pag_index);

			// if the removed node caused the page to go below the garbage collection 
			// threshold, and the database was created by a version of the engine greater 
			// than 8.2, then we can garbage-collect the page
			const CONTENTS result = remove_node(tdbb, insertion, window);

			if ((result != contents_above_threshold)
				&& (dbb->dbb_ods_version >= ODS_VERSION9))
			{
				return garbage_collect(tdbb, window, parent_number);
			}

			if (window->win_bdb) {
				CCH_RELEASE(tdbb, window);
			}
			return contents_above_threshold;
		}

		// we've hit end of bucket, so go to the sibling looking for the node
		page = (btree_page*) CCH_HANDOFF(tdbb, window, page->btr_sibling,
			LCK_read, pag_index);
	}

	// NOTREACHED
	return contents_empty;	// superfluous return to shut lint up
}


static CONTENTS remove_leaf_node(TDBB tdbb, IIB * insertion, WIN * window)
{
/**************************************
 *
 *	r e m o v e _ l e a f _ n o d e
 *
 **************************************
 *
 * Functional description
 *	Remove an index node from the leaf level.
 *
 **************************************/
	SET_TDBB(tdbb);
	btree_page* page = (btree_page*) window->win_buffer;
	KEY *key = insertion->iib_key;

	// Look for the first node with the value to be removed.
	UCHAR *pointer;
	USHORT prefix;
	while (!(pointer = find_node_start_point(page, key, 0, &prefix,
			insertion->iib_descriptor->idx_flags & idx_descending, 
			false, false, insertion->iib_number)))
	{
		page = (btree_page*) CCH_HANDOFF(tdbb, window, page->btr_sibling, LCK_write, pag_index);
	}

	// Make sure first node looks ok
	const SCHAR flags = page->btr_header.pag_flags;
	IndexNode node;
	pointer = BTreeNode::readNode(&node, pointer, flags, true);
	if (prefix > node.prefix
		|| key->key_length != node.length + node.prefix)
	{
#ifdef DEBUG_BTR
		CCH_RELEASE(tdbb, window);
		CORRUPT(204);	// msg 204 index inconsistent
#endif
		return contents_above_threshold;
	}

	// check to make sure the node has the same value
	UCHAR* p = node.data;
	const UCHAR* q = key->key_data + node.prefix;
	USHORT l = node.length;
	if (l) {
		do {
			if (*p++ != *q++) {
#ifdef DEBUG_BTR
				CCH_RELEASE(tdbb, window);
				CORRUPT(204);	// msg 204 index inconsistent
#endif
				return contents_above_threshold;
			}
		} while (--l);
	}

	// *****************************************************
	// AB: This becomes a very expensive task if there are
	// many duplicates inside the index (non-unique index)!
	// Therefore we also need to add the record-number to the
	// non-leaf pages and sort duplicates by record-number.
	// *****************************************************

	// now look through the duplicate nodes to find the one 
	// with matching record number
	ULONG pages = 0;
	while (true) {

		// if we find the right one, quit
		if (insertion->iib_number == node.recordNumber) {
			break;
		}

		if (BTreeNode::isEndLevel(&node, true)) {
#ifdef DEBUG_BTR
			CCH_RELEASE(tdbb, window);
			CORRUPT(204);	// msg 204 index inconsistent
#endif
			return contents_above_threshold;
		}

		// go to the next node and check that it is a duplicate
		if (!BTreeNode::isEndBucket(&node, true)) {
			pointer = BTreeNode::readNode(&node, pointer, flags, true);
			if (node.length != 0 || node.prefix != key->key_length)
			{
#ifdef DEBUG_BTR
				CCH_RELEASE(tdbb, window);
				CORRUPT(204);	// msg 204 index inconsistent
#endif
				return contents_above_threshold;
			}
			continue;
		}

		// if we hit the end of bucket, go to the right sibling page, 
		// and check that the first node is a duplicate
		++pages;
		page = (btree_page*) CCH_HANDOFF(tdbb, window, page->btr_sibling, LCK_write, pag_index);

		pointer = BTreeNode::getPointerFirstNode(page);
		pointer = BTreeNode::readNode(&node, pointer, flags, true);
		l = node.length;
		if (l != key->key_length) {
#ifdef DEBUG_BTR
			CCH_RELEASE(tdbb, window);
			CORRUPT(204);		// msg 204 index inconsistent
#endif
			return contents_above_threshold;
		}

		if (l) {
			p = node.data;
			q = key->key_data;
			do {
				if (*p++ != *q++) {
#ifdef DEBUG_BTR
					CCH_RELEASE(tdbb, window);
					CORRUPT(204);	// msg 204 index inconsistent
#endif
					return contents_above_threshold;
				}
			} while (--l);
		}

#ifdef SUPERSERVER
		// Until deletion of duplicate nodes becomes efficient, limit
		// leaf level traversal by rescheduling.
		if (--tdbb->tdbb_quantum < 0 && !tdbb->tdbb_inhibit) {
			if (JRD_reschedule(tdbb, 0, false)) {
				CCH_RELEASE(tdbb, window);
				ERR_punt();
			}
		}
#endif
	}

	// If we've needed to search thru a significant number of pages, warn the
	// cache manager in case we come back this way
	if (pages > 75) {
		CCH_expand(tdbb, pages + 25);
	}

	return delete_node(tdbb, window, node.nodePointer);
}


static bool scan(TDBB tdbb, UCHAR *pointer, SBM *bitmap, USHORT to_segment,
				 USHORT prefix, KEY *key, USHORT flag, SCHAR page_flags)
{
/**************************************
 *
 *	s c a n
 *
 **************************************
 *
 * Functional description
 *	Do an index scan.  
 *  If we run over the bucket, return true.
 *  If we're completely done (passed END_LEVEL),
 *  return false.
 *
 **************************************/

	SET_TDBB(tdbb);

	// if the search key is flagged to indicate a multi-segment index
	// stuff the key to the stuff boundary
	USHORT count;
	if ((flag & irb_partial) && (flag & irb_equality)
		&& !(flag & irb_starting) && !(flag & irb_descending)) 
	{
		count = STUFF_COUNT -
			((key->key_length + STUFF_COUNT) % (STUFF_COUNT + 1));
		USHORT i;
		for (i = 0; i < count; i++) {
			key->key_data[key->key_length + i] = 0;
		}
		count += key->key_length;
	}
	else {
		count = key->key_length;
	}

	const UCHAR* const end_key = key->key_data + count;
	count -= key->key_length;

	// reset irb_equality flag passed for optimization
	flag &= ~irb_equality;

	bool descending = (flag & irb_descending);
	bool done = false;
	UCHAR* p = NULL;

	if (page_flags & btr_large_keys) {
		IndexNode node;
		pointer = BTreeNode::readNode(&node, pointer, page_flags, true);
		while (true) {

			if (BTreeNode::isEndLevel(&node, true)) {
				return false;
			}

			if (descending && 
				((done && (node.prefix < prefix)) || 
				 (node.prefix + node.length < key->key_length))) 
			{
				return false;
			}

			if (key->key_length == 0) {
				// Scanning for NULL keys
				if (to_segment == 0) {
					// All segments are expected to be NULL
					if (node.prefix + node.length > 0) {
						return false;
					}
				}
				else {
					// Up to (partial/starting) to_segment is expected to be NULL.
					if (node.length && (node.prefix == 0)) {
						const UCHAR* q = node.data;
						if (*q > to_segment) {
							return false;
						}
					}
				}
			}
			else if (node.prefix <= prefix) {
				prefix = node.prefix;
				p = key->key_data + prefix;
				const UCHAR* q = node.data;
				for (USHORT l = node.length; l; --l, prefix++) {
					if (p >= end_key) {
						if (flag) {
							break;
						}
						else {
							return false;
						}
					}
					if (p > (end_key - count)) {
						if (*p++ == *q++) {
							break;
						}
						else {
							continue;
						}
					}
					if (*p < *q) {
						return false;
					}
					if (*p++ > *q++) {
						break;
					}
				}
				if (p >= end_key) {
					done = true;
				}
			}

			if (BTreeNode::isEndBucket(&node, true)) {
				// Our caller will fetch the next page
				return true;
			}

			if ((flag & irb_starting) || !count) {
				SBM_set(tdbb, bitmap, node.recordNumber);
			}
			else if (p > (end_key - count)) {
				SBM_set(tdbb, bitmap, node.recordNumber);
			}

			pointer = BTreeNode::readNode(&node, pointer, page_flags, true);
		}
	}
	else {
		SLONG number;
		btn* node = (btn*)pointer;
		while (true) {

			number = get_long(node->btn_number);

			if (number == END_LEVEL) {
				return false;
			}

			if (descending && 
				((done && (node->btn_prefix < prefix)) || 
				 (node->btn_prefix + node->btn_length < key->key_length))) 
			{
				return false;
			}

			if (key->key_length == 0) {
				// Scanning for NULL keys
				if (to_segment == 0) {
					// All segments are expected to be NULL
					if (node->btn_prefix + node->btn_length > 0) {
						return false;
					}
				}
				else {
					// Up to (partial/starting) to_segment is expected to be NULL.
					if (node->btn_length && (node->btn_prefix == 0)) {
						const UCHAR* q = node->btn_data;
						if (*q > to_segment) {
							return false;
						}
					}
				}
			}
			else if (node->btn_prefix <= prefix) {
				prefix = node->btn_prefix;
				p = key->key_data + prefix;
				const UCHAR* q = node->btn_data;
				for (USHORT l = node->btn_length; l; --l, prefix++) {
					if (p >= end_key) {
						if (flag) {
							break;
						}
						else {
							return false;
						}
					}
					if (p > (end_key - count)) {
						if (*p++ == *q++) {
							break;
						}
						else {
							continue;
						}
					}
					if (*p < *q) {
						return false;
					}
					if (*p++ > *q++) {
						break;
					}
				}
				if (p >= end_key) {
					done = true;
				}
			}

			if (number == END_BUCKET) {
				// Our caller will fetch the next page
				return true;
			}

			if ((flag & irb_starting) || !count) {
				SBM_set(tdbb, bitmap, number);
			}
			else if (p > (end_key - count)) {
				SBM_set(tdbb, bitmap, number);
			}

			node = NEXT_NODE(node);
		}
	}

	// NOTREACHED
	return false;	// superfluous return to shut lint up
}


void update_selectivity(index_root_page* root, USHORT id, const SelectivityList& selectivity)
{
/**************************************
 *
 *	u p d a t e _ s e l e c t i v i t y
 *
 **************************************
 *
 * Functional description
 *	Update selectivity on the index root page.
 *
 **************************************/
	DBB dbb = GET_DBB;

	index_root_page::irt_repeat* irt_desc = &root->irt_rpt[id];
	const USHORT idx_count = irt_desc->irt_keys;
	fb_assert(selectivity.getCount() == idx_count);

	if (dbb->dbb_ods_version >= ODS_VERSION11) {
		// dimitr: per-segment selectivities exist only for ODS11 and above
		irtd* key_descriptor = (irtd*) ((UCHAR*) root + irt_desc->irt_desc);
		for (int i = 0; i < idx_count; i++, key_descriptor++)
			key_descriptor->irtd_selectivity = selectivity[i];
	}
	irt_desc->irt_stuff.irt_selectivity = selectivity.back();
}

