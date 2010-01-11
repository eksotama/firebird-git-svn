/*
 *	PROGRAM:	JRD Access Method
 *	MODULE:		opt.cpp
 *	DESCRIPTION:	Optimizer / record selection expression compiler
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
 * 2002.10.12: Nickolay Samofatov: Fixed problems with wrong results produced by
 *            outer joins
 * 2001.07.28: John Bellardo: Added code to handle rse_skip nodes.
 * 2001.07.17 Claudio Valderrama: Stop crash with indices and recursive calls
 *            of OPT_compile: indicator csb_indices set to zero after used memory is
 *            returned to the free pool.
 * 2001.02.15: Claudio Valderrama: Don't obfuscate the plan output if a selectable
 *             stored procedure doesn't access tables, views or other procedures directly.
 * 2002.10.29 Sean Leyne - Removed obsolete "Netware" port
 * 2002.10.30: Arno Brinkman: Changes made to gen_retrieval, OPT_compile and make_inversion.
 *             Procedure sort_indices added. The changes in gen_retrieval are that now
 *             an index with high field-count has priority to build an index from.
 *             Procedure make_inversion is changed so that it not pick every index
 *             that comes away, this was slow performance with bad selectivity indices
 *             which most are foreign_keys with a reference to a few records.
 * 2002.11.01: Arno Brinkman: Added match_indices for better support of OR handling
 *             in INNER JOIN (gen_join) statements.
 * 2002.12.15: Arno Brinkman: Added find_used_streams, so that inside opt_compile all the
 *             streams are marked active. This causes that more indices can be used for
 *             a retrieval. With this change BUG SF #219525 is solved too.
 */

#include "firebird.h"
#include "../jrd/common.h"
#include <stdio.h>
#include <string.h>
#include "../jrd/ibase.h"
#include "../jrd/jrd.h"
#include "../jrd/align.h"
#include "../jrd/val.h"
#include "../jrd/req.h"
#include "../jrd/exe.h"
#include "../jrd/lls.h"
#include "../jrd/ods.h"
#include "../jrd/btr.h"
#include "../jrd/sort.h"
#include "../jrd/rse.h"
#include "../jrd/intl.h"
#include "../jrd/gdsassert.h"
#include "../jrd/btr_proto.h"
#include "../jrd/cch_proto.h"
#include "../jrd/cmp_proto.h"
#include "../jrd/dpm_proto.h"
#include "../jrd/dsc_proto.h"
#include "../jrd/err_proto.h"
#include "../jrd/ext_proto.h"
#include "../jrd/evl_proto.h"
#include "../jrd/intl_proto.h"

#include "../jrd/lck_proto.h"
#include "../jrd/met_proto.h"
#include "../jrd/mov_proto.h"
#include "../jrd/opt_proto.h"
#include "../jrd/par_proto.h"
#include "../jrd/gds_proto.h"
#include "../jrd/dbg_proto.h"
#include "../jrd/DataTypeUtil.h"
#include "../jrd/VirtualTable.h"
#include "../common/classes/array.h"
#include "../common/classes/objects_array.h"
#include "../jrd/recsrc/RecordSource.h"
#include "../jrd/recsrc/Cursor.h"

#include "../jrd/Optimizer.h"

using namespace Jrd;
using namespace Firebird;

#ifdef DEV_BUILD
#define OPT_DEBUG
#endif

static bool augment_stack(jrd_nod*, NodeStack&);
static void check_indices(const CompilerScratch::csb_repeat*);
static void check_sorts(RecordSelExpr*);
static void class_mask(USHORT, jrd_nod**, ULONG *);
static jrd_nod* compose(jrd_nod**, jrd_nod*, nod_t);
static void compute_dependencies(const jrd_nod*, ULONG*);
static void compute_dbkey_streams(const CompilerScratch*, const jrd_nod*, UCHAR*);
static void compute_rse_streams(const CompilerScratch*, const RecordSelExpr*, UCHAR*);
static bool check_for_nod_from(const jrd_nod*);
static SLONG decompose(thread_db*, jrd_nod*, NodeStack&, CompilerScratch*);
static USHORT distribute_equalities(NodeStack&, CompilerScratch*, USHORT);
static bool expression_possible_unknown(const jrd_nod*);
static bool expression_contains_stream(CompilerScratch*, const jrd_nod*, UCHAR, bool*);
static void find_index_relationship_streams(thread_db*, OptimizerBlk*, const UCHAR*, UCHAR*, UCHAR*);
static jrd_nod* find_dbkey(jrd_nod*, USHORT, SLONG*);
static void form_rivers(thread_db*, OptimizerBlk*, const UCHAR*, RiverStack&, jrd_nod**, jrd_nod*);
static bool form_river(thread_db*, OptimizerBlk*, USHORT, const UCHAR*, UCHAR*, RiverStack&, jrd_nod**);
static RecordSource* gen_aggregate(thread_db*, OptimizerBlk*, jrd_nod*, NodeStack*, UCHAR);
static void gen_deliver_unmapped(thread_db*, NodeStack*, jrd_nod*, NodeStack*, UCHAR);
static void gen_join(thread_db*, OptimizerBlk*, const UCHAR*, RiverStack&, jrd_nod**, jrd_nod*);
static RecordSource* gen_outer(thread_db*, OptimizerBlk*, RecordSelExpr*, RiverStack&, jrd_nod**);
static ProcedureScan* gen_procedure(thread_db*, OptimizerBlk*, jrd_nod*);
static RecordSource* gen_residual_boolean(thread_db*, OptimizerBlk*, RecordSource*);
static RecordSource* gen_retrieval(thread_db*, OptimizerBlk*, SSHORT, jrd_nod**, bool, bool, jrd_nod**);
static SortedStream* gen_sort(thread_db*, OptimizerBlk*, const UCHAR*, const UCHAR*,
					  RecordSource*, jrd_nod*, bool);
static bool gen_sort_merge(thread_db*, OptimizerBlk*, RiverStack&);
static RecordSource* gen_union(thread_db*, OptimizerBlk*, jrd_nod*, UCHAR *, USHORT, NodeStack*, UCHAR);
static void get_expression_streams(const jrd_nod*, Firebird::SortedArray<int>&);
static jrd_nod* get_unmapped_node(thread_db*, jrd_nod*, jrd_nod*, UCHAR, bool);
static RecordSource* make_cross(thread_db*, OptimizerBlk*, RiverStack&);
static jrd_nod* make_dbkey(thread_db*, OptimizerBlk*, jrd_nod*, USHORT);
static jrd_nod* make_inference_node(CompilerScratch*, jrd_nod*, jrd_nod*, jrd_nod*);
static bool map_equal(const jrd_nod*, const jrd_nod*, const jrd_nod*);
static void mark_indices(CompilerScratch::csb_repeat*, SSHORT);
static bool node_equality(const jrd_nod*, const jrd_nod*);
static jrd_nod* optimize_like(thread_db*, CompilerScratch*, jrd_nod*);
static USHORT river_count(USHORT, jrd_nod**);
static bool river_reference(const River*, const jrd_nod*, bool* field_found = NULL);
static bool search_stack(const jrd_nod*, const NodeStack&);
static void set_active(OptimizerBlk*, const River*);
static void set_direction(const jrd_nod*, jrd_nod*);
static void set_inactive(OptimizerBlk*, const River*);
static void set_made_river(OptimizerBlk*, const River*);
static void set_position(const jrd_nod*, jrd_nod*, const jrd_nod*);
static void set_rse_inactive(CompilerScratch*, const RecordSelExpr*);
static void sort_indices_by_selectivity(CompilerScratch::csb_repeat*);


// macro definitions

#ifdef OPT_DEBUG
const int DEBUG_PUNT			= 5;
const int DEBUG_RELATIONSHIPS	= 4;
const int DEBUG_ALL				= 3;
const int DEBUG_CANDIDATE		= 2;
const int DEBUG_BEST			= 1;
const int DEBUG_NONE			= 0;

FILE *opt_debug_file = 0;
static int opt_debug_flag = DEBUG_NONE;
#endif

inline void SET_DEP_BIT(ULONG* array, const SLONG bit)
{
	array[bit / 32] |= (1L << (bit % 32));
}

inline void CLEAR_DEP_BIT(ULONG* array, const SLONG bit)
{
	array[bit / 32] &= ~(1L << (bit % 32));
}

inline bool TEST_DEP_BIT(const ULONG* array, const ULONG bit)
{
	return (array[bit / 32] & (1L << (bit % 32))) != 0;
}

inline bool TEST_DEP_ARRAYS(const ULONG* ar1, const ULONG* ar2)
{	return (ar1[0] & ar2[0]) || (ar1[1] & ar2[1]) || (ar1[2] & ar2[2]) || (ar1[3] & ar2[3]) ||
		   (ar1[4] & ar2[4]) || (ar1[5] & ar2[5]) || (ar1[6] & ar2[6]) || (ar1[7] & ar2[7]);
}

// some arbitrary fudge factors for calculating costs, etc.--
// these could probably be better tuned someday

const double ESTIMATED_SELECTIVITY			= 0.01;
const int INVERSE_ESTIMATE					= 10;
const double INDEX_COST						= 30.0;
const int CACHE_PAGES_PER_STREAM			= 15;
const int SELECTIVITY_THRESHOLD_FACTOR		= 10;
const int OR_SELECTIVITY_THRESHOLD_FACTOR	= 2000;
const FB_UINT64 LOWEST_PRIORITY_LEVEL		= 0;

// enumeration of sort datatypes

static const UCHAR sort_dtypes[] =
{
	0,							// dtype_unknown
	SKD_text,					// dtype_text
	SKD_cstring,				// dtype_cstring
	SKD_varying,				// dtype_varying
	0,
	0,
	0,							// dtype_packed
	0,							// dtype_byte
	SKD_short,					// dtype_short
	SKD_long,					// dtype_long
	SKD_quad,					// dtype_quad
	SKD_float,					// dtype_real
	SKD_double,					// dtype_double
	SKD_double,					// dtype_d_float
	SKD_sql_date,				// dtype_sql_date
	SKD_sql_time,				// dtype_sql_time
	SKD_timestamp2,				// dtype_timestamp
	SKD_quad,					// dtype_blob
	0,							// dtype_array
	SKD_int64,					// dtype_int64
	SKD_text					// dtype_dbkey - use text sort for backward compatibility
};

typedef UCHAR stream_array_t[MAX_STREAMS + 1];


bool OPT_access_path(const jrd_req* request, UCHAR* buffer, SLONG buffer_length, ULONG* return_length)
{
/**************************************
 *
 *	O P T _ a c c e s s _ p a t h
 *
 **************************************
 *
 * Functional description
 *	Returns a formatted access path for all
 *	RecordSelExpr's in the specified request.
 *
 **************************************/
	DEV_BLKCHK(request, type_req);

	thread_db* tdbb = JRD_get_thread_data();

	if (!buffer || buffer_length < 0 || !return_length)
		return false;

	// loop through all RSEs in the request, and describe the rsb tree for that rsb

	UCharBuffer infoBuffer;

	for (size_t i = 0; i < request->req_fors.getCount(); i++)
	{
		request->req_fors[i]->getAccessPath()->dump(tdbb, infoBuffer);
	}

	const size_t length = infoBuffer.getCount();

	if (length > static_cast<ULONG>(buffer_length))
	{
		*return_length = 0;
		return false;
	}

	*return_length = (ULONG) length;
	memcpy(buffer, infoBuffer.begin(), length);
	return true;
}


