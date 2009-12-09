/*
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

#include "firebird.h"
#include "../jrd/common.h"
#include "../jrd/jrd.h"
#include "../jrd/req.h"
#include "../jrd/cmp_proto.h"

#include "RecordSource.h"

using namespace Firebird;
using namespace Jrd;

// ------------------------------
// Data access: single row stream
// ------------------------------

SingularStream::SingularStream(CompilerScratch* csb, RecordSource* next)
	: m_next(next)
{
	fb_assert(m_next);

	m_impure = CMP_impure(csb, sizeof(Impure));
}

void SingularStream::open(thread_db* tdbb)
{
	jrd_req* const request = tdbb->getRequest();
	Impure* const impure = (Impure*) ((UCHAR*) request + m_impure);

	impure->irsb_flags = irsb_open;

	m_next->open(tdbb);
}

void SingularStream::close(thread_db* tdbb)
{
	jrd_req* const request = tdbb->getRequest();

	invalidateRecords(request);

	Impure* const impure = (Impure*) ((UCHAR*) request + m_impure);

	if (impure->irsb_flags & irsb_open)
	{
		impure->irsb_flags &= ~irsb_open;

		m_next->close(tdbb);
	}
}

bool SingularStream::getRecord(thread_db* tdbb)
{
	jrd_req* const request = tdbb->getRequest();
	Impure* const impure = (Impure*) ((UCHAR*) request + m_impure);

	if (!(impure->irsb_flags & irsb_open))
	{
		return false;
	}

	if (impure->irsb_flags & irsb_singular_processed)
	{
		return false;
	}

	if (m_next->getRecord(tdbb))
	{
		saveRecords(tdbb);

		if (m_next->getRecord(tdbb))
		{
			ERR_post(Arg::Gds(isc_sing_select_err));
		}

		restoreRecords(tdbb);

		impure->irsb_flags |= irsb_singular_processed;

		return true;
	}

	return false;
}

bool SingularStream::refetchRecord(thread_db* tdbb)
{
	return m_next->refetchRecord(tdbb);
}

bool SingularStream::lockRecord(thread_db* tdbb)
{
	return m_next->lockRecord(tdbb);
}

void SingularStream::dump(thread_db* tdbb, UCharBuffer& buffer)
{
	buffer.add(isc_info_rsb_begin);

	buffer.add(isc_info_rsb_type);
	buffer.add(isc_info_rsb_singular);

	m_next->dump(tdbb, buffer);

	buffer.add(isc_info_rsb_end);
}

void SingularStream::markRecursive()
{
	m_next->markRecursive();
}

void SingularStream::findUsedStreams(UCHAR* streams)
{
	m_next->findUsedStreams(streams);
}

void SingularStream::invalidateRecords(jrd_req* request)
{
	m_next->invalidateRecords(request);
}

void SingularStream::nullRecords(thread_db* tdbb)
{
	m_next->nullRecords(tdbb);
}

void SingularStream::saveRecords(thread_db* tdbb)
{
	m_next->saveRecords(tdbb);
}

void SingularStream::restoreRecords(thread_db* tdbb)
{
	m_next->restoreRecords(tdbb);
}
