/*
 *	PROGRAM:	JRD Access Method
 *	MODULE:		cmp.cpp
 *	DESCRIPTION:	Request compiler
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
 * 2001.07.28: John Bellardo: Added code to handle rse_skip.
 * 2001.07.17 Claudio Valderrama: Stop crash when parsing user-supplied SQL plan.
 * 2001.10.04 Claudio Valderrama: Fix annoying & invalid server complaint about
 *   triggers not having REFERENCES privilege over their owner table.
 * 2002.02.24 Claudio Valderrama: substring() should signal output as string even
 *   if source is blob and should check implementation limits on field lengths.
 * 2002.02.25 Claudio Valderrama: concatenate() should be a civilized function.
 *   This closes the heart of SF Bug #518282.
 * 2002.09.28 Dmitry Yemanov: Reworked internal_info stuff, enhanced
 *                            exception handling in SPs/triggers,
 *                            implemented ROWS_AFFECTED system variable
 * 2002.10.21 Nickolay Samofatov: Added support for explicit pessimistic locks
 * 2002.10.29 Nickolay Samofatov: Added support for savepoints
 * 2002.10.29 Sean Leyne - Removed obsolete "Netware" port
 * 2002.10.30 Sean Leyne - Removed support for obsolete "PC_PLATFORM" define
 * 2003.10.05 Dmitry Yemanov: Added support for explicit cursors in PSQL
 * Adriano dos Santos Fernandes
 */

#include "firebird.h"
#include <string.h>
#include <stdlib.h>				// abort
#include "../common/common.h"
#include "../jrd/ibase.h"
#include "../jrd/jrd.h"
#include "../jrd/req.h"
#include "../jrd/val.h"
#include "../jrd/align.h"
#include "../jrd/lls.h"
#include "../jrd/exe.h"
#include "../jrd/rse.h"
#include "../jrd/scl.h"
#include "../jrd/tra.h"
#include "../jrd/lck.h"
#include "../jrd/irq.h"
#include "../jrd/drq.h"
#include "../jrd/intl.h"
#include "../jrd/btr.h"
#include "../jrd/sort.h"
#include "../common/gdsassert.h"
#include "../jrd/cmp_proto.h"
#include "../common/dsc_proto.h"
#include "../jrd/err_proto.h"
#include "../jrd/exe_proto.h"
#include "../jrd/fun_proto.h"
#include "../yvalve/gds_proto.h"
#include "../jrd/idx_proto.h"
#include "../jrd/intl_proto.h"
#include "../jrd/jrd_proto.h"

#include "../jrd/lck_proto.h"
#include "../jrd/opt_proto.h"
#include "../jrd/par_proto.h"
#include "../jrd/met_proto.h"
#include "../jrd/mov_proto.h"
#include "../common/dsc_proto.h"
#include "../jrd/Optimizer.h"

#include "../jrd/DataTypeUtil.h"
#include "../jrd/SysFunction.h"

// Pick up relation ids
#include "../jrd/ini.h"

#include "../common/classes/auto.h"
#include "../common/utils_proto.h"
#include "../dsql/Nodes.h"
#include "../jrd/RecordSourceNodes.h"
#include "../jrd/ValuesImpl.h"
#include "../jrd/recsrc/RecordSource.h"
#include "../jrd/recsrc/Cursor.h"
#include "../jrd/Function.h"
#include "../dsql/BoolNodes.h"
#include "../dsql/ExprNodes.h"
#include "../dsql/StmtNodes.h"

using namespace Jrd;
using namespace Firebird;


#ifdef CMP_DEBUG
#include <stdarg.h>
IMPLEMENT_TRACE_ROUTINE(cmp_trace, "CMP")
#endif


// Clone a node.
ValueExprNode* CMP_clone_node(thread_db* tdbb, CompilerScratch* csb, ValueExprNode* node)
{
	return NodeCopier::copy(tdbb, csb, node, NULL);
}


// Clone a value node for the optimizer.
// Make a copy of the node (if necessary) and assign impure space.