RecordSource* OPT_compile(thread_db*		tdbb,
						  CompilerScratch*	csb,
						  RecordSelExpr*	rse,
						  NodeStack*		parent_stack)
{
/**************************************
 *
 *	O P T _ c o m p i l e
 *
 **************************************
 *
 * Functional description
 *	Compile and optimize a record selection expression into a
 *	set of record source blocks (rsb's).
 *
 **************************************/
	stream_array_t streams, beds, local_streams, key_streams;
	StreamsArray outerStreams, subStreams;

	DEV_BLKCHK(csb, type_csb);
	DEV_BLKCHK(rse, type_nod);

	SET_TDBB(tdbb);
	//Database* dbb = tdbb->getDatabase();

#ifdef OPT_DEBUG
	if (opt_debug_flag != DEBUG_NONE && !opt_debug_file)
		opt_debug_file = fopen("opt_debug.out", "w");
#endif


	// If there is a boolean, there is some work to be done.  First,
	// decompose the boolean into conjunctions.  Then get descriptions
	// of all indices for all relations in the RecordSelExpr.  This will give
	// us the info necessary to allocate a optimizer block big
	// enough to hold this crud.

	// Do not allocate the index_desc struct. Let BTR_all do the job. The allocated
	// memory will then be in csb->csb_rpt[stream].csb_idx_allocation, which
	// gets cleaned up before this function exits.

	OptimizerBlk* opt = FB_NEW(*tdbb->getDefaultPool()) OptimizerBlk(tdbb->getDefaultPool());
	opt->opt_streams.grow(csb->csb_n_stream);
	RecordSource* rsb = 0;

	try {

	opt->opt_csb = csb;

	beds[0] = streams[0] = key_streams[0] = 0;
	NodeStack conjunct_stack;
	RiverStack rivers_stack;
	SLONG conjunct_count = 0;

	check_sorts(rse);
	jrd_nod* sort = rse->rse_sorted;
	jrd_nod* project = rse->rse_projection;
	jrd_nod* aggregate = rse->rse_aggregate;

	// put any additional booleans on the conjunct stack, and see if we
	// can generate additional booleans by associativity--this will help
	// to utilize indices that we might not have noticed
	if (rse->rse_boolean) {
		conjunct_count = decompose(tdbb, rse->rse_boolean, conjunct_stack, csb);
	}

	conjunct_count += distribute_equalities(conjunct_stack, csb, conjunct_count);

	// AB: If we have limit our retrieval with FIRST / SKIP syntax then
	// we may not deliver above conditions (from higher rse's) to this
	// rse, because the results should be consistent.
    if (rse->rse_skip || rse->rse_first) {
		parent_stack = NULL;
	}

	// clear the csb_active flag of all streams in the RecordSelExpr
	set_rse_inactive(csb, rse);

	UCHAR* p = streams + 1;

	// go through the record selection expression generating
	// record source blocks for all streams

	// CVC: I defined this var here because it's assigned inside an if() shortly
	// below but it's used later in the loop always, so I assume the idea is that
	// iterations where nod_type != nod_rse are the ones that set up a new stream.
	// Hope this isn't some kind of logic error.
	SSHORT stream = -1;
	jrd_nod** ptr = rse->rse_relation;
	for (const jrd_nod* const* const end = ptr + rse->rse_count; ptr < end; ptr++)
	{
		jrd_nod* node = *ptr;

		// find the stream number and place it at the end of the beds array
		// (if this is really a stream and not another RecordSelExpr)

		if (node->nod_type != nod_rse)
		{
			stream = (USHORT)(IPTR) node->nod_arg[STREAM_INDEX(node)];
			fb_assert(stream <= MAX_UCHAR);
			fb_assert(beds[0] < MAX_STREAMS && beds[0] < MAX_UCHAR); // debug check
			//if (beds[0] >= MAX_STREAMS) // all builds check
			//	ERR_post(Arg::Gds(isc_too_many_contexts));

			beds[++beds[0]] = (UCHAR) stream;
		}

		// for nodes which are not relations, generate an rsb to
		// represent that work has to be done to retrieve them;
		// find all the substreams involved and compile them as well

		rsb = NULL;
		local_streams[0] = 0;

		switch (node->nod_type)
		{
		case nod_union:
			{
				const SSHORT i = (SSHORT) key_streams[0];
				compute_dbkey_streams(csb, node, key_streams);

				NodeStack::const_iterator stack_end;
				if (parent_stack) {
					stack_end = conjunct_stack.merge(*parent_stack);
				}
				rsb = gen_union(tdbb, opt, node, key_streams + i + 1,
								(USHORT) (key_streams[0] - i), &conjunct_stack, stream);
				if (parent_stack) {
					conjunct_stack.split(stack_end, *parent_stack);
				}

				fb_assert(local_streams[0] < MAX_STREAMS && local_streams[0] < MAX_UCHAR);
				local_streams[++local_streams[0]] = (UCHAR)(IPTR) node->nod_arg[e_uni_stream];
			}
			break;

		case nod_aggregate:
			{
				fb_assert((int)(IPTR) node->nod_arg[e_agg_stream] <= MAX_STREAMS);
				fb_assert((int)(IPTR) node->nod_arg[e_agg_stream] <= MAX_UCHAR);

				NodeStack::const_iterator stack_end;
				if (parent_stack) {
					stack_end = conjunct_stack.merge(*parent_stack);
				}
				rsb = gen_aggregate(tdbb, opt, node, &conjunct_stack, stream);
				if (parent_stack) {
					conjunct_stack.split(stack_end, *parent_stack);
				}

				fb_assert(local_streams[0] < MAX_STREAMS && local_streams[0] < MAX_UCHAR);
				local_streams[++local_streams[0]] = (UCHAR)(IPTR) node->nod_arg[e_agg_stream];
			}
			break;

		case nod_procedure:
			rsb = gen_procedure(tdbb, opt, node);
			fb_assert(local_streams[0] < MAX_STREAMS && local_streams[0] < MAX_UCHAR);
			local_streams[++local_streams[0]] = (UCHAR)(IPTR) node->nod_arg[e_prc_stream];
			break;

		case nod_rse:
			compute_rse_streams(csb, (RecordSelExpr*) node, beds);
			compute_rse_streams(csb, (RecordSelExpr*) node, local_streams);
			compute_dbkey_streams(csb, node, key_streams);
			// pass RecordSelExpr boolean only to inner substreams because join condition
			// should never exclude records from outer substreams
			if (rse->rse_jointype == blr_inner ||
			   (rse->rse_jointype == blr_left && (ptr - rse->rse_relation) == 1) )
			{
				// AB: For an (X LEFT JOIN Y) mark the outer-streams (X) as
				// active because the inner-streams (Y) are always "dependent"
				// on the outer-streams. So that index retrieval nodes could be made.
				// For an INNER JOIN mark previous generated RecordSource's as active.
				if (rse->rse_jointype == blr_left)
				{
					for (StreamsArray::iterator i = outerStreams.begin(); i != outerStreams.end(); ++i)
						csb->csb_rpt[*i].csb_flags |= csb_active;
				}

				//const NodeStack::iterator stackSavepoint(conjunct_stack);
				NodeStack::const_iterator stack_end;
				NodeStack deliverStack;

				if (rse->rse_jointype != blr_inner)
				{
					// Make list of nodes that can be delivered to an outer-stream.
					// In fact these are all nodes except when a IS NULL (nod_missing)
					// comparision is done.
					// Note! Don't forget that this can be burried inside a expression
					// such as "CASE WHEN (FieldX IS NULL) THEN 0 ELSE 1 END = 0"
					NodeStack::iterator stackItem;
					if (parent_stack)
					{
						stackItem = *parent_stack;
					}
					for (; stackItem.hasData(); ++stackItem)
					{
						jrd_nod* deliverNode = stackItem.object();
						if (!expression_possible_unknown(deliverNode)) {
							deliverStack.push(deliverNode);
						}
					}
					stack_end = conjunct_stack.merge(deliverStack);
				}
				else
				{
					if (parent_stack)
					{
						stack_end = conjunct_stack.merge(*parent_stack);
					}
				}

				rsb = OPT_compile(tdbb, csb, (RecordSelExpr*) node, &conjunct_stack);

				if (rse->rse_jointype != blr_inner)
				{
					// Remove previously added parent conjuctions from the stack.
					conjunct_stack.split(stack_end, deliverStack);
				}
				else
				{
					if (parent_stack)
					{
						conjunct_stack.split(stack_end, *parent_stack);
					}
				}

				if (rse->rse_jointype == blr_left)
				{
					for (StreamsArray::iterator i = outerStreams.begin(); i != outerStreams.end(); ++i)
						csb->csb_rpt[*i].csb_flags &= ~csb_active;
				}
			}
			else {
				rsb = OPT_compile(tdbb, csb, (RecordSelExpr*) node, parent_stack);
			}
			break;
		}

		// if an rsb has been generated, we have a non-relation;
		// so it forms a river of its own since it is separately
		// optimized from the streams in this rsb

		if (rsb)
		{
			const SSHORT i = local_streams[0];
			River* river = FB_NEW_RPT(*tdbb->getDefaultPool(), i) River();
			river->riv_count = (UCHAR) i;
			river->riv_rsb = rsb;
			memcpy(river->riv_streams, local_streams + 1, i);
			// AB: Save all inner-part streams
			if (rse->rse_jointype == blr_inner ||
			   (rse->rse_jointype == blr_left && (ptr - rse->rse_relation) == 0))
			{
				rsb->findUsedStreams(subStreams);
				// Save also the outer streams
				if (rse->rse_jointype == blr_left)
					rsb->findUsedStreams(outerStreams);
			}
			set_made_river(opt, river);
			set_inactive(opt, river);
			rivers_stack.push(river);
			continue;
		}

		// we have found a base relation; record its stream
		// number in the streams array as a candidate for
		// merging into a river

		// TMN: Is the intention really to allow streams[0] to overflow?
		// I must assume that is indeed not the intention (not to mention
		// it would make code later on fail), so I added the following fb_assert.
		fb_assert(streams[0] < MAX_STREAMS && streams[0] < MAX_UCHAR);

		++streams[0];
		*p++ = (UCHAR) stream;

		if (rse->rse_jointype == blr_left)
			outerStreams.add(stream);

		// if we have seen any booleans or sort fields, we may be able to
		// use an index to optimize them; retrieve the current format of
		// all indices at this time so we can determine if it's possible
		// AB: if a parent_stack was available and conjunct_count was 0
		// then no indices where retrieved. Added also OR check on
		// parent_stack below. SF BUG # [ 508594 ]

		if (conjunct_count || sort || aggregate || parent_stack)
		{
			jrd_rel* relation = (jrd_rel*) node->nod_arg[e_rel_relation];
			if (relation && !relation->rel_file && !relation->isVirtual())
			{
				csb->csb_rpt[stream].csb_indices =
					BTR_all(tdbb, relation, &csb->csb_rpt[stream].csb_idx, relation->getPages(tdbb));
				sort_indices_by_selectivity(&csb->csb_rpt[stream]);
				mark_indices(&csb->csb_rpt[stream], relation->rel_id);
			}
			else
				csb->csb_rpt[stream].csb_indices = 0;

			const Format* format = CMP_format(tdbb, csb, stream);
			csb->csb_rpt[stream].csb_cardinality = OPT_getRelationCardinality(tdbb, relation, format);
		}
	}

	// this is an attempt to make sure we have a large enough cache to
	// efficiently retrieve this query; make sure the cache has a minimum
	// number of pages for each stream in the RecordSelExpr (the number is just a guess)
	if (streams[0] > 5) {
		CCH_expand(tdbb, (ULONG) (streams[0] * CACHE_PAGES_PER_STREAM));
	}

	// At this point we are ready to start optimizing.
	// We will use the opt block to hold information of
	// a global nature, meaning that it needs to stick
	// around for the rest of the optimization process.

	// Set base-point before the parent/distributed nodes begin.
	const USHORT base_count = (USHORT) conjunct_count;
	opt->opt_base_conjuncts = base_count;

	// AB: Add parent conjunctions to conjunct_stack, keep in mind
	// the outer-streams! For outer streams put missing (IS NULL)
	// conjunctions in the missing_stack.
	//
	// opt_rpt[0..opt_base_conjuncts-1] = defined conjunctions to this stream
	// opt_rpt[0..opt_base_parent_conjuncts-1] = defined conjunctions to this
	//   stream and allowed distributed conjunctions (with parent)
	// opt_rpt[0..opt_base_missing_conjuncts-1] = defined conjunctions to this
	//   stream and allowed distributed conjunctions and allowed parent
	// opt_rpt[0..opt_conjuncts_count-1] = all conjunctions
	//
	// allowed = booleans that can never evaluate to NULL/Unknown or turn
	//   NULL/Unknown into a True or False.

	USHORT parent_count = 0, distributed_count = 0;
	NodeStack missing_stack;
	if (parent_stack)
	{
		for (NodeStack::iterator iter(*parent_stack);
			iter.hasData() && conjunct_count < MAX_CONJUNCTS; ++iter)
		{
			jrd_nod* const node = iter.object();

			if ((rse->rse_jointype != blr_inner) && expression_possible_unknown(node))
			{
				// parent missing conjunctions shouldn't be
				// distributed to FULL OUTER JOIN streams at all
				if (rse->rse_jointype != blr_full)
				{
					missing_stack.push(node);
				}
			}
			else
			{
				conjunct_stack.push(node);
				conjunct_count++;
				parent_count++;
			}
		}

		// We've now merged parent, try again to make more conjunctions.
		distributed_count = distribute_equalities(conjunct_stack, csb, conjunct_count);
		conjunct_count += distributed_count;
	}

	// The newly created conjunctions belong to the base conjunctions.
	// After them are starting the parent conjunctions.
	opt->opt_base_parent_conjuncts = opt->opt_base_conjuncts + distributed_count;

	// Set base-point before the parent IS NULL nodes begin
	opt->opt_base_missing_conjuncts = (USHORT) conjunct_count;

	// Check if size of optimizer block exceeded.
	if (conjunct_count > MAX_CONJUNCTS)
	{
		ERR_post(Arg::Gds(isc_optimizer_blk_exc));
		// Msg442: size of optimizer block exceeded
	}

	// Put conjunctions in opt structure.
	// Note that it's a stack and we get the nodes in reversed order from the stack.
	opt->opt_conjuncts.grow(conjunct_count);
	SSHORT nodeBase = -1, j = -1;
	for (SLONG i = conjunct_count; i > 0; i--, j--)
	{
		jrd_nod* const node = conjunct_stack.pop();

		if (i == base_count)
		{
			// The base conjunctions
			j = base_count - 1;
			nodeBase = 0;
		}
		else if (i == conjunct_count - distributed_count)
		{
			// The parent conjunctions
			j = parent_count - 1;
			nodeBase = opt->opt_base_parent_conjuncts;
		}
		else if (i == conjunct_count)
		{
			// The new conjunctions created by "distribution" from the stack
			j = distributed_count - 1;
			nodeBase = opt->opt_base_conjuncts;
		}

		fb_assert(nodeBase >= 0 && j >= 0);
		opt->opt_conjuncts[nodeBase + j].opt_conjunct_node = node;
		compute_dependencies(node, opt->opt_conjuncts[nodeBase + j].opt_dependencies);
	}

	// Put the parent missing nodes on the stack
	for (NodeStack::iterator iter(missing_stack);
		iter.hasData() && conjunct_count < MAX_CONJUNCTS; ++iter)
	{
		jrd_nod* const node = iter.object();

		opt->opt_conjuncts.grow(conjunct_count + 1);
		opt->opt_conjuncts[conjunct_count].opt_conjunct_node = node;
		compute_dependencies(node, opt->opt_conjuncts[conjunct_count].opt_dependencies);
		conjunct_count++;
	}

	// Deoptimize some conjuncts in advance
	for (size_t iter = 0; iter < opt->opt_conjuncts.getCount(); iter++)
	{
		if (opt->opt_conjuncts[iter].opt_conjunct_node->nod_flags & nod_deoptimize)
		{
			// Fake an index match for them
			opt->opt_conjuncts[iter].opt_conjunct_flags |= opt_conjunct_matched;
		}
	}

	// attempt to optimize aggregates via an index, if possible
	if (aggregate && !sort) {
		sort = aggregate;
	}
	else {
		rse->rse_aggregate = aggregate = NULL;
	}

	// AB: Mark the previous used streams (sub-RecordSelExpr's) as active
	for (StreamsArray::iterator i = subStreams.begin(); i != subStreams.end(); ++i)
		csb->csb_rpt[*i].csb_flags |= csb_active;

	// outer joins require some extra processing
	if (rse->rse_jointype != blr_inner) {
		rsb = gen_outer(tdbb, opt, rse, rivers_stack, &sort);
	}
	else
	{
		bool sort_can_be_used = true;
		jrd_nod* const saved_sort_node = sort;

		// AB: If previous rsb's are already on the stack we can't use
		// a navigational-retrieval for an ORDER BY because the next
		// streams are JOINed to the previous ones
		if (rivers_stack.hasData())
		{
			sort = NULL;
			sort_can_be_used = false;
			// AB: We could already have multiple rivers at this
			// point so try to do some sort/merging now.
			while (rivers_stack.hasMore(1) && gen_sort_merge(tdbb, opt, rivers_stack))
				;

			// AB: Mark the previous used streams (sub-RecordSelExpr's) again
			// as active, because a SORT/MERGE could reset the flags
			for (StreamsArray::iterator i = subStreams.begin(); i != subStreams.end(); ++i)
				csb->csb_rpt[*i].csb_flags |= csb_active;
		}

		fb_assert(streams[0] != 1 || csb->csb_rpt[streams[1]].csb_relation != 0);

		while (true)
		{
			// AB: Determine which streams have an index relationship
			// with the currently active rivers. This is needed so that
			// no merge is made between a new cross river and the
			// currently active rivers. Where in the new cross river
			// a stream depends (index) on the active rivers.
			stream_array_t dependent_streams, free_streams;
			dependent_streams[0] = free_streams[0] = 0;
			find_index_relationship_streams(tdbb, opt, streams, dependent_streams, free_streams);

			// If we have dependent and free streams then we can't rely on
			// the sort node to be used for index navigation.
			if (dependent_streams[0] && free_streams[0])
			{
				sort = NULL;
				sort_can_be_used = false;
			}

			if (dependent_streams[0])
			{
				// copy free streams
				for (USHORT i = 0; i <= free_streams[0]; i++) {
					streams[i] = free_streams[i];
				}

				// Make rivers from the dependent streams
				gen_join(tdbb, opt, dependent_streams, rivers_stack, &sort, rse->rse_plan);

				// Generate 1 river which holds a cross join rsb between
				// all currently available rivers.

				// First get total count of streams.
				int count = 0;
				RiverStack::iterator stack1(rivers_stack);
				for (; stack1.hasData(); ++stack1) {
					count += stack1.object()->riv_count;
				}

				// Create river and copy the streams.
				River* river = FB_NEW_RPT(*tdbb->getDefaultPool(), count) River();
				river->riv_count = (UCHAR) count;
				UCHAR* stream_itr = river->riv_streams;
				RiverStack::iterator stack2(rivers_stack);
				for (; stack2.hasData(); ++stack2)
				{
					River* subRiver = stack2.object();
					memcpy(stream_itr, subRiver->riv_streams, subRiver->riv_count);
					stream_itr += subRiver->riv_count;
				}
				river->riv_rsb = make_cross(tdbb, opt, rivers_stack);
				rivers_stack.push(river);

				// Mark the river as active.
				set_made_river(opt, river);
				set_active(opt, river);
			}
			else
			{
				if (free_streams[0])
				{
					// Deactivate streams from rivers on stack, because
					// the remaining streams don't have any indexed relationship with them.
					RiverStack::iterator stack1(rivers_stack);
					for (; stack1.hasData(); ++stack1) {
						set_inactive(opt, stack1.object());
					}
				}

				break;
			}
		}

		// attempt to form joins in decreasing order of desirability
		gen_join(tdbb, opt, streams, rivers_stack, &sort, rse->rse_plan);

		// If there are multiple rivers, try some sort/merging
		while (rivers_stack.hasMore(1) && gen_sort_merge(tdbb, opt, rivers_stack))
			;

		rsb = make_cross(tdbb, opt, rivers_stack);

		// Assign the sort node back if it wasn't used by the index navigation
		if (saved_sort_node && !sort_can_be_used)
		{
			sort = saved_sort_node;
		}

		// Pick up any residual boolean that may have fallen thru the cracks
		rsb = gen_residual_boolean(tdbb, opt, rsb);
	}

	// if the aggregate was not optimized via an index, get rid of the
	// sort and flag the fact to the calling routine
	if (aggregate && sort)
	{
		rse->rse_aggregate = NULL;
		sort = NULL;
	}

	// check index usage in all the base streams to ensure
	// that any user-specified access plan is followed

	for (USHORT i = 1; i <= streams[0]; i++) {
		check_indices(&csb->csb_rpt[streams[i]]);
	}

	if (project || sort)
	{
		// Eliminate any duplicate dbkey streams
		const UCHAR* const b_end = beds + beds[0];
		const UCHAR* const k_end = key_streams + key_streams[0];
		UCHAR* k = &key_streams[1];
		for (const UCHAR* p2 = k; p2 <= k_end; p2++)
		{
			const UCHAR* q = &beds[1];
			while (q <= b_end && *q != *p2) {
				q++;
			}
			if (q > b_end) {
				*k++ = *p2;
			}
		}
		key_streams[0] = k - &key_streams[1];

		// Handle project clause, if present.
		if (project) {
			rsb = gen_sort(tdbb, opt, beds, key_streams, rsb, project, true);
		}

		// Handle sort clause if present
		if (sort) {
			rsb = gen_sort(tdbb, opt, beds, key_streams, rsb, sort, false);
		}
	}

    // Handle first and/or skip.  The skip MUST (if present)
    // appear in the rsb list AFTER the first.  Since the gen_first and gen_skip
    // functions add their nodes at the beginning of the rsb list we MUST call
    // gen_skip before gen_first.

    if (rse->rse_skip) {
		rsb = FB_NEW(*tdbb->getDefaultPool()) SkipRowsStream(csb, rsb, rse->rse_skip);
	}

	if (rse->rse_first) {
		rsb = FB_NEW(*tdbb->getDefaultPool()) FirstRowsStream(csb, rsb, rse->rse_first);
	}

	// release memory allocated for index descriptions
	for (USHORT i = 1; i <= streams[0]; ++i)
	{
		stream = streams[i];
		delete csb->csb_rpt[stream].csb_idx;
		csb->csb_rpt[stream].csb_idx = 0;

		// CVC: The following line added because OPT_compile is recursive, both directly
		//   and through gen_union(), too. Otherwise, we happen to step on deallocated memory
		//   and this is the cause of the crashes with indices that have plagued IB since v4.

		csb->csb_rpt[stream].csb_indices = 0;
	}

	DEBUG
	// free up memory for optimizer structures
	delete opt;

#ifdef OPT_DEBUG
	if (opt_debug_file)
	{
		fflush(opt_debug_file);
		//fclose(opt_debug_file);
		//opt_debug_file = 0;
	}
#endif

	}	// try
	catch (const Firebird::Exception&)
	{
		for (USHORT i = 1; i <= streams[0]; ++i)
		{
			const USHORT stream = streams[i];
			delete csb->csb_rpt[stream].csb_idx;
			csb->csb_rpt[stream].csb_idx = 0;
			csb->csb_rpt[stream].csb_indices = 0; // Probably needed to be safe
		}
		delete opt;
		throw;
	}

	if (rse->nod_flags & rse_writelock)
	{
		for (USHORT i = 1; i <= streams[0]; ++i)
		{
			const USHORT stream = streams[i];
			csb->csb_rpt[stream].csb_flags |= csb_update;
		}
	}

	return rsb;
}


static bool augment_stack(jrd_nod* node, NodeStack& stack)
{
/**************************************
 *
 *	a u g m e n t _ s t a c k
 *
 **************************************
 *
 * Functional description
 *	Add node to stack unless node is already on stack.
 *
 **************************************/
	DEV_BLKCHK(node, type_nod);

	for (NodeStack::const_iterator temp(stack); temp.hasData(); ++temp)
	{
		if (node_equality(node, temp.object())) {
			return false;
		}
	}

	stack.push(node);

	return true;
}


static void check_indices(const CompilerScratch::csb_repeat* csb_tail)
{
/**************************************
 *
 *	c h e c k _ i n d i c e s
 *
 **************************************
 *
 * Functional description
 *	Check to make sure that the user-specified
 *	indices were actually utilized by the optimizer.
 *
 **************************************/
	thread_db* tdbb = JRD_get_thread_data();

	const jrd_nod* plan = csb_tail->csb_plan;
	if (!plan) {
		return;
	}

	if (plan->nod_type != nod_retrieve) {
		return;
	}

	const jrd_rel* relation = csb_tail->csb_relation;

	// if there were no indices fetched at all but the
	// user specified some, error out using the first index specified
	const jrd_nod* access_type = 0;
	if (!csb_tail->csb_indices && (access_type = plan->nod_arg[e_retrieve_access_type]))
	{
		// index %s cannot be used in the specified plan
		const char* iname =
			reinterpret_cast<const char*>(access_type->nod_arg[e_access_type_index_name]);
		ERR_post(Arg::Gds(isc_index_unused) << Arg::Str(iname));
	}

	// check to make sure that all indices are either used or marked not to be used,
	// and that there are no unused navigational indices
	Firebird::MetaName index_name;

	const index_desc* idx = csb_tail->csb_idx->items;
	for (USHORT i = 0; i < csb_tail->csb_indices; i++)
	{
		if (!(idx->idx_runtime_flags & (idx_plan_dont_use | idx_used)) ||
			((idx->idx_runtime_flags & idx_plan_navigate) && !(idx->idx_runtime_flags & idx_navigate)))
		{
			if (relation)
			{
				MET_lookup_index(tdbb, index_name, relation->rel_name, (USHORT) (idx->idx_id + 1));
			}
			else
			{
				index_name = "";
			}

			// index %s cannot be used in the specified plan
			ERR_post(Arg::Gds(isc_index_unused) << Arg::Str(index_name));
		}
		++idx;
	}
}


