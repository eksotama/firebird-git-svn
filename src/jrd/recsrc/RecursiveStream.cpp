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
 *  The Original Code was created by Vlad Khorsun
 *  for the Firebird Open Source RDBMS project.
 *
 *  Copyright (c) 2008 Vlad Khorsun <hvlad@users.sourceforge.net>
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
#include "../jrd/exe_proto.h"
#include "../jrd/vio_proto.h"

#include "RecordSource.h"

using namespace Firebird;
using namespace Jrd;

// ----------------------------
// Data access: recursive union
// ----------------------------

RecursiveStream::RecursiveStream(CompilerScratch* csb, UCHAR stream, UCHAR mapStream,
							     RecordSource* root, RecordSource* inner,
							     jrd_nod* rootMap, jrd_nod* innerMap,
							     size_t streamCount, const UCHAR* innerStreams,
							     size_t baseImpure)
	: RecordStream(csb, stream),
	  m_mapStream(mapStream),
	  m_root(m_root), m_inner(inner),
	  m_rootMap(rootMap), m_innerMap(innerMap),
	  m_innerStreams(csb->csb_pool)
{
	fb_assert(m_root && m_inner && m_rootMap && m_innerMap);

	m_impure = CMP_impure(csb, sizeof(Impure));
	m_impureSize = csb->csb_impure - baseImpure;

	m_innerStreams.resize(streamCount);

	for (size_t i = 0; i < streamCount; i++)
	{
		m_innerStreams[i] = innerStreams[i];
	}

	m_root->markRecursive();
	m_inner->markRecursive();
}

void RecursiveStream::open(thread_db* tdbb)
{
	jrd_req* const request = tdbb->getRequest();
	Impure* const impure = (Impure*) ((UCHAR*) request + m_impure);

	impure->irsb_flags = irsb_open;

	VIO_record(tdbb, &request->req_rpb[m_stream], m_format, tdbb->getDefaultPool());
	VIO_record(tdbb, &request->req_rpb[m_mapStream], m_format, tdbb->getDefaultPool());

	// Set up our instance data

	impure->irsb_mode = ROOT;
	impure->irsb_level = 1;
	impure->irsb_stack = NULL;
	impure->irsb_data = NULL;

	// Initialize the record number of each stream in the union

	for (size_t i = 0; i < m_innerStreams.getCount(); i++)
	{
		const UCHAR stream = m_innerStreams[i];
		request->req_rpb[stream].rpb_number.setValue(BOF_NUMBER);
	}

	m_root->open(tdbb);
}

void RecursiveStream::close(thread_db* tdbb)
{
	jrd_req* const request = tdbb->getRequest();

	invalidateRecords(request);

	Impure* const impure = (Impure*) ((UCHAR*) request + m_impure);

	if (impure->irsb_flags & irsb_open)
	{
		impure->irsb_flags &= ~irsb_open;

		while (impure->irsb_level > 1)
		{
			m_inner->close(tdbb);

			delete[] impure->irsb_data;

			UCHAR* const tmp = impure->irsb_stack;
			memcpy(impure, tmp, m_impureSize);
			delete[] tmp;
		}

		m_root->close(tdbb);
	}
}