ValueExprNode* CMP_clone_node_opt(thread_db* tdbb, CompilerScratch* csb, ValueExprNode* node)
{
	SET_TDBB(tdbb);

	DEV_BLKCHK(csb, type_csb);

	if (node->is<ParameterNode>())
		return node;

	ValueExprNode* clone = NodeCopier::copy(tdbb, csb, node, NULL);
	ExprNode::doPass2(tdbb, csb, &clone);

	return clone;
}

BoolExprNode* CMP_clone_node_opt(thread_db* tdbb, CompilerScratch* csb, BoolExprNode* node)
{
	SET_TDBB(tdbb);

	DEV_BLKCHK(csb, type_csb);

	NodeCopier copier(csb, NULL);
	BoolExprNode* clone = copier.copy(tdbb, node);

	ExprNode::doPass2(tdbb, csb, &clone);

	return clone;
}


jrd_req* CMP_compile2(thread_db* tdbb, const UCHAR* blr, ULONG blr_length, bool internal_flag,
					  ULONG dbginfo_length, const UCHAR* dbginfo)
{
/**************************************
 *
 *	C M P _ c o m p i l e 2
 *
 **************************************
 *
 * Functional description
 *	Compile a BLR request.
 *
 **************************************/
	jrd_req* request = NULL;

	SET_TDBB(tdbb);
	Database* const dbb = tdbb->getDatabase();

	// 26.09.2002 Nickolay Samofatov: default memory pool will become statement pool
	// and will be freed by CMP_release
	MemoryPool* const new_pool = dbb->createPool();

	try
	{
		Jrd::ContextPoolHolder context(tdbb, new_pool);

		CompilerScratch* csb =
			PAR_parse(tdbb, blr, blr_length, internal_flag, dbginfo_length, dbginfo);

		request = JrdStatement::makeRequest(tdbb, csb, internal_flag);
		new_pool->setStatsGroup(request->req_memory_stats);

#ifdef CMP_DEBUG
		if (csb->csb_dump.hasData())
		{
			csb->dump("streams:\n");
			for (unsigned i = 0; i < csb->csb_n_stream; ++i)
			{
				const CompilerScratch::csb_repeat& s = csb->csb_rpt[i];
				csb->dump(
					"\t%2d - view_stream: %2d; alias: %s; relation: %s; procedure: %s; view: %s\n",
					i, s.csb_view_stream,
					(s.csb_alias ? s.csb_alias->c_str() : ""),
					(s.csb_relation ? s.csb_relation->rel_name.c_str() : ""),
					(s.csb_procedure ? s.csb_procedure->getName().toString().c_str() : ""),
					(s.csb_view ? s.csb_view->rel_name.c_str() : ""));
			}

			cmp_trace("\n%s\n", csb->csb_dump.c_str());
		}
#endif

		request->getStatement()->verifyAccess(tdbb);

		delete csb;
	}
	catch (const Firebird::Exception& ex)
	{
		Firebird::stuff_exception(tdbb->tdbb_status_vector, ex);
		if (request)
			CMP_release(tdbb, request);
		else
			dbb->deletePool(new_pool);
		ERR_punt();
	}

	return request;
}


CompilerScratch::csb_repeat* CMP_csb_element(CompilerScratch* csb, USHORT element)
{
/**************************************
 *
 *	C M P _ c s b _ e l e m e n t
 *
 **************************************
 *
 * Functional description
 *	Find tail element of compiler scratch block.  If the csb isn't big
 *	enough, extend it.
 *
 **************************************/
	DEV_BLKCHK(csb, type_csb);
	CompilerScratch::csb_repeat empty_item;
	while (element >= csb->csb_rpt.getCount()) {
		csb->csb_rpt.add(empty_item);
	}
	return &csb->csb_rpt[element];
}


void CMP_fini(thread_db* tdbb)
{
/**************************************
 *
 *	C M P _ f i n i
 *
 **************************************
 *
 * Functional description
 *	Get rid of resource locks during shutdown.
 *
 **************************************/
	SET_TDBB(tdbb);
	Database* const dbb = tdbb->getDatabase();

	CMP_shutdown_database(tdbb);	// Shutdown shared database locks.

	// And release the system requests.

	for (JrdStatement** itr = dbb->dbb_internal.begin(); itr != dbb->dbb_internal.end(); ++itr)
	{
		if (*itr)
			(*itr)->release(tdbb);
	}

	for (JrdStatement** itr = dbb->dbb_dyn_req.begin(); itr != dbb->dbb_dyn_req.end(); ++itr)
	{
		if (*itr)
			(*itr)->release(tdbb);
	}
}