static void check_sorts(RecordSelExpr* rse)
{
/**************************************
 *
 *	c h e c k _ s o r t s
 *
 **************************************
 *
 * Functional description
 *	Try to optimize out unnecessary sorting.
 *
 **************************************/
	DEV_BLKCHK(rse, type_nod);

	jrd_nod* sort = rse->rse_sorted;
	jrd_nod* project = rse->rse_projection;

	// check if a GROUP BY exists using the same fields as the project or sort:
	// if so, the projection can be eliminated; if no projection exists, then
	// the sort can be eliminated.

	jrd_nod *group, *sub_rse;
	if ((project || sort) && rse->rse_count == 1 && (sub_rse = rse->rse_relation[0]) &&
		sub_rse->nod_type == nod_aggregate && (group = sub_rse->nod_arg[e_agg_group]))
	{
		// if all the fields of the project are the same as all the fields
		// of the group by, get rid of the project.

		if (project && (project->nod_count == group->nod_count))
		{
			jrd_nod** project_ptr = project->nod_arg;
			const jrd_nod* const* const project_end = project_ptr + project->nod_count;
			for (; project_ptr < project_end; project_ptr++)
			{
				const jrd_nod* const* group_ptr = group->nod_arg;
				const jrd_nod* const* const group_end = group_ptr + group->nod_count;
				for (; group_ptr < group_end; group_ptr++)
				{
					if (map_equal(*group_ptr, *project_ptr, sub_rse->nod_arg[e_agg_map])) {
						break;
					}
				}
				if (group_ptr == group_end) {
					break;
				}
			}

			// we can now ignore the project, but in case the project is being done
			// in descending order because of an order by, do the group by the same way.
			if (project_ptr == project_end)
			{
				set_direction(project, group);
				project = rse->rse_projection = NULL;
			}
		}

		// if there is no projection, then we can make a similar optimization
		// for sort, except that sort may have fewer fields than group by.
		if (!project && sort && (sort->nod_count <= group->nod_count))
		{
			const jrd_nod* const* sort_ptr = sort->nod_arg;
			const jrd_nod* const* const sort_end = sort_ptr + sort->nod_count;
			for (; sort_ptr < sort_end; sort_ptr++)
			{
				const jrd_nod* const* group_ptr = group->nod_arg;
				const jrd_nod* const* const group_end = group_ptr + sort->nod_count;
				for (; group_ptr < group_end; group_ptr++)
				{
					if (map_equal(*group_ptr, *sort_ptr, sub_rse->nod_arg[e_agg_map])) {
						break;
					}
				}
				if (group_ptr == group_end) {
					break;
				}
			}

			// if all the fields in the sort list match the first n fields in the
			// project list, we can ignore the sort, but update the sort order
			// (ascending/descending) to match that in the sort list

			if (sort_ptr == sort_end)
			{
				set_direction(sort, group);
				set_position(sort, group, sub_rse->nod_arg[e_agg_map]);
				sort = rse->rse_sorted = NULL;
			}
		}

	}

	// examine the ORDER BY and DISTINCT clauses; if all the fields in the
	// ORDER BY match the first n fields in the DISTINCT in any order, the
	// ORDER BY can be removed, changing the fields in the DISTINCT to match
	// the ordering of fields in the ORDER BY.

	if (sort && project && (sort->nod_count <= project->nod_count))
	{
		const jrd_nod* const* sort_ptr = sort->nod_arg;
		const jrd_nod* const* const sort_end = sort_ptr + sort->nod_count;
		for (; sort_ptr < sort_end; sort_ptr++)
		{
			const jrd_nod* const* project_ptr = project->nod_arg;
			const jrd_nod* const* const project_end = project_ptr + sort->nod_count;
			for (; project_ptr < project_end; project_ptr++)
			{
				if ((*sort_ptr)->nod_type == nod_field &&
					(*project_ptr)->nod_type == nod_field &&
					(*sort_ptr)->nod_arg[e_fld_stream] == (*project_ptr)->nod_arg[e_fld_stream] &&
					(*sort_ptr)->nod_arg[e_fld_id] == (*project_ptr)->nod_arg[e_fld_id])
				{
					break;
				}
			}

			if (project_ptr == project_end) {
				break;
			}
		}

		// if all the fields in the sort list match the first n fields
		// in the project list, we can ignore the sort, but update
		// the project to match the sort.
		if (sort_ptr == sort_end)
		{
			set_direction(sort, project);
			set_position(sort, project, NULL);
			sort = rse->rse_sorted = NULL;
		}
	}

	// RP: optimize sort with OUTER JOIN
	// if all the fields in the sort list are from one stream, check the stream is
	// the most outer stream, if true update rse and ignore the sort
	if (sort && !project)
	{
		int sort_stream = 0;
		bool usableSort = true;
		const jrd_nod* const* sort_ptr = sort->nod_arg;
		const jrd_nod* const* const sort_end = sort_ptr + sort->nod_count;
		for (; sort_ptr < sort_end; sort_ptr++)
		{
			if ((*sort_ptr)->nod_type == nod_field)
			{
				// Get stream for this field at this position.
				const int current_stream = (int)(IPTR)(*sort_ptr)->nod_arg[e_fld_stream];
				// If this is the first position node, save this stream.
				if (sort_ptr == sort->nod_arg) {
			    	sort_stream = current_stream;
				}
				else if (current_stream != sort_stream)
				{
					// If the current stream is different then the previous stream
					// then we can't use this sort for an indexed order retrieval.
					usableSort = false;
					break;
				}
			}
			else
			{
				// If this is not the first position node, reject this sort.
				// Two expressions cannot be mapped to a single index.
				if (sort_ptr > sort->nod_arg)
				{
					usableSort = false;
					break;
				}
				// This position doesn't use a simple field, thus we should
				// check the expression internals.
				Firebird::SortedArray<int> streams;
				get_expression_streams(*sort_ptr, streams);
				// We can use this sort only if there's a single stream
				// referenced by the expression.
				if (streams.getCount() == 1) {
					sort_stream = streams[0];
				}
				else
				{
					usableSort = false;
					break;
				}
			}
		}

		if (usableSort)
		{
			RecordSelExpr* new_rse = NULL;
			jrd_nod* node = (jrd_nod*) rse;
			while (node)
			{
				if (node->nod_type == nod_rse)
				{
					new_rse = (RecordSelExpr*) node;

					// AB: Don't distribute the sort when a FIRST/SKIP is supplied,
					// because that will affect the behaviour from the deeper RSE.
					if (new_rse != rse && (new_rse->rse_first || new_rse->rse_skip))
					{
						node = NULL;
						break;
					}

					// Walk trough the relations of the RSE and see if a
					// matching stream can be found.
					if (new_rse->rse_jointype == blr_inner)
					{
						if (new_rse->rse_count == 1) {
							node = new_rse->rse_relation[0];
						}
						else
						{
							bool sortStreamFound = false;
							for (int i = 0; i < new_rse->rse_count; i++)
							{
								jrd_nod* subNode = (jrd_nod*) new_rse->rse_relation[i];
								if (subNode->nod_type == nod_relation &&
									((int)(IPTR) subNode->nod_arg[e_rel_stream]) == sort_stream &&
									new_rse != rse)
								{
									// We have found the correct stream
									sortStreamFound = true;
									break;
								}

							}
							if (sortStreamFound)
							{
								// Set the sort to the found stream and clear the original sort
								new_rse->rse_sorted = sort;
								sort = rse->rse_sorted = NULL;
							}
							node = NULL;
						}
					}
					else if (new_rse->rse_jointype == blr_left) {
						node = new_rse->rse_relation[0];
					}
					else {
						node = NULL;
					}
				}
				else
				{
					if (node->nod_type == nod_relation &&
						((int)(IPTR)node->nod_arg[e_rel_stream]) == sort_stream &&
						new_rse && new_rse != rse)
					{
						// We have found the correct stream, thus apply the sort here
						new_rse->rse_sorted = sort;
						sort = rse->rse_sorted = NULL;
					}
					node = NULL;
				}
			}
		}
	}
}


static void class_mask(USHORT count, jrd_nod** eq_class, ULONG* mask)
{
/**************************************
 *
 *	c l a s s _ m a s k
 *
 **************************************
 *
 * Functional description
 *	Given an sort/merge join equivalence class (vector of node pointers
 *	of representative values for rivers), return a bit mask of rivers
 *	with values.
 *
 **************************************/
	if (*eq_class) {
		DEV_BLKCHK(*eq_class, type_nod);
	}

	if (count > MAX_CONJUNCTS)
	{
		ERR_post(Arg::Gds(isc_optimizer_blk_exc));
		// Msg442: size of optimizer block exceeded
	}

	for (SLONG i = 0; i < OPT_STREAM_BITS; i++) {
		mask[i] = 0;
	}

	for (SLONG i = 0; i < count; i++, eq_class++)
	{
		if (*eq_class)
		{
			SET_DEP_BIT(mask, i);
			DEV_BLKCHK(*eq_class, type_nod);
		}
	}
}


static jrd_nod* compose(jrd_nod** node1, jrd_nod* node2, nod_t node_type)
{
/**************************************
 *
 *	c o m p o s e
 *
 **************************************
 *
 * Functional description
 *	Build and AND out of two conjuncts.
 *
 **************************************/

	DEV_BLKCHK(*node1, type_nod);
	DEV_BLKCHK(node2, type_nod);

	if (!node2) {
		return *node1;
	}

	if (!*node1) {
		return (*node1 = node2);
	}

	return *node1 = OPT_make_binary_node(node_type, *node1, node2, false);
}


static void compute_dependencies(const jrd_nod* node, ULONG* dependencies)
{
/**************************************
 *
 *	c o m p u t e _ d e p e n d e n c i e s
 *
 **************************************
 *
 * Functional description
 *	Compute stream dependencies for evaluation of an expression.
 *
 **************************************/

	DEV_BLKCHK(node, type_nod);

	// Recurse thru interesting sub-nodes

	const jrd_nod* const* ptr = node->nod_arg;

	if (node->nod_type == nod_procedure) {
		return;
	}

	for (const jrd_nod* const* const end = ptr + node->nod_count; ptr < end; ptr++)
	{
		compute_dependencies(*ptr, dependencies);
	}

	const RecordSelExpr* rse;
	const jrd_nod* sub;
	const jrd_nod* value;

	switch (node->nod_type)
	{
	case nod_field:
		{
			const SLONG n = (SLONG)(IPTR) node->nod_arg[e_fld_stream];
			SET_DEP_BIT(dependencies, n);
			return;
		}

	case nod_rec_version:
	case nod_dbkey:
		{
			const SLONG n = (SLONG)(IPTR) node->nod_arg[0];
			SET_DEP_BIT(dependencies, n);
			return;
		}

	case nod_min:
	case nod_max:
	case nod_average:
	case nod_total:
	case nod_count:
	case nod_from:
		if ( (sub = node->nod_arg[e_stat_default]) ) {
			compute_dependencies(sub, dependencies);
		}
		rse = (RecordSelExpr*) node->nod_arg[e_stat_rse];
		value = node->nod_arg[e_stat_value];
		break;

	case nod_rse:
		rse = (RecordSelExpr*) node;
		value = NULL;
		break;

	default:
		return;
	}

	// Node is a record selection expression.  Groan.  Ugh.  Yuck.

	if ( (sub = rse->rse_first) ) {
		compute_dependencies(sub, dependencies);
	}

	// Check sub-expressions

	if ( (sub = rse->rse_boolean) ) {
		compute_dependencies(sub, dependencies);
	}

	if ( (sub = rse->rse_sorted) ) {
		compute_dependencies(sub, dependencies);
	}

	if ( (sub = rse->rse_projection) ) {
		compute_dependencies(sub, dependencies);
	}

	// Check value expression, if any

	if (value) {
		compute_dependencies(value, dependencies);
	}

	// Reset streams inactive

	ptr = rse->rse_relation;
	for (const jrd_nod* const* const end = ptr + rse->rse_count; ptr < end; ptr++)
	{
		if ((*ptr)->nod_type != nod_rse)
		{
			const SLONG n = (SLONG)(IPTR) (*ptr)->nod_arg[STREAM_INDEX((*ptr))];
			CLEAR_DEP_BIT(dependencies, n);
		}
	}
}


static void compute_dbkey_streams(const CompilerScratch* csb, const jrd_nod* node, UCHAR* streams)
{
/**************************************
 *
 *	c o m p u t e _ d b k e y _ s t r e a m s
 *
 **************************************
 *
 * Functional description
 *	Identify all of the streams for which a
 *	dbkey may need to be carried through a sort.
 *
 **************************************/
	DEV_BLKCHK(csb, type_csb);
	DEV_BLKCHK(node, type_nod);

	switch (node->nod_type)
	{
	case nod_relation:
		fb_assert(streams[0] < MAX_STREAMS && streams[0] < MAX_UCHAR);
		streams[++streams[0]] = (UCHAR)(IPTR) node->nod_arg[e_rel_stream];
		break;
	case nod_union:
		{
			const jrd_nod* clauses = node->nod_arg[e_uni_clauses];
			if (clauses->nod_type != nod_procedure)
			{
				const jrd_nod* const* ptr = clauses->nod_arg;
				for (const jrd_nod* const* const end = ptr + clauses->nod_count; ptr < end; ptr += 2)
				{
					compute_dbkey_streams(csb, *ptr, streams);
				}
			}
		}
		break;
	case nod_rse:
		{
			const RecordSelExpr* rse = (RecordSelExpr*) node;
			const jrd_nod* const* ptr = rse->rse_relation;
			for (const jrd_nod* const* const end = ptr + rse->rse_count; ptr < end; ptr++)
			{
				compute_dbkey_streams(csb, *ptr, streams);
			}
		}
		break;
	}
}


static void compute_rse_streams(const CompilerScratch* csb, const RecordSelExpr* rse, UCHAR* streams)
{
/**************************************
 *
 *	c o m p u t e _ r s e _ s t r e a m s
 *
 **************************************
 *
 * Functional description
 *	Identify the streams that make up an RecordSelExpr.
 *
 **************************************/
	DEV_BLKCHK(csb, type_csb);
	DEV_BLKCHK(rse, type_nod);

	const jrd_nod* const* ptr = rse->rse_relation;
	for (const jrd_nod* const* const end = ptr + rse->rse_count; ptr < end; ptr++)
	{
		const jrd_nod* node = *ptr;
		if (node->nod_type != nod_rse)
		{
			fb_assert(streams[0] < MAX_STREAMS && streams[0] < MAX_UCHAR);
			streams[++streams[0]] = (UCHAR)(IPTR) node->nod_arg[STREAM_INDEX(node)];
		}
		else {
			compute_rse_streams(csb, (const RecordSelExpr*) node, streams);
		}
	}
}

static bool check_for_nod_from(const jrd_nod* node)
{
/**************************************
 *
 *	c h e c k _ f o r _ n o d _ f r o m
 *
 **************************************
 *
 * Functional description
 *	Check for nod_from under >=0 nod_cast nodes.
 *
 **************************************/
	switch (node->nod_type)
	{
	case nod_from:
		return true;
	case nod_cast:
		return check_for_nod_from(node->nod_arg[e_cast_source]);
	default:
		return false;
	}
}

static SLONG decompose(thread_db* tdbb, jrd_nod* boolean_node, NodeStack& stack, CompilerScratch* csb)
{
/**************************************
 *
 *	d e c o m p o s e
 *
 **************************************
 *
 * Functional description
 *	Decompose a boolean into a stack of conjuctions.
 *
 **************************************/
	DEV_BLKCHK(boolean_node, type_nod);
	DEV_BLKCHK(csb, type_csb);

	if (boolean_node->nod_type == nod_and)
	{
		SLONG count = decompose(tdbb, boolean_node->nod_arg[0], stack, csb);
		count += decompose(tdbb, boolean_node->nod_arg[1], stack, csb);
		return count;
	}

	// turn a between into (a greater than or equal) AND (a less than  or equal)

	if (boolean_node->nod_type == nod_between)
	{
		jrd_nod* arg = boolean_node->nod_arg[0];
		if (check_for_nod_from(arg))
		{
			// Without this ERR_punt(), server was crashing with sub queries
			// under "between" predicate, Bug No. 73766
			ERR_post(Arg::Gds(isc_optimizer_between_err));
			// Msg 493: Unsupported field type specified in BETWEEN predicate
		}
		jrd_nod* node = OPT_make_binary_node(nod_geq, arg, boolean_node->nod_arg[1], true);
		stack.push(node);
		arg = CMP_clone_node_opt(tdbb, csb, arg);
		node = OPT_make_binary_node(nod_leq, arg, boolean_node->nod_arg[2], true);
		stack.push(node);
		return 2;
	}

	// turn a LIKE into a LIKE and a STARTING WITH, if it starts
	// with anything other than a pattern-matching character

	jrd_nod* arg;
	if (boolean_node->nod_type == nod_like && (arg = optimize_like(tdbb, csb, boolean_node)))
	{
		stack.push(OPT_make_binary_node(nod_starts, boolean_node->nod_arg[0], arg, false));
		stack.push(boolean_node);
		return 2;
	}

	if (boolean_node->nod_type == nod_or)
	{
		NodeStack or_stack;
		if (decompose(tdbb, boolean_node->nod_arg[0], or_stack, csb) >= 2)
		{
			boolean_node->nod_arg[0] = or_stack.pop();
			while (or_stack.hasData())
			{
				boolean_node->nod_arg[0] =
					OPT_make_binary_node(nod_and, or_stack.pop(), boolean_node->nod_arg[0], true);
			}
		}

		or_stack.clear();
		if (decompose(tdbb, boolean_node->nod_arg[1], or_stack, csb) >= 2)
		{
			boolean_node->nod_arg[1] = or_stack.pop();
			while (or_stack.hasData())
			{
				boolean_node->nod_arg[1] =
					OPT_make_binary_node(nod_and, or_stack.pop(), boolean_node->nod_arg[1], true);
			}
		}
	}

	stack.push(boolean_node);

	return 1;
}


static USHORT distribute_equalities(NodeStack& org_stack, CompilerScratch* csb, USHORT base_count)
{
/**************************************
 *
 *	d i s t r i b u t e _ e q u a l i t i e s
 *
 **************************************
 *
 * Functional description
 *	Given a stack of conjunctions, generate some simple
 *	inferences.  In general, find classes of equalities,
 *	then find operations based on members of those classes.
 *	If we find any, generate additional conjunctions.  In
 *	short:
 *
 *		If (a == b) and (a $ c) --> (b $ c) for any
 *		operation '$'.
 *
 **************************************/
	Firebird::ObjectsArray<NodeStack> classes;
	Firebird::ObjectsArray<NodeStack>::iterator eq_class;

	DEV_BLKCHK(csb, type_csb);

	// Zip thru stack of booleans looking for field equalities

	for (NodeStack::iterator stack1(org_stack); stack1.hasData(); ++stack1)
	{
		jrd_nod* boolean = stack1.object();
		if (boolean->nod_flags & nod_deoptimize)
			continue;
		if (boolean->nod_type != nod_eql)
			continue;
		jrd_nod* node1 = boolean->nod_arg[0];
		if (node1->nod_type != nod_field)
			continue;
		jrd_nod* node2 = boolean->nod_arg[1];
		if (node2->nod_type != nod_field)
			continue;
		for (eq_class = classes.begin(); eq_class != classes.end(); ++eq_class)
		{
			if (search_stack(node1, *eq_class))
			{
				augment_stack(node2, *eq_class);
				break;
			}
			else if (search_stack(node2, *eq_class))
			{
				eq_class->push(node1);
				break;
			}
		}
		if (eq_class == classes.end())
		{
			NodeStack& s = classes.add();
			s.push(node1);
			s.push(node2);
			eq_class = classes.back();
		}
	}

	if (classes.getCount() == 0)
		return 0;

	// Make another pass looking for any equality relationships that may have crept
	// in between classes (this could result from the sequence (A = B, C = D, B = C)

	for (eq_class = classes.begin(); eq_class != classes.end(); ++eq_class)
	{
		for (NodeStack::const_iterator stack2(*eq_class); stack2.hasData(); ++stack2)
		{
			for (Firebird::ObjectsArray<NodeStack>::iterator eq_class2(eq_class);
				++eq_class2 != classes.end();)
			{
				if (search_stack(stack2.object(), *eq_class2))
				{
					DEBUG;
					while (eq_class2->hasData()) {
						augment_stack(eq_class2->pop(), *eq_class);
					}
				}
			}
		}
	}

	USHORT count = 0;

	// Start by making a pass distributing field equalities

	for (eq_class = classes.begin(); eq_class != classes.end(); ++eq_class)
	{
		if (eq_class->hasMore(2))
		{
			for (NodeStack::iterator outer(*eq_class); outer.hasData(); ++outer)
			{
				for (NodeStack::iterator inner(outer); (++inner).hasData(); )
				{
					jrd_nod* boolean =
						OPT_make_binary_node(nod_eql, outer.object(), inner.object(), true);

					if ((base_count + count < MAX_CONJUNCTS) && augment_stack(boolean, org_stack))
					{
						DEBUG;
						count++;
					}
					else
						delete boolean;
				}
			}
		}
	}

	// Now make a second pass looking for non-field equalities

	for (NodeStack::iterator stack3(org_stack); stack3.hasData(); ++stack3)
	{
		jrd_nod* boolean = stack3.object();
		if (boolean->nod_type != nod_eql &&
			boolean->nod_type != nod_gtr &&
			boolean->nod_type != nod_geq &&
			boolean->nod_type != nod_leq &&
			boolean->nod_type != nod_lss &&
			boolean->nod_type != nod_matches &&
			boolean->nod_type != nod_contains &&
			boolean->nod_type != nod_like &&
			boolean->nod_type != nod_similar)
		{
			continue;
		}
		const jrd_nod* node1 = boolean->nod_arg[0];
		const jrd_nod* node2 = boolean->nod_arg[1];
		bool reverse = false;
		if (node1->nod_type != nod_field)
		{
			const jrd_nod* swap_node = node1;
			node1 = node2;
			node2 = swap_node;
			reverse = true;
		}
		if (node1->nod_type != nod_field) {
			continue;
		}
		if (node2->nod_type != nod_literal &&
			node2->nod_type != nod_variable &&
			node2->nod_type != nod_argument)
		{
			continue;
		}
		for (eq_class = classes.begin(); eq_class != classes.end(); ++eq_class)
		{
			if (search_stack(node1, *eq_class))
			{
				for (NodeStack::iterator temp(*eq_class); temp.hasData(); ++temp)
				{
					if (!node_equality(node1, temp.object()))
					{
						jrd_nod* arg1;
						jrd_nod* arg2;
						if (reverse)
						{
							arg1 = boolean->nod_arg[0];
							arg2 = temp.object();
						}
						else
						{
							arg1 = temp.object();
							arg2 = boolean->nod_arg[1];
						}

						// From the conjuncts X(A,B) and A=C, infer the conjunct X(C,B)
						jrd_nod* new_node = make_inference_node(csb, boolean, arg1, arg2);
						if ((base_count + count < MAX_CONJUNCTS) && augment_stack(new_node, org_stack))
						{
							count++;
						}
					}
				}
				break;
			}
		}
	}

	return count;
}


