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
#include "../jrd/intl.h"
#include "../jrd/cmp_proto.h"
#include "../jrd/exe_proto.h"
#include "../jrd/mov_proto.h"
#include "../jrd/vio_proto.h"
#include "../jrd/trace/TraceManager.h"
#include "../jrd/trace/TraceJrdHelpers.h"

#include "RecordSource.h"

using namespace Firebird;
using namespace Jrd;

// ---------------------------
// Data access: procedure scan
// ---------------------------

ProcedureScan::ProcedureScan(CompilerScratch* csb, const Firebird::string& name, UCHAR stream,
							 jrd_prc* procedure, jrd_nod* inputs, jrd_nod* message)
	: RecordStream(csb, stream, procedure->prc_format), m_name(csb->csb_pool, name),
	  m_procedure(procedure), m_inputs(inputs), m_message(message)
{
	m_impure = CMP_impure(csb, sizeof(Impure));
}

void ProcedureScan::open(thread_db* tdbb)
{
	jrd_req* const request = tdbb->getRequest();
	Impure* const impure = request->getImpure<Impure>(m_impure);

	impure->irsb_flags = irsb_open;

	record_param* const rpb = &request->req_rpb[m_stream];
	rpb->getWindow(tdbb).win_flags = 0;

	// get rid of any lingering record

	delete rpb->rpb_record;
	rpb->rpb_record = NULL;

	USHORT iml;
	const UCHAR* im;

	jrd_req* const proc_request = EXE_find_request(tdbb, m_procedure->getRequest(), false);
	impure->irsb_req_handle = proc_request;

	if (m_inputs)
	{
		enum jrd_req::req_s saved_state = request->req_operation;

		jrd_nod** ptr = m_inputs->nod_arg;
		for (const jrd_nod* const* const end = ptr + m_inputs->nod_count; ptr < end; ptr++)
		{
			EXE_assignment(tdbb, *ptr);
		}

		request->req_operation = saved_state;
		const Format* const format = (Format*) m_message->nod_arg[e_msg_format];
		iml = format->fmt_length;
		im = request->getImpure<UCHAR>(m_message->nod_impure);
	}
	else
	{
		iml = 0;
		im = NULL;
	}

	// req_proc_fetch flag used only when fetching rows, so
	// is set at end of open()

	proc_request->req_flags &= ~req_proc_fetch;

	try
	{
		proc_request->req_timestamp = request->req_timestamp;

		TraceProcExecute trace(tdbb, proc_request, request, m_inputs);

		EXE_start(tdbb, proc_request, request->req_transaction);

		if (iml)
		{
			EXE_send(tdbb, proc_request, 0, iml, im);
		}

		trace.finish(true, res_successful);
	}
	catch (const Firebird::Exception&)
	{
		close(tdbb);
		throw;
	}

	proc_request->req_flags |= req_proc_fetch;
}

void ProcedureScan::close(thread_db* tdbb)
{
	jrd_req* const request = tdbb->getRequest();

	invalidateRecords(request);

	Impure* const impure = request->getImpure<Impure>(m_impure);

	if (impure->irsb_flags & irsb_open)
	{
		impure->irsb_flags &= ~irsb_open;

		jrd_req* const proc_request = impure->irsb_req_handle;

		if (proc_request)
		{
			EXE_unwind(tdbb, proc_request);
			proc_request->req_flags &= ~req_in_use;
			impure->irsb_req_handle = 0;
			proc_request->req_attachment = NULL;
		}

		delete impure->irsb_message;
		impure->irsb_message = NULL;
	}
}

bool ProcedureScan::getRecord(thread_db* tdbb)
{
	jrd_req* const request = tdbb->getRequest();
	record_param* const rpb = &request->req_rpb[m_stream];
	Impure* const impure = request->getImpure<Impure>(m_impure);

	if (!(impure->irsb_flags & irsb_open))
	{
		rpb->rpb_number.setValid(false);
		return false;
	}

	jrd_req* const proc_request = impure->irsb_req_handle;
	Format* const rec_format = m_format;

	const Format* const msg_format = (Format*) m_procedure->prc_output_msg->nod_arg[e_msg_format];
	if (!impure->irsb_message)
	{
		const SLONG size = msg_format->fmt_length + FB_ALIGNMENT;
		impure->irsb_message = FB_NEW_RPT(*tdbb->getDefaultPool(), size) VaryingString();
		impure->irsb_message->str_length = size;
	}
	UCHAR* om = (UCHAR *) FB_ALIGN((U_IPTR) impure->irsb_message->str_data, FB_ALIGNMENT);
	USHORT oml = impure->irsb_message->str_length - FB_ALIGNMENT;

	Record* record;
	if (!rpb->rpb_record)
	{
		record = rpb->rpb_record =
			FB_NEW_RPT(*tdbb->getDefaultPool(), rec_format->fmt_length) Record(*tdbb->getDefaultPool());
		record->rec_format = rec_format;
		record->rec_length = rec_format->fmt_length;
	}
	else
	{
		record = rpb->rpb_record;
	}

	TraceProcFetch trace(tdbb, proc_request);

	try
	{
		EXE_receive(tdbb, proc_request, 1, oml, om);

		dsc desc = msg_format->fmt_desc[msg_format->fmt_count - 1];
		desc.dsc_address = (UCHAR*) (om + (IPTR) desc.dsc_address);
		SSHORT eos;
		dsc eos_desc;
		eos_desc.makeShort(0, &eos);
		MOV_move(tdbb, &desc, &eos_desc);

		if (!eos)
		{
			trace.fetch(true, res_successful);
			return false;
		}
	}
	catch (const Firebird::Exception&)
	{
		trace.fetch(true, res_failed);
		close(tdbb);
		throw;
	}

	for (unsigned i = 0; i < rec_format->fmt_count; i++)
	{
		assignParams(tdbb, &msg_format->fmt_desc[2 * i], &msg_format->fmt_desc[2 * i + 1],
					 om, &rec_format->fmt_desc[i], i, record);
	}

	trace.fetch(false, res_successful);
	return true;
}

