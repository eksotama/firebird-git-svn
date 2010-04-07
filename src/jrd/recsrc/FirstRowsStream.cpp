/*
 *  The contents of this file are subject to the Initial
 *  Developer's Public License Version 1.0 (the "License");
 *  you may not use this file except in compliance with the
 *  License. You may obtain a copy of the License at
 *  http://www.ibphoenix.com/main.nfs?a=ibphoenix&page=ibp_idpl.
 *
 *  Software distributed under the License is distributed AS IS,
 *  WITHOUT WARRANTY OF ANY KIND, either express or implied.
 *  See the License for the specific language governing rights
 *  and limitations under the License.
 *
 *  The Original Code was created by John Bellardo
 *  for the Firebird Open Source RDBMS project.
 *
 *  Copyright (c) 2000 John Bellardo <bellardo@users.sourceforge.net>
 *  and all contributors signed below.
 *
 *  All Rights Reserved.
 *  Contributor(s): ______________________________________.
 */

#include "firebird.h"
#include "../jrd/common.h"
#include "../jrd/jrd.h"
#include "../jrd/req.h"
#include "../jrd/cmp_proto.h"
#include "../jrd/evl_proto.h"
#include "../jrd/mov_proto.h"

#include "RecordSource.h"

using namespace Firebird;
using namespace Jrd;

// --------------------------------
// Data access: first N rows filter
// --------------------------------

FirstRowsStream::FirstRowsStream(CompilerScratch* csb, RecordSource* next, jrd_nod* value)
	: m_next(next), m_value(value)
{
	fb_assert(m_next && m_value);

	m_impure = CMP_impure(csb, sizeof(Impure));
}

void FirstRowsStream::open(thread_db* tdbb)
{
	jrd_req* const request = tdbb->getRequest();
	Impure* const impure = request->getImpure<Impure>(m_impure);

	impure->irsb_flags = 0;

	const dsc* desc = EVL_expr(tdbb, m_value);
	const SINT64 value = (desc && !(request->req_flags & req_null)) ? MOV_get_int64(desc, 0) : 0;

    if (value < 0)
	{
		status_exception::raise(Arg::Gds(isc_bad_limit_param));
	}

	if (value)
	{
		impure->irsb_flags = irsb_open;
		impure->irsb_count = value;
		m_next->open(tdbb);
	}
}

void FirstRowsStream::close(thread_db* tdbb)
{
	jrd_req* const request = tdbb->getRequest();

	invalidateRecords(request);

	Impure* const impure = request->getImpure<Impure>(m_impure);

	if (impure->irsb_flags & irsb_open)
	{
		impure->irsb_flags &= ~irsb_open;

		m_next->close(tdbb);
	}
}

bool FirstRowsStream::getRecord(thread_db* tdbb)
{
	jrd_req* const request = tdbb->getRequest();
	Impure* const impure = request->getImpure<Impure>(m_impure);

	if (!(impure->irsb_flags & irsb_open))
	{
		return false;
	}

	if (impure->irsb_count <= 0)
	{
		invalidateRecords(request);
		return false;
	}

	impure->irsb_count--;

	return m_next->getRecord(tdbb);
}

bool FirstRowsStream::refetchRecord(thread_db* tdbb)
{
	return m_next->refetchRecord(tdbb);
}

bool FirstRowsStream::lockRecord(thread_db* tdbb)
{
	return m_next->lockRecord(tdbb);
}

void FirstRowsStream::dump(thread_db* tdbb, UCharBuffer& buffer)
{
	buffer.add(isc_info_rsb_begin);

	buffer.add(isc_info_rsb_type);
	buffer.add(isc_info_rsb_first);

	m_next->dump(tdbb, buffer);

	buffer.add(isc_info_rsb_end);
}

void FirstRowsStream::markRecursive()
{
	m_next->markRecursive();
}

void FirstRowsStream::findUsedStreams(StreamsArray& streams)
{
	m_next->findUsedStreams(streams);
}

void FirstRowsStream::invalidateRecords(jrd_req* request)
{
	m_next->invalidateRecords(request);
}

void FirstRowsStream::nullRecords(thread_db* tdbb)
{
	m_next->nullRecords(tdbb);
}

void FirstRowsStream::saveRecords(thread_db* tdbb)
{
	m_next->saveRecords(tdbb);
}

void FirstRowsStream::restoreRecords(thread_db* tdbb)
{
	m_next->restoreRecords(tdbb);
}