static bool expression_possible_unknown(const jrd_nod* node)
{
/**************************************
 *
 *      e x p r e s s i o n _ p o s s i b l e _ u n k n o w n
 *
 **************************************
 *
 * Functional description
 *  Check if expression could return NULL
 *  or expression can turn NULL into
 *  a True/False.
 *
 **************************************/
	DEV_BLKCHK(node, type_nod);

	if (!node) {
		return false;
	}

	switch (node->nod_type)
	{
		case nod_cast:
			return expression_possible_unknown(node->nod_arg[e_cast_source]);

		case nod_extract:
			return expression_possible_unknown(node->nod_arg[e_extract_value]);

		case nod_strlen:
			return expression_possible_unknown(node->nod_arg[e_strlen_value]);

		case nod_field:
		case nod_rec_version:
		case nod_dbkey:
		case nod_argument:
		case nod_current_date:
		case nod_current_role:
		case nod_current_time:
		case nod_current_timestamp:
		case nod_gen_id:
		case nod_gen_id2:
		case nod_internal_info:
		case nod_literal:
		case nod_null:
		case nod_user_name:
		case nod_variable:
			return false;

		case nod_or:
		case nod_and:

		case nod_add:
		case nod_add2:
		case nod_concatenate:
		case nod_divide:
		case nod_divide2:
		case nod_multiply:
		case nod_multiply2:
		case nod_negate:
		case nod_subtract:
		case nod_subtract2:

		case nod_upcase:
		case nod_lowcase:
		case nod_substr:
		case nod_trim:
		case nod_sys_function:
		case nod_derived_expr:

		case nod_like:
		case nod_between:
		case nod_contains:
		case nod_similar:
		case nod_starts:
		case nod_eql:
		case nod_neq:
		case nod_geq:
		case nod_gtr:
		case nod_lss:
		case nod_leq:
		{
			const jrd_nod* const* ptr = node->nod_arg;
			// Check all sub-nodes of this node.
			for (const jrd_nod* const* const end = ptr + node->nod_count; ptr < end; ptr++)
			{
				if (expression_possible_unknown(*ptr)) {
					return true;
				}
			}

			return false;
		}

		default :
			return true;
	}
}


static bool expression_contains_stream(CompilerScratch* csb,
									   const jrd_nod* node,
									   UCHAR stream,
									   bool* otherActiveStreamFound)
{
/**************************************
 *
 *      e x p r e s s i o n _ c o n t a i n s _ s t r e a m
 *
 **************************************
 *
 * Functional description
 *  Search if somewhere in the expression the given stream
 *  is used. If a unknown node is found it will return true.
 *
 **************************************/
	DEV_BLKCHK(node, type_nod);

	if (!node) {
		return false;
	}

	RecordSelExpr* rse = NULL;

	USHORT n;
	switch (node->nod_type)
	{

		case nod_field:
			n = (USHORT)(IPTR) node->nod_arg[e_fld_stream];
			if (otherActiveStreamFound && (n != stream))
			{
				if (csb->csb_rpt[n].csb_flags & csb_active) {
					*otherActiveStreamFound = true;
				}
			}
			return ((USHORT)(IPTR) node->nod_arg[e_fld_stream] == stream);

		case nod_rec_version:
		case nod_dbkey:
			n = (USHORT)(IPTR) node->nod_arg[0];
			if (otherActiveStreamFound && (n != stream))
			{
				if (csb->csb_rpt[n].csb_flags & csb_active) {
					*otherActiveStreamFound = true;
				}
			}
			return ((USHORT)(IPTR) node->nod_arg[0] == stream);

		case nod_cast:
			return expression_contains_stream(csb,
				node->nod_arg[e_cast_source], stream, otherActiveStreamFound);

		case nod_extract:
			return expression_contains_stream(csb,
				node->nod_arg[e_extract_value], stream, otherActiveStreamFound);

		case nod_strlen:
			return expression_contains_stream(csb,
				node->nod_arg[e_strlen_value], stream, otherActiveStreamFound);

		case nod_function:
			return expression_contains_stream(csb,
				node->nod_arg[e_fun_args], stream, otherActiveStreamFound);

		case nod_procedure:
			return expression_contains_stream(csb, node->nod_arg[e_prc_inputs],
				stream, otherActiveStreamFound);

		case nod_any:
		case nod_unique:
		case nod_ansi_any:
		case nod_ansi_all:
		case nod_exists:
			return expression_contains_stream(csb, node->nod_arg[e_any_rse],
				stream, otherActiveStreamFound);

		case nod_argument:
		case nod_current_date:
		case nod_current_role:
		case nod_current_time:
		case nod_current_timestamp:
		case nod_gen_id:
		case nod_gen_id2:
		case nod_internal_info:
		case nod_literal:
		case nod_null:
		case nod_user_name:
		case nod_variable:
			return false;

		case nod_rse:
			rse = (RecordSelExpr*) node;
			break;

		case nod_average:
		case nod_count:
		case nod_from:
		case nod_max:
		case nod_min:
		case nod_total:
			{
				const jrd_nod* nodeDefault = node->nod_arg[e_stat_rse];
				bool result = false;
				if (nodeDefault &&
					expression_contains_stream(csb, nodeDefault, stream, otherActiveStreamFound))
				{
					result = true;
					if (!otherActiveStreamFound) {
						return result;
					}
				}
				rse = (RecordSelExpr*) node->nod_arg[e_stat_rse];
				const jrd_nod* value = node->nod_arg[e_stat_value];
				if (value && expression_contains_stream(csb, value, stream, otherActiveStreamFound))
				{
					result = true;
					if (!otherActiveStreamFound) {
						return result;
					}
				}
				return result;
			}
			break;

		// go into the node arguments
		case nod_add:
		case nod_add2:
		case nod_agg_average:
		case nod_agg_average2:
		case nod_agg_average_distinct:
		case nod_agg_average_distinct2:
		case nod_agg_max:
		case nod_agg_min:
		case nod_agg_total:
		case nod_agg_total2:
		case nod_agg_total_distinct:
		case nod_agg_total_distinct2:
		case nod_agg_list:
		case nod_agg_list_distinct:
		case nod_concatenate:
		case nod_divide:
		case nod_divide2:
		case nod_multiply:
		case nod_multiply2:
		case nod_negate:
		case nod_subtract:
		case nod_subtract2:

		case nod_upcase:
		case nod_lowcase:
		case nod_substr:
		case nod_trim:
		case nod_sys_function:
		case nod_derived_expr:

		case nod_like:
		case nod_between:
		case nod_similar:
		case nod_sleuth:
		case nod_missing:
		case nod_value_if:
		case nod_matches:
		case nod_contains:
		case nod_starts:
		case nod_equiv:
		case nod_eql:
		case nod_neq:
		case nod_geq:
		case nod_gtr:
		case nod_lss:
		case nod_leq:
		{
			const jrd_nod* const* ptr = node->nod_arg;
			// Check all sub-nodes of this node.
			bool result = false;
			for (const jrd_nod* const* const end = ptr + node->nod_count; ptr < end; ptr++)
			{
				if (expression_contains_stream(csb, *ptr, stream, otherActiveStreamFound))
				{
					result = true;
					if (!otherActiveStreamFound) {
						return result;
					}
				}
			}
			return result;
		}

		default :
			return true;
	}

	if (rse)
	{

		jrd_nod* sub;
		if ((sub = rse->rse_first) &&
			expression_contains_stream(csb, sub, stream, otherActiveStreamFound))
		{
			return true;
		}

		if ((sub = rse->rse_skip) &&
			expression_contains_stream(csb, sub, stream, otherActiveStreamFound))
		{
			return true;
		}

		if ((sub = rse->rse_boolean) &&
			expression_contains_stream(csb, sub, stream, otherActiveStreamFound))
		{
			return true;
		}

		if ((sub = rse->rse_sorted) &&
			expression_contains_stream(csb, sub, stream, otherActiveStreamFound))
		{
			return true;
		}

		if ((sub = rse->rse_projection) &&
			expression_contains_stream(csb, sub, stream, otherActiveStreamFound))
		{
			return true;
		}

	}

	return false;
}


static void find_index_relationship_streams(thread_db* tdbb,
											OptimizerBlk* opt,
											const UCHAR* streams,
											UCHAR* dependent_streams,
											UCHAR* free_streams)
{
/**************************************
 *
 *	f i n d _ i n d e x _r e l a t i o n s h i p _ s t r e a m s
 *
 **************************************
 *
 * Functional description
 *	Find the streams that can use a index
 *	with the currently active streams.
 *
 **************************************/

	DEV_BLKCHK(opt, type_opt);
	SET_TDBB(tdbb);
	//Database* dbb = tdbb->getDatabase();

	CompilerScratch* const csb = opt->opt_csb;
	const UCHAR* end_stream = streams + 1 + streams[0];
	for (const UCHAR* stream = streams + 1; stream < end_stream; stream++)
	{

		CompilerScratch::csb_repeat* csb_tail = &csb->csb_rpt[*stream];
		// Set temporary active flag for this stream
		csb_tail->csb_flags |= csb_active;

		bool indexed_relationship = false;
		if (opt->opt_conjuncts.getCount())
		{
			// Calculate the inversion for this stream.
			// The returning candidate contains the streams that will be used for
			// index retrieval. This meant that if some stream is used this stream
			// depends on already active streams and can not be used in a separate
			// SORT/MERGE.
			InversionCandidate* candidate = NULL;
			OptimizerRetrieval* optimizerRetrieval = FB_NEW(*tdbb->getDefaultPool())
				OptimizerRetrieval(*tdbb->getDefaultPool(), opt, *stream, false, false, NULL);
			candidate = optimizerRetrieval->getCost();
			if (candidate->dependentFromStreams.getCount() >= 1) {
				indexed_relationship = true;
			}
			delete candidate;
			delete optimizerRetrieval;
		}

		if (indexed_relationship) {
			dependent_streams[++dependent_streams[0]] = *stream;
		}
		else {
			free_streams[++free_streams[0]] = *stream;
		}

		// Reset active flag
		csb_tail->csb_flags &= ~csb_active;
	}
}


static jrd_nod* find_dbkey(jrd_nod* dbkey, USHORT stream, SLONG* position)
{
/**************************************
 *
 *	f i n d _ d b k e y
 *
 **************************************
 *
 * Functional description
 *	Search a dbkey (possibly a concatenated one) for
 *	a dbkey for specified stream.
 *
 **************************************/
	DEV_BLKCHK(dbkey, type_nod);
	if (dbkey->nod_type == nod_dbkey)
	{
		if ((USHORT)(IPTR) dbkey->nod_arg[0] == stream)
			return dbkey;

		*position = *position + 1;
		return NULL;
	}

	if (dbkey->nod_type == nod_concatenate)
	{
        jrd_nod** ptr = dbkey->nod_arg;
		for (const jrd_nod* const* const end = ptr + dbkey->nod_count; ptr < end; ptr++)
		{
			jrd_nod* dbkey_temp = find_dbkey(*ptr, stream, position);
			if (dbkey_temp)
				return dbkey_temp;
		}
	}
	return NULL;
}


static void form_rivers(thread_db*		tdbb,
						OptimizerBlk*	opt,
						const UCHAR*	streams,
						RiverStack&		river_stack,
						jrd_nod**		sort_clause,
						jrd_nod*		plan_clause)
{
/**************************************
 *
 *	f o r m _ r i v e r s
 *
 **************************************
 *
 * Functional description
 *	Form streams into rivers according
 *	to the user-specified plan.
 *
 **************************************/
	SET_TDBB(tdbb);
	DEV_BLKCHK(opt, type_opt);
	if (sort_clause) {
		DEV_BLKCHK(*sort_clause, type_nod);
	}
	DEV_BLKCHK(plan_clause, type_nod);

	stream_array_t temp;
	temp[0] = 0;
	USHORT count = plan_clause->nod_count;

	// this must be a join or a merge node, so go through
	// the substreams and place them into the temp vector
	// for formation into a river.
	jrd_nod* plan_node = 0;
	jrd_nod** ptr = plan_clause->nod_arg;
	for (const jrd_nod* const* const end = ptr + count; ptr < end; ptr++)
	{
		plan_node = *ptr;
		if (plan_node->nod_type == nod_merge || plan_node->nod_type == nod_join)
		{
			form_rivers(tdbb, opt, streams, river_stack, sort_clause, plan_node);
			continue;
		}

		// at this point we must have a retrieval node, so put
		// the stream into the river.
		fb_assert(plan_node->nod_type == nod_retrieve);
		const jrd_nod* relation_node = plan_node->nod_arg[e_retrieve_relation];
		const UCHAR stream = (UCHAR)(IPTR) relation_node->nod_arg[e_rel_stream];
		// dimitr:	the plan may contain more retrievals than the "streams"
		//			array (some streams could already be joined to the active
		//			rivers), so we populate the "temp" array only with the
		//			streams that appear in both the plan and the "streams"
		//			array.
		const UCHAR* ptr_stream = streams + 1;
		const UCHAR* const end_stream = ptr_stream + streams[0];
		while (ptr_stream < end_stream)
		{
			if (*ptr_stream++ == stream)
			{
				temp[0]++;
				temp[temp[0]] = stream;
				break;
			}
		}
	}

	// just because the user specified a join does not mean that
	// we are able to form a river;  thus form as many rivers out
	// of the join are as necessary to exhaust the streams.
	// AB: Only form rivers when any retrieval node is seen, for
	// example a MERGE on two JOINs will come with no retrievals
	// at this point.
	// CVC: Notice "plan_node" is pointing to the last element in the loop above.
	// If the loop didn't execute, we had garbage in "plan_node".

	if (temp[0] != 0)
	{
		OptimizerInnerJoin* const innerJoin = FB_NEW(*tdbb->getDefaultPool())
			OptimizerInnerJoin(*tdbb->getDefaultPool(), opt, temp, sort_clause, plan_clause);

		do {
			count = innerJoin->findJoinOrder();
		} while (form_river(tdbb, opt, count, streams, temp, river_stack, sort_clause));

		delete innerJoin;
	}
}


static bool form_river(thread_db*		tdbb,
					   OptimizerBlk*	opt,
					   USHORT			count,
					   const UCHAR*		streams,
					   UCHAR*			temp,
					   RiverStack&		river_stack,
					   jrd_nod**		sort_clause)
{
/**************************************
 *
 *	f o r m _ r i v e r
 *
 **************************************
 *
 * Functional description
 *	Form streams into rivers (combinations of streams).
 *
 **************************************/
	DEV_BLKCHK(opt, type_opt);
	if (sort_clause) {
		DEV_BLKCHK(*sort_clause, type_nod);
	}
	DEV_BLKCHK(plan_clause, type_nod);

	SET_TDBB(tdbb);

	CompilerScratch* const csb = opt->opt_csb;

	// Allocate a river block and move the best order into it.
	River* river = FB_NEW_RPT(*tdbb->getDefaultPool(), count) River();
	river_stack.push(river);
	river->riv_count = (UCHAR) count;

	RecordSource** ptr;

	Array<RecordSource*> rsbs;

	if (count == 1)
	{
		ptr = &river->riv_rsb;
	}
	else
	{
		rsbs.resize(count);
		ptr = rsbs.begin();
	}

	UCHAR* stream = river->riv_streams;
	const OptimizerBlk::opt_stream* const opt_end = opt->opt_streams.begin() + count;
	if (count != streams[0]) {
		sort_clause = NULL;
	}

	for (OptimizerBlk::opt_stream* tail = opt->opt_streams.begin();
		 tail < opt_end; tail++, stream++, ptr++)
	{
		*stream = (UCHAR) tail->opt_best_stream;
		*ptr = gen_retrieval(tdbb, opt, *stream, sort_clause, false, false, NULL);
		sort_clause = NULL;
	}

	if (count > 1)
	{
		river->riv_rsb = FB_NEW(*tdbb->getDefaultPool()) NestedLoopJoin(csb, count, rsbs.begin());
	}

	set_made_river(opt, river);
	set_inactive(opt, river);

	// Reform "temp" from streams not consumed
	stream = temp + 1;
	const UCHAR* const end_stream = stream + temp[0];
	if (!(temp[0] -= count)) {
		return false;
	}

	for (const UCHAR* t2 = stream; t2 < end_stream; t2++)
	{
		bool used = false;

		for (OptimizerBlk::opt_stream* tail = opt->opt_streams.begin(); tail < opt_end; tail++)
		{
			if (*t2 == tail->opt_best_stream)
			{
				used = true;
				break;
			}
		}

		if (!used)
		{
			*stream++ = *t2;
		}
	}

	return true;
}


static RecordSource* gen_aggregate(thread_db* tdbb, OptimizerBlk* opt, jrd_nod* node,
									NodeStack* parent_stack, UCHAR shellStream)
{
/**************************************
 *
 *	g e n _ a g g r e g a t e
 *
 **************************************
 *
 * Functional description
 *	Generate an RecordSource (Record Source Block) for each aggregate operation.
 *	Generate an AggregateSort (Aggregate SortedStream Block) for each DISTINCT aggregate.
 *
 **************************************/
	DEV_BLKCHK(opt, type_opt);
	DEV_BLKCHK(node, type_nod);
	SET_TDBB(tdbb);
	CompilerScratch* const csb = opt->opt_csb;
	RecordSelExpr* rse = (RecordSelExpr*) node->nod_arg[e_agg_rse];
	rse->rse_sorted = node->nod_arg[e_agg_group];
	jrd_nod* map = node->nod_arg[e_agg_map];

	// AB: Try to distribute items from the HAVING CLAUSE to the WHERE CLAUSE.
	// Zip thru stack of booleans looking for fields that belong to shellStream.
	// Those fields are mappings. Mappings that hold a plain field may be used
	// to distribute. Handle the simple cases only.
	NodeStack deliverStack;
	gen_deliver_unmapped(tdbb, &deliverStack, map, parent_stack, shellStream);

	// try to optimize MAX and MIN to use an index; for now, optimize
	// only the simplest case, although it is probably possible
	// to use an index in more complex situations
	jrd_nod** ptr;
	jrd_nod* agg_operator = NULL;

	if (map->nod_count == 1 && (ptr = map->nod_arg) &&
		(agg_operator = (*ptr)->nod_arg[e_asgn_from]) &&
		(agg_operator->nod_type == nod_agg_min || agg_operator->nod_type == nod_agg_max))
	{
		// generate a sort block which the optimizer will try to map to an index

		jrd_nod* aggregate = PAR_make_node(tdbb, 3);
		aggregate->nod_type = nod_sort;
		aggregate->nod_count = 1;
		aggregate->nod_arg[0] = agg_operator->nod_arg[e_asgn_from];
		// in the max case, flag the sort as descending
		if (agg_operator->nod_type == nod_agg_max) {
			aggregate->nod_arg[1] = (jrd_nod*) TRUE;
		}
		// 10-Aug-2004. Nickolay Samofatov
		// Unneeded nulls seem to be skipped somehow.
		aggregate->nod_arg[2] = (jrd_nod*)(IPTR) rse_nulls_default;
		rse->rse_aggregate = aggregate;
	}

	RecordSource* const next_rsb = OPT_compile(tdbb, csb, rse, &deliverStack);

	const UCHAR stream = (UCHAR)(IPTR) node->nod_arg[e_agg_stream];
	fb_assert(stream <= MAX_STREAMS);
	fb_assert(stream <= MAX_UCHAR);

	// allocate and optimize the record source block

	AggregatedStream* const rsb =
		FB_NEW(*tdbb->getDefaultPool()) AggregatedStream(csb, stream, node, next_rsb);

	if (rse->rse_aggregate)
	{
		// The rse_aggregate is still set. That means the optimizer
		// was able to match the field to an index, so flag that fact
		// so that it can be handled in EVL_group
		if (agg_operator->nod_type == nod_agg_min) {
			agg_operator->nod_type = nod_agg_min_indexed;
		}
		else if (agg_operator->nod_type == nod_agg_max) {
			agg_operator->nod_type = nod_agg_max_indexed;
		}
	}

	// Now generate a separate AggregateSort (Aggregate SortedStream Block) for each
	// distinct operation;
	// note that this should be optimized to use indices if possible

	DSC descriptor;
	DSC* desc = &descriptor;
	ptr = map->nod_arg;
	for (const jrd_nod* const* const end = ptr + map->nod_count; ptr < end; ptr++)
	{
		jrd_nod* from = (*ptr)->nod_arg[e_asgn_from];
		if ((from->nod_type == nod_agg_count_distinct)    ||
			(from->nod_type == nod_agg_total_distinct)    ||
			(from->nod_type == nod_agg_total_distinct2)   ||
			(from->nod_type == nod_agg_average_distinct2) ||
			(from->nod_type == nod_agg_average_distinct)  ||
			(from->nod_type == nod_agg_list_distinct))
		{
			// Build the sort key definition. Turn cstrings into varying text.
			CMP_get_desc(tdbb, csb, from->nod_arg[0], desc);
			if (desc->dsc_dtype == dtype_cstring)
			{
				desc->dsc_dtype = dtype_varying;
				desc->dsc_length++;
			}

			const bool asb_intl = desc->isText() && desc->getTextType() != ttype_none &&
				desc->getTextType() != ttype_binary && desc->getTextType() != ttype_ascii;

			const USHORT count = asb_delta + 1 +
				((sizeof(sort_key_def) + sizeof(jrd_nod**)) * (asb_intl ? 2 : 1) - 1) / sizeof(jrd_nod**);
			AggregateSort* asb = (AggregateSort*) PAR_make_node(tdbb, count);
			asb->nod_type = nod_asb;
			asb->asb_intl = asb_intl;
			asb->nod_count = 0;

			sort_key_def* sort_key = asb->asb_key_desc = (sort_key_def*) asb->asb_key_data;
			sort_key->skd_offset = 0;

			if (asb_intl)
			{
				const USHORT key_length = ROUNDUP(INTL_key_length(tdbb,
					INTL_TEXT_TO_INDEX(desc->getTextType()), desc->getStringLength()), sizeof(SINT64));

				sort_key->skd_dtype = SKD_bytes;
				sort_key->skd_flags = SKD_ascending;
				sort_key->skd_length = key_length;
				sort_key->skd_offset = 0;
				sort_key->skd_vary_offset = 0;

				++sort_key;
				asb->asb_length = sort_key->skd_offset = key_length;
			}
			else
			{
				asb->asb_length = 0;
			}

			fb_assert(desc->dsc_dtype < FB_NELEM(sort_dtypes));
			sort_key->skd_dtype = sort_dtypes[desc->dsc_dtype];
			if (!sort_key->skd_dtype)
			{
				ERR_post(Arg::Gds(isc_invalid_sort_datatype) << Arg::Str(DSC_dtype_tostring(desc->dsc_dtype)));
			}

			sort_key->skd_length = desc->dsc_length;

			if (desc->dsc_dtype == dtype_varying)
			{
				// allocate space to store varying length
				sort_key->skd_vary_offset = sort_key->skd_offset + ROUNDUP(desc->dsc_length, sizeof(SLONG));
				asb->asb_length = sort_key->skd_vary_offset + sizeof(USHORT);
			}
			else
			{
				asb->asb_length += sort_key->skd_length;
			}

			sort_key->skd_flags = SKD_ascending;
			asb->nod_impure = CMP_impure(csb, sizeof(impure_agg_sort));
			asb->asb_desc = *desc;
			// store asb as a last argument
			const size_t asb_index = (from->nod_type == nod_agg_list_distinct) ? 2 : 1;
			from->nod_arg[asb_index] = (jrd_nod*) asb;
		}
	}

	if (node->nod_flags & nod_window)
	{
		return FB_NEW(*tdbb->getDefaultPool()) WindowedStream(csb, stream, rsb, next_rsb);
	}

	return rsb;
}