const Format* CMP_format(thread_db* tdbb, CompilerScratch* csb, USHORT stream)
{
/**************************************
 *
 *	C M P _ f o r m a t
 *
 **************************************
 *
 * Functional description
 *	Pick up a format for a stream.
 *
 **************************************/
	SET_TDBB(tdbb);

	DEV_BLKCHK(csb, type_csb);

	CompilerScratch::csb_repeat* tail = &csb->csb_rpt[stream];

	if (tail->csb_format) {
		return tail->csb_format;
	}

	if (tail->csb_relation) {
		return tail->csb_format = MET_current(tdbb, tail->csb_relation);
	}

	if (tail->csb_procedure) {
		return tail->csb_format = tail->csb_procedure->prc_format;
	}

	IBERROR(222);				// msg 222 bad blr - invalid stream
	return NULL;
}


IndexLock* CMP_get_index_lock(thread_db* tdbb, jrd_rel* relation, USHORT id)
{
/**************************************
 *
 *	C M P _ g e t _ i n d e x _ l o c k
 *
 **************************************
 *
 * Functional description
 *	Get index lock block for index.  If one doesn't exist,
 *	make one.
 *
 **************************************/
	SET_TDBB(tdbb);
	Database* dbb = tdbb->getDatabase();

	DEV_BLKCHK(relation, type_rel);

	if (relation->rel_id < (USHORT) rel_MAX) {
		return NULL;
	}

	// for to find an existing block

	for (IndexLock* index = relation->rel_index_locks; index; index = index->idl_next)
	{
		if (index->idl_id == id) {
			return index;
		}
	}

	IndexLock* index = FB_NEW(*dbb->dbb_permanent) IndexLock();
	index->idl_next = relation->rel_index_locks;
	relation->rel_index_locks = index;
	index->idl_relation = relation;
	index->idl_id = id;
	index->idl_count = 0;

	Lock* lock = FB_NEW_RPT(*dbb->dbb_permanent, 0) Lock;
	index->idl_lock = lock;
	lock->lck_parent = dbb->dbb_lock;
	lock->lck_dbb = dbb;
	lock->lck_key.lck_long = (relation->rel_id << 16) | id;
	lock->lck_length = sizeof(lock->lck_key.lck_long);
	lock->lck_type = LCK_idx_exist;
	lock->lck_owner_handle = LCK_get_owner_handle(tdbb, lock->lck_type);

	return index;
}


ULONG CMP_impure(CompilerScratch* csb, ULONG size)
{
/**************************************
 *
 *	C M P _ i m p u r e
 *
 **************************************
 *
 * Functional description
 *	Allocate space (offset) in request.
 *
 **************************************/
	DEV_BLKCHK(csb, type_csb);

	if (!csb)
	{
		return 0;
	}

	const ULONG offset = FB_ALIGN(csb->csb_impure, FB_ALIGNMENT);

	if (offset + size > JrdStatement::MAX_REQUEST_SIZE)
	{
		IBERROR(226);	// msg 226: request size limit exceeded
	}

	csb->csb_impure = offset + size;

	return offset;
}


void CMP_post_access(thread_db* tdbb,
					 CompilerScratch* csb,
					 const Firebird::MetaName& security_name,
					 SLONG view_id,
					 SecurityClass::flags_t mask,
					 SLONG type_name,
					 const Firebird::MetaName& name,
					 const Firebird::MetaName& r_name)
{
/**************************************
 *
 *	C M P _ p o s t _ a c c e s s
 *
 **************************************
 *
 * Functional description
 *	Post access to security class to request.
 *      We append the new security class to the existing list of
 *      security classes for that request.
 *
 **************************************/
	DEV_BLKCHK(csb, type_csb);
	DEV_BLKCHK(view, type_rel);

	// allow all access to internal requests

	if (csb->csb_g_flags & (csb_internal | csb_ignore_perm))
		return;

	SET_TDBB(tdbb);

	AccessItem access(security_name, view_id, name, type_name, mask, r_name);

	size_t i;

	if (csb->csb_access.find(access, i))
	{
		return;
	}

	csb->csb_access.insert(i, access);
}


