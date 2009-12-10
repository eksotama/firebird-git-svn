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

#ifndef JRD_CURSOR_H
#define JRD_CURSOR_H

#include "../common/classes/array.h"

namespace Jrd
{
	class thread_db;
	class CompilerScratch;
	class RecordSource;

	// Cursor class (wrapper around the whole access tree)

	class Cursor
	{
		enum State { BOS, POSITIONED, EOS };

		struct Impure
		{
			bool irsb_active;
			State irsb_state;
			FB_UINT64 irsb_position;
			RecordBuffer* irsb_buffer;
		};

	public:
		Cursor(CompilerScratch* csb, RecordSource* rsb, const VarInvariantArray* invariants,
			bool scrollable);

		void open(thread_db* tdbb);
		void close(thread_db* tdbb);

		bool fetchNext(thread_db* tdbb);
		bool fetchPrior(thread_db* tdbb);
		bool fetchFirst(thread_db* tdbb);
		bool fetchLast(thread_db* tdbb);
		bool fetchAbsolute(thread_db* tdbb, SINT64 offset);
		bool fetchRelative(thread_db* tdbb, SINT64 offset);

		RecordSource* getAccessPath() const
		{
			return m_top;
		}

	private:
		ULONG m_impure;
		RecordSource* const m_top;
		const VarInvariantArray* const m_invariants;
		const bool m_scrollable;
	};

} // namespace

#endif // JRD_CURSOR_H
