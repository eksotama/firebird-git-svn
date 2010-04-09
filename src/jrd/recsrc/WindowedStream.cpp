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
#include "../dsql/Nodes.h"
#include "../jrd/mov_proto.h"
#include "../jrd/opt_proto.h"
#include "../jrd/evl_proto.h"
#include "../jrd/exe_proto.h"
#include "../jrd/par_proto.h"
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
	class BufferedStreamWindow : public BaseBufferedStream
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

		void locate(thread_db* tdbb, FB_UINT64 position)
		{
			jrd_req* const request = tdbb->getRequest();
			Impure* const impure = request->getImpure<Impure>(m_impure);
			impure->irsb_position = position;
		}

		FB_UINT64 getCount(jrd_req* request) const
		{
			return m_next->getCount(request);
		}

		FB_UINT64 getPosition(jrd_req* request) const
		{
			Impure* const impure = request->getImpure<Impure>(m_impure);
			return impure->irsb_position;
		}

	public:
		BufferedStream* m_next;
	};

	// Make join between outer stream and already sorted (aggregated) partition.
	class WindowJoin : public RecordSource
	{
		struct DscNull
		{
			dsc* desc;
			bool null;
		};

		struct Impure : public RecordSource::Impure
		{
			FB_UINT64 innerRecordCount;
		};

	public:
		WindowJoin(CompilerScratch* csb, RecordSource* outer, RecordSource* inner,
			const jrd_nod* outerKeys, const jrd_nod* innerKeys);

		void open(thread_db* tdbb);
		void close(thread_db* tdbb);

		bool getRecord(thread_db* tdbb);
		bool refetchRecord(thread_db* tdbb);
		bool lockRecord(thread_db* tdbb);

		void dump(thread_db* tdbb, Firebird::UCharBuffer& buffer);

		void markRecursive();
		void invalidateRecords(jrd_req* request);

		void findUsedStreams(StreamsArray& streams);
		void nullRecords(thread_db* tdbb);
		void saveRecords(thread_db* tdbb);
		void restoreRecords(thread_db* tdbb);

	private:
		int compareKeys(thread_db* tdbb, jrd_req* request, DscNull* outerValues);

		RecordSource* const m_outer;
		BufferedStream* const m_inner;
		const jrd_nod* const m_outerKeys;
		const jrd_nod* const m_innerKeys;
	};

	// BufferedStreamWindow implementation

	BufferedStreamWindow::BufferedStreamWindow(CompilerScratch* csb, BufferedStream* next)
		: m_next(next)
	{
		m_impure = CMP_impure(csb, sizeof(Impure));
	}

	void BufferedStreamWindow::open(thread_db* tdbb)
	{
		jrd_req* const request = tdbb->getRequest();
		Impure* const impure = request->getImpure<Impure>(m_impure);

		impure->irsb_flags = irsb_open;
		impure->irsb_position = 0;
	}

	void BufferedStreamWindow::close(thread_db* tdbb)
	{
		jrd_req* const request = tdbb->getRequest();

		invalidateRecords(request);

		Impure* const impure = request->getImpure<Impure>(m_impure);

		if (impure->irsb_flags & irsb_open)
			impure->irsb_flags &= ~irsb_open;
	}

	bool BufferedStreamWindow::getRecord(thread_db* tdbb)
	{
		jrd_req* const request = tdbb->getRequest();
		Impure* const impure = request->getImpure<Impure>(m_impure);

		if (!(impure->irsb_flags & irsb_open))
			return false;

		m_next->locate(tdbb, impure->irsb_position);
		if (!m_next->getRecord(tdbb))
			return false;

		++impure->irsb_position;
		return true;
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

	// WindowJoin implementation

	WindowJoin::WindowJoin(CompilerScratch* csb, RecordSource* outer, RecordSource* inner,
					   const jrd_nod* outerKeys, const jrd_nod* innerKeys)
		: m_outer(outer), m_inner(FB_NEW(csb->csb_pool) BufferedStream(csb, inner)),
		  m_outerKeys(outerKeys), m_innerKeys(innerKeys)
	{
		fb_assert(m_outer && m_inner && m_innerKeys->nod_count == m_outerKeys->nod_count);
		fb_assert(m_outerKeys && (m_outerKeys->nod_type == nod_list || m_outerKeys->nod_type == nod_sort));
		fb_assert(m_innerKeys && (m_innerKeys->nod_type == nod_list || m_innerKeys->nod_type == nod_sort));

		m_impure = CMP_impure(csb, sizeof(Impure));
	}

	void WindowJoin::open(thread_db* tdbb)
	{
		jrd_req* const request = tdbb->getRequest();
		Impure* const impure = request->getImpure<Impure>(m_impure);

		impure->irsb_flags = irsb_open;

		// Read and cache the inner stream. Also gets its total number of records.

		m_inner->open(tdbb);
		FB_UINT64 position = 0;

		while (m_inner->getRecord(tdbb))
			++position;

		impure->innerRecordCount = position;

		m_outer->open(tdbb);
	}

	void WindowJoin::close(thread_db* tdbb)
	{
		jrd_req* const request = tdbb->getRequest();

		invalidateRecords(request);

		Impure* const impure = request->getImpure<Impure>(m_impure);

		if (impure->irsb_flags & irsb_open)
		{
			impure->irsb_flags &= ~irsb_open;

			m_outer->close(tdbb);
			m_inner->close(tdbb);
		}
	}

	bool WindowJoin::getRecord(thread_db* tdbb)
	{
		jrd_req* const request = tdbb->getRequest();
		Impure* const impure = request->getImpure<Impure>(m_impure);

		if (!(impure->irsb_flags & irsb_open))
			return false;

		if (!m_outer->getRecord(tdbb))
			return false;

		// Evaluate the outer stream keys.

		HalfStaticArray<DscNull, 8> outerValues;
		DscNull* outerValue = outerValues.getBuffer(m_outerKeys->nod_count);

		for (unsigned i = 0; i < m_outerKeys->nod_count; ++i)
		{
			outerValue->desc = EVL_expr(tdbb, m_outerKeys->nod_arg[i]);
			outerValue->null = (request->req_flags & req_null);
			++outerValue;
		}

		outerValue -= m_outerKeys->nod_count;	// go back to begin

		// Join the streams. That should be a 1-to-1 join.

		SINT64 start = 0;
		SINT64 finish = impure->innerRecordCount;
		SINT64 pos = finish / 2;

		while (pos >= start && pos < finish)
		{
			m_inner->locate(tdbb, pos);
			if (!m_inner->getRecord(tdbb))
			{
				fb_assert(false);
				return false;
			}

			int cmp = compareKeys(tdbb, request, outerValue);

			if (cmp == 0)
				return true;

			if (cmp < 0)
			{
				finish = pos;
				pos -= MAX(1, (finish - start) / 2);
			}
			else //if (cmp > 0)
			{
				start = pos;
				pos += MAX(1, (finish - start) / 2);
			}
		}

		fb_assert(false);

		return false;
	}

	bool WindowJoin::refetchRecord(thread_db* tdbb)
	{
		return true;
	}

	bool WindowJoin::lockRecord(thread_db* tdbb)
	{
		status_exception::raise(Arg::Gds(isc_record_lock_not_supp));
		return false; // compiler silencer
	}

	void WindowJoin::dump(thread_db* tdbb, UCharBuffer& buffer)
	{
		buffer.add(isc_info_rsb_begin);

		m_outer->dump(tdbb, buffer);
		m_inner->dump(tdbb, buffer);

		buffer.add(isc_info_rsb_end);
	}

	void WindowJoin::markRecursive()
	{
		m_outer->markRecursive();
		m_inner->markRecursive();
	}

	void WindowJoin::findUsedStreams(StreamsArray& streams)
	{
		m_outer->findUsedStreams(streams);
		m_inner->findUsedStreams(streams);
	}

	void WindowJoin::invalidateRecords(jrd_req* request)
	{
		m_outer->invalidateRecords(request);
		m_inner->invalidateRecords(request);
	}

	void WindowJoin::nullRecords(thread_db* tdbb)
	{
		m_outer->nullRecords(tdbb);
		m_inner->nullRecords(tdbb);
	}

	void WindowJoin::saveRecords(thread_db* tdbb)
	{
		m_outer->saveRecords(tdbb);
		m_inner->saveRecords(tdbb);
	}

	void WindowJoin::restoreRecords(thread_db* tdbb)
	{
		m_outer->restoreRecords(tdbb);
		m_inner->restoreRecords(tdbb);
	}

	int WindowJoin::compareKeys(thread_db* tdbb, jrd_req* request, DscNull* outerValues)
	{
		int cmp;

		for (size_t i = 0; i < m_innerKeys->nod_count; i++)
		{
			const DscNull& outerValue = outerValues[i];
			const dsc* const innerDesc = EVL_expr(tdbb, m_innerKeys->nod_arg[i]);
			const bool innerNull = (request->req_flags & req_null);

			if (outerValue.null && !innerNull)
				return -1;

			if (!outerValue.null && innerNull)
				return 1;

			if (!outerValue.null && !innerNull && (cmp = MOV_compare(outerValue.desc, innerDesc)) != 0)
				return cmp;
		}

		return 0;
	}
}	// namespace

