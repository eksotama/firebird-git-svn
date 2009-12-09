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
 *  The Original Code was created by Dmitry Yemanov
 *  for the Firebird Open Source RDBMS project.
 *
 *  Copyright (c) 2009 Dmitry Yemanov <dimitr@firebirdsql.org>
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
#include "../jrd/met_proto.h"
#include "../jrd/vio_proto.h"

#include "RecordSource.h"

using namespace Firebird;
using namespace Jrd;

// ----------------------------
// Data access: full outer join
// ----------------------------

FullOuterJoin::FullOuterJoin(CompilerScratch* csb, RecordSource* arg1, RecordSource* arg2)
	: m_arg1(arg1), m_arg2(arg2)
{
	fb_assert(m_arg1 && m_arg2);

	m_impure = CMP_impure(csb, sizeof(Impure));
}

void FullOuterJoin::open(thread_db* tdbb)
{
	jrd_req* const request = tdbb->getRequest();
	Impure* const impure = (Impure*) ((UCHAR*) request + m_impure);

	impure->irsb_flags = irsb_open | irsb_first;

	m_arg1->open(tdbb);
}

void FullOuterJoin::close(thread_db* tdbb)
{
	jrd_req* const request = tdbb->getRequest();

	invalidateRecords(request);

	Impure* const impure = (Impure*) ((UCHAR*) request + m_impure);

	if (impure->irsb_flags & irsb_open)
	{
		impure->irsb_flags &= ~irsb_open;

		if (impure->irsb_flags & irsb_first)
		{
			m_arg1->close(tdbb);
		}
		else
		{
			m_arg2->close(tdbb);
		}
	}
}

bool FullOuterJoin::getRecord(thread_db* tdbb)
{
	jrd_req* const request = tdbb->getRequest();
	Impure* const impure = (Impure*) ((UCHAR*) request + m_impure);

	if (!(impure->irsb_flags & irsb_open))
	{
		return false;
	}

	if (impure->irsb_flags & irsb_first)
	{
		if (m_arg1->getRecord(tdbb))
		{
			return true;
		}

		impure->irsb_flags &= ~irsb_first;
		m_arg1->close(tdbb);
		m_arg2->open(tdbb);
	}

	if (m_arg2->getRecord(tdbb))
	{
		return true;
	}

	return false;
}

bool FullOuterJoin::refetchRecord(thread_db* tdbb)
{
	return true;
}

bool FullOuterJoin::lockRecord(thread_db* tdbb)
{
	status_exception::raise(Arg::Gds(isc_record_lock_not_supp));
	return false; // compiler silencer
}

void FullOuterJoin::dump(thread_db* tdbb, UCharBuffer& buffer)
{
	buffer.add(isc_info_rsb_begin);

	buffer.add(isc_info_rsb_type);
	buffer.add(isc_info_rsb_union);

	const size_t count = 2;
	buffer.add((UCHAR) count);

	m_arg1->dump(tdbb, buffer);
	m_arg2->dump(tdbb, buffer);

	buffer.add(isc_info_rsb_end);
}

void FullOuterJoin::markRecursive()
{
	m_arg1->markRecursive();
	m_arg2->markRecursive();
}

void FullOuterJoin::findUsedStreams(UCHAR* streams)
{
	m_arg1->findUsedStreams(streams);
	m_arg2->findUsedStreams(streams);
}

void FullOuterJoin::invalidateRecords(jrd_req* request)
{
	m_arg1->invalidateRecords(request);
	m_arg2->invalidateRecords(request);
}

void FullOuterJoin::nullRecords(thread_db* tdbb)
{
	m_arg1->nullRecords(tdbb);
	m_arg2->nullRecords(tdbb);
}

void FullOuterJoin::saveRecords(thread_db* tdbb)
{
	m_arg1->saveRecords(tdbb);
	m_arg2->saveRecords(tdbb);
}

void FullOuterJoin::restoreRecords(thread_db* tdbb)
{
	m_arg1->restoreRecords(tdbb);
	m_arg2->restoreRecords(tdbb);
}