void CMP_post_resource(	ResourceList* rsc_ptr, void* obj, Resource::rsc_s type, USHORT id)
{
/**************************************
 *
 *	C M P _ p o s t _ r e s o u r c e
 *
 **************************************
 *
 * Functional description
 *	Post a resource usage to the compiler scratch block.
 *
 **************************************/
	// Initialize resource block
	Resource resource(type, id, NULL, NULL, NULL);
	switch (type)
	{
	case Resource::rsc_relation:
	case Resource::rsc_index:
		resource.rsc_rel = (jrd_rel*) obj;
		break;
	case Resource::rsc_procedure:
		resource.rsc_prc = (jrd_prc*) obj;
		break;
	case Resource::rsc_collation:
		resource.rsc_coll = (Collation*) obj;
		break;
	case Resource::rsc_function:
		resource.rsc_fun = (Function*) obj;
		break;
	default:
		BUGCHECK(220);			// msg 220 unknown resource
		break;
	}

	// Add it into list if not present already
	size_t pos;
	if (!rsc_ptr->find(resource, pos))
		rsc_ptr->insert(pos, resource);
}


void CMP_decrement_prc_use_count(thread_db* tdbb, jrd_prc* procedure)
{
/*********************************************
 *
 *	C M P _ d e c r e m e n t _ p r c _ u s e _ c o u n t
 *
 *********************************************
 *
 * Functional description
 *	decrement the procedure's use count
 *
 *********************************************/
	// Actually, it's possible for procedures to have intermixed dependencies, so
	// this routine can be called for the procedure which is being freed itself.
	// Hence we should just silently ignore such a situation.

	if (!procedure->prc_use_count)
		return;

	if (procedure->prc_int_use_count > 0)
		procedure->prc_int_use_count--;

	--procedure->prc_use_count;

#ifdef DEBUG_PROCS
	{
		string buffer;
		buffer.printf(
			"Called from CMP_decrement():\n\t Decrementing use count of %s\n",
			procedure->getName()->toString().c_str());
		JRD_print_procedure_info(tdbb, buffer.c_str());
	}
#endif

	// Call recursively if and only if the use count is zero AND the procedure
	// in dbb_procedures is different than this procedure.
	// The procedure will be different than in dbb_procedures only if it is a
	// floating copy, i.e. an old copy or a deleted procedure.
	if ((procedure->prc_use_count == 0) &&
		( (*tdbb->getDatabase()->dbb_procedures)[procedure->getId()] != procedure))
	{
		if (procedure->getStatement())
		{
			procedure->getStatement()->release(tdbb);
			procedure->setStatement(NULL);
		}
		procedure->prc_flags &= ~PRC_being_altered;
		MET_remove_procedure(tdbb, procedure->getId(), procedure);
	}
}


void CMP_release(thread_db* tdbb, jrd_req* request)
{
/**************************************
 *
 *	C M P _ r e l e a s e
 *
 **************************************
 *
 * Functional description
 *	Release a request's statement.
 *
 **************************************/
	DEV_BLKCHK(request, type_req);
	request->getStatement()->release(tdbb);
}