// ------------------------------

WindowedStream::WindowedStream(CompilerScratch* csb, const jrd_nod* nodWindows, RecordSource* next)
	: m_joinedStream(NULL)
{
	thread_db* tdbb = JRD_get_thread_data();

	m_impure = CMP_impure(csb, sizeof(Impure));
	m_next = FB_NEW(csb->csb_pool) BufferedStream(csb, next);

	// Process the unpartioned and unordered map, if existent.

	for (unsigned i = 0; i < nodWindows->nod_count; ++i)
	{
		jrd_nod* const nodWindow = nodWindows->nod_arg[i];
		jrd_nod* const partition = nodWindow->nod_arg[e_part_group];
		jrd_nod* const partitionMap = nodWindow->nod_arg[e_part_map];
		jrd_nod* const order = nodWindow->nod_arg[e_part_order];
		const USHORT stream = (USHORT)(IPTR) nodWindow->nod_arg[e_part_stream];

		// While here, verify not supported functions/clauses.

		const jrd_nod* const* ptr = partitionMap->nod_arg;
		for (const jrd_nod* const* const end = ptr + partitionMap->nod_count; ptr < end; ++ptr)
		{
			jrd_nod* from = (*ptr)->nod_arg[e_asgn_from];
			const AggNode* aggNode = ExprNode::as<AggNode>(from);

			if (order && aggNode)
				aggNode->checkOrderedWindowCapable();
		}

		if (!partition && !order)
		{
			fb_assert(!m_joinedStream);

			m_joinedStream = FB_NEW(csb->csb_pool) AggregatedStream(csb, stream, NULL,
				partitionMap, FB_NEW(csb->csb_pool) BufferedStreamWindow(csb, m_next), NULL);

			OPT_gen_aggregate_distincts(tdbb, csb, partitionMap);
		}
	}

	if (!m_joinedStream)
		m_joinedStream = FB_NEW(csb->csb_pool) BufferedStreamWindow(csb, m_next);

	// Process ordered partitions.

	StreamsArray streams;

	for (unsigned i = 0; i < nodWindows->nod_count; ++i)
	{
		jrd_nod* const nodWindow = nodWindows->nod_arg[i];
		jrd_nod* const partition = nodWindow->nod_arg[e_part_group];
		jrd_nod* const partitionMap = nodWindow->nod_arg[e_part_map];
		jrd_nod* const order = nodWindow->nod_arg[e_part_order];
		const USHORT stream = (USHORT)(IPTR) nodWindow->nod_arg[e_part_stream];

		// Refresh the stream list based on the last m_joinedStream.
		streams.clear();
		m_joinedStream->findUsedStreams(streams);
		streams.insert(0, streams.getCount());

		// Build the sort key. It's the order items following the partition items.

		jrd_nod* partitionOrder;

		if (partition)
		{
			USHORT orderCount = order ? order->nod_count : 0;
			partitionOrder = PAR_make_node(tdbb, (partition->nod_count + orderCount) * 3);
			partitionOrder->nod_type = nod_sort;
			partitionOrder->nod_count = partition->nod_count + orderCount;

			jrd_nod** node1 = partitionOrder->nod_arg;
			jrd_nod** node2 = partitionOrder->nod_arg + partition->nod_count + orderCount;
			jrd_nod** node3 = node2 + partition->nod_count + orderCount;

			for (jrd_nod** node = partition->nod_arg;
				 node != partition->nod_arg + partition->nod_count;
				 ++node)
			{
				*node1++ = *node;
				*node2++ = (jrd_nod*)(IPTR) false;	// ascending
				*node3++ = (jrd_nod*)(IPTR) rse_nulls_default;
			}

			if (order)
			{
				for (jrd_nod** node = order->nod_arg; node != order->nod_arg + orderCount; ++node)
				{
					*node1++ = *node;
					*node2++ = *(node + orderCount);
					*node3++ = *(node + orderCount * 2);
				}
			}
		}
		else
			partitionOrder = order;

		if (partitionOrder)
		{
			SortedStream* sortedStream = OPT_gen_sort(tdbb, csb, streams.begin(), NULL,
				m_joinedStream, partitionOrder, false);

			m_joinedStream = FB_NEW(csb->csb_pool) AggregatedStream(csb, stream, partition,
				partitionMap, FB_NEW(csb->csb_pool) BufferedStream(csb, sortedStream), order);

			OPT_gen_aggregate_distincts(tdbb, csb, partitionMap);
		}
	}
}

void WindowedStream::open(thread_db* tdbb)
{
	jrd_req* const request = tdbb->getRequest();
	Impure* const impure = request->getImpure<Impure>(m_impure);

	impure->irsb_flags = irsb_open;

	m_next->open(tdbb);
	m_joinedStream->open(tdbb);
}

void WindowedStream::close(thread_db* tdbb)
{
	jrd_req* const request = tdbb->getRequest();

	invalidateRecords(request);

	Impure* const impure = request->getImpure<Impure>(m_impure);

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
	Impure* const impure = request->getImpure<Impure>(m_impure);

	if (!(impure->irsb_flags & irsb_open))
		return false;

	if (!m_joinedStream->getRecord(tdbb))
		return false;

	return true;
}

bool WindowedStream::refetchRecord(thread_db* tdbb)
{
	return m_joinedStream->refetchRecord(tdbb);
}

bool WindowedStream::lockRecord(thread_db* tdbb)
{
	status_exception::raise(Arg::Gds(isc_record_lock_not_supp));
	return false; // compiler silencer
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