bool RecursiveStream::getRecord(thread_db* tdbb)
{
	jrd_req* const request = tdbb->getRequest();
	Impure* const impure = (Impure*) ((UCHAR*) request + m_impure);

	if (!(impure->irsb_flags & irsb_open))
	{
		return false;
	}

	const size_t rpbsSize = sizeof(record_param) * m_innerStreams.getCount();
	Record* const record = request->req_rpb[m_stream].rpb_record;
	Record* const mapRecord = request->req_rpb[m_mapStream].rpb_record;

	RecordSource* rsb;

	switch (impure->irsb_mode)
	{
	case ROOT:
		rsb = m_root;
		break;

	case RECURSE:
		{
			// Stop infinite recursion of bad queries
			if (impure->irsb_level > MAX_RECURSE_LEVEL)
			{
				status_exception::raise(Arg::Gds(isc_req_max_clones_exceeded));
			}

			// Save where we are
			UCHAR* const tmp = FB_NEW(*tdbb->getDefaultPool()) UCHAR[m_impureSize + rpbsSize];
			memcpy(tmp, impure, m_impureSize);

			UCHAR* p = tmp + m_impureSize;
			for (size_t i = 0; i < m_innerStreams.getCount(); i++)
			{
				const UCHAR stream = m_innerStreams[i];
				record_param* const rpb = &request->req_rpb[stream];
				memmove(p, rpb, sizeof(record_param));
				p += sizeof(record_param);

				// Don't overwrite record contents at next level of recursion.
				// RSE_open() and company will allocate new record if needed.
				rpb->rpb_record = NULL;
			}
			impure->irsb_stack = tmp;

			impure->irsb_data = FB_NEW(*request->req_pool) UCHAR[record->rec_length];
			memcpy(impure->irsb_data, record->rec_data, record->rec_length);

			const Impure r = *impure;
			memset(impure, 0, m_impureSize);
			*impure = r;

			// (Re-)Open a new child stream & reset record number
			m_inner->open(tdbb);
			rsb = m_inner;

			impure->irsb_level++;
		}
		break;

	default:
		fb_assert(false);
	}

	// Get the data -- if there is none go back one level and when
	// there isn't a previous level, we're done
	while (!rsb->getRecord(tdbb))
	{
		if (impure->irsb_level == 1)
		{
			return false;
		}

		rsb->close(tdbb);
		delete[] impure->irsb_data;

		UCHAR* const tmp = impure->irsb_stack;
		memcpy(impure, tmp, m_impureSize);

		UCHAR* p = tmp + m_impureSize;
		for (size_t i = 0; i < m_innerStreams.getCount(); i++)
		{
			const UCHAR stream = m_innerStreams[i];
			record_param* const rpb = &request->req_rpb[stream];
			Record* const tempRecord = rpb->rpb_record;
			memmove(rpb, p, sizeof(record_param));
			p += sizeof(record_param);

			// We just restored record of current recursion level, delete record
			// from upper level. It may be optimized in the future if needed.
			delete tempRecord;
		}
		delete[] tmp;

		if (impure->irsb_level > 1)
		{
			rsb = m_inner;

			// Reset our record data so that recursive WHERE clauses work
			memcpy(record->rec_data, impure->irsb_data, record->rec_length);
		}
		else
		{
			rsb = m_root;
		}
	}

	impure->irsb_mode = RECURSE;

	// We've got a record, map it into the target record
	jrd_nod* const map = (rsb == m_root) ? m_rootMap : m_innerMap;
	jrd_nod** ptr = map->nod_arg;
	for (const jrd_nod *const *end = ptr + map->nod_count; ptr < end; ptr++)
	{
		EXE_assignment(tdbb, *ptr);
	}

	// copy target (next level) record into main (current level) record
	memcpy(record->rec_data, mapRecord->rec_data, record->rec_length);

	return true;
}

bool RecursiveStream::refetchRecord(thread_db* tdbb)
{
	return true;
}

bool RecursiveStream::lockRecord(thread_db* tdbb)
{
	status_exception::raise(Arg::Gds(isc_record_lock_not_supp));
	return false; // compiler silencer
}

void RecursiveStream::dump(thread_db* tdbb, UCharBuffer& buffer)
{
	buffer.add(isc_info_rsb_begin);

	buffer.add(isc_info_rsb_type);
	buffer.add(isc_info_rsb_left_cross);

	buffer.add(2);
	m_root->dump(tdbb, buffer);
	m_inner->dump(tdbb, buffer);

	buffer.add(isc_info_rsb_end);
}

void RecursiveStream::markRecursive()
{
	m_root->markRecursive();
	m_inner->markRecursive();
}

void RecursiveStream::invalidateRecords(jrd_req* request)
{
	m_root->invalidateRecords(request);
	m_inner->invalidateRecords(request);
}
