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
 *  The Original Code was created by Adriano dos Santos Fernandes
 *  for the Firebird Open Source RDBMS project.
 *
 *  Copyright (c) 2009 Adriano dos Santos Fernandes <adrianosf@gmail.com>
 *  and all contributors signed below.
 *
 *  All Rights Reserved.
 *  Contributor(s): ______________________________________.
 */

#include "firebird.h"
#include "../jrd/common.h"
#include "../jrd/opt_proto.h"
#include "../jrd/exe_proto.h"
#include "RecordSource.h"

using namespace Firebird;
using namespace Jrd;

// ------------------------------
// Data access: window expression
// ------------------------------

namespace
{
	// This stream makes possible to reuse a BufferedStream, so each usage maintains a different
	// cursor position.
	class BufferedStreamWindow : public RecordSource
	{
		struct Impure : public RecordSource::Impure
		{
			FB_UINT64 irsb_position;
		};

	public:
		BufferedStreamWindow(CompilerScratch* csb, BufferedStream* next);

		void open(thread_db* tdbb);
		void close(thread_db* tdbb);

		bool getRecord(thread_db* tdbb);
		bool refetchRecord(thread_db* tdbb);
		bool lockRecord(thread_db* tdbb);

		void dump(thread_db* tdbb, UCharBuffer& buffer);

		void markRecursive();
		void invalidateRecords(jrd_req* request);

		void findUsedStreams(StreamsArray& streams);
		void nullRecords(thread_db* tdbb);
		void saveRecords(thread_db* tdbb);
		void restoreRecords(thread_db* tdbb);

	public:
		BufferedStream* m_next;
	};

	BufferedStreamWindow::BufferedStreamWindow(CompilerScratch* csb, BufferedStream* next)
		: m_next(next)
	{
		m_impure = CMP_impure(csb, sizeof(Impure));
	}

	void BufferedStreamWindow::open(thread_db* tdbb)
	{
		jrd_req* const request = tdbb->getRequest();
		Impure* const impure = (Impure*) ((UCHAR*) request + m_impure);

		impure->irsb_flags = irsb_open;
		impure->irsb_position = 0;
	}

	void BufferedStreamWindow::close(thread_db* tdbb)
	{
		jrd_req* const request = tdbb->getRequest();

		invalidateRecords(request);

		Impure* const impure = (Impure*) ((UCHAR*) request + m_impure);

		if (impure->irsb_flags & irsb_open)
			impure->irsb_flags &= ~irsb_open;
	}

	bool BufferedStreamWindow::getRecord(thread_db* tdbb)
	{
		jrd_req* const request = tdbb->getRequest();
		Impure* const impure = (Impure*) ((UCHAR*) request + m_impure);

		if (!(impure->irsb_flags & irsb_open))
			return false;

		m_next->locate(tdbb, impure->irsb_position++);
		return m_next->getRecord(tdbb);
	}

	bool BufferedStreamWindow::refetchRecord(thread_db* tdbb)
	{
		return m_next->refetchRecord(tdbb);
	}

	bool BufferedStreamWindow::lockRecord(thread_db* tdbb)
	{
		return m_next->lockRecord(tdbb);
	}

	void BufferedStreamWindow::dump(thread_db* tdbb, UCharBuffer& buffer)
	{
		m_next->dump(tdbb, buffer);
	}

	void BufferedStreamWindow::markRecursive()
	{
		m_next->markRecursive();
	}

	void BufferedStreamWindow::findUsedStreams(StreamsArray& streams)
	{
		m_next->findUsedStreams(streams);
	}

	void BufferedStreamWindow::invalidateRecords(jrd_req* request)
	{
		m_next->invalidateRecords(request);
	}

	void BufferedStreamWindow::nullRecords(thread_db* tdbb)
	{
		m_next->nullRecords(tdbb);
	}

	void BufferedStreamWindow::saveRecords(thread_db* tdbb)
	{
		m_next->saveRecords(tdbb);
	}

	void BufferedStreamWindow::restoreRecords(thread_db* tdbb)
	{
		m_next->restoreRecords(tdbb);
	}
}	// namespace

// ------------------------------