void CMP_shutdown_database(thread_db* tdbb)
{
/**************************************
 *
 *	C M P _ s h u t d o w n _ d a t a b a s e
 *
 **************************************
 *
 * Functional description
 *	Release compile-time locks for database.
 *	Since this can be called at AST level, don't
 *	release any data structures.
 *
 **************************************/
	SET_TDBB(tdbb);
	Database* dbb = tdbb->getDatabase();
	CHECK_DBB(dbb);

	DEV_BLKCHK(dbb, type_dbb);

	// go through relations and indices and release
	// all existence locks that might have been taken

	vec<jrd_rel*>* rvector = dbb->dbb_relations;
	if (rvector)
	{
		vec<jrd_rel*>::iterator ptr, end;
		for (ptr = rvector->begin(), end = rvector->end(); ptr < end; ++ptr)
		{
			jrd_rel* relation = *ptr;
			if (relation)
			{
				if (relation->rel_existence_lock)
				{
					LCK_release(tdbb, relation->rel_existence_lock);
					relation->rel_flags |= REL_check_existence;
					relation->rel_use_count = 0;
				}
				if (relation->rel_partners_lock)
				{
					LCK_release(tdbb, relation->rel_partners_lock);
					relation->rel_flags |= REL_check_partners;
				}
				if (relation->rel_rescan_lock)
				{
					LCK_release(tdbb, relation->rel_rescan_lock);
					relation->rel_flags &= ~REL_scanned;
				}
				for (IndexLock* index = relation->rel_index_locks; index; index = index->idl_next)
				{
					if (index->idl_lock)
					{
						index->idl_count = 0;
						LCK_release(tdbb, index->idl_lock);
					}
				}
			}
		}
	}

	// release all procedure existence locks that might have been taken

	vec<jrd_prc*>* pvector = dbb->dbb_procedures;
	if (pvector)
	{
		vec<jrd_prc*>::iterator pptr, pend;
		for (pptr = pvector->begin(), pend = pvector->end(); pptr < pend; ++pptr)
		{
			jrd_prc* procedure = *pptr;
			if (procedure)
			{
				if (procedure->prc_existence_lock)
				{
					LCK_release(tdbb, procedure->prc_existence_lock);
					procedure->prc_flags |= PRC_check_existence;
					procedure->prc_use_count = 0;
				}
			}
		}
	}

	// release all function existence locks that might have been taken

	for (Function** iter = dbb->dbb_functions.begin(); iter < dbb->dbb_functions.end(); ++iter)
	{
		Function* const function = *iter;

		if (function)
		{
			function->releaseLocks(tdbb);
		}
	}

	// release collation existence locks

	dbb->releaseIntlObjects();
}


UCHAR* CMP_alloc_map(thread_db* tdbb, CompilerScratch* csb, USHORT stream)
{
/**************************************
 *
 *	C M P _ a l l o c _ m a p
 *
 **************************************
 *
 * Functional description
 *	Allocate and initialize stream map for view processing.
 *
 **************************************/
	DEV_BLKCHK(csb, type_csb);

	SET_TDBB(tdbb);

	fb_assert(stream <= MAX_STREAMS); // CVC: MAX_UCHAR maybe?
	UCHAR* const p = FB_NEW(*tdbb->getDefaultPool()) UCHAR[JrdStatement::MAP_LENGTH];
	memset(p, 0, JrdStatement::MAP_LENGTH);
	p[0] = (UCHAR) stream;
	csb->csb_rpt[stream].csb_map = p;

	return p;
}


USHORT NodeCopier::getFieldId(const FieldNode* field)
{
	return field->fieldId;
}


// Expand dbkey for view.
void CMP_expand_view_nodes(thread_db* tdbb, CompilerScratch* csb, USHORT stream,
	ValueExprNodeStack& stack, UCHAR blrOp, bool allStreams)
{
	SET_TDBB(tdbb);

	DEV_BLKCHK(csb, type_csb);

	// if the stream's dbkey should be ignored, do so

	if (!allStreams && (csb->csb_rpt[stream].csb_flags & csb_no_dbkey))
		return;

	// if the stream references a view, follow map
	const UCHAR* map = csb->csb_rpt[stream].csb_map;
	if (map)
	{
		++map;
		while (*map)
			CMP_expand_view_nodes(tdbb, csb, *map++, stack, blrOp, allStreams);
		return;
	}

	// relation is primitive - make dbkey node

	if (allStreams || csb->csb_rpt[stream].csb_relation)
	{
		RecordKeyNode* node = FB_NEW(csb->csb_pool) RecordKeyNode(csb->csb_pool, blrOp);
		node->recStream = stream;
		stack.push(node);
	}
}