static void gen_deliver_unmapped(thread_db* tdbb, NodeStack* deliverStack,
								 jrd_nod* map, NodeStack* parentStack,
								 UCHAR shellStream)
{
/**************************************
 *
 *	g e n _ d e l i v e r _ u n m a p p e d
 *
 **************************************
 *
 * Functional description
 *	Make new boolean nodes from nodes that
 *	contain a field from the given shellStream.
 *  Those fields are references (mappings) to
 *	other nodes and are used by aggregates and
 *	union rse's.
 *
 **************************************/
	DEV_BLKCHK(map, type_nod);
	SET_TDBB(tdbb);

	for (NodeStack::iterator stack1(*parentStack); stack1.hasData(); ++stack1)
	{
		jrd_nod* boolean = stack1.object();

		// Reduce to simple comparisons
		if (!((boolean->nod_type == nod_eql) ||
			(boolean->nod_type == nod_gtr) ||
			(boolean->nod_type == nod_geq) ||
			(boolean->nod_type == nod_leq) ||
			(boolean->nod_type == nod_lss) ||
			(boolean->nod_type == nod_starts) ||
			(boolean->nod_type == nod_missing)))
		{
			continue;
		}

		// At least 1 mapping should be used in the arguments
		int indexArg;
		bool mappingFound = false;
		for (indexArg = 0; (indexArg < boolean->nod_count) && !mappingFound; indexArg++)
		{
			jrd_nod* booleanNode = boolean->nod_arg[indexArg];
			if ((booleanNode->nod_type == nod_field) &&
				((USHORT)(IPTR) booleanNode->nod_arg[e_fld_stream] == shellStream))
			{
				mappingFound = true;
			}
		}
		if (!mappingFound) {
			continue;
		}

		// Create new node and assign the correct existing arguments
		jrd_nod* deliverNode = PAR_make_node(tdbb, boolean->nod_count);
		deliverNode->nod_count = boolean->nod_count;
		deliverNode->nod_type = boolean->nod_type;
		deliverNode->nod_flags = boolean->nod_flags;
		deliverNode->nod_impure = boolean->nod_impure;
		bool wrongNode = false;
		for (indexArg = 0; (indexArg < boolean->nod_count) && (!wrongNode); indexArg++)
		{

			jrd_nod* booleanNode =
				get_unmapped_node(tdbb, boolean->nod_arg[indexArg], map, shellStream, true);

			wrongNode = (booleanNode == NULL);
			if (!wrongNode) {
				deliverNode->nod_arg[indexArg] = booleanNode;
			}
		}
		if (wrongNode) {
			delete deliverNode;
		}
		else {
			deliverStack->push(deliverNode);
		}
	}
}


static void gen_join(thread_db*		tdbb,
					 OptimizerBlk*	opt,
					 const UCHAR*	streams,
					 RiverStack&	river_stack,
					 jrd_nod**		sort_clause,
					 jrd_nod*		plan_clause)
{
/**************************************
 *
 *	g e n _ j o i n
 *
 **************************************
 *
 * Functional description
 *	Find all indexed relationships between streams,
 *	then form streams into rivers (combinations of
 * 	streams).
 *
 **************************************/
	DEV_BLKCHK(opt, type_opt);
	DEV_BLKCHK(*sort_clause, type_nod);
	DEV_BLKCHK(plan_clause, type_nod);
	SET_TDBB(tdbb);

	//Database* dbb = tdbb->getDatabase();
	//CompilerScratch* csb = opt->opt_csb;

	if (!streams[0]) {
		return;
	}

	if (plan_clause && streams[0] > 1)
	{
		// this routine expects a join/merge
		form_rivers(tdbb, opt, streams, river_stack, sort_clause, plan_clause);
		return;
	}

	OptimizerInnerJoin* const innerJoin = FB_NEW(*tdbb->getDefaultPool())
		OptimizerInnerJoin(*tdbb->getDefaultPool(), opt, streams, sort_clause, plan_clause);

	stream_array_t temp;
	memcpy(temp, streams, streams[0] + 1);

	USHORT count;
	do {
		count = innerJoin->findJoinOrder();
	} while (form_river(tdbb, opt, count, streams, temp, river_stack, sort_clause));

	delete innerJoin;
}


static RecordSource* gen_outer(thread_db* tdbb,
					 OptimizerBlk* opt,
					 RecordSelExpr* rse,
					 RiverStack& river_stack,
					 jrd_nod** sort_clause)
{
/**************************************
 *
 *	g e n _ o u t e r
 *
 **************************************
 *
 * Functional description
 *	Generate a top level outer join.  The "outer" and "inner"
 *	sub-streams must be handled differently from each other.
 *	The inner is like other streams.  The outer stream isn't
 *	because conjuncts may not eliminate records from the
 *	stream.  They only determine if a join with an inner
 *	stream record is to be attempted.
 *
 **************************************/
	struct {
		RecordSource* stream_rsb;
		USHORT stream_num;
	} stream_o, stream_i, *stream_ptr[2];

	DEV_BLKCHK(opt, type_opt);
	DEV_BLKCHK(rse, type_nod);
	DEV_BLKCHK(*sort_clause, type_nod);
	SET_TDBB(tdbb);

	// Determine which stream should be outer and which is inner.
	// In the case of a left join, the syntactically left stream is the
	// outer, and the right stream is the inner.  For all others, swap
	// the sense of inner and outer, though for a full join it doesn't
	// matter and we should probably try both orders to see which is
	// more efficient.
	if (rse->rse_jointype != blr_left)
	{
		stream_ptr[1] = &stream_o;
		stream_ptr[0] = &stream_i;
	}
	else
	{
		stream_ptr[0] = &stream_o;
		stream_ptr[1] = &stream_i;
	}

	// Loop through the outer join sub-streams in
	// reverse order because rivers may have been PUSHed
	for (int i = 1; i >= 0; i--)
	{
		jrd_nod* const node = rse->rse_relation[i];

		if (node->nod_type == nod_union ||
			node->nod_type == nod_aggregate ||
			node->nod_type == nod_procedure ||
			node->nod_type == nod_rse)
		{
			River* const river = river_stack.pop();
			stream_ptr[i]->stream_rsb = river->riv_rsb;
		}
		else
		{
			stream_ptr[i]->stream_rsb = NULL;
			stream_ptr[i]->stream_num = (USHORT)(IPTR) node->nod_arg[STREAM_INDEX(node)];
		}
	}

	CompilerScratch* const csb = opt->opt_csb;

	const bool isFullJoin = (rse->rse_jointype == blr_full);

	if (!isFullJoin)
	{
		// Generate rsbs for the sub-streams.
		// For the left sub-stream we also will get a boolean back.
		jrd_nod* boolean = NULL;
		if (!stream_o.stream_rsb)
		{
			stream_o.stream_rsb = gen_retrieval(tdbb, opt, stream_o.stream_num, sort_clause,
												true, false, &boolean);
		}

		if (!stream_i.stream_rsb)
		{
			// AB: the sort clause for the inner stream of an OUTER JOIN
			//	   should never be used for the index retrieval
			stream_i.stream_rsb =
				gen_retrieval(tdbb, opt, stream_i.stream_num, NULL, false, true, NULL);
		}

		// generate a parent boolean rsb for any remaining booleans that
		// were not satisfied via an index lookup
		stream_i.stream_rsb = gen_residual_boolean(tdbb, opt, stream_i.stream_rsb);

		// Allocate and fill in the rsb
		return FB_NEW(*tdbb->getDefaultPool())
			NestedLoopJoin(csb, stream_o.stream_rsb, stream_i.stream_rsb, boolean, false, false);
	}

	bool hasOuterRsb = true, hasInnerRsb = true;

	jrd_nod* boolean = NULL;
	if (!stream_o.stream_rsb)
	{
		hasOuterRsb = false;
		stream_o.stream_rsb =
			gen_retrieval(tdbb, opt, stream_o.stream_num, NULL, true, false, &boolean);
	}

	if (!stream_i.stream_rsb)
	{
		hasInnerRsb = false;
		stream_i.stream_rsb =
			gen_retrieval(tdbb, opt, stream_i.stream_num, NULL, false, true, NULL);
	}

	stream_i.stream_rsb = gen_residual_boolean(tdbb, opt, stream_i.stream_rsb);

	RecordSource* const rsb1 = FB_NEW(*tdbb->getDefaultPool())
		NestedLoopJoin(csb, stream_o.stream_rsb, stream_i.stream_rsb, boolean, false, false);

	for (size_t i = 0; i < opt->opt_conjuncts.getCount(); i++)
	{
		if (opt->opt_conjuncts[i].opt_conjunct_flags & opt_conjunct_used)
		{
			jrd_nod* const org_node = opt->opt_conjuncts[i].opt_conjunct_node;
			opt->opt_conjuncts[i].opt_conjunct_node = CMP_clone_node_opt(tdbb, csb, org_node);
			opt->opt_conjuncts[i].opt_conjunct_flags = 0;
		}
	}

	if (!hasInnerRsb)
	{
		csb->csb_rpt[stream_i.stream_num].csb_flags &= ~csb_active;
	}
	if (!hasOuterRsb)
	{
		csb->csb_rpt[stream_o.stream_num].csb_flags &= ~csb_active;
	}

	if (!hasInnerRsb)
	{
		stream_i.stream_rsb =
			gen_retrieval(tdbb, opt, stream_i.stream_num, NULL, false, false, NULL);
	}

	if (!hasOuterRsb)
	{
		stream_o.stream_rsb =
			gen_retrieval(tdbb, opt, stream_o.stream_num, NULL, false, false, NULL);
	}

	stream_o.stream_rsb = gen_residual_boolean(tdbb, opt, stream_o.stream_rsb);

	RecordSource* const rsb2 = FB_NEW(*tdbb->getDefaultPool())
		NestedLoopJoin(csb, stream_i.stream_rsb, stream_o.stream_rsb, NULL, false, true);

	return FB_NEW(*tdbb->getDefaultPool()) FullOuterJoin(csb, rsb1, rsb2);
}


static ProcedureScan* gen_procedure(thread_db* tdbb, OptimizerBlk* opt, jrd_nod* node)
{
/**************************************
 *
 *	g e n _ p r o c e d u r e
 *
 **************************************
 *
 * Functional description
 *	Compile and optimize a record selection expression into a
 *	set of record source blocks (rsb's).
 *
 **************************************/
	DEV_BLKCHK(opt, type_opt);
	DEV_BLKCHK(node, type_nod);
	SET_TDBB(tdbb);

	jrd_prc* const procedure =
		MET_lookup_procedure_id(tdbb, (SSHORT)(IPTR)node->nod_arg[e_prc_procedure], false, false, 0);

	CompilerScratch* const csb = opt->opt_csb;
	const UCHAR stream = (UCHAR)(IPTR) node->nod_arg[e_prc_stream];
	CompilerScratch::csb_repeat* const csb_tail = &csb->csb_rpt[stream];
	const string alias = OPT_make_alias(tdbb, csb, csb_tail);

	return FB_NEW(*tdbb->getDefaultPool()) ProcedureScan(csb, alias, stream, procedure,
														 node->nod_arg[e_prc_inputs],
														 node->nod_arg[e_prc_in_msg]);
}


static RecordSource* gen_residual_boolean(thread_db* tdbb, OptimizerBlk* opt, RecordSource* prior_rsb)
{
/**************************************
 *
 *	g e n _ r e s i d u a l _ b o o l e a n
 *
 **************************************
 *
 * Functional description
 *	Pick up any residual boolean remaining,
 *	meaning those that have not been used
 *	as part of some join.  These booleans
 *	must still be applied to the result stream.
 *
 **************************************/
	SET_TDBB(tdbb);
	DEV_BLKCHK(opt, type_opt);
	DEV_BLKCHK(prior_rsb, type_rsb);

	jrd_nod* boolean = NULL;
	const OptimizerBlk::opt_conjunct* const opt_end =
		opt->opt_conjuncts.begin() + opt->opt_base_conjuncts;
	for (OptimizerBlk::opt_conjunct* tail = opt->opt_conjuncts.begin(); tail < opt_end; tail++)
	{
		jrd_nod* node = tail->opt_conjunct_node;
		if (!(tail->opt_conjunct_flags & opt_conjunct_used))
		{
			compose(&boolean, node, nod_and);
			tail->opt_conjunct_flags |= opt_conjunct_used;
		}
	}

	return boolean ? FB_NEW(*tdbb->getDefaultPool())
		FilteredStream(opt->opt_csb, prior_rsb, boolean) : prior_rsb;
}


static RecordSource* gen_retrieval(thread_db*     tdbb,
						 OptimizerBlk*      opt,
						 SSHORT   stream,
						 jrd_nod** sort_ptr,
						 bool     outer_flag,
						 bool     inner_flag,
						 jrd_nod** return_boolean)
{
/**************************************
 *
 *	g e n _ r e t r i e v a l
 *
 **************************************
 *
 * Functional description
 *	Compile and optimize a record selection expression into a
 *	set of record source blocks (rsb's).
 *
 **************************************/
	OptimizerBlk::opt_conjunct* tail;

	SET_TDBB(tdbb);

	DEV_BLKCHK(opt, type_opt);
	if (sort_ptr) {
		DEV_BLKCHK(*sort_ptr, type_nod);
	}
	if (return_boolean) {
		DEV_BLKCHK(*return_boolean, type_nod);
	}

	CompilerScratch* csb = opt->opt_csb;
	CompilerScratch::csb_repeat* csb_tail = &csb->csb_rpt[stream];
	jrd_rel* relation = csb_tail->csb_relation;

	fb_assert(relation);

	const string alias = OPT_make_alias(tdbb, csb, csb_tail);
	csb_tail->csb_flags |= csb_active;

	// Time to find inversions. For each index on the relation
	// match all unused booleans against the index looking for upper
	// and lower bounds that can be computed by the index. When
	// all unused conjunctions are exhausted, see if there is enough
	// information for an index retrieval. If so, build up an
	// inversion component of the boolean.

	// It's recalculated later.
	const OptimizerBlk::opt_conjunct* opt_end = opt->opt_conjuncts.begin() +
		(inner_flag ? opt->opt_base_missing_conjuncts : opt->opt_conjuncts.getCount());

	RecordSource* rsb = NULL;
	IndexTableScan* nav_rsb = NULL;
	jrd_nod* inversion = NULL;

	if (relation->rel_file)
	{
		// External table
		rsb = FB_NEW(*tdbb->getDefaultPool()) ExternalTableScan(csb, alias, stream);
	}
	else if (relation->isVirtual())
	{
		// Virtual table
		rsb = FB_NEW(*tdbb->getDefaultPool()) VirtualTableScan(csb, alias, stream);
	}
	else
	{
		// Persistent table
		OptimizerRetrieval optimizerRetrieval(*tdbb->getDefaultPool(),
												opt, stream, outer_flag, inner_flag, sort_ptr);
		AutoPtr<InversionCandidate> candidate(optimizerRetrieval.getInversion(&nav_rsb));

		if (candidate && candidate->inversion)
		{
			inversion = candidate->inversion;
		}
	}

	if (outer_flag)
	{
		fb_assert(return_boolean);
		// Now make another pass thru the outer conjuncts only, finding unused,
		// computable booleans.  When one is found, roll it into a final
		// boolean and mark it used.
		*return_boolean = NULL;
		opt_end = opt->opt_conjuncts.begin() + opt->opt_base_conjuncts;
		for (tail = opt->opt_conjuncts.begin(); tail < opt_end; tail++)
		{
			jrd_nod* node = tail->opt_conjunct_node;
			if (!(tail->opt_conjunct_flags & opt_conjunct_used) &&
				OPT_computable(csb, node, -1, false, false))
			{
				compose(return_boolean, node, nod_and);
				tail->opt_conjunct_flags |= opt_conjunct_used;
			}
		}
	}

	// Now make another pass thru the conjuncts finding unused, computable
	// booleans.  When one is found, roll it into a final boolean and mark
	// it used. If a computable boolean didn't match against an index then
	// mark the stream to denote unmatched booleans.
	jrd_nod* boolean = NULL;
	opt_end = opt->opt_conjuncts.begin() + (inner_flag ? opt->opt_base_missing_conjuncts : opt->opt_conjuncts.getCount());
	tail = opt->opt_conjuncts.begin();
	if (outer_flag)
	{
		tail += opt->opt_base_parent_conjuncts;
	}

	for (; tail < opt_end; tail++)
	{
		jrd_nod* const node = tail->opt_conjunct_node;
		if (!relation->rel_file && !relation->isVirtual())
		{
			compose(&inversion, make_dbkey(tdbb, opt, node, stream), nod_bit_and);
		}
		if (!(tail->opt_conjunct_flags & opt_conjunct_used) &&
			OPT_computable(csb, node, -1, false, false))
		{
			// If no index is used then leave other nodes alone, because they
			// could be used for building a SORT/MERGE.
			if ((inversion && expression_contains_stream(csb, node, stream, NULL)) ||
				(!inversion && OPT_computable(csb, node, stream, false, true)))
			{
				compose(&boolean, node, nod_and);
				tail->opt_conjunct_flags |= opt_conjunct_used;

				if (!outer_flag && !(tail->opt_conjunct_flags & opt_conjunct_matched))
				{
					csb_tail->csb_flags |= csb_unmatched;
				}
			}
		}
	}

	if (nav_rsb)
	{
		nav_rsb->setInversion(inversion);
		fb_assert(!rsb);
		rsb = nav_rsb;
	}

	if (!rsb)
	{
		if (inversion)
		{
			rsb = FB_NEW(*tdbb->getDefaultPool()) BitmapTableScan(csb, alias, stream, inversion);
		}
		else
		{
			rsb = FB_NEW(*tdbb->getDefaultPool()) FullTableScan(csb, alias, stream);

			if (boolean)
			{
				csb->csb_rpt[stream].csb_flags |= csb_unmatched;
			}
		}
	}

	return boolean ? FB_NEW(*tdbb->getDefaultPool()) FilteredStream(csb, rsb, boolean) : rsb;
}