WindowedStream::WindowedStream(CompilerScratch* csb, const jrd_nod* nodWindows, RecordSource* next)
	: m_mainMap(NULL)
{
	thread_db* tdbb = JRD_get_thread_data();

	m_next = FB_NEW(csb->csb_pool) BufferedStream(csb, next);
	m_joinedStream = FB_NEW(csb->csb_pool) BufferedStreamWindow(csb, m_next);

	AggregatedStream* mainWindow = NULL;
	StreamsArray streams;
	m_next->findUsedStreams(streams);
	streams.insert(0, streams.getCount());

	// Process the partitions.

	for (unsigned i = 0; i < nodWindows->nod_count; ++i)
	{
		jrd_nod* nodWindow = nodWindows->nod_arg[i];

		jrd_nod* partition = nodWindow->nod_arg[e_part_group];
		jrd_nod* partitionMap = nodWindow->nod_arg[e_part_map];
		jrd_nod* repartition = nodWindow->nod_arg[e_part_regroup];
		USHORT stream = (USHORT)(IPTR) nodWindow->nod_arg[e_part_stream];

		if (!partition)
		{
			// This is the main window. It have special processing.

			m_mainMap = nodWindows->nod_arg[i]->nod_arg[e_part_map];
			USHORT stream = (USHORT)(IPTR) nodWindows->nod_arg[i]->nod_arg[e_part_stream];

			mainWindow = FB_NEW(csb->csb_pool) AggregatedStream(
				csb, stream, partition, m_mainMap,
				FB_NEW(csb->csb_pool) BufferedStreamWindow(csb, m_next));

			OPT_gen_aggregate_distincts(tdbb, csb, m_mainMap);

			continue;
		}

		SortedStream* sortedStream = OPT_gen_sort(tdbb, csb, streams.begin(), NULL,
			FB_NEW(csb->csb_pool) BufferedStreamWindow(csb, m_next), partition, false);
		AggregatedStream* aggStream = FB_NEW(csb->csb_pool) AggregatedStream(
			csb, stream, partition, partitionMap, sortedStream);

		OPT_gen_aggregate_distincts(tdbb, csb, partitionMap);

		m_joinedStream = FB_NEW(csb->csb_pool) HashJoin(csb, m_joinedStream, aggStream,
			partition, repartition);
	}

	if (mainWindow)
	{
		// Make a cross join with the main window.
		RecordSource* rsbs[] = {mainWindow, m_joinedStream};
		m_joinedStream = FB_NEW(csb->csb_pool) NestedLoopJoin(csb, 2, rsbs);
	}
}

void WindowedStream::open(thread_db* tdbb)
{
	jrd_req* const request = tdbb->getRequest();
	Impure* const impure = (Impure*) ((UCHAR*) request + m_impure);

	impure->irsb_flags = irsb_open;

	m_next->open(tdbb);
	m_joinedStream->open(tdbb);
}

void WindowedStream::close(thread_db* tdbb)
{
	jrd_req* const request = tdbb->getRequest();

	invalidateRecords(request);

	Impure* const impure = (Impure*) ((UCHAR*) request + m_impure);

	if (impure->irsb_flags & irsb_open)
	{
		impure->irsb_flags &= ~irsb_open;
		m_joinedStream->close(tdbb);
		m_next->close(tdbb);
	}
}

bool WindowedStream::getRecord(thread_db* tdbb)
{
	jrd_req* const request = tdbb->getRequest();
	Impure* const impure = (Impure*) ((UCHAR*) request + m_impure);

	if (!(impure->irsb_flags & irsb_open))
		return false;

	if (!m_joinedStream->getRecord(tdbb))
		return false;

	// Map the inner stream non-aggregate fields to the main partition.
	if (m_mainMap)
	{
		jrd_nod* const* ptr = m_mainMap->nod_arg;
		for (const jrd_nod* const* end = ptr + m_mainMap->nod_count; ptr < end; ptr++)
		{
			jrd_nod* const from = (*ptr)->nod_arg[e_asgn_from];

			switch (from->nod_type)
			{
				case nod_agg_average:
				case nod_agg_average_distinct:
				case nod_agg_average2:
				case nod_agg_average_distinct2:
				case nod_agg_total:
				case nod_agg_total_distinct:
				case nod_agg_total2:
				case nod_agg_total_distinct2:
				case nod_agg_min:
				case nod_agg_min_indexed:
				case nod_agg_max:
				case nod_agg_max_indexed:
				case nod_agg_count:
				case nod_agg_count2:
				case nod_agg_count_distinct:
				case nod_agg_list:
				case nod_agg_list_distinct:
					break;

				default:
					EXE_assignment(tdbb, *ptr);
					break;
			}
		}
	}

	return true;
}

bool WindowedStream::refetchRecord(thread_db* tdbb)
{
	return m_joinedStream->refetchRecord(tdbb);
}

bool WindowedStream::lockRecord(thread_db* tdbb)
{
	return m_joinedStream->lockRecord(tdbb);
}

void WindowedStream::dump(thread_db* tdbb, UCharBuffer& buffer)
{
	buffer.add(isc_info_rsb_begin);

	buffer.add(isc_info_rsb_type);
	buffer.add(isc_info_rsb_window);

	m_joinedStream->dump(tdbb, buffer);

	buffer.add(isc_info_rsb_end);
}

void WindowedStream::markRecursive()
{
	m_joinedStream->markRecursive();
}

void WindowedStream::invalidateRecords(jrd_req* request)
{
	m_joinedStream->invalidateRecords(request);
}

void WindowedStream::findUsedStreams(StreamsArray& streams)
{
	m_joinedStream->findUsedStreams(streams);
}

void WindowedStream::nullRecords(thread_db* tdbb)
{
	m_joinedStream->nullRecords(tdbb);
}

void WindowedStream::saveRecords(thread_db* tdbb)
{
	m_joinedStream->saveRecords(tdbb);
}

void WindowedStream::restoreRecords(thread_db* tdbb)
{
	m_joinedStream->restoreRecords(tdbb);
}