bool ProcedureScan::refetchRecord(thread_db* tdbb)
{
	return true;
}

bool ProcedureScan::lockRecord(thread_db* tdbb)
{
	status_exception::raise(Arg::Gds(isc_record_lock_not_supp));
	return false; // compiler silencer
}

void ProcedureScan::dump(thread_db* tdbb, UCharBuffer& buffer)
{
	buffer.add(isc_info_rsb_begin);
	buffer.add(isc_info_rsb_procedure);

	buffer.add(isc_info_rsb_begin);

	buffer.add(isc_info_rsb_relation);
	dumpName(tdbb, m_name, buffer);

	buffer.add(isc_info_rsb_type);
	buffer.add(isc_info_rsb_sequential);

	buffer.add(isc_info_rsb_end);
	buffer.add(isc_info_rsb_end);
}

void ProcedureScan::assignParams(thread_db* tdbb,
								 const dsc* from_desc,
								 const dsc* flag_desc,
								 const UCHAR* msg,
								 const dsc* to_desc,
								 SSHORT to_id,
								 Record* record)
{
	SSHORT indicator;
	dsc desc2;
	desc2.makeShort(0, &indicator);

	dsc desc1;
	desc1 = *flag_desc;
	desc1.dsc_address = const_cast<UCHAR*>(msg) + (IPTR) flag_desc->dsc_address;

	MOV_move(tdbb, &desc1, &desc2);

	if (indicator)
	{
		SET_NULL(record, to_id);
		const USHORT l = to_desc->dsc_length;
		UCHAR* const p = record->rec_data + (IPTR) to_desc->dsc_address;
		switch (to_desc->dsc_dtype)
		{
		case dtype_text:
			/* YYY - not necessarily the right thing to do */
			/* YYY for text formats that don't have trailing spaces */
			if (l)
			{
				const CHARSET_ID chid = DSC_GET_CHARSET(to_desc);
				/*
				CVC: I don't know if we have to check for dynamic-127 charset here.
				If that is needed, the line above should be replaced by the commented code here.
				CHARSET_ID chid = INTL_TTYPE(to_desc);
				if (chid == ttype_dynamic)
					chid = INTL_charset(tdbb, chid);
				*/
				const char pad = chid == ttype_binary ? '\0' : ' ';
				memset(p, pad, l);
			}
			break;

		case dtype_cstring:
			*p = 0;
			break;

		case dtype_varying:
			*reinterpret_cast<SSHORT*>(p) = 0;
			break;

		default:
			if (l)
			{
				memset(p, 0, l);
			}
			break;
		}
	}
	else
	{
		CLEAR_NULL(record, to_id);
		desc1 = *from_desc;
		desc1.dsc_address = const_cast<UCHAR*>(msg) + (IPTR) desc1.dsc_address;
		desc2 = *to_desc;
		desc2.dsc_address = record->rec_data + (IPTR) desc2.dsc_address;
		if (!DSC_EQUIV(&desc1, &desc2, false))
		{
			MOV_move(tdbb, &desc1, &desc2);
			return;
		}

		switch (desc1.dsc_dtype)
		{
		case dtype_short:
			*reinterpret_cast<SSHORT*>(desc2.dsc_address) =
				*reinterpret_cast<SSHORT*>(desc1.dsc_address);
			break;
		case dtype_long:
			*reinterpret_cast<SLONG*>(desc2.dsc_address) =
				*reinterpret_cast<SLONG*>(desc1.dsc_address);
			break;
		case dtype_int64:
			*reinterpret_cast<SINT64*>(desc2.dsc_address) =
				*reinterpret_cast<SINT64*>(desc1.dsc_address);
			break;
		default:
			memcpy(desc2.dsc_address, desc1.dsc_address, desc1.dsc_length);
		}
	}
}