static SortedStream* gen_sort(thread_db* tdbb,
					  OptimizerBlk* opt,
					  const UCHAR* streams,
					  const UCHAR* dbkey_streams,
					  RecordSource* prior_rsb,
					  jrd_nod* sort,
					  bool project_flag)
{
/**************************************
 *
 *	g e n _ s o r t
 *
 **************************************
 *
 * Functional description
 *	Generate a record source block to handle either a sort or a project.
 *	The two case are virtual identical -- the only difference is that
 *	project eliminates duplicates.  However, since duplicates are
 *	recognized and handled by sort, the JRD processing is identical.
 *
 **************************************/
	DEV_BLKCHK(opt, type_opt);
	DEV_BLKCHK(prior_rsb, type_rsb);
	DEV_BLKCHK(sort, type_nod);
	SET_TDBB(tdbb);

	/* We already know the number of keys, but we also need to compute the
	total number of fields, keys and non-keys, to be pumped thru sort.  Starting
	with the number of keys, count the other field referenced.  Since a field
	is often a key, check for overlap to keep the length of the sort record
	down. */

	/* Along with the record number, the transaction id of the
	 * record will also be stored in the sort file.  This will
	 * be used to detect update conflict in read committed
	 * transactions. */

	const UCHAR* ptr;
	dsc descriptor;

	CompilerScratch* csb = opt->opt_csb;
	ULONG items = sort->nod_count + (streams[0] * 3) + 2 * (dbkey_streams ? dbkey_streams[0] : 0);
	const UCHAR* const end_ptr = streams + streams[0];
	const jrd_nod* const* const end_node = sort->nod_arg + sort->nod_count;
	Firebird::Stack<SLONG> id_stack;
	StreamStack stream_stack;

	for (ptr = &streams[1]; ptr <= end_ptr; ptr++)
	{
		UInt32Bitmap::Accessor accessor(csb->csb_rpt[*ptr].csb_fields);

		if (accessor.getFirst())
		{
			do {
				const ULONG id = accessor.current();
				items++;
				id_stack.push(id);
				stream_stack.push(*ptr);
				for (jrd_nod** node_ptr = sort->nod_arg; node_ptr < end_node; node_ptr++)
				{
					jrd_nod* node = *node_ptr;
					if (node->nod_type == nod_field &&
						(USHORT)(IPTR) node->nod_arg[e_fld_stream] == *ptr &&
						(USHORT)(IPTR) node->nod_arg[e_fld_id] == id)
					{
						dsc* desc = &descriptor;
						CMP_get_desc(tdbb, csb, node, desc);
						// International type text has a computed key
						if (IS_INTL_DATA(desc))
							break;
						--items;
						id_stack.pop();
						stream_stack.pop();
						break;
					}
				}
			} while (accessor.getNext());
		}
	}

	if (items > MAX_USHORT)
		ERR_post(Arg::Gds(isc_imp_exc));

	// Now that we know the number of items, allocate a sort map block.
	SortedStream::SortMap* map =
		FB_NEW(*tdbb->getDefaultPool()) SortedStream::SortMap(*tdbb->getDefaultPool());

	if (project_flag)
		map->flags |= SortedStream::FLAG_PROJECT;

	if (sort->nod_flags & nod_unique_sort)
		map->flags |= SortedStream::FLAG_UNIQUE;

    ULONG map_length = 0;

	// Loop thru sort keys building sort keys.  Actually, to handle null values
	// correctly, two sort keys are made for each field, one for the null flag
	// and one for field itself.

	SortedStream::SortMap::Item* map_item = map->items.getBuffer((USHORT) items);
	sort_key_def* sort_key = map->keyItems.getBuffer(2 * sort->nod_count);

	for (jrd_nod** node_ptr = sort->nod_arg; node_ptr < end_node; node_ptr++, map_item++)
	{
		// Pick up sort key expression.

		jrd_nod* node = *node_ptr;
		dsc* desc = &descriptor;
		CMP_get_desc(tdbb, csb, node, desc);

		// Allow for "key" forms of International text to grow
		if (IS_INTL_DATA(desc))
		{
			// Turn varying text and cstrings into text.

			if (desc->dsc_dtype == dtype_varying)
			{
				desc->dsc_dtype = dtype_text;
				desc->dsc_length -= sizeof(USHORT);
			}
			else if (desc->dsc_dtype == dtype_cstring)
			{
				desc->dsc_dtype = dtype_text;
				desc->dsc_length--;
			}
			desc->dsc_length = INTL_key_length(tdbb, INTL_INDEX_TYPE(desc), desc->dsc_length);
		}

		// Make key for null flag

#ifndef WORDS_BIGENDIAN
		map_length = ROUNDUP(map_length, sizeof(SLONG));
#endif
		const USHORT flag_offset = (USHORT) map_length++;
		sort_key->skd_offset = flag_offset;
		sort_key->skd_dtype = SKD_text;
		sort_key->skd_length = 1;
		// Handle nulls placement
		sort_key->skd_flags = SKD_ascending;
		// Have SQL-compliant nulls ordering for ODS11+
		if (((IPTR) *(node_ptr + sort->nod_count * 2) == rse_nulls_default && !*(node_ptr + sort->nod_count)) ||
			(IPTR) *(node_ptr + sort->nod_count * 2) == rse_nulls_first)
		{
			sort_key->skd_flags |= SKD_descending;
		}
		++sort_key;
		// Make key for sort key proper
#ifndef WORDS_BIGENDIAN
		map_length = ROUNDUP(map_length, sizeof(SLONG));
#else
		if (desc->dsc_dtype >= dtype_aligned)
			map_length = FB_ALIGN(map_length, type_alignments[desc->dsc_dtype]);
#endif
		sort_key->skd_offset = (USHORT) map_length;
		sort_key->skd_flags = SKD_ascending;
		if (*(node_ptr + sort->nod_count))
			sort_key->skd_flags |= SKD_descending;
		fb_assert(desc->dsc_dtype < FB_NELEM(sort_dtypes));
		sort_key->skd_dtype = sort_dtypes[desc->dsc_dtype];
		if (!sort_key->skd_dtype) {
			ERR_post(Arg::Gds(isc_invalid_sort_datatype) << Arg::Str(DSC_dtype_tostring(desc->dsc_dtype)));
		}
		if (sort_key->skd_dtype == SKD_varying || sort_key->skd_dtype == SKD_cstring)
		{
			if (desc->dsc_ttype() == ttype_binary)
				sort_key->skd_flags |= SKD_binary;
		}
		sort_key->skd_length = desc->dsc_length;
		++sort_key;
		map_item->clear();
		map_item->node = node;
		map_item->flagOffset = flag_offset;
		map_item->desc = *desc;
		map_item->desc.dsc_address = (UCHAR*)(IPTR) map_length;
		map_length += desc->dsc_length;
		if (node->nod_type == nod_field)
		{
			map_item->stream = (USHORT)(IPTR) node->nod_arg[e_fld_stream];
			map_item->fieldId = (USHORT)(IPTR) node->nod_arg[e_fld_id];
		}
	}

	map_length = ROUNDUP(map_length, sizeof(SLONG));
	map->keyLength = (USHORT) map_length >> SHIFTLONG;
	USHORT flag_offset = (USHORT) map_length;
	map_length += items - sort->nod_count;
	// Now go back and process all to fields involved with the sort.  If the
	// field has already been mentioned as a sort key, don't bother to repeat it.
	while (stream_stack.hasData())
	{
		const SLONG id = id_stack.pop();
		// AP: why USHORT - we pushed UCHAR
		const USHORT stream = stream_stack.pop();
		const Format* format = CMP_format(tdbb, csb, stream);
		const dsc* desc = &format->fmt_desc[id];
		if (id >= format->fmt_count || desc->dsc_dtype == dtype_unknown)
			IBERROR(157);		// msg 157 cannot sort on a field that does not exist
		if (desc->dsc_dtype >= dtype_aligned)
			map_length = FB_ALIGN(map_length, type_alignments[desc->dsc_dtype]);
		map_item->clear();
		map_item->fieldId = (SSHORT) id;
		map_item->stream = stream;
		map_item->flagOffset = flag_offset++;
		map_item->desc = *desc;
		map_item->desc.dsc_address = (UCHAR*)(IPTR) map_length;
		map_length += desc->dsc_length;
		map_item++;
	}

	// Make fields for record numbers record for all streams

	map_length = ROUNDUP(map_length, sizeof(SINT64));
	for (ptr = &streams[1]; ptr <= end_ptr; ptr++, map_item++)
	{
		map_item->clear();
		map_item->fieldId = SortedStream::ID_DBKEY;
		map_item->stream = *ptr;
		dsc* desc = &map_item->desc;
		desc->dsc_dtype = dtype_int64;
		desc->dsc_length = sizeof(SINT64);
		desc->dsc_address = (UCHAR*)(IPTR) map_length;
		map_length += desc->dsc_length;
	}

	// Make fields for transaction id of record for all streams

	for (ptr = &streams[1]; ptr <= end_ptr; ptr++, map_item++)
	{
		map_item->clear();
		map_item->fieldId = SortedStream::ID_TRANS;
		map_item->stream = *ptr;
		dsc* desc = &map_item->desc;
		desc->dsc_dtype = dtype_long;
		desc->dsc_length = sizeof(SLONG);
		desc->dsc_address = (UCHAR*)(IPTR) map_length;
		map_length += desc->dsc_length;
	}

	if (dbkey_streams)
	{
		const UCHAR* const end_ptrL = dbkey_streams + dbkey_streams[0];

		map_length = ROUNDUP(map_length, sizeof(SINT64));
		for (ptr = &dbkey_streams[1]; ptr <= end_ptrL; ptr++, map_item++)
		{
			map_item->clear();
			map_item->fieldId = SortedStream::ID_DBKEY;
			map_item->stream = *ptr;
			dsc* desc = &map_item->desc;
			desc->dsc_dtype = dtype_int64;
			desc->dsc_length = sizeof(SINT64);
			desc->dsc_address = (UCHAR*)(IPTR) map_length;
			map_length += desc->dsc_length;
		}

		for (ptr = &dbkey_streams[1]; ptr <= end_ptrL; ptr++, map_item++)
		{
			map_item->clear();
			map_item->fieldId = SortedStream::ID_DBKEY_VALID;
			map_item->stream = *ptr;
			dsc* desc = &map_item->desc;
			desc->dsc_dtype = dtype_text;
			desc->dsc_ttype() = CS_BINARY;
			desc->dsc_length = 1;
			desc->dsc_address = (UCHAR*)(IPTR) map_length;
			map_length += desc->dsc_length;
		}
	}

	for (ptr = &streams[1]; ptr <= end_ptr; ptr++, map_item++)
	{
		map_item->clear();
		map_item->fieldId = SortedStream::ID_DBKEY_VALID;
		map_item->stream = *ptr;
		dsc* desc = &map_item->desc;
		desc->dsc_dtype = dtype_text;
		desc->dsc_ttype() = CS_BINARY;
		desc->dsc_length = 1;
		desc->dsc_address = (UCHAR*)(IPTR) map_length;
		map_length += desc->dsc_length;
	}

	fb_assert(map_item - map->items.begin() == (USHORT) items);
	fb_assert(sort_key - map->keyItems.begin() == sort->nod_count * 2);

	map_length = ROUNDUP(map_length, sizeof(SLONG));

	// Make fields to store varying and cstring length.

	const sort_key_def* const end_key = sort_key;
	for (sort_key = map->keyItems.begin(); sort_key < end_key; sort_key++)
	{
		fb_assert(sort_key->skd_dtype != 0);
		if (sort_key->skd_dtype == SKD_varying || sort_key->skd_dtype == SKD_cstring)
		{
			sort_key->skd_vary_offset = (USHORT) map_length;
			map_length += sizeof(USHORT);
		}
	}

	if (map_length > MAX_SORT_RECORD)
	{
		ERR_post(Arg::Gds(isc_sort_rec_size_err) << Arg::Num(map_length));
		// Msg438: sort record size of %ld bytes is too big
	}

	map->length = (USHORT) map_length;

	// That was most unpleasant.  Never the less, it's done (except for the debugging).
	// All that remains is to build the record source block for the sort.
	return FB_NEW(*tdbb->getDefaultPool()) SortedStream(csb, prior_rsb, map);
}


static bool gen_sort_merge(thread_db* tdbb, OptimizerBlk* opt, RiverStack& org_rivers)
{
/**************************************
 *
 *	g e n _ s o r t _ m e r g e
 *
 **************************************
 *
 * Functional description
 *	We've got a set of rivers that may or may not be amenable to
 *	a sort/merge join, and it's time to find out.  If there are,
 *	build a sort/merge RecordSource, push it on the rsb stack, and update
 *	rivers accordingly.  If two or more rivers were successfully
 *	joined, return true.  If the whole things is a moby no-op,
 *	return false.
 *
 **************************************/
	USHORT i;
	ULONG selected_rivers[OPT_STREAM_BITS], selected_rivers2[OPT_STREAM_BITS];
	jrd_nod **eq_class, **ptr;
	DEV_BLKCHK(opt, type_opt);
	SET_TDBB(tdbb);

	// Count the number of "rivers" involved in the operation, then allocate
	// a scratch block large enough to hold values to compute equality
	// classes.
	USHORT cnt = 0;
	for (RiverStack::iterator stack1(org_rivers); stack1.hasData(); ++stack1)
	{
		stack1.object()->riv_number = cnt++;
	}

	Firebird::HalfStaticArray<jrd_nod*, OPT_STATIC_ITEMS> scratch(*tdbb->getDefaultPool());
	scratch.grow(opt->opt_base_conjuncts * cnt);
	jrd_nod** classes = scratch.begin();

	// Compute equivalence classes among streams.  This involves finding groups
	// of streams joined by field equalities.
	jrd_nod** last_class = classes;
	OptimizerBlk::opt_conjunct* tail = opt->opt_conjuncts.begin();
	const OptimizerBlk::opt_conjunct* const end = tail + opt->opt_base_conjuncts;
	for (; tail < end; tail++)
	{
		if (tail->opt_conjunct_flags & opt_conjunct_used)
		{
			continue;
		}

		jrd_nod* node = tail->opt_conjunct_node;

		if (node->nod_type != nod_eql &&
			node->nod_type != nod_equiv)
		{
			continue;
		}

		jrd_nod* node1 = node->nod_arg[0];
		jrd_nod* node2 = node->nod_arg[1];

		for (RiverStack::iterator stack0(org_rivers); stack0.hasData(); ++stack0)
		{
			River* const river1 = stack0.object();

			if (!river_reference(river1, node1))
			{
				if (river_reference(river1, node2))
				{
					node = node1;
					node1 = node2;
					node2 = node;
				}
				else
				{
					continue;
				}
			}

			for (RiverStack::iterator stack2(stack0); (++stack2).hasData();)
			{
				River* const river2 = stack2.object();

				if (river_reference(river2, node2))
				{
					for (eq_class = classes; eq_class < last_class; eq_class += cnt)
					{
						if (node_equality(node1, classes[river1->riv_number]) ||
							node_equality(node2, classes[river2->riv_number]))
						{
							break;
						}
					}

					eq_class[river1->riv_number] = node1;
					eq_class[river2->riv_number] = node2;

					if (eq_class == last_class)
					{
						last_class += cnt;
					}
				}
			}
		}
	}

	// Pick both a set of classes and a set of rivers on which to join with
	// sort merge.  Obviously, if the set of classes is empty, return false
	// to indicate that nothing could be done.

	USHORT river_cnt = 0, stream_cnt = 0;
	Firebird::HalfStaticArray<jrd_nod**, OPT_STATIC_ITEMS> selected_classes(cnt);
	for (eq_class = classes; eq_class < last_class; eq_class += cnt)
	{
		i = river_count(cnt, eq_class);

		if (i > river_cnt)
		{
			river_cnt = i;
			selected_classes.shrink(0);
			selected_classes.add(eq_class);
			class_mask(cnt, eq_class, selected_rivers);
		}
		else
		{
			class_mask(cnt, eq_class, selected_rivers2);

			for (i = 0; i < OPT_STREAM_BITS; i++)
			{
				if ((selected_rivers[i] & selected_rivers2[i]) != selected_rivers[i])
				{
					break;
				}
			}

			if (i == OPT_STREAM_BITS)
			{
				selected_classes.add(eq_class);
			}
		}
	}

	if (!river_cnt)
	{
		return false;
	}

	HalfStaticArray<SortedStream*, OPT_STATIC_ITEMS> sorts;
	HalfStaticArray<jrd_nod*, OPT_STATIC_ITEMS> keys;

	stream_cnt = 0;
	// AB: Get the lowest river position from the rivers that are merged.
	// Note that we're walking the rivers in backwards direction.
	USHORT lowestRiverPosition = 0;
	for (RiverStack::iterator stack3(org_rivers); stack3.hasData(); ++stack3)
	{
		River* const river1 = stack3.object();

		if (!(TEST_DEP_BIT(selected_rivers, river1->riv_number)))
		{
			continue;
		}

		if (river1->riv_number > lowestRiverPosition)
		{
			lowestRiverPosition = river1->riv_number;
		}

		stream_cnt += river1->riv_count;

		jrd_nod* const sort = FB_NEW_RPT(*tdbb->getDefaultPool(), selected_classes.getCount() * 3) jrd_nod();
		sort->nod_type = nod_sort;
		sort->nod_count = (USHORT) selected_classes.getCount();
		jrd_nod*** selected_class;
		for (selected_class = selected_classes.begin(), ptr = sort->nod_arg;
			selected_class < selected_classes.end(); selected_class++)
		{
			ptr[sort->nod_count] = (jrd_nod*) FALSE; // Ascending sort
			ptr[sort->nod_count * 2] = (jrd_nod*)(IPTR) rse_nulls_default; // Default nulls placement
			*ptr++ = (*selected_class)[river1->riv_number];
		}

		sorts.add(gen_sort(tdbb, opt, &river1->riv_count, NULL, river1->riv_rsb, sort, false));
		keys.add(sort);
	}

	fb_assert(sorts.getCount() == keys.getCount());

	// Build a merge stream
	MergeJoin* const merge_rsb = FB_NEW(*tdbb->getDefaultPool())
		MergeJoin(opt->opt_csb, sorts.getCount(), sorts.begin(), keys.begin());

	// Finally, merge selected rivers into a single river, and rebuild
	// original river stack.
	// AB: Be sure that the rivers 'order' will be kept.
	River* river1 = FB_NEW_RPT(*tdbb->getDefaultPool(), stream_cnt) River();
	river1->riv_count = (UCHAR) stream_cnt;
	river1->riv_rsb = merge_rsb;
	UCHAR* stream = river1->riv_streams;
	RiverStack newRivers(org_rivers.getPool());
	while (org_rivers.hasData())
	{
		River* const river2 = org_rivers.pop();

		if (TEST_DEP_BIT(selected_rivers, river2->riv_number))
		{
			memcpy(stream, river2->riv_streams, river2->riv_count);
			stream += river2->riv_count;
			// If this is the lowest position put in the new river.
			if (river2->riv_number == lowestRiverPosition)
			{
				newRivers.push(river1);
			}
		}
		else
		{
			newRivers.push(river2);
		}
	}

	// AB: Put new rivers list back in the original list.
	// Note that the rivers in the new stack are reversed.
	while (newRivers.hasData())
	{
		org_rivers.push(newRivers.pop());
	}

	// Pick up any boolean that may apply.
	{
		USHORT flag_vector[MAX_STREAMS + 1], *fv;
		UCHAR stream_nr;
		// AB: Inactivate currently all streams from every river, because we
		// need to know which nodes are computable between the rivers used
		// for the merge.
		for (stream_nr = 0, fv = flag_vector; stream_nr < opt->opt_csb->csb_n_stream; stream_nr++)
		{
			*fv++ = opt->opt_csb->csb_rpt[stream_nr].csb_flags & csb_active;
			opt->opt_csb->csb_rpt[stream_nr].csb_flags &= ~csb_active;
		}

		set_active(opt, river1);
		jrd_nod* boolean = NULL;
		for (tail = opt->opt_conjuncts.begin(); tail < end; tail++)
		{
			jrd_nod* const node = tail->opt_conjunct_node;

			if (!(tail->opt_conjunct_flags & opt_conjunct_used) &&
				OPT_computable(opt->opt_csb, node, -1, false, false))
			{
				compose(&boolean, node, nod_and);
				tail->opt_conjunct_flags |= opt_conjunct_used;
			}
		}

		if (boolean)
		{
			river1->riv_rsb = FB_NEW(*tdbb->getDefaultPool())
				FilteredStream(opt->opt_csb, river1->riv_rsb, boolean);
		}

		set_inactive(opt, river1);

		for (stream_nr = 0, fv = flag_vector; stream_nr < opt->opt_csb->csb_n_stream; stream_nr++)
		{
			opt->opt_csb->csb_rpt[stream_nr].csb_flags |= *fv++;
		}
	}

	return true;
}