// Look at all RseNode's which are lower in scope than the RseNode which this field is
// referencing, and mark them as variant - the rule is that if a field from one RseNode is
// referenced within the scope of another RseNode, the inner RseNode can't be invariant.
// This won't optimize all cases, but it is the simplest operating assumption for now.
void CMP_mark_variant(CompilerScratch* csb, USHORT stream)
{
	if (csb->csb_current_nodes.isEmpty())
		return;

	for (RseOrExprNode* node = csb->csb_current_nodes.end() - 1;
		 node != csb->csb_current_nodes.begin(); --node)
	{
		if (node->rseNode)
		{
			if (node->rseNode->containsStream(stream))
				break;
			node->rseNode->flags |= RseNode::FLAG_VARIANT;
		}
		else if (node->exprNode)
			node->exprNode->nodFlags &= ~ExprNode::FLAG_INVARIANT;
	}
}


// Copy items' information into appropriate node.
ItemInfo* CMP_pass2_validation(thread_db* tdbb, CompilerScratch* csb, const Item& item)
{
	ItemInfo itemInfo;
	return csb->csb_map_item_info.get(item, itemInfo) ?
		FB_NEW(*tdbb->getDefaultPool()) ItemInfo(*tdbb->getDefaultPool(), itemInfo) : NULL;
}


void CMP_post_procedure_access(thread_db* tdbb, CompilerScratch* csb, jrd_prc* procedure)
{
/**************************************
 *
 *	C M P _ p o s t _ p r o c e d u r e _ a c c e s s
 *
 **************************************
 *
 * Functional description
 *
 *	The request will inherit access requirements to all the objects
 *	the called stored procedure has access requirements for.
 *
 **************************************/
	SET_TDBB(tdbb);

	DEV_BLKCHK(csb, type_csb);

	// allow all access to internal requests

	if (csb->csb_g_flags & (csb_internal | csb_ignore_perm))
		return;

	const TEXT* prc_sec_name = procedure->getSecurityName().nullStr();

	// this request must have EXECUTE permission on the stored procedure
	if (procedure->getName().package.isEmpty())
	{
		CMP_post_access(tdbb, csb, prc_sec_name, 0, SCL_execute, SCL_object_procedure,
						procedure->getName().identifier.c_str());
	}
	else
	{
		CMP_post_access(tdbb, csb, prc_sec_name, 0, SCL_execute, SCL_object_package,
						procedure->getName().package.c_str());
	}

	// Add the procedure to list of external objects accessed
	ExternalAccess temp(ExternalAccess::exa_procedure, procedure->getId());
	size_t idx;
	if (!csb->csb_external.find(temp, idx))
		csb->csb_external.insert(idx, temp);
}


RecordSource* CMP_post_rse(thread_db* tdbb, CompilerScratch* csb, RseNode* rse)
{
/**************************************
 *
 *	C M P _ p o s t _ r s e
 *
 **************************************
 *
 * Functional description
 *	Perform actual optimization of an RseNode and clear activity.
 *
 **************************************/
	SET_TDBB(tdbb);

	DEV_BLKCHK(csb, type_csb);
	DEV_BLKCHK(rse, type_nod);

	RecordSource* rsb = OPT_compile(tdbb, csb, rse, NULL);

	if (rse->flags & RseNode::FLAG_SINGULAR)
		rsb = FB_NEW(*tdbb->getDefaultPool()) SingularStream(csb, rsb);

	if (rse->flags & RseNode::FLAG_WRITELOCK)
	{
		for (USHORT i = 0; i < csb->csb_n_stream; i++)
			csb->csb_rpt[i].csb_flags |= csb_update;

		rsb = FB_NEW(*tdbb->getDefaultPool()) LockedStream(csb, rsb);
	}

	if (rse->flags & RseNode::FLAG_SCROLLABLE)
		rsb = FB_NEW(*tdbb->getDefaultPool()) BufferedStream(csb, rsb);

	// mark all the substreams as inactive

	const NestConst<RecordSourceNode>* ptr = rse->rse_relations.begin();
	for (const NestConst<RecordSourceNode>* const end = rse->rse_relations.end(); ptr != end; ++ptr)
	{
		const RecordSourceNode* const node = *ptr;

		StreamsArray streams;
		node->getStreams(streams);

		for (StreamsArray::iterator i = streams.begin(); i != streams.end(); ++i)
			csb->csb_rpt[*i].csb_flags &= ~csb_active;
	}

	return rsb;
}