static RecordSource* gen_union(thread_db* tdbb,
							   OptimizerBlk* opt,
							   jrd_nod* union_node,
							   UCHAR* streams,
							   USHORT nstreams,
							   NodeStack* parent_stack,
							   UCHAR shellStream)
{
/**************************************
 *
 *	g e n _ u n i o n
 *
 **************************************
 *
 * Functional description
 *	Generate a union complex.
 *
 **************************************/
	DEV_BLKCHK(opt, type_opt);
	DEV_BLKCHK(union_node, type_nod);

	SET_TDBB(tdbb);
	jrd_nod* clauses = union_node->nod_arg[e_uni_clauses];
	const USHORT count = clauses->nod_count;
	const bool recurse = (union_node->nod_flags & nod_recurse);
	const UCHAR stream = (UCHAR)(IPTR) union_node->nod_arg[e_uni_stream];

	CompilerScratch* csb = opt->opt_csb;

	HalfStaticArray<RecordSource*, OPT_STATIC_ITEMS> rsbs;
	HalfStaticArray<jrd_nod*, OPT_STATIC_ITEMS> maps;

	const SLONG base_impure = CMP_impure(csb, 0);

	jrd_nod** ptr = clauses->nod_arg;
	for (const jrd_nod* const* const end = ptr + count; ptr < end;)
	{

		RecordSelExpr* const rse = (RecordSelExpr*) * ptr++;
		jrd_nod* map = (jrd_nod*) * ptr++;

		// AB: Try to distribute booleans from the top rse for an UNION to
		// the WHERE clause of every single rse.
		// hvlad: don't do it for recursive unions else they will work wrong !
		NodeStack deliverStack;
		if (!recurse)
		{
			gen_deliver_unmapped(tdbb, &deliverStack, map, parent_stack, shellStream);
		}

		rsbs.add(OPT_compile(tdbb, csb, rse, &deliverStack));
		maps.add(map);

		// hvlad: activate recursive union itself after processing first (non-recursive)
		// member to allow recursive members be optimized
		if (recurse)
		{
			const SSHORT stream = (USHORT)(IPTR) union_node->nod_arg[STREAM_INDEX(union_node)];
			csb->csb_rpt[stream].csb_flags |= csb_active;
		}
	}

	if (recurse)
	{
		fb_assert(rsbs.getCount() == 2 && maps.getCount() == 2);
		// hvlad: save size of inner impure area and context of mapped record
		// for recursive processing later
		const UCHAR map_stream = (UCHAR)(IPTR) union_node->nod_arg[e_uni_map_stream];
		return FB_NEW(*tdbb->getDefaultPool()) RecursiveStream(csb, stream, map_stream,
									rsbs[0], rsbs[1], maps[0], maps[1], nstreams, streams, base_impure);
	}

	return FB_NEW(*tdbb->getDefaultPool()) Union(csb, stream, count / 2, rsbs.begin(),
		maps.begin(), nstreams, streams);
}


static void get_expression_streams(const jrd_nod* node, Firebird::SortedArray<int>& streams)
{
/**************************************
 *
 *  g e t _ e x p r e s s i o n _ s t r e a m s
 *
 **************************************
 *
 * Functional description
 *  Return all streams referenced by the expression.
 *
 **************************************/
	DEV_BLKCHK(node, type_nod);

	if (!node) {
		return;
	}

	RecordSelExpr* rse = NULL;

	int n;
	size_t pos;

	switch (node->nod_type)
	{

		case nod_field:
			n = (int)(IPTR) node->nod_arg[e_fld_stream];
			if (!streams.find(n, pos))
				streams.add(n);
			break;

		case nod_rec_version:
		case nod_dbkey:
			n = (int)(IPTR) node->nod_arg[0];
			if (!streams.find(n, pos))
				streams.add(n);
			break;

		case nod_cast:
			get_expression_streams(node->nod_arg[e_cast_source], streams);
			break;

		case nod_extract:
			get_expression_streams(node->nod_arg[e_extract_value], streams);
			break;

		case nod_strlen:
			get_expression_streams(node->nod_arg[e_strlen_value], streams);
			break;

		case nod_function:
			get_expression_streams(node->nod_arg[e_fun_args], streams);
			break;

		case nod_procedure:
			get_expression_streams(node->nod_arg[e_prc_inputs], streams);
			break;

		case nod_any:
		case nod_unique:
		case nod_ansi_any:
		case nod_ansi_all:
		case nod_exists:
			get_expression_streams(node->nod_arg[e_any_rse], streams);
			break;

		case nod_argument:
		case nod_current_date:
		case nod_current_role:
		case nod_current_time:
		case nod_current_timestamp:
		case nod_gen_id:
		case nod_gen_id2:
		case nod_internal_info:
		case nod_literal:
		case nod_null:
		case nod_user_name:
		case nod_variable:
			break;

		case nod_rse:
			rse = (RecordSelExpr*) node;
			break;

		case nod_average:
		case nod_count:
		case nod_from:
		case nod_max:
		case nod_min:
		case nod_total:
			get_expression_streams(node->nod_arg[e_stat_rse], streams);
			get_expression_streams(node->nod_arg[e_stat_value], streams);
			break;

		// go into the node arguments
		case nod_add:
		case nod_add2:
		case nod_agg_average:
		case nod_agg_average2:
		case nod_agg_average_distinct:
		case nod_agg_average_distinct2:
		case nod_agg_max:
		case nod_agg_min:
		case nod_agg_total:
		case nod_agg_total2:
		case nod_agg_total_distinct:
		case nod_agg_total_distinct2:
		case nod_agg_list:
		case nod_agg_list_distinct:
		case nod_concatenate:
		case nod_divide:
		case nod_divide2:
		case nod_multiply:
		case nod_multiply2:
		case nod_negate:
		case nod_subtract:
		case nod_subtract2:

		case nod_upcase:
		case nod_lowcase:
		case nod_substr:
		case nod_trim:
		case nod_sys_function:
		case nod_derived_expr:

		case nod_like:
		case nod_between:
		case nod_similar:
		case nod_sleuth:
		case nod_missing:
		case nod_value_if:
		case nod_matches:
		case nod_contains:
		case nod_starts:
		case nod_equiv:
		case nod_eql:
		case nod_neq:
		case nod_geq:
		case nod_gtr:
		case nod_lss:
		case nod_leq:
		{
			const jrd_nod* const* ptr = node->nod_arg;
			// Check all sub-nodes of this node.
			for (const jrd_nod* const* const end = ptr + node->nod_count; ptr < end; ptr++)
			{
				get_expression_streams(*ptr, streams);
			}
			break;
		}

		default:
			break;
	}

	if (rse) {
		get_expression_streams(rse->rse_first, streams);
		get_expression_streams(rse->rse_skip, streams);
		get_expression_streams(rse->rse_boolean, streams);
		get_expression_streams(rse->rse_sorted, streams);
		get_expression_streams(rse->rse_projection, streams);
	}
}


static jrd_nod* get_unmapped_node(thread_db* tdbb, jrd_nod* node,
	jrd_nod* map, UCHAR shellStream, bool rootNode)
{
/**************************************
 *
 *	g e t _ u n m a p p e d _ n o d e
 *
 **************************************
 *
 *	Return correct node if this node is
 *  allowed in a unmapped boolean.
 *
 **************************************/
	DEV_BLKCHK(map, type_nod);
	SET_TDBB(tdbb);

	// Check if node is a mapping and if so unmap it, but
	// only for root nodes (not contained in another node).
	// This can be expanded by checking complete expression
	// (Then don't forget to leave aggregate-functions alone
	//  in case of aggregate rse)
	// Because this is only to help using an index we keep
	// it simple.
	if ((node->nod_type == nod_field) && ((USHORT)(IPTR) node->nod_arg[e_fld_stream] == shellStream))
	{
		const USHORT fieldId = (USHORT)(IPTR) node->nod_arg[e_fld_id];
		if (!rootNode || (fieldId >= map->nod_count)) {
			return NULL;
		}
		// Check also the expression inside the map, because aggregate
		// functions aren't allowed to be delivered to the WHERE clause.
		return get_unmapped_node(tdbb, map->nod_arg[fieldId]->nod_arg[e_asgn_from],
								 map, shellStream, false);
	}

	jrd_nod* returnNode = NULL;

	switch (node->nod_type)
	{

		case nod_cast:
			if (get_unmapped_node(tdbb, node->nod_arg[e_cast_source], map, shellStream, false) != NULL)
			{
				returnNode = node;
			}
			break;

		case nod_extract:
			if (get_unmapped_node(tdbb, node->nod_arg[e_extract_value], map, shellStream, false) != NULL)
			{
				returnNode = node;
			}
			break;

		case nod_strlen:
			if (get_unmapped_node(tdbb, node->nod_arg[e_strlen_value], map, shellStream, false) != NULL)
			{
				returnNode = node;
			}
			break;

		case nod_argument:
		case nod_current_date:
		case nod_current_role:
		case nod_current_time:
		case nod_current_timestamp:
		case nod_field:
		case nod_gen_id:
		case nod_gen_id2:
		case nod_internal_info:
		case nod_literal:
		case nod_null:
		case nod_user_name:
		case nod_variable:
			returnNode = node;
			break;

		case nod_add:
		case nod_add2:
		case nod_concatenate:
		case nod_divide:
		case nod_divide2:
		case nod_multiply:
		case nod_multiply2:
		case nod_negate:
		case nod_subtract:
		case nod_subtract2:

		case nod_upcase:
		case nod_lowcase:
		case nod_substr:
		case nod_trim:
		case nod_sys_function:
		case nod_derived_expr:
		{
			// Check all sub-nodes of this node.
			jrd_nod** ptr = node->nod_arg;
			for (const jrd_nod* const* const end = ptr + node->nod_count; ptr < end; ptr++)
			{
				if (!get_unmapped_node(tdbb, *ptr, map, shellStream, false))
				{
					return NULL;
				}
			}
			return node;
		}

		default:
			return NULL;
	}

	return returnNode;
}


static RecordSource* make_cross(thread_db* tdbb, OptimizerBlk* opt, RiverStack& stack)
{
/**************************************
 *
 *	m a k e _ c r o s s
 *
 **************************************
 *
 * Functional description
 *	Generate a cross block.
 *
 **************************************/
	DEV_BLKCHK(opt, type_opt);
	SET_TDBB(tdbb);

	const size_t count = stack.getCount();

	if (count == 1)
	{
		return stack.pop()->riv_rsb;
	}

	HalfStaticArray<RecordSource*, OPT_STATIC_ITEMS> rsbs;
	rsbs.resize(count);
	RecordSource** ptr = rsbs.end();
	while (stack.hasData())
	{
		*--ptr = stack.pop()->riv_rsb;
	}

	return FB_NEW(*tdbb->getDefaultPool()) NestedLoopJoin(opt->opt_csb, count, rsbs.begin());
}


jrd_nod* make_dbkey(thread_db* tdbb, OptimizerBlk* opt, jrd_nod* boolean, USHORT stream)
{
/**************************************
 *
 *	m a k e _ d b k e y
 *
 **************************************
 *
 * Functional description
 *	If boolean is an equality comparison on the proper dbkey,
 *	make a "bit_dbkey" operator (makes bitmap out of dbkey
 *	expression.
 *
 *	This is a little hairy, since view dbkeys are expressed as
 *	concatenations of primitive dbkeys.
 *
 **************************************/
	SET_TDBB(tdbb);

	DEV_BLKCHK(opt, type_opt);
	DEV_BLKCHK(boolean, type_nod);

	// If this isn't an equality, it isn't even interesting

	if (boolean->nod_type != nod_eql)
		return NULL;

	// Find the side of the equality that is potentially a dbkey.  If
	// neither, make the obvious deduction

	jrd_nod* dbkey = boolean->nod_arg[0];
	jrd_nod* value = boolean->nod_arg[1];

	if (dbkey->nod_type != nod_dbkey && dbkey->nod_type != nod_concatenate)
	{
		if (value->nod_type != nod_dbkey && value->nod_type != nod_concatenate)
		{
			return NULL;
		}
		dbkey = value;
		value = boolean->nod_arg[0];
	}

	// If the value isn't computable, this has been a waste of time

	CompilerScratch* csb = opt->opt_csb;
	if (!OPT_computable(csb, value, stream, false, false)) {
		return NULL;
	}

	// If this is a concatenation, find an appropriate dbkey

	SLONG n = 0;
	if (dbkey->nod_type == nod_concatenate)
	{
		dbkey = find_dbkey(dbkey, stream, &n);
		if (!dbkey) {
			return NULL;
		}
	}

	// Make sure we have the correct stream

	if ((USHORT)(IPTR) dbkey->nod_arg[0] != stream)
		return NULL;

	// If this is a dbkey for the appropriate stream, it's invertable

	dbkey = PAR_make_node(tdbb, 2);
	dbkey->nod_count = 1;
	dbkey->nod_type = nod_bit_dbkey;
	dbkey->nod_arg[0] = value;
	dbkey->nod_arg[1] = (jrd_nod*)(IPTR) n;
	dbkey->nod_impure = CMP_impure(csb, sizeof(impure_inversion));

	return dbkey;
}


static jrd_nod* make_inference_node(CompilerScratch* csb, jrd_nod* boolean,
									jrd_nod* arg1, jrd_nod* arg2)
{
/**************************************
 *
 *	m a k e _ i n f e r e n c e _ n o d e
 *
 **************************************
 *
 * Defined
 *	1996-Jan-15 David Schnepper
 *
 * Functional description
 *	From the predicate, boolean, and infer a new
 *	predicate using arg1 & arg2 as the first two
 *	parameters to the predicate.
 *
 *	This is used when the engine knows A<B and A=C, and
 *	creates a new node to represent the infered knowledge C<B.
 *
 *	Note that this may be sometimes incorrect with 3-value
 *	logic (per Chris Date's Object & Relations seminar).
 *	Later stages of query evaluation evaluate exactly
 *	the originally specified query, so 3-value issues are
 *	caught there.  Making this inference might cause us to
 *	examine more records than needed, but would not result
 *	in incorrect results.
 *
 *	Note that some nodes, specifically nod_like, have
 *	more than two parameters for a boolean operation.
 *	(nod_like has an optional 3rd parameter for the ESCAPE character
 *	 option of SQL)
 *	Nod_sleuth also has an optional 3rd parameter (for the GDML
 *	matching ESCAPE character language).  But nod_sleuth is
 *	(apparently) not considered during optimization.
 *
 *
 **************************************/
	thread_db* tdbb = JRD_get_thread_data();
	DEV_BLKCHK(csb, type_csb);
	DEV_BLKCHK(boolean, type_nod);
	DEV_BLKCHK(arg1, type_nod);
	DEV_BLKCHK(arg2, type_nod);
	fb_assert(boolean->nod_count >= 2);	// must be a conjunction boolean

	// Clone the input predicate
	jrd_nod* node = PAR_make_node(tdbb, boolean->nod_count);
	node->nod_type = boolean->nod_type;

	// We may safely copy invariantness flag because
	// (1) we only distribute field equalities
	// (2) invariantness of second argument of STARTING WITH or LIKE is solely
	//    determined by its dependency on any of the fields
	// If provisions above change the line below will have to be modified
	node->nod_flags = boolean->nod_flags;

	// But substitute new values for some of the predicate arguments
	node->nod_arg[0] = CMP_clone_node_opt(tdbb, csb, arg1);
	node->nod_arg[1] = CMP_clone_node_opt(tdbb, csb, arg2);

	// Arguments after the first two are just cloned (eg: LIKE ESCAPE clause)
	for (USHORT n = 2; n < boolean->nod_count; n++)
		node->nod_arg[n] = CMP_clone_node_opt(tdbb, csb, boolean->nod_arg[n]);

	// Share impure area for cached invariant value used to hold pre-compiled
	// pattern for new LIKE and CONTAINING algorithms.
	// Proper cloning of impure area for this node would require careful accounting
	// of new invariant dependencies - we avoid such hassles via using single
	// cached pattern value for all node clones. This is faster too.
	if (node->nod_flags & nod_invariant)
		node->nod_impure = boolean->nod_impure;

	return node;
}


static bool map_equal(const jrd_nod* field1, const jrd_nod* field2, const jrd_nod* map)
{
/**************************************
 *
 *	m a p _ e q u a l
 *
 **************************************
 *
 * Functional description
 *	Test to see if two fields are equal, where the fields
 *	are in two different streams possibly mapped to each other.
 *	Order of the input fields is important.
 *
 **************************************/
	DEV_BLKCHK(field1, type_nod);
	DEV_BLKCHK(field2, type_nod);
	DEV_BLKCHK(map, type_nod);
	if (field1->nod_type != nod_field) {
		return false;
	}
	if (field2->nod_type != nod_field) {
		return false;
	}

	// look through the mapping and see if we can find an equivalence.
	const jrd_nod* const* map_ptr = map->nod_arg;
	for (const jrd_nod* const* const map_end = map_ptr + map->nod_count; map_ptr < map_end; map_ptr++)
	{
		const jrd_nod* map_from = (*map_ptr)->nod_arg[e_asgn_from];
		const jrd_nod* map_to = (*map_ptr)->nod_arg[e_asgn_to];
		if (map_from->nod_type != nod_field || map_to->nod_type != nod_field) {
			continue;
		}
		if (field1->nod_arg[e_fld_stream] != map_from->nod_arg[e_fld_stream] ||
			field1->nod_arg[e_fld_id] != map_from->nod_arg[e_fld_id])
		{
			continue;
		}
		if (field2->nod_arg[e_fld_stream] != map_to->nod_arg[e_fld_stream] ||
			field2->nod_arg[e_fld_id] != map_to->nod_arg[e_fld_id])
		{
			continue;
		}
		return true;
	}

	return false;
}



static void mark_indices(CompilerScratch::csb_repeat* csb_tail, SSHORT relation_id)
{
/**************************************
 *
 *	m a r k _ i n d i c e s
 *
 **************************************
 *
 * Functional description
 *	Mark indices that were not included
 *	in the user-specified access plan.
 *
 **************************************/
	const jrd_nod* plan = csb_tail->csb_plan;
	if (!plan)
		return;
	if (plan->nod_type != nod_retrieve)
		return;
	// find out how many indices were specified; if
	// there were none, this is a sequential retrieval
	USHORT plan_count = 0;
	const jrd_nod* access_type = plan->nod_arg[e_retrieve_access_type];
	if (access_type)
		plan_count = access_type->nod_count;
	// go through each of the indices and mark it unusable
	// for indexed retrieval unless it was specifically mentioned
	// in the plan; also mark indices for navigational access
	index_desc* idx = csb_tail->csb_idx->items;
	for (USHORT i = 0; i < csb_tail->csb_indices; i++)
	{
		if (access_type)
		{
			const jrd_nod* const* arg = access_type->nod_arg;
			const jrd_nod* const* const end = arg + plan_count;
			for (; arg < end; arg += e_access_type_length)
			{
				if (relation_id != (SSHORT)(IPTR) arg[e_access_type_relation])
				{
					// index %s cannot be used in the specified plan
					const char* iname = reinterpret_cast<const char*>(arg[e_access_type_index_name]);
					ERR_post(Arg::Gds(isc_index_unused) << Arg::Str(iname));
				}
				if (idx->idx_id == (USHORT)(IPTR) arg[e_access_type_index])
				{
					if (access_type->nod_type == nod_navigational && arg == access_type->nod_arg)
					{
						// dimitr:	navigational access can use only one index,
						//			hence the extra check added (see the line above)
						idx->idx_runtime_flags |= idx_plan_navigate;
					}
					else {
						// nod_indices
						break;
					}
				}
			}

			if (arg == end)
				idx->idx_runtime_flags |= idx_plan_dont_use;
		}
		else {
			idx->idx_runtime_flags |= idx_plan_dont_use;
		}
		++idx;
	}
}


static bool node_equality(const jrd_nod* node1, const jrd_nod* node2)
{
/**************************************
 *
 *	n o d e _ e q u a l i t y
 *
 **************************************
 *
 * Functional description
 *	Test two field node pointers for symbolic equality.
 *
 **************************************/
	DEV_BLKCHK(node1, type_nod);
	DEV_BLKCHK(node2, type_nod);
	if (!node1 || !node2) {
		return false;
	}
	if (node1->nod_type != node2->nod_type) {
		return false;
	}
	if (node1 == node2) {
		return true;
	}
	switch (node1->nod_type)
	{
		case nod_field:
			return (node1->nod_arg[e_fld_stream] == node2->nod_arg[e_fld_stream] &&
					node1->nod_arg[e_fld_id] == node2->nod_arg[e_fld_id]);
		case nod_equiv:
		case nod_eql:
			if (node_equality(node1->nod_arg[0], node2->nod_arg[0]) &&
				node_equality(node1->nod_arg[1], node2->nod_arg[1]))
			{
				return true;
			}
			if (node_equality(node1->nod_arg[0], node2->nod_arg[1]) &&
				 node_equality(node1->nod_arg[1], node2->nod_arg[0]))
			{
				return true;
			}
			return false;

		default:
			break;
	}

	return false;
}


static jrd_nod* optimize_like(thread_db* tdbb, CompilerScratch* csb, jrd_nod* like_node)
{
/**************************************
 *
 *	o p t i m i z e _ l i k e
 *
 **************************************
 *
 * Functional description
 *	Optimize a LIKE expression, if possible,
 *	into a "starting with" AND a "like".  This
 *	will allow us to use the index for the
 *	starting with, and the LIKE can just tag
 *	along for the ride.
 *	But on the ride it does useful work, consider
 *	match LIKE "ab%c".  This is optimized by adding
 *	AND starting_with "ab", but the LIKE clause is
 *	still needed.
 *
 **************************************/
	SET_TDBB(tdbb);
	DEV_BLKCHK(like_node, type_nod);

	jrd_nod* match_node = like_node->nod_arg[0];
	jrd_nod* pattern_node = like_node->nod_arg[1];
	jrd_nod* escape_node = (like_node->nod_count > 2) ? like_node->nod_arg[2] : NULL;

	// if the pattern string or the escape string can't be
	// evaluated at compile time, forget it
	if ((pattern_node->nod_type != nod_literal) || (escape_node && escape_node->nod_type != nod_literal))
	{
		return NULL;
	}

	dsc match_desc;
	CMP_get_desc(tdbb, csb, match_node, &match_desc);

	dsc* pattern_desc = &((Literal*) pattern_node)->lit_desc;
	dsc* escape_desc = 0;
	if (escape_node)
		escape_desc = &((Literal*) escape_node)->lit_desc;

	// if either is not a character expression, forget it
	if ((match_desc.dsc_dtype > dtype_any_text) ||
		(pattern_desc->dsc_dtype > dtype_any_text) ||
		(escape_node && escape_desc->dsc_dtype > dtype_any_text))
	{
		return NULL;
	}

	TextType* matchTextType = INTL_texttype_lookup(tdbb, INTL_TTYPE(&match_desc));
	CharSet* matchCharset = matchTextType->getCharSet();
	TextType* patternTextType = INTL_texttype_lookup(tdbb, INTL_TTYPE(pattern_desc));
	CharSet* patternCharset = patternTextType->getCharSet();

	UCHAR escape_canonic[sizeof(ULONG)];
	UCHAR first_ch[sizeof(ULONG)];
	ULONG first_len;
	UCHAR* p;
	USHORT p_count;

	// Get the escape character, if any
	if (escape_node)
	{
		// Ensure escape string is same character set as match string

		MoveBuffer escape_buffer;

		p_count = MOV_make_string2(tdbb, escape_desc, INTL_TTYPE(&match_desc), &p, escape_buffer);

		first_len = matchCharset->substring(p_count, p, sizeof(first_ch), first_ch, 0, 1);
		matchTextType->canonical(first_len, p, sizeof(escape_canonic), escape_canonic);
	}

	MoveBuffer pattern_buffer;

	p_count = MOV_make_string2(tdbb, pattern_desc, INTL_TTYPE(&match_desc), &p, pattern_buffer);

	first_len = matchCharset->substring(p_count, p, sizeof(first_ch), first_ch, 0, 1);

	UCHAR first_canonic[sizeof(ULONG)];
	matchTextType->canonical(first_len, p, sizeof(first_canonic), first_canonic);

	const BYTE canWidth = matchTextType->getCanonicalWidth();

	// If the first character is a wildcard char, forget it.
	if ((!escape_node ||
			(memcmp(first_canonic, escape_canonic, canWidth) != 0)) &&
		(memcmp(first_canonic, matchTextType->getCanonicalChar(TextType::CHAR_SQL_MATCH_ONE), canWidth) == 0 ||
		 memcmp(first_canonic, matchTextType->getCanonicalChar(TextType::CHAR_SQL_MATCH_ANY), canWidth) == 0))
	{
		return NULL;
	}

	// allocate a literal node to store the starting with string;
	// assume it will be shorter than the pattern string
	// CVC: This assumption may not be true if we use "value like field".
	const SSHORT count = lit_delta + (pattern_desc->dsc_length + sizeof(jrd_nod*) - 1) / sizeof(jrd_nod*);
	jrd_nod* node = PAR_make_node(tdbb, count);
	node->nod_type = nod_literal;
	node->nod_count = 0;
	Literal* literal = (Literal*) node;
	literal->lit_desc = *pattern_desc;
	UCHAR* q = reinterpret_cast<UCHAR*>(literal->lit_data);
	literal->lit_desc.dsc_address = q;

	// copy the string into the starting with literal, up to the first wildcard character

	Firebird::HalfStaticArray<UCHAR, BUFFER_SMALL> patternCanonical;
	ULONG patternCanonicalLen = p_count / matchCharset->minBytesPerChar() * canWidth;

	patternCanonicalLen = matchTextType->canonical(p_count, p,
		patternCanonicalLen, patternCanonical.getBuffer(patternCanonicalLen));

	for (UCHAR* patternPtr = patternCanonical.begin(); patternPtr < patternCanonical.end(); )
	{
		// if there are escape characters, skip past them and
		// don't treat the next char as a wildcard
		const UCHAR* patternPtrStart = patternPtr;
		patternPtr += canWidth;

		if (escape_node && (memcmp(patternPtrStart, escape_canonic, canWidth) == 0))
		{
			// Check for Escape character at end of string
			if (!(patternPtr < patternCanonical.end()))
				break;

			patternPtrStart = patternPtr;
			patternPtr += canWidth;
		}
		else if (memcmp(patternPtrStart, matchTextType->getCanonicalChar(TextType::CHAR_SQL_MATCH_ONE), canWidth) == 0 ||
				 memcmp(patternPtrStart, matchTextType->getCanonicalChar(TextType::CHAR_SQL_MATCH_ANY), canWidth) == 0)
		{
			break;
		}

		q += patternCharset->substring(pattern_desc->dsc_length, pattern_desc->dsc_address,
								literal->lit_desc.dsc_length - (q - literal->lit_desc.dsc_address),
								q, (patternPtrStart - patternCanonical.begin()) / canWidth, 1);
	}

	literal->lit_desc.dsc_length = q - literal->lit_desc.dsc_address;
	return node;
}


static USHORT river_count(USHORT count, jrd_nod** eq_class)
{
/**************************************
 *
 *	r i v e r _ c o u n t
 *
 **************************************
 *
 * Functional description
 *	Given an sort/merge join equivalence class (vector of node pointers
 *	of representative values for rivers), return the count of rivers
 *	with values.
 *
 **************************************/
	if (*eq_class) {
		DEV_BLKCHK(*eq_class, type_nod);
	}

	USHORT cnt = 0;
	for (USHORT i = 0; i < count; i++, eq_class++)
	{
		if (*eq_class)
		{
			cnt++;
			DEV_BLKCHK(*eq_class, type_nod);
		}
	}

	return cnt;
}


static bool river_reference(const River* river, const jrd_nod* node, bool* field_found)
{
/**************************************
 *
 *	r i v e r _ r e f e r e n c e
 *
 **************************************
 *
 * Functional description
 *	See if a value node is a reference to a given river.
 *  AB: Handle also expressions (F1 + F2 * 3, etc..)
 *  The expression is checked if all fields that are
 *  buried inside are pointing to the the given river.
 *  If a passed field isn't referenced by the river then
 *  we have an expression with 2 fields pointing to
 *  different rivers and then the result is always false.
 *  NOTE! The first time this function is called
 *  field_found should be NULL.
 *
 **************************************/
	DEV_BLKCHK(river, type_riv);
	DEV_BLKCHK(node, type_nod);

	bool lfield_found = false;
	bool root_caller = false;

	// If no boolean parameter is given then this is the first call
	// to this function and we use the local boolean to pass to
	// itselfs. The boolean is used to see if any field has passed
	// that references to the river.
	if (!field_found)
	{
		root_caller = true;
		field_found = &lfield_found;
	}

	switch (node->nod_type)
	{

	case nod_field :
		{
			// Check if field references to the river.
			const UCHAR* streams = river->riv_streams;
			for (const UCHAR* const end = streams + river->riv_count; streams < end; streams++)
			{
				if ((USHORT)(IPTR) node->nod_arg[e_fld_stream] == *streams)
				{
					*field_found = true;
					return true;
				}
			}
			return false;
		}

	default :
		{
			const jrd_nod* const* ptr = node->nod_arg;
			// Check all sub-nodes of this node.
			for (const jrd_nod* const* const end = ptr + node->nod_count; ptr < end; ptr++)
			{
				if (!river_reference(river, *ptr, field_found)) {
					return false;
				}
			}
			// if this was the first call then field_found tells
			// us if any field (referenced by river) was found.
			return root_caller ? *field_found : true;
		}
	}

	/*
	// AB: Original code FB1.0 , just left as reference for a while
	UCHAR *streams, *end;
	DEV_BLKCHK(river, type_riv);
	DEV_BLKCHK(node, type_nod);
	if (node->nod_type != nod_field)
		return false;
	for (streams = river->riv_streams, end = streams + river->riv_count; streams < end; streams++)
	{
		if ((USHORT) node->nod_arg[e_fld_stream] == *streams)
			return true;
	}
	return false;
	*/
}


static bool search_stack(const jrd_nod* node, const NodeStack& stack)
{
/**************************************
 *
 *	s e a r c h _ s t a c k
 *
 **************************************
 *
 * Functional description
 *	Search a stack for the presence of a particular value.
 *
 **************************************/
	DEV_BLKCHK(node, type_nod);
	for (NodeStack::const_iterator iter(stack); iter.hasData(); ++iter)
	{
		if (node_equality(node, iter.object())) {
			return true;
		}
	}
	return false;
}


static void set_active(OptimizerBlk* opt, const River* river)
{
/**************************************
 *
 *	s e t _ a c t i v e
 *
 **************************************
 *
 * Functional description
 *	Set a group of streams active.
 *
 **************************************/
	DEV_BLKCHK(opt, type_opt);
	DEV_BLKCHK(river, type_riv);
	CompilerScratch* csb = opt->opt_csb;
	const UCHAR* streams = river->riv_streams;
	for (const UCHAR* const end = streams + river->riv_count; streams < end; streams++)
	{
		csb->csb_rpt[*streams].csb_flags |= csb_active;
	}
}


static void set_direction(const jrd_nod* from_clause, jrd_nod* to_clause)
{
/**************************************
 *
 *	s e t _ d i r e c t i o n
 *
 **************************************
 *
 * Functional description
 *	Update the direction of a GROUP BY, DISTINCT, or ORDER BY
 *	clause to the same direction as another clause. Do the same
 *  for the nulls placement flag.
 *
 **************************************/
	DEV_BLKCHK(from_clause, type_nod);
	DEV_BLKCHK(to_clause, type_nod);
	// Both clauses are allocated with thrice the number of arguments to
	// leave room at the end for an ascending/descending and nulls placement flags,
	// one for each field.
	jrd_nod* const* from_ptr = from_clause->nod_arg;
	jrd_nod** to_ptr = to_clause->nod_arg;
	const ULONG fromCount = from_clause->nod_count;
	const ULONG toCount = to_clause->nod_count;
	for (const jrd_nod* const* const end = from_ptr + fromCount; from_ptr < end; from_ptr++, to_ptr++)
	{
		to_ptr[toCount] = from_ptr[fromCount];
		to_ptr[toCount * 2] = from_ptr[fromCount * 2];
	}
}


static void set_inactive(OptimizerBlk* opt, const River* river)
{
/**************************************
 *
 *	s e t _ i n a c t i v e
 *
 **************************************
 *
 * Functional description
 *	Set a group of streams inactive.
 *
 **************************************/
	DEV_BLKCHK(opt, type_opt);
	DEV_BLKCHK(river, type_riv);
	CompilerScratch* csb = opt->opt_csb;
	const UCHAR* streams = river->riv_streams;
	for (const UCHAR* const end = streams + river->riv_count; streams < end; streams++)
	{
		csb->csb_rpt[*streams].csb_flags &= ~csb_active;
	}
}


static void set_made_river(OptimizerBlk* opt, const River* river)
{
/**************************************
 *
 *	s e t _ m a d e _ r i v e r
 *
 **************************************
 *
 * Functional description
 *      Mark all the streams in a river with the csb_made_river flag.
 *
 *      A stream with this flag set, incicates that this stream has
 *      already been made into a river. Currently, this flag is used
 *      in OPT_computable() to decide if we can use the an index to
 *      optimise retrieving the streams involved in the conjunct.
 *
 *      We can use an index in retrieving the streams involved in a
 *      conjunct if both of the streams are currently active or have
 *      been processed (and made into rivers) before.
 *
 **************************************/
	DEV_BLKCHK(opt, type_opt);
	DEV_BLKCHK(river, type_riv);
	CompilerScratch* csb = opt->opt_csb;
	const UCHAR* streams = river->riv_streams;
	for (const UCHAR* const end = streams + river->riv_count; streams < end; streams++)
	{
		csb->csb_rpt[*streams].csb_flags |= csb_made_river;
	}
}


static void set_position(const jrd_nod* from_clause, jrd_nod* to_clause, const jrd_nod* map)
{
/**************************************
 *
 *	s e t _ p o s i t i o n
 *
 **************************************
 *
 * Functional description
 *	Update the fields in a GROUP BY, DISTINCT, or ORDER BY
 *	clause to the same position as another clause, possibly
 *	using a mapping between the streams.
 *
 **************************************/
	DEV_BLKCHK(from_clause, type_nod);
	DEV_BLKCHK(to_clause, type_nod);
	DEV_BLKCHK(map, type_nod);
	// Track the position in the from list with "to_swap", and find the corresponding
	// field in the from list with "to_ptr", then swap the two fields.  By the time
	// we get to the end of the from list, all fields in the to list will be reordered.
	jrd_nod** to_swap = to_clause->nod_arg;
	const jrd_nod* const* from_ptr = from_clause->nod_arg;
	for (const jrd_nod* const* const from_end = from_ptr + from_clause->nod_count;
		from_ptr < from_end; from_ptr++)
	{
		jrd_nod** to_ptr = to_clause->nod_arg;
		for (const jrd_nod* const* const to_end = to_ptr + from_clause->nod_count;
			to_ptr < to_end; to_ptr++)
		{
			if ((map && map_equal(*to_ptr, *from_ptr, map)) ||
				(!map &&
					(*from_ptr)->nod_arg[e_fld_stream] == (*to_ptr)->nod_arg[e_fld_stream] &&
					(*from_ptr)->nod_arg[e_fld_id] == (*to_ptr)->nod_arg[e_fld_id]))
			{
				jrd_nod* swap = *to_swap;
				*to_swap = *to_ptr;
				*to_ptr = swap;
			}
		}

		to_swap++;
	}

}


static void set_rse_inactive(CompilerScratch* csb, const RecordSelExpr* rse)
{
/***************************************************
 *
 *  s e t _ r s e _ i n a c t i v e
 *
 ***************************************************
 *
 * Functional Description:
 *    Set all the streams involved in an RecordSelExpr as inactive. Do it recursively.
 *
 ***************************************************/
	const jrd_nod* const* ptr = rse->rse_relation;
	for (const jrd_nod* const* const end = ptr + rse->rse_count; ptr < end; ptr++)
	{
		const jrd_nod* node = *ptr;
		if (node->nod_type != nod_rse)
		{
			const SSHORT stream = (USHORT)(IPTR) node->nod_arg[STREAM_INDEX(node)];
			csb->csb_rpt[stream].csb_flags &= ~csb_active;
		}
		else
			set_rse_inactive(csb, (const RecordSelExpr*) node);
	}
}


static void sort_indices_by_selectivity(CompilerScratch::csb_repeat* csb_tail)
{
/***************************************************
 *
 *  s o r t _ i n d i c e s _ b y _ s e l e c t i v i t y
 *
 ***************************************************
 *
 * Functional Description:
 *    Sort SortedStream indices based on there selectivity.
 *    Lowest selectivy as first, highest as last.
 *
 ***************************************************/

	if (csb_tail->csb_plan) {
		return;
	}

	index_desc* selected_idx = NULL;
	Firebird::Array<index_desc> idx_sort(*JRD_get_thread_data()->getDefaultPool(), csb_tail->csb_indices);
	bool same_selectivity = false;

	// Walk through the indices and sort them into into idx_sort
	// where idx_sort[0] contains the lowest selectivity (best) and
	// idx_sort[csb_tail->csb_indices - 1] the highest (worst)

	if (csb_tail->csb_idx && (csb_tail->csb_indices > 1))
	{
		for (USHORT j = 0; j < csb_tail->csb_indices; j++)
		{
			float selectivity = 1; // Maximum selectivity is 1 (when all keys are the same)
			index_desc* idx = csb_tail->csb_idx->items;
			for (USHORT i = 0; i < csb_tail->csb_indices; i++)
			{
				// Prefer ASC indices in the case of almost the same selectivities
				if (selectivity > idx->idx_selectivity) {
					same_selectivity = ((selectivity - idx->idx_selectivity) <= 0.00001);
				}
				else {
					same_selectivity = ((idx->idx_selectivity - selectivity) <= 0.00001);
				}
				if (!(idx->idx_runtime_flags & idx_marker) &&
					 (idx->idx_selectivity <= selectivity) &&
					 !((idx->idx_flags & idx_descending) && same_selectivity))
				{
					selectivity = idx->idx_selectivity;
					selected_idx = idx;
				}
				++idx;
			}

			// If no index was found than pick the first one available out of the list
			if ((!selected_idx) || (selected_idx->idx_runtime_flags & idx_marker))
			{
				idx = csb_tail->csb_idx->items;
				for (USHORT i = 0; i < csb_tail->csb_indices; i++)
				{
					if (!(idx->idx_runtime_flags & idx_marker))
					{
						selected_idx = idx;
						break;
					}
					++idx;
				}
			}

			selected_idx->idx_runtime_flags |= idx_marker;
			idx_sort.add(*selected_idx);
		}

		// Finally store the right order in cbs_tail->csb_idx
		index_desc* idx = csb_tail->csb_idx->items;
		for (USHORT j = 0; j < csb_tail->csb_indices; j++)
		{
			idx->idx_runtime_flags &= ~idx_marker;
			memcpy(idx, &idx_sort[j], sizeof(index_desc));
			++idx;
		}
	}
}
