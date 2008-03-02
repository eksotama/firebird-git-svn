/*
 *	PROGRAM:	Dynamic SQL runtime support
 *	MODULE:		dsql.cpp
 *	DESCRIPTION:	Local processing for External entry points.
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
 * 2001.07.06 Sean Leyne - Code Cleanup, removed "#ifdef READONLY_DATABASE"
 *                         conditionals, as the engine now fully supports
 *                         readonly databases.
 * December 2001 Mike Nordell: Major overhaul to (try to) make it C++
 * 2001.6.3 Claudio Valderrama: fixed a bad behaved loop in get_plan_info()
 * and get_rsb_item() that caused a crash when plan info was requested.
 * 2001.6.9 Claudio Valderrama: Added nod_del_view, nod_current_role and nod_breakleave.
 * 2002.10.29 Nickolay Samofatov: Added support for savepoints
 * 2002.10.29 Sean Leyne - Removed obsolete "Netware" port
 * 2004.01.16 Vlad Horsun: added support for EXECUTE BLOCK statement
 * Adriano dos Santos Fernandes
 */

#include "firebird.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../dsql/dsql.h"
#include "../dsql/node.h"
#include "../jrd/ibase.h"
#include "../jrd/align.h"
#include "../jrd/intl.h"
#include "../jrd/iberr.h"
#include "../jrd/jrd.h"
#include "../dsql/Parser.h"
#include "../dsql/ddl_proto.h"
#include "../dsql/dsql_proto.h"
#include "../dsql/errd_proto.h"
#include "../dsql/gen_proto.h"
#include "../dsql/hsh_proto.h"
#include "../dsql/make_proto.h"
#include "../dsql/movd_proto.h"
#include "../dsql/parse_proto.h"
#include "../dsql/pass1_proto.h"
#include "../jrd/blb_proto.h"
#include "../jrd/cmp_proto.h"
#include "../jrd/gds_proto.h"
#include "../jrd/inf_proto.h"
#include "../jrd/jrd_proto.h"
#include "../jrd/sch_proto.h"
#include "../jrd/tra_proto.h"
#include "../common/classes/init.h"
#include "../common/utils_proto.h"
#ifdef SCROLLABLE_CURSORS
#include "../jrd/scroll_cursors.h"
#endif

#ifdef HAVE_CTYPE_H
#include <ctype.h>
#endif

using namespace Jrd;
using namespace Dsql;


static void		close_cursor(dsql_req*);
static USHORT	convert(SLONG, UCHAR*);
static void		execute_blob(thread_db*, dsql_req*, USHORT, const UCHAR*, USHORT, const UCHAR*,
						 USHORT, UCHAR*, USHORT, UCHAR*);
static void execute_immediate(thread_db*, Attachment*, jrd_tra**,
							  USHORT, const TEXT*, USHORT,
							  USHORT, const UCHAR*, USHORT, USHORT, const UCHAR*,
							  USHORT, UCHAR*, USHORT, USHORT, UCHAR*,
							  long);
static void		execute_request(thread_db*, dsql_req*, jrd_tra**, USHORT, const UCHAR*,
	USHORT, const UCHAR*, USHORT, UCHAR*, USHORT, UCHAR*, bool);
static SSHORT	filter_sub_type(dsql_req*, const dsql_nod*);
static bool		get_indices(SSHORT*, const SCHAR**, SSHORT*, SCHAR**);
static USHORT	get_plan_info(thread_db*, dsql_req*, SSHORT, SCHAR**);
static USHORT	get_request_info(thread_db*, dsql_req*, SSHORT, UCHAR*);
static bool		get_rsb_item(SSHORT*, const SCHAR**, SSHORT*, SCHAR**, USHORT*,
							USHORT*);
static dsql_dbb*	init(Attachment*);
static void		map_in_out(dsql_req*, dsql_msg*, USHORT, const UCHAR*, USHORT, UCHAR*);
static USHORT	parse_blr(USHORT, const UCHAR*, const USHORT, dsql_par*);
static dsql_req*		prepare(thread_db*, dsql_req*, USHORT, const TEXT*, USHORT, USHORT);
static UCHAR*	put_item(UCHAR, USHORT, const UCHAR*, UCHAR*, const UCHAR* const);
static void		release_request(thread_db*, dsql_req*, bool);
static void		sql_info(thread_db*, dsql_req*, USHORT, const UCHAR*, USHORT, UCHAR*);
static UCHAR*	var_info(dsql_msg*, const UCHAR*, const UCHAR* const, UCHAR*,
	const UCHAR* const, USHORT);

#ifdef DSQL_DEBUG
unsigned DSQL_debug = 0;
#endif

namespace
{
	const SCHAR db_hdr_info_items[] = {
		isc_info_db_sql_dialect,
		isc_info_ods_version,
		isc_info_ods_minor_version,
		isc_info_base_level,
		isc_info_db_read_only,
		isc_info_end
	};

	const SCHAR explain_info[] = {
		isc_info_access_path
	};

	const SCHAR record_info[] = {
		isc_info_req_update_count, isc_info_req_delete_count,
		isc_info_req_select_count, isc_info_req_insert_count
	};

	const UCHAR sql_records_info[] = {
		isc_info_sql_records
	};

}	// namespace


#ifdef DSQL_DEBUG
IMPLEMENT_TRACE_ROUTINE(dsql_trace, "DSQL")
#endif

dsql_dbb::~dsql_dbb()
{
	HSHD_finish(this);
}


/**
  
 	DSQL_allocate_statement
  
    @brief	Allocate a statement handle.
 

    @param tdbb
    @param attachment

 **/
dsql_req* DSQL_allocate_statement(thread_db* tdbb,
								  Attachment* attachment)
{
	SET_TDBB(tdbb);

	dsql_dbb* const database = init(attachment);
	Jrd::ContextPoolHolder context(tdbb, database->createPool());

	// allocate the request block 

	MemoryPool& pool = *tdbb->getDefaultPool();
	dsql_req* const request = FB_NEW(pool) dsql_req(pool);
	request->req_dbb = database;

	return request;
}


/**
  
 	DSQL_execute
  
    @brief	Execute a non-SELECT dynamic SQL statement.
 

    @param tdbb
    @param tra_handle
    @param request
    @param in_blr_length
    @param in_blr
    @param in_msg_type
    @param in_msg_length
    @param in_msg
    @param out_blr_length
    @param out_blr
    @param out_msg_type
    @param out_msg_length
    @param out_msg

 **/
void DSQL_execute(thread_db* tdbb,
				  jrd_tra** tra_handle,
				  dsql_req* request,
				  USHORT in_blr_length, const UCHAR* in_blr,
				  USHORT in_msg_type, USHORT in_msg_length, const UCHAR* in_msg,
				  USHORT out_blr_length, UCHAR* out_blr,
				  USHORT out_msg_type, USHORT out_msg_length, UCHAR* out_msg)
{
	SET_TDBB(tdbb);

	Jrd::ContextPoolHolder context(tdbb, &request->req_pool);

	if (request->req_flags & REQ_orphan) 
	{
		ERRD_post (isc_sqlerr, isc_arg_number, (SLONG) -901,
		           isc_arg_gds, isc_bad_req_handle,
			       0);
	}

	if ((SSHORT) in_msg_type == -1) {
		request->req_type = REQ_EMBED_SELECT;
	}

// Only allow NULL trans_handle if we're starting a transaction 

	if (!*tra_handle && request->req_type != REQ_START_TRANS)
	{
		ERRD_post(isc_sqlerr, isc_arg_number, (SLONG) - 901,
			  	isc_arg_gds, isc_bad_trans_handle, 0);
	}

/* If the request is a SELECT or blob statement then this is an open.
   Make sure the cursor is not already open. */

	if (request->req_type == REQ_SELECT ||
		request->req_type == REQ_EXEC_BLOCK ||  
		request->req_type == REQ_SELECT_BLOCK ||
		request->req_type == REQ_SELECT_UPD ||
		request->req_type == REQ_EMBED_SELECT ||
		request->req_type == REQ_GET_SEGMENT ||
		request->req_type == REQ_PUT_SEGMENT)
	{
		if (request->req_flags & REQ_cursor_open)
		{
			ERRD_post(isc_sqlerr, isc_arg_number, (SLONG) - 502,
				  	isc_arg_gds, isc_dsql_cursor_open_err, 0);
		}
	}

// A select with a non zero output length is a singleton select 
	bool singleton;
	if (request->req_type == REQ_SELECT && out_msg_length != 0) {
		singleton = true;
	}
	else {
		singleton = false;
	}

	if (request->req_type != REQ_EMBED_SELECT)
	{
		execute_request(tdbb, request, tra_handle,
						in_blr_length, in_blr,
						in_msg_length, in_msg,
						out_blr_length, out_blr,
						out_msg_length, out_msg,
						singleton);
	}
	else
	{
		request->req_transaction = *tra_handle;
	}

/* If the output message length is zero on a REQ_SELECT then we must
 * be doing an OPEN cursor operation.
 * If we do have an output message length, then we're doing
 * a singleton SELECT.  In that event, we don't add the cursor
 * to the list of open cursors (it's not really open).
 */
	if ((request->req_type == REQ_SELECT && out_msg_length == 0) ||
		(request->req_type == REQ_SELECT_BLOCK) ||  
		request->req_type == REQ_SELECT_UPD ||
		request->req_type == REQ_EMBED_SELECT ||
		request->req_type == REQ_GET_SEGMENT ||
		request->req_type == REQ_PUT_SEGMENT)
	{
		request->req_flags |= REQ_cursor_open;
		TRA_link_cursor(request->req_transaction, request);
	}
}


/**
  
 	DSQL_execute_immediate
  
    @brief	Execute a non-SELECT dynamic SQL statement.
 

    @param tdbb
    @param attachment
    @param tra_handle
    @param length
    @param string
    @param dialect
    @param in_blr_length
    @param in_blr
    @param in_msg_type
    @param in_msg_length
    @param in_msg
    @param out_blr_length
    @param out_blr
    @param out_msg_type
    @param out_msg_length
    @param out_msg

 **/
void DSQL_execute_immediate(thread_db* tdbb,
							Attachment* attachment,
							jrd_tra** tra_handle,
							USHORT length, const TEXT* string, USHORT dialect,
							USHORT in_blr_length, const UCHAR* in_blr,
							USHORT in_msg_type, USHORT in_msg_length, const UCHAR* in_msg,
							USHORT out_blr_length, UCHAR* out_blr,
							USHORT out_msg_type, USHORT out_msg_length, UCHAR* out_msg)
{
	execute_immediate(tdbb, attachment, tra_handle, length,
		string, dialect, in_blr_length, 
		in_blr,
		in_msg_type, in_msg_length,
		in_msg,
		out_blr_length, 
		out_blr, 
		out_msg_type, out_msg_length, 
		out_msg,
		~0);
}

/**
  
 	DSQL_callback_execute_immediate
  
    @brief	Execute sql_operator in context of jrd_transaction_handle
 

    @param tdbb
    @param sql_operator
    @param dialect

 **/
void DSQL_callback_execute_immediate(thread_db* tdbb,
									 const Firebird::string& sql_operator)
{
	// Other requests appear to be incorrect in this context 
	long requests = (1 << REQ_INSERT) | (1 << REQ_DELETE) | (1 << REQ_UPDATE)
			      | (1 << REQ_DDL) | (1 << REQ_SET_GENERATOR) | (1 << REQ_EXEC_PROCEDURE) 
				  | (1 << REQ_EXEC_BLOCK);

	Attachment* const attachment = tdbb->getAttachment();

	const Database* const dbb = attachment->att_database;
	const int dialect = dbb->dbb_flags & DBB_DB_SQL_dialect_3 ? SQL_DIALECT_V6 : SQL_DIALECT_V5;

	jrd_tra* transaction = tdbb->getTransaction();

	execute_immediate(tdbb, attachment, &transaction,
					  sql_operator.length(), sql_operator.c_str(), dialect,
					  0, NULL, 0, 0, NULL, 0, NULL, 0, 0, NULL, requests);

	fb_assert(transaction == tdbb->getTransaction());
}


/**
  
 	DSQL_fetch
  
    @brief	Fetch next record from a dynamic SQL cursor
 

    @param user_status
    @param req_handle
    @param blr_length
    @param blr
    @param msg_type
    @param msg_length
    @param dsql_msg
    @param direction
    @param offset

 **/
ISC_STATUS DSQL_fetch(thread_db* tdbb,
					  dsql_req* request,
					  USHORT blr_length, const UCHAR* blr,
					  USHORT msg_type, USHORT msg_length, UCHAR* dsql_msg_buf
#ifdef SCROLLABLE_CURSORS
						  , USHORT direction, SLONG offset
#endif
						  )
{
	SET_TDBB(tdbb);

	Jrd::ContextPoolHolder context(tdbb, &request->req_pool);

// if the cursor isn't open, we've got a problem 

	if (request->req_type == REQ_SELECT ||
		request->req_type == REQ_SELECT_UPD ||
		request->req_type == REQ_SELECT_BLOCK ||
		request->req_type == REQ_EMBED_SELECT ||
		request->req_type == REQ_GET_SEGMENT)
	{
		if (!(request->req_flags & REQ_cursor_open))
			ERRD_post(isc_sqlerr, isc_arg_number, (SLONG) - 504,
				  isc_arg_gds, isc_dsql_cursor_err,
				  isc_arg_gds, isc_dsql_cursor_not_open, 0);
	}

#ifdef SCROLLABLE_CURSORS

/* check whether we need to send an asynchronous scrolling message
   to the engine; the engine will automatically advance one record
   in the same direction as before, so optimize out messages of that
   type */

	if (request->req_type == REQ_SELECT &&
		request->req_dbb->dbb_base_level >= 5)
	{
		switch (direction)
		{
		case isc_fetch_next:
			if (!(request->req_flags & REQ_backwards))
				offset = 0;
			else {
				direction = blr_forward;
				offset = 1;
				request->req_flags &= ~REQ_backwards;
			}
			break;

		case isc_fetch_prior:
			if (request->req_flags & REQ_backwards)
				offset = 0;
			else {
				direction = blr_backward;
				offset = 1;
				request->req_flags |= REQ_backwards;
			}
			break;

		case isc_fetch_first:
			direction = blr_bof_forward;
			offset = 1;
			request->req_flags &= ~REQ_backwards;
			break;

		case isc_fetch_last:
			direction = blr_eof_backward;
			offset = 1;
			request->req_flags |= REQ_backwards;
			break;

		case isc_fetch_absolute:
			direction = blr_bof_forward;
			request->req_flags &= ~REQ_backwards;
			break;

		case isc_fetch_relative:
			if (offset < 0) {
				direction = blr_backward;
				offset = -offset;
				request->req_flags |= REQ_backwards;
			}
			else {
				direction = blr_forward;
				request->req_flags &= ~REQ_backwards;
			}
			break;

		default:
			ERRD_post(isc_sqlerr, isc_arg_number, (SLONG) - 804,
				  isc_arg_gds, isc_dsql_sqlda_err, 0);
		}

		if (offset)
		{
			DSC desc;

			dsql_msg* message = (dsql_msg*) request->req_async;

			desc.dsc_dtype = dtype_short;
			desc.dsc_scale = 0;
			desc.dsc_length = sizeof(USHORT);
			desc.dsc_flags = 0;
			desc.dsc_address = (UCHAR*) & direction;

			dsql_par* offset_parameter = message->msg_parameters;
			dsql_par* parameter = offset_parameter->par_next;
			MOVD_move(&desc, &parameter->par_desc);

			desc.dsc_dtype = dtype_long;
			desc.dsc_scale = 0;
			desc.dsc_length = sizeof(SLONG);
			desc.dsc_flags = 0;
			desc.dsc_address = (UCHAR*) & offset;

			MOVD_move(&desc, &offset_parameter->par_desc);

			DsqlCheckout dcoHolder(request->req_dbb);

			if (isc_receive2(tdbb->tdbb_status_vector, &request->req_request,
							 message->msg_number, message->msg_length,
							 message->msg_buffer, 0, direction, offset))
			{
				Firebird::status_exception::raise(tdbb->tdbb_status_vector);
			}
		}
	}
#endif

	dsql_msg* message = (dsql_msg*) request->req_receive;

/* Insure that the blr for the message is parsed, regardless of
   whether anything is found by the call to receive. */

	if (blr_length) {
		parse_blr(blr_length, blr, msg_length, message->msg_parameters);
	}

	if (request->req_type == REQ_GET_SEGMENT)
	{
		// For get segment, use the user buffer and indicator directly.

		dsql_par* parameter = request->req_blob->blb_segment;
		dsql_par* null = parameter->par_null;
		USHORT* ret_length =
			(USHORT *) (dsql_msg_buf + (IPTR) null->par_user_desc.dsc_address);
		UCHAR* buffer = dsql_msg_buf + (IPTR) parameter->par_user_desc.dsc_address;

		*ret_length = BLB_get_segment(tdbb, request->req_blob->blb_blob,
			buffer, parameter->par_user_desc.dsc_length);

		if (request->req_blob->blb_blob->blb_flags & BLB_eof)
			return 100;
		else if (request->req_blob->blb_blob->blb_fragment_size)
			return 101;
		else
			return 0;
	}

	JRD_receive(tdbb, request->req_request, message->msg_number, message->msg_length,
		reinterpret_cast<SCHAR*>(message->msg_buffer), 0);

	const dsql_par* const eof = request->req_eof;
	if (eof)
	{
		if (!*((USHORT *) eof->par_desc.dsc_address)) {
			return 100;
		}
	}

	map_in_out(NULL, message, 0, blr, msg_length, dsql_msg_buf);

	return FB_SUCCESS;
}


/**
  
 	DSQL_free_statement
  
    @brief	Release request for a dsql statement
 

    @param user_status
    @param req_handle
    @param option

 **/
void DSQL_free_statement(thread_db* tdbb,
						 dsql_req* request,
						 USHORT option)
{
	SET_TDBB(tdbb);

	Jrd::ContextPoolHolder context(tdbb, &request->req_pool);

	if (option & DSQL_drop) {
		// Release everything associated with the request
		release_request(tdbb, request, true);
	}
	else if (option & DSQL_unprepare) {
		// Release everything but the request itself
		release_request(tdbb, request, false);
	}
	else if (option & DSQL_close) {
		// Just close the cursor associated with the request
		if (!(request->req_flags & REQ_cursor_open))
		{
			ERRD_post(isc_sqlerr, isc_arg_number, (SLONG) - 501,
				  isc_arg_gds, isc_dsql_cursor_close_err, 0);
		}
		close_cursor(request);
	}
}


/**
  
 	DSQL_insert
  
    @brief	Insert next record into a dynamic SQL cursor
 

    @param user_status
    @param req_handle
    @param blr_length
    @param blr
    @param msg_type
    @param msg_length
    @param dsql_msg

 **/
void DSQL_insert(thread_db* tdbb,
				 dsql_req* request,
				 USHORT blr_length, const UCHAR* blr,
				 USHORT msg_type, USHORT msg_length, const UCHAR* dsql_msg_buf)
{
	SET_TDBB(tdbb);

	Jrd::ContextPoolHolder context(tdbb, &request->req_pool);

	if (request->req_flags & REQ_orphan) 
	{
		ERRD_post (isc_sqlerr, isc_arg_number, (SLONG) -901,
		           isc_arg_gds, isc_bad_req_handle,
			       0);
	}

// if the cursor isn't open, we've got a problem 

	if (request->req_type == REQ_PUT_SEGMENT)
	{
		if (!(request->req_flags & REQ_cursor_open))
		{
			ERRD_post(isc_sqlerr, isc_arg_number, (SLONG) - 504,
				  isc_arg_gds, isc_dsql_cursor_err,
				  isc_arg_gds, isc_dsql_cursor_not_open, 0);
		}
	}

	dsql_msg* message = (dsql_msg*) request->req_receive;

/* Insure that the blr for the message is parsed, regardless of
   whether anything is found by the call to receive. */

	if (blr_length)
		parse_blr(blr_length, blr, msg_length, message->msg_parameters);

	if (request->req_type == REQ_PUT_SEGMENT) {
		// For put segment, use the user buffer and indicator directly. 

		dsql_par* parameter = request->req_blob->blb_segment;
		const UCHAR* buffer = dsql_msg_buf + (IPTR) parameter->par_user_desc.dsc_address;

		BLB_put_segment(tdbb, request->req_blob->blb_blob, buffer,
			parameter->par_user_desc.dsc_length);
	}
}


/**
  
 	DSQL_prepare
  
    @brief	Prepare a statement for execution.
 

    @param user_status
    @param trans_handle
    @param req_handle
    @param length
    @param string
    @param dialect
    @param item_length
    @param items
    @param buffer_length
    @param buffer

 **/
void DSQL_prepare(thread_db* tdbb,
				  jrd_tra* transaction,
				  dsql_req** req_handle,
				  USHORT length, const TEXT* string, USHORT dialect,
				  USHORT item_length, const UCHAR* items,
				  USHORT buffer_length, UCHAR* buffer)
{
	SET_TDBB(tdbb);

	dsql_req* const old_request = *req_handle;

	if (!old_request) {
		ERRD_post (isc_sqlerr, isc_arg_number, (SLONG) -901,
		           isc_arg_gds, isc_bad_req_handle,
			       0);
	}

	dsql_dbb* database = old_request->req_dbb;
	if (!database) {
		ERRD_post (isc_sqlerr, isc_arg_number, (SLONG) -901,
                   isc_arg_gds, isc_bad_req_handle,
	               0);
	}

	Jrd::ContextPoolHolder context(tdbb, database->createPool());

	// check to see if old request has an open cursor 

	if (old_request && (old_request->req_flags & REQ_cursor_open)) {
		ERRD_post(isc_sqlerr, isc_arg_number, (SLONG) - 519,
			  isc_arg_gds, isc_dsql_open_cursor_request, 0);
	}

/* Allocate a new request block and then prepare the request.  We want to
   keep the old request around, as is, until we know that we are able
   to prepare the new one. */
/* It would be really *nice* to know *why* we want to
   keep the old request around -- 1994-October-27 David Schnepper */
/* Because that's the client's allocated statement handle and we
   don't want to trash the context in it -- 2001-Oct-27 Ann Harrison */

	MemoryPool& pool = *tdbb->getDefaultPool();
	dsql_req* request = FB_NEW(pool) dsql_req(pool);
	request->req_dbb = database;
	request->req_transaction = transaction;

	try {

		if (!string) {
			ERRD_post(isc_sqlerr, isc_arg_number, (SLONG) - 104, 
				isc_arg_gds, isc_command_end_err2,
				// CVC: Nothing will be line 1, column 1 for the user.
				isc_arg_number, (SLONG) 1, isc_arg_number, (SLONG) 1,
				0);	// Unexpected end of command
		}

		if (!length) {
			length = strlen(string);
		}

// Figure out which parser version to use 
/* Since the API to dsql8_prepare is public and can not be changed, there needs to
 * be a way to send the parser version to DSQL so that the parser can compare the keyword
 * version to the parser version.  To accomplish this, the parser version is combined with
 * the client dialect and sent across that way.  In dsql8_prepare_statement, the parser version
 * and client dialect are separated and passed on to their final desintations.  The information
 * is combined as follows:
 *     Dialect * 10 + parser_version
 *
 * and is extracted in dsql8_prepare_statement as follows:
 *      parser_version = ((dialect *10)+parser_version)%10
 *      client_dialect = ((dialect *10)+parser_version)/10
 *
 * For example, parser_version = 1 and client dialect = 1
 *
 *  combined = (1 * 10) + 1 == 11
 *
 *  parser = (combined) %10 == 1
 *  dialect = (combined) / 19 == 1
 *
 * If the parser version is not part of the dialect, then assume that the
 * connection being made is a local classic connection.
 */

		USHORT parser_version;
		if ((dialect / 10) == 0)
			parser_version = 2;
		else {
			parser_version = dialect % 10;
			dialect /= 10;
		}

		request->req_client_dialect = dialect;

		request = prepare(tdbb, request, length, string, dialect, parser_version);

		// Can not prepare a CREATE DATABASE/SCHEMA statement

		if (request->req_type == REQ_CREATE_DB)
		{
			ERRD_post(isc_sqlerr, isc_arg_number, (SLONG) - 530,
				isc_arg_gds, isc_dsql_crdb_prepare_err, 0);
		}

		request->req_flags |= REQ_prepared;

		// Now that we know that the new request exists, zap the old one. 

		{
			Jrd::ContextPoolHolder another_context(tdbb, &old_request->req_pool);
			release_request(tdbb, old_request, true);
		}

		*req_handle = request;

		sql_info(tdbb, request, item_length, items, buffer_length, buffer);
	}
	catch (const Firebird::Exception&) {
		release_request(tdbb, request, true);
		throw;
	}
}


/**
  
 	DSQL_set_cursor_name
  
    @brief	Set a cursor name for a dynamic request
 

    @param user_status
    @param req_handle
    @param input_cursor
    @param type

 **/
void DSQL_set_cursor(thread_db* tdbb,
				     dsql_req* request,
					 const TEXT* input_cursor,
					 USHORT type)
{
	SET_TDBB(tdbb);

	Jrd::ContextPoolHolder context(tdbb, &request->req_pool);

	const size_t MAX_CURSOR_LENGTH = 132 - 1;
	Firebird::string cursor = input_cursor;

	if (cursor[0] == '\"')
	{
		// Quoted cursor names eh? Strip'em.
		// Note that "" will be replaced with ".
		// The code is very strange, because it doesn't check for "" really
		// and thus deletes one isolated " in the middle of the cursor.
		for (Firebird::string::iterator i = cursor.begin(); 
						i < cursor.end(); ++i)
		{
			if (*i == '\"') {
				cursor.erase(i);
			}
		}
	}
	else	// not quoted name
	{
		Firebird::string::size_type i = cursor.find(' ');
		if (i != Firebird::string::npos)
		{
			cursor.resize(i);
		}
		cursor.upper();
	}
	USHORT length = (USHORT) fb_utils::name_length(cursor.c_str());

	if (length == 0) {
		ERRD_post(isc_sqlerr, isc_arg_number, (SLONG) - 502,
				  isc_arg_gds, isc_dsql_decl_err,
				  isc_arg_gds, isc_dsql_cursor_invalid, 0);
	}
	if (length > MAX_CURSOR_LENGTH) {
		length = MAX_CURSOR_LENGTH;
	}
	cursor.resize(length);

	// If there already is a different cursor by the same name, bitch 

	const dsql_sym* symbol = 
		HSHD_lookup(request->req_dbb, cursor.c_str(), length, SYM_cursor, 0);
	if (symbol)
	{
		if (request->req_cursor == symbol)
			return;

		ERRD_post(isc_sqlerr, isc_arg_number, (SLONG) - 502,
				  isc_arg_gds, isc_dsql_decl_err,
				  isc_arg_gds, isc_dsql_cursor_redefined,
				  isc_arg_string, symbol->sym_string, 0);
	}

	// If there already is a cursor and its name isn't the same, ditto.
	// We already know there is no cursor by this name in the hash table

	if (!request->req_cursor) {
		request->req_cursor = MAKE_symbol(request->req_dbb, cursor.c_str(),
									  length, SYM_cursor, request);
	}
	else {
		fb_assert(request->req_cursor != symbol);
		ERRD_post(isc_sqlerr, isc_arg_number, (SLONG) - 502,
				  isc_arg_gds, isc_dsql_decl_err,
				  isc_arg_gds, isc_dsql_cursor_redefined,
				  isc_arg_string, request->req_cursor->sym_string, 0);
	}
}


/**
  
 	DSQL_sql_info
  
    @brief	Provide information on dsql statement
 

    @param user_status
    @param req_handle
    @param item_length
    @param items
    @param info_length
    @param info

 **/
void DSQL_sql_info(thread_db* tdbb,
				   dsql_req* request,
				   USHORT item_length, const UCHAR* items,
				   USHORT info_length, UCHAR* info)
{
	SET_TDBB(tdbb);

	Jrd::ContextPoolHolder context(tdbb, &request->req_pool);

	sql_info(tdbb, request, item_length, items, info_length, info);
}


/**
  
 	close_cursor
  
    @brief	Close an open cursor.
 

    @param request

 **/
static void close_cursor(dsql_req* request)
{
	thread_db* tdbb = JRD_get_thread_data();

	ISC_STATUS_ARRAY status_vector;

	if (request->req_request)
	{
		if (request->req_type == REQ_GET_SEGMENT ||
			request->req_type == REQ_PUT_SEGMENT)
		{
			BLB_close(tdbb, request->req_blob->blb_blob);
			request->req_blob->blb_blob = NULL;
		}
		else
		{
			Database::Checkout dcoHolder(request->req_dbb->dbb_database);
			jrd8_unwind_request(status_vector, &request->req_request, 0);
		}
	}

	request->req_flags &= ~REQ_cursor_open;
	TRA_unlink_cursor(request->req_transaction, request);
}


/**
  
 	convert
  
    @brief	Convert a number to VAX form -- least significant bytes first.
 	Return the length.
 

    @param number
    @param buffer

 **/

// CVC: This routine should disappear in favor of a centralized function.
static USHORT convert( SLONG number, UCHAR* buffer)
{
	const UCHAR* p;

#ifndef WORDS_BIGENDIAN
	const SLONG n = number;
	p = (UCHAR *) & n;
	*buffer++ = *p++;
	*buffer++ = *p++;
	*buffer++ = *p++;
	*buffer++ = *p++;

#else

	p = (UCHAR *) (&number + 1);
	*buffer++ = *--p;
	*buffer++ = *--p;
	*buffer++ = *--p;
	*buffer++ = *--p;

#endif

	return 4;
}


/**
  
 	execute_blob
  
    @brief	Open or create a blob.
 

	@param tdbb
    @param request
    @param in_blr_length
    @param in_blr
    @param in_msg_length
    @param in_msg
    @param out_blr_length
    @param out_blr
    @param out_msg_length
    @param out_msg

 **/
static void execute_blob(thread_db* tdbb,
						 dsql_req* request,
						 USHORT in_blr_length,
						 const UCHAR* in_blr,
						 USHORT in_msg_length,
						 const UCHAR* in_msg,
						 USHORT out_blr_length,
						 UCHAR* out_blr,
						 USHORT out_msg_length,
						 UCHAR* out_msg)
{
	UCHAR bpb[24];

	dsql_blb* blob = request->req_blob;
#pragma FB_COMPILER_MESSAGE("constness hack")
	map_in_out(request, blob->blb_open_in_msg,
			   in_blr_length, in_blr,
			   in_msg_length, const_cast<UCHAR*>(in_msg));

	UCHAR* p = bpb;
	*p++ = isc_bpb_version1;
	SSHORT filter = filter_sub_type(request, blob->blb_to);
	if (filter) {
		*p++ = isc_bpb_target_type;
		*p++ = 2;
		*p++ = static_cast<UCHAR>(filter);
		*p++ = filter >> 8;
	}
	filter = filter_sub_type(request, blob->blb_from);
	if (filter) {
		*p++ = isc_bpb_source_type;
		*p++ = 2;
		*p++ = static_cast<UCHAR>(filter);
		*p++ = filter >> 8;
	}
	USHORT bpb_length = p - bpb;
	if (bpb_length == 1) {
		bpb_length = 0;
	}

	dsql_par* parameter = blob->blb_blob_id;
	const dsql_par* null = parameter->par_null;

	if (request->req_type == REQ_GET_SEGMENT)
	{
		bid* blob_id = (bid*) parameter->par_desc.dsc_address;
		if (null && *((SSHORT *) null->par_desc.dsc_address) < 0) {
			memset(blob_id, 0, sizeof(bid));
		}

		request->req_blob->blb_blob = BLB_open2(tdbb, request->req_transaction, blob_id,
			bpb_length, bpb, true);
	}
	else
	{
		request->req_request = 0;
		bid* blob_id = (bid*) parameter->par_desc.dsc_address;
		memset(blob_id, 0, sizeof(bid));

		request->req_blob->blb_blob = BLB_create2(tdbb, request->req_transaction,
			blob_id, bpb_length, const_cast<const UCHAR*>(bpb));

		map_in_out(NULL, blob->blb_open_out_msg,
				   out_blr_length, out_blr,
				   out_msg_length, out_msg);
	}
}


/**
  
 	execute_immediate
  
    @brief	Common part of prepare and execute a statement.
 

    @param tdbb
    @param attachment
    @param tra_handle
	@param length
	@param string
	@param dialect
    @param in_blr_length
    @param in_blr
    @param in_msg_type
    @param in_msg_length
    @param in_msg
    @param out_blr_length
    @param out_blr
    @param out_msg_type
    @param out_msg_length
    @param out_msg
	@param possible_requests

 **/
static void execute_immediate(thread_db* tdbb,
							  Attachment* attachment,
							  jrd_tra** tra_handle,
							  USHORT length, const TEXT* string, USHORT dialect,
							  USHORT in_blr_length, const UCHAR* in_blr,
							  USHORT in_msg_type, USHORT in_msg_length, const UCHAR* in_msg,
							  USHORT out_blr_length, UCHAR* out_blr,
							  USHORT out_msg_type, USHORT out_msg_length, UCHAR* out_msg,
							  long possible_requests)
{
	SET_TDBB(tdbb);

	dsql_dbb* const database = init(attachment);
	Jrd::ContextPoolHolder context(tdbb, database->createPool());

	// allocate the request block, then prepare the request 

	MemoryPool& pool = *tdbb->getDefaultPool();
	dsql_req* request = FB_NEW(pool) dsql_req(pool);
	request->req_dbb = database;
	request->req_transaction = *tra_handle;

	try {

		if (!length) {
			length = strlen(string);
		}

// Figure out which parser version to use 
/* Since the API to dsql8_execute_immediate is public and can not be changed, there needs to
 * be a way to send the parser version to DSQL so that the parser can compare the keyword
 * version to the parser version.  To accomplish this, the parser version is combined with
 * the client dialect and sent across that way.  In dsql8_execute_immediate, the parser version
 * and client dialect are separated and passed on to their final destinations.  The information
 * is combined as follows:
 *     Dialect * 10 + parser_version
 *
 * and is extracted in dsql8_execute_immediate as follows:
 *      parser_version = ((dialect *10)+parser_version)%10
 *      client_dialect = ((dialect *10)+parser_version)/10
 *
 * For example, parser_version = 1 and client dialect = 1
 *
 *  combined = (1 * 10) + 1 == 11
 *
 *  parser = (combined) %10 == 1
 *  dialect = (combined) / 19 == 1
 *
 * If the parser version is not part of the dialect, then assume that the
 * connection being made is a local classic connection.
 */

		USHORT parser_version;
		if ((dialect / 10) == 0)
			parser_version = 2;
		else {
			parser_version = dialect % 10;
			dialect /= 10;
		}

		request->req_client_dialect = dialect;

		request = prepare(tdbb, request, length, string, dialect, parser_version);

		if (!((1 << request->req_type) & possible_requests))
		{
			const int max_diag_len = 50;
			char err_str[max_diag_len + 1];
			strncpy(err_str, string, max_diag_len);
			err_str[max_diag_len] = 0;
			ERRD_post(isc_sqlerr, isc_arg_number, (SLONG) -902,
				isc_arg_gds, isc_exec_sql_invalid_req, 
				isc_arg_string, err_str, isc_arg_end);
		}

		execute_request(tdbb, request, tra_handle, in_blr_length, in_blr,
						in_msg_length, in_msg, out_blr_length, out_blr,
						out_msg_length,	out_msg, false);

		release_request(tdbb, request, true);
	}
	catch (const Firebird::Exception&) {
		release_request(tdbb, request, true);
		throw;
	}
}


/**
  
 	execute_request
  
    @brief	Execute a dynamic SQL statement.
 

	@param tdbb
    @param request
    @param trans_handle
    @param in_blr_length
    @param in_blr
    @param in_msg_length
    @param in_msg
    @param out_blr_length
    @param out_blr
    @param out_msg_length
    @param out_msg
    @param singleton

 **/
static void execute_request(thread_db* tdbb,
							dsql_req* request,
							jrd_tra** tra_handle,
							USHORT in_blr_length, const UCHAR* in_blr,
							USHORT in_msg_length, const UCHAR* in_msg,
							USHORT out_blr_length, UCHAR* out_blr,
							USHORT out_msg_length, UCHAR* out_msg,
							bool singleton)
{
	request->req_transaction = *tra_handle;
	ISC_STATUS return_status = FB_SUCCESS;

	switch (request->req_type)
	{
	case REQ_START_TRANS:
		{
			Database::Checkout dcoHolder(request->req_dbb->dbb_database);
			if (jrd8_start_transaction(tdbb->tdbb_status_vector,
									   &request->req_transaction,
									   1,
									   &request->req_dbb->dbb_attachment,
									   request->req_blr_data.getCount(),
									   request->req_blr_data.begin()))
			{
				Firebird::status_exception::raise(tdbb->tdbb_status_vector);
			}
			*tra_handle = request->req_transaction;
			return;
		}

	case REQ_COMMIT:
		{
			Database::Checkout dcoHolder(request->req_dbb->dbb_database);
			if (jrd8_commit_transaction(tdbb->tdbb_status_vector, &request->req_transaction))
			{
				Firebird::status_exception::raise(tdbb->tdbb_status_vector);
			}
			*tra_handle = NULL;
			return;
		}

	case REQ_COMMIT_RETAIN:
		{
			Database::Checkout dcoHolder(request->req_dbb->dbb_database);
			if (jrd8_commit_retaining(tdbb->tdbb_status_vector, &request->req_transaction))
			{
				Firebird::status_exception::raise(tdbb->tdbb_status_vector);
			}
			return;
		}

	case REQ_ROLLBACK:
		{
			Database::Checkout dcoHolder(request->req_dbb->dbb_database);
			if (jrd8_rollback_transaction(tdbb->tdbb_status_vector, &request->req_transaction))
			{
				Firebird::status_exception::raise(tdbb->tdbb_status_vector);
			}
			*tra_handle = NULL;
			return;
		}

	case REQ_ROLLBACK_RETAIN:
		{
			Database::Checkout dcoHolder(request->req_dbb->dbb_database);
			if (jrd8_rollback_retaining(tdbb->tdbb_status_vector, &request->req_transaction))
			{
				Firebird::status_exception::raise(tdbb->tdbb_status_vector);
			}
			return;
		}

	case REQ_CREATE_DB:
	case REQ_DDL:
		DDL_execute(request);
		return;

	case REQ_GET_SEGMENT:
		execute_blob(tdbb, request,
					 in_blr_length, in_blr, in_msg_length, in_msg,
					 out_blr_length, out_blr, out_msg_length, out_msg);
		return;

	case REQ_PUT_SEGMENT:
		execute_blob(tdbb, request,
					 in_blr_length, in_blr, in_msg_length, in_msg,
					 out_blr_length, out_blr, out_msg_length, out_msg);
		return;

	case REQ_SELECT:
	case REQ_SELECT_UPD:
	case REQ_EMBED_SELECT:
	case REQ_INSERT:
	case REQ_UPDATE:
	case REQ_UPDATE_CURSOR:
	case REQ_DELETE:
	case REQ_DELETE_CURSOR:
	case REQ_EXEC_PROCEDURE:
	case REQ_SET_GENERATOR:
	case REQ_SAVEPOINT:
	case REQ_EXEC_BLOCK:
	case REQ_SELECT_BLOCK:
		break;

	default:
		// Catch invalid request types 
		fb_assert(false);
	}

	// If there is no data required, just start the request 

	dsql_msg* message = request->req_send;
	if (!message)
		JRD_start(tdbb, request->req_request, request->req_transaction, 0);
	else
	{
#pragma FB_COMPILER_MESSAGE("constness hack")
		map_in_out(request, message,
				   in_blr_length, in_blr,
				   in_msg_length, const_cast<UCHAR*>(in_msg));

		Database::Checkout dcoHolder(request->req_dbb->dbb_database);
		if (jrd8_start_and_send(tdbb->tdbb_status_vector,
							    &request->req_request,
							    &request->req_transaction,
							    message->msg_number,
							    message->msg_length,
							    reinterpret_cast<SCHAR*>(message->msg_buffer),
							    0))
		{
			Firebird::status_exception::raise(tdbb->tdbb_status_vector);
		}
	}

	ISC_STATUS_ARRAY local_status;
	// REQ_EXEC_BLOCK has no outputs so there are no out_msg 
	// supplied from client side, but REQ_EXEC_BLOCK requires
	// 2-byte message for EOS synchronization
	const bool isBlock = (request->req_type == REQ_EXEC_BLOCK);

	message = request->req_receive;
	if ((out_msg_length && message) || isBlock)
	{
		char temp_buffer[DOUBLE_ALIGN * 2];
		dsql_msg temp_msg;

		/* Insure that the blr for the message is parsed, regardless of
		   whether anything is found by the call to receive. */

		if (out_msg_length && out_blr_length) {
			parse_blr(out_blr_length, out_blr, out_msg_length,
					  message->msg_parameters);
		} 
		else if (!out_msg_length && isBlock) {
			message = &temp_msg;
			message->msg_number = 1;
			message->msg_length = 2;
			message->msg_buffer = (UCHAR*)FB_ALIGN((U_IPTR) temp_buffer, DOUBLE_ALIGN);
		}

		JRD_receive(tdbb, request->req_request, message->msg_number, message->msg_length,
			reinterpret_cast<SCHAR*>(message->msg_buffer), 0);

		if (out_msg_length)
			map_in_out(NULL, message, 0, out_blr, out_msg_length, out_msg);

		// if this is a singleton select, make sure there's in fact one record 

		if (singleton)
		{
			USHORT counter;

			/* Create a temp message buffer and try two more receives.
			   If both succeed then the first is the next record and the
			   second is either another record or the end of record message.
			   In either case, there's more than one record. */

			UCHAR* message_buffer =
				(UCHAR*)gds__alloc((ULONG) message->msg_length);

			ISC_STATUS status = FB_SUCCESS;

			for (counter = 0; counter < 2 && !status; counter++)
			{
				ISC_STATUS* old_status = tdbb->tdbb_status_vector;

				try
				{
					tdbb->tdbb_status_vector = local_status;
					JRD_receive(tdbb, request->req_request, message->msg_number,
						message->msg_length, reinterpret_cast<SCHAR*>(message_buffer), 0);
					status = FB_SUCCESS;
				}
				catch (Firebird::Exception&)
				{
					status = tdbb->tdbb_status_vector[1];
				}

				tdbb->tdbb_status_vector = old_status;
			}

			gds__free(message_buffer);

			/* two successful receives means more than one record
			   a req_sync error on the first pass above means no records
			   a non-req_sync error on any of the passes above is an error */

			if (!status)
			{
				tdbb->tdbb_status_vector[0] = isc_arg_gds;
				tdbb->tdbb_status_vector[1] = isc_sing_select_err;
				tdbb->tdbb_status_vector[2] = isc_arg_end;
				return_status = isc_sing_select_err;
			}
			else if (status == isc_req_sync && counter == 1)
			{
				tdbb->tdbb_status_vector[0] = isc_arg_gds;
				tdbb->tdbb_status_vector[1] = isc_stream_eof;
				tdbb->tdbb_status_vector[2] = isc_arg_end;
				return_status = isc_stream_eof;
			}
			else if (status != isc_req_sync)
			{
				Firebird::status_exception::raise(tdbb->tdbb_status_vector);
			}
		}
	}

	UCHAR buffer[20]; // Not used after retrieved
	if (request->req_type == REQ_UPDATE_CURSOR)
	{
		sql_info(tdbb, request,
				 sizeof(sql_records_info),
				 sql_records_info,
				 sizeof(buffer),
				 buffer);

		if (!request->req_updates)
		{
			ERRD_post(isc_sqlerr, isc_arg_number, (SLONG) - 913,
					  isc_arg_gds, isc_deadlock, isc_arg_gds,
					  isc_update_conflict, 0);
		}
	}
	else if (request->req_type == REQ_DELETE_CURSOR)
	{
		sql_info(tdbb, request,
				 sizeof(sql_records_info),
				 sql_records_info,
				 sizeof(buffer),
				 buffer);

		if (!request->req_deletes)
		{
			ERRD_post(isc_sqlerr, isc_arg_number, (SLONG) - 913,
					  isc_arg_gds, isc_deadlock, isc_arg_gds,
					  isc_update_conflict, 0);
		}
	}
}

/**
  
 	filter_sub_type
  
    @brief	Determine the sub_type to use in filtering
 	a blob.
 

    @param request
    @param node

 **/
static SSHORT filter_sub_type( dsql_req* request, const dsql_nod* node)
{
	if (node->nod_type == nod_constant)
		return (SSHORT) node->getSlong();

	const dsql_par* parameter = (dsql_par*) node->nod_arg[e_par_parameter];
	const dsql_par* null = parameter->par_null;
	if (null)
	{
		if (*((SSHORT *) null->par_desc.dsc_address))
			return 0;
	}

	return *((SSHORT *) parameter->par_desc.dsc_address);
}


/**
  
 	get_indices
  
    @brief	Retrieve the indices from the index tree in
 	the request info buffer (explain_ptr), and print them out
 	in the plan buffer. Return true on success and false on failure.
 

    @param explain_length_ptr
    @param explain_ptr
    @param plan_length_ptr
    @param plan_ptr

 **/
static bool get_indices(
						   SSHORT* explain_length_ptr,
						   const SCHAR** explain_ptr,
						   SSHORT* plan_length_ptr, SCHAR** plan_ptr)
{
	USHORT length;

	SSHORT explain_length = *explain_length_ptr;
	const SCHAR* explain = *explain_ptr;
	SSHORT plan_length = *plan_length_ptr;
	SCHAR* plan = *plan_ptr;

/* go through the index tree information, just
   extracting the indices used */

	explain_length--;
	switch (*explain++) {
	case isc_info_rsb_and:
	case isc_info_rsb_or:
		if (!get_indices(&explain_length, &explain, &plan_length, &plan))
			return false;
		if (!get_indices(&explain_length, &explain, &plan_length, &plan))
			return false;
		break;

	case isc_info_rsb_dbkey:
		break;

	case isc_info_rsb_index:
		explain_length--;
		length = *explain++;

		// if this isn't the first index, put out a comma 

		if (plan[-1] != '(' && plan[-1] != ' ') {
			plan_length -= 2;
			if (plan_length < 0)
				return false;
			*plan++ = ',';
			*plan++ = ' ';
		}

		// now put out the index name 

		if ((plan_length -= length) < 0)
			return false;
		explain_length -= length;
		while (length--)
			*plan++ = *explain++;
		break;

	default:
		return false;
	}

	*explain_length_ptr = explain_length;
	*explain_ptr = explain;
	*plan_length_ptr = plan_length;
	*plan_ptr = plan;

	return true;
}


/**
  
 	get_plan_info
  
    @brief	Get the access plan for the request and turn
 	it into a textual representation suitable for
 	human reading.
 

    @param request
    @param buffer_length
    @param buffer

 **/
static USHORT get_plan_info(thread_db* tdbb,
							dsql_req* request,
							SSHORT buffer_length, SCHAR** out_buffer)
{
	SCHAR explain_buffer[BUFFER_SMALL];

	memset(explain_buffer, 0, sizeof(explain_buffer));
	SCHAR* explain_ptr = explain_buffer;
	SCHAR* buffer_ptr = *out_buffer;

	// get the access path info for the underlying request from the engine 

	try
	{
		JRD_request_info(tdbb, request->req_request, 0, sizeof(explain_info), explain_info,
			sizeof(explain_buffer), explain_buffer);
	}
	catch (Firebird::Exception&)
	{
		return 0;
	}

	if (*explain_buffer == isc_info_truncated) {
		explain_ptr = (SCHAR *) gds__alloc(BUFFER_XLARGE);
		// CVC: Added test for memory exhaustion here.
		// Should we throw an exception or simply return 0 to the caller?
		if (!explain_ptr) {
			return 0;
		}

		try
		{
			JRD_request_info(tdbb, request->req_request, 0, sizeof(explain_info), explain_info,
				BUFFER_XLARGE, explain_ptr);
		}
		catch (Firebird::Exception&)
		{
			gds__free(explain_ptr);
			return 0;
		}

		if (*explain_ptr == isc_info_truncated)
		{
			// CVC: Before returning, deallocate the buffer!
			gds__free(explain_ptr);
			return 0;
		}
	}

	SCHAR* plan;
	for (int i = 0; i < 2; i++) {
		const SCHAR* explain = explain_ptr;

		if (*explain++ != isc_info_access_path)
		{
			// CVC: deallocate memory!
			if (explain_ptr != explain_buffer) {
				gds__free(explain_ptr);
			}
			return 0;
		}

		SSHORT explain_length = (UCHAR) *explain++;
		explain_length += (UCHAR) (*explain++) << 8;

		plan = buffer_ptr;

		/* CVC: What if we need to do 2nd pass? Those variables were only initialized
		at the begining of the function hence they had trash the second time. */
		USHORT join_count = 0, level = 0;

		// keep going until we reach the end of the explain info 

		while (explain_length > 0 && buffer_length > 0) {
			if (!get_rsb_item(&explain_length, &explain, &buffer_length, &plan,
							  &join_count, &level)) 
			{
				// don't allocate buffer of the same length second time
				// and let user know plan is incomplete
				if (buffer_ptr != *out_buffer) {
					if (buffer_length < 3) {
						plan -= 3 - buffer_length;
					}
					*plan++ = '.';
					*plan++ = '.';
					*plan++ = '.';
					break;
				}

				// assume we have run out of room in the buffer, try again with a larger one 
				char* temp = reinterpret_cast<char*>(gds__alloc(BUFFER_XLARGE));
				if (!temp) {
					// NOMEM. Do not attempt one more try
					i++;
					continue;
				}
				else {
					buffer_ptr = temp;
					buffer_length = BUFFER_XLARGE;
				}
				break;
			}
		}

		if (buffer_ptr == *out_buffer)
			break;
	}

	if (explain_ptr != explain_buffer) {
		gds__free(explain_ptr);
	}

	*out_buffer = buffer_ptr;
	return plan - *out_buffer;
}


/**
  
 	get_request_info
  
    @brief	Get the records updated/deleted for record
 

    @param request
    @param buffer_length
    @param buffer

 **/
static USHORT get_request_info(thread_db* tdbb,
							   dsql_req* request,
							   SSHORT buffer_length, UCHAR* buffer)
{
	// get the info for the request from the engine 

	try
	{
		JRD_request_info(tdbb, request->req_request, 0, sizeof(record_info),
			record_info, buffer_length, reinterpret_cast<char*>(buffer));
	}
	catch (Firebird::Exception&)
	{
		return 0;
	}

	const UCHAR* data = buffer;

	request->req_updates = request->req_deletes = 0;
	request->req_selects = request->req_inserts = 0;

	UCHAR p;
	while ((p = *data++) != isc_info_end) {
		const USHORT data_length =
			static_cast<USHORT>(gds__vax_integer(data, 2));
		data += 2;

		switch (p) {
		case isc_info_req_update_count:
			request->req_updates = gds__vax_integer(data, data_length);
			break;

		case isc_info_req_delete_count:
			request->req_deletes = gds__vax_integer(data, data_length);
			break;

		case isc_info_req_select_count:
			request->req_selects = gds__vax_integer(data, data_length);
			break;

		case isc_info_req_insert_count:
			request->req_inserts = gds__vax_integer(data, data_length);
			break;

		default:
			break;
		}

		data += data_length;
	}

	return data - buffer;
}


/**
  
 	get_rsb_item
  
    @brief	Use recursion to print out a reverse-polish
 	access plan of joins and join types. Return true on success
 	and false on failure
 

    @param explain_length_ptr
    @param explain_ptr
    @param plan_length_ptr
    @param plan_ptr
    @param parent_join_count
    @param level_ptr

 **/
static bool get_rsb_item(SSHORT*		explain_length_ptr,
							const SCHAR**	explain_ptr,
							SSHORT*		plan_length_ptr,
							SCHAR**		plan_ptr,
							USHORT*		parent_join_count,
							USHORT*		level_ptr)
{
	const SCHAR* p;
	SSHORT rsb_type;

	SSHORT explain_length = *explain_length_ptr;
	const SCHAR* explain = *explain_ptr;
	SSHORT plan_length = *plan_length_ptr;
	SCHAR* plan = *plan_ptr;

	explain_length--;
	switch (*explain++)
	{
	case isc_info_rsb_begin:
		if (!*level_ptr) {
			// put out the PLAN prefix 

			p = "\nPLAN ";
			if ((plan_length -= strlen(p)) < 0)
				return false;
			while (*p)
				*plan++ = *p++;
		}

		(*level_ptr)++;
		break;

	case isc_info_rsb_end:
		if (*level_ptr) {
			(*level_ptr)--;
		}
		/* else --*parent_join_count; ??? */
		break;

	case isc_info_rsb_relation:

		/* for the single relation case, initiate
		   the relation with a parenthesis */

		if (!*parent_join_count) {
			if (--plan_length < 0)
				return false;
			*plan++ = '(';
		}

		// if this isn't the first relation, put out a comma 

		if (plan[-1] != '(') {
			plan_length -= 2;
			if (plan_length < 0)
				return false;
			*plan++ = ',';
			*plan++ = ' ';
		}

		// put out the relation name 
		{ // scope to keep length local.
			explain_length--;
			SSHORT length = (UCHAR) * explain++;
			explain_length -= length;
			if ((plan_length -= length) < 0)
				return false;
			while (length--)
				*plan++ = *explain++;
		} // scope
		break;

	case isc_info_rsb_type:
		explain_length--;
		switch (rsb_type = *explain++)
		{
			/* for stream types which have multiple substreams, print out
			   the stream type and recursively print out the substreams so
			   we will know where to put the parentheses */

		case isc_info_rsb_union:
		case isc_info_rsb_recursive:

			// put out all the substreams of the join 
			{ // scope to have union_count, union_level and union_join_count local.
				explain_length--;
				USHORT union_count = (USHORT) * explain++ - 1;

				// finish the first union member

				USHORT union_level = *level_ptr;
				USHORT union_join_count = 0;
				while (explain_length > 0 && plan_length > 0) {
					if (!get_rsb_item(&explain_length, &explain, &plan_length, &plan,
									  &union_join_count, &union_level))
					{
						return false;
					}
					if (union_level == *level_ptr)
						break;
				}

				/* for the rest of the members, start the level at 0 so each
				   gets its own "PLAN ... " line */

				while (union_count) {
					union_join_count = 0;
					union_level = 0;
					while (explain_length > 0 && plan_length > 0) {
						if (!get_rsb_item(&explain_length, &explain, &plan_length,
										  &plan, &union_join_count, &union_level))
						{
							return false;
						}
						if (!union_level)
							break;
					}
					union_count--;
				}
			} // scope
			break;

		case isc_info_rsb_cross:
		case isc_info_rsb_left_cross:
		case isc_info_rsb_merge:

			/* if this join is itself part of a join list,
			   but not the first item, then put out a comma */

			if (*parent_join_count && plan[-1] != '(') {
				plan_length -= 2;
				if (plan_length < 0)
					return false;
				*plan++ = ',';
				*plan++ = ' ';
			}

			// put out the join type 

			if (rsb_type == isc_info_rsb_cross ||
				rsb_type == isc_info_rsb_left_cross)
			{
				p = "JOIN (";
			}
			else {
				p = "MERGE (";
			}

			if ((plan_length -= strlen(p)) < 0)
				return false;
			while (*p)
				*plan++ = *p++;

			// put out all the substreams of the join 

			explain_length--;
			{ // scope to have join_count local.
				USHORT join_count = (USHORT) * explain++;
				while (join_count && explain_length > 0 && plan_length > 0) {
					if (!get_rsb_item(&explain_length, &explain, &plan_length,
									  &plan, &join_count, level_ptr))
					{
						return false;
					}
					// CVC: Here's the additional stop condition.
					if (!*level_ptr) {
						break;
					}
				}
			} // scope

			// put out the final parenthesis for the join 

			if (--plan_length < 0)
				return false;
			else
				*plan++ = ')';

			// this qualifies as a stream, so decrement the join count 

			if (*parent_join_count)
				-- * parent_join_count;
			break;

		case isc_info_rsb_indexed:
		case isc_info_rsb_navigate:
		case isc_info_rsb_sequential:
		case isc_info_rsb_ext_sequential:
		case isc_info_rsb_ext_indexed:
		case isc_info_rsb_virt_sequential:
			switch (rsb_type)
			{
			case isc_info_rsb_indexed:
			case isc_info_rsb_ext_indexed:
				p = " INDEX (";
				break;
			case isc_info_rsb_navigate:
				p = " ORDER ";
				break;
			default:
				p = " NATURAL";
			}

			if ((plan_length -= strlen(p)) < 0)
				return false;
			while (*p)
				*plan++ = *p++;

			// print out additional index information 

			if (rsb_type == isc_info_rsb_indexed ||
				rsb_type == isc_info_rsb_navigate ||
				rsb_type == isc_info_rsb_ext_indexed)
			{
				if (!get_indices(&explain_length, &explain, &plan_length, &plan))
					return false;
			}

			if (rsb_type == isc_info_rsb_navigate &&
				*explain == isc_info_rsb_indexed)
			{
				USHORT idx_count = 1;
				if (!get_rsb_item(&explain_length, &explain, &plan_length,
								  &plan, &idx_count, level_ptr))
				{
					return false;
				}
			}

			if (rsb_type == isc_info_rsb_indexed ||
				rsb_type == isc_info_rsb_ext_indexed)
			{
				if (--plan_length < 0)
					return false;
				*plan++ = ')';
			}

			// detect the end of a single relation and put out a final parenthesis 

			if (!*parent_join_count)
				if (--plan_length < 0)
					return false;
				else
					*plan++ = ')';

			// this also qualifies as a stream, so decrement the join count 

			if (*parent_join_count)
				-- * parent_join_count;
			break;

		case isc_info_rsb_sort:

			/* if this sort is on behalf of a union, don't bother to
			   print out the sort, because unions handle the sort on all
			   substreams at once, and a plan maps to each substream
			   in the union, so the sort doesn't really apply to a particular plan */

			if (explain_length > 2 &&
				(explain[0] == isc_info_rsb_begin) &&
				(explain[1] == isc_info_rsb_type) &&
				(explain[2] == isc_info_rsb_union))
			{
				break;
			}

			// if this isn't the first item in the list, put out a comma 

			if (*parent_join_count && plan[-1] != '(') {
				plan_length -= 2;
				if (plan_length < 0)
					return false;
				*plan++ = ',';
				*plan++ = ' ';
			}

			p = "SORT (";

			if ((plan_length -= strlen(p)) < 0)
				return false;
			while (*p)
				*plan++ = *p++;

			/* the rsb_sort should always be followed by a begin...end block,
			   allowing us to include everything inside the sort in parentheses */

			{ // scope to have save_level local.
				const USHORT save_level = *level_ptr;
				while (explain_length > 0 && plan_length > 0) {
					if (!get_rsb_item(&explain_length, &explain, &plan_length,
									  &plan, parent_join_count, level_ptr))
					{
						return false;
					}
					if (*level_ptr == save_level)
						break;
				}

				if (--plan_length < 0)
					return false;
				*plan++ = ')';
			} // scope
			break;

		default:
			break;
		} // switch (rsb_type = *explain++)
		break;

	default:
		break;
	}

	*explain_length_ptr = explain_length;
	*explain_ptr = explain;
	*plan_length_ptr = plan_length;
	*plan_ptr = plan;

	return true;
}


/**
  
 	init
  
    @brief	Initialize dynamic SQL.  This is called only once.
 

    @param db_handle

 **/
static dsql_dbb* init(Attachment* attachment)
{
	if (!attachment->att_dsql_instance)
	{
		MemoryPool& pool = *attachment->att_database->createPool();
		dsql_dbb* const database = FB_NEW(pool) dsql_dbb(pool);
		database->dbb_attachment = attachment;
		database->dbb_database = attachment->att_database;
		database->dbb_sys_trans = attachment->att_database->dbb_sys_trans;
		attachment->att_dsql_instance = database;

		SCHAR buffer[BUFFER_TINY];

		try
		{
			INF_database_info(db_hdr_info_items, sizeof(db_hdr_info_items), buffer, sizeof(buffer));
		}
		catch (Firebird::Exception&)
		{
			return database;
		}

		const UCHAR* data = reinterpret_cast<UCHAR*>(buffer);
		UCHAR p;
		while ((p = *data++) != isc_info_end)
		{
			const SSHORT l = static_cast<SSHORT>(gds__vax_integer(data, 2));
			data += 2;

			switch (p)
			{
			case isc_info_db_sql_dialect:
				database->dbb_db_SQL_dialect = (USHORT) data[0];
				break;

			case isc_info_ods_version:
				database->dbb_ods_version = gds__vax_integer(data, l);
				if (database->dbb_ods_version <= 7)
				{
					ERRD_post(isc_sqlerr, isc_arg_number, (SLONG) - 804,
					  isc_arg_gds, isc_dsql_too_old_ods,
					  isc_arg_number, (SLONG) 8, 0);
				}
				break;

			case isc_info_ods_minor_version:
				database->dbb_minor_version = gds__vax_integer(data, l);
				break;

			// This flag indicates the version level of the engine
			// itself, so we can tell what capabilities the engine
			// code itself (as opposed to the on-disk structure).
			// Apparently the base level up to now indicated the major
			// version number, but for 4.1 the base level is being
			// incremented, so the base level indicates an engine version
			// as follows:
			// 1 == v1.x
			// 2 == v2.x
			// 3 == v3.x
			// 4 == v4.0 only
			// 5 == v4.1
			// Note: this info item is so old it apparently uses an
			// archaic format, not a standard vax integer format.

			case isc_info_base_level:
				database->dbb_base_level = (USHORT) data[1];
				break;

			case isc_info_db_read_only:
				database->dbb_read_only = (USHORT) data[0] ? true : false;
				break;

			default:
				break;
			}

			data += l;
		}
	}

	return attachment->att_dsql_instance;
}


/**
  
 	map_in_out
  
    @brief	Map data from external world into message or
 	from message to external world.
 

    @param request
    @param message
    @param blr_length
    @param blr
    @param msg_length
    @param dsql_msg

 **/
static void map_in_out(	dsql_req*		request,
						dsql_msg*		message,
						USHORT	blr_length,
						const UCHAR*	blr,
						USHORT	msg_length,
						UCHAR*	dsql_msg_buf)
{
	dsql_par* parameter;

	USHORT count = parse_blr(blr_length, blr, msg_length, message->msg_parameters);

/* When mapping data from the external world, request will be non-NULL.
   When mapping data from an internal message, request will be NULL. */

	for (parameter = message->msg_parameters; parameter;
		 parameter = parameter->par_next)
	{
		if (parameter->par_index)
		{
			 // Make sure the message given to us is long enough 

			DSC    desc   = parameter->par_user_desc;
			USHORT length = (IPTR) desc.dsc_address + desc.dsc_length;
			if (length > msg_length)
				break;
			if (!desc.dsc_dtype)
				break;

			SSHORT* flag = NULL;
			dsql_par* null = parameter->par_null;
			if (null != NULL)
			{
				const USHORT null_offset = (IPTR) null->par_user_desc.dsc_address;
				length = null_offset + sizeof(SSHORT);
				if (length > msg_length)
					break;

				if (!request) {
					flag = (SSHORT *) (dsql_msg_buf + null_offset);
					*flag = *((SSHORT *) null->par_desc.dsc_address);
				}
				else {
					flag = (SSHORT *) null->par_desc.dsc_address;
					*flag = *((SSHORT *) (dsql_msg_buf + null_offset));
				}
			}

			desc.dsc_address = dsql_msg_buf + (IPTR) desc.dsc_address;
			if (!request)
				MOVD_move(&parameter->par_desc, &desc);
			else if (!flag || *flag >= 0)
				MOVD_move(&desc, &parameter->par_desc);
			else if (parameter->par_desc.isBlob())
				memset(parameter->par_desc.dsc_address, 0, parameter->par_desc.dsc_length);

			count--;
		}
	}

/* If we got here because the loop was exited early or if part of the
   message given to us hasn't been used, complain. */

	if (parameter || count)
	{
		ERRD_post(isc_sqlerr, isc_arg_number, (SLONG) - 804,
				  isc_arg_gds, isc_dsql_sqlda_err, 0);
	}

	dsql_par* dbkey;
	if (request &&
		((dbkey = request->req_parent_dbkey) != NULL) &&
		((parameter = request->req_dbkey) != NULL))
	{
		MOVD_move(&dbkey->par_desc, &parameter->par_desc);
		dsql_par* null = parameter->par_null;
		if (null != NULL)
		{
			SSHORT* flag = (SSHORT *) null->par_desc.dsc_address;
			*flag = 0;
		}
	}

	dsql_par* rec_version;
	if (request &&
		((rec_version = request->req_parent_rec_version) != NULL) &&
		((parameter = request->req_rec_version) != NULL))
	{
		MOVD_move(&rec_version->par_desc, &parameter->par_desc);
		dsql_par* null = parameter->par_null;
		if (null != NULL)
		{
			SSHORT* flag = (SSHORT *) null->par_desc.dsc_address;
			*flag = 0;
		}
	}
}


/**
  
 	parse_blr
  
    @brief	Parse the message of a blr request.
 

    @param blr_length
    @param blr
    @param msg_length
    @param parameters

 **/
static USHORT parse_blr(
						USHORT blr_length,
						const UCHAR* blr, const USHORT msg_length, dsql_par* parameters)
{
/* If there's no blr length, then the format of the current message buffer
   is identical to the format of the previous one. */

	if (!blr_length)
	{
		USHORT par_count = 0;
		for (const dsql_par* parameter = parameters; parameter;
			 parameter = parameter->par_next)
		{
			if (parameter->par_index) {
				++par_count;
			}
		}
		return par_count;
	}

	if (*blr != blr_version4 && *blr != blr_version5)
	{
		ERRD_post(isc_sqlerr, isc_arg_number, (SLONG) - 804,
				  isc_arg_gds, isc_dsql_sqlda_err, 0);
	}
	blr++;						// skip the blr_version 
	if (*blr++ != blr_begin || *blr++ != blr_message)
	{
		ERRD_post(isc_sqlerr, isc_arg_number, (SLONG) - 804,
				  isc_arg_gds, isc_dsql_sqlda_err, 0);
	}

	++blr;						// skip the message number 
	USHORT count = *blr++;
	count += (*blr++) << 8;
	count /= 2;

	USHORT offset = 0;
	for (USHORT index = 1; index <= count; index++)
	{
		dsc desc;
		desc.dsc_scale = 0;
		desc.dsc_sub_type = 0;
		desc.dsc_flags = 0;
		
		switch (*blr++)
		{
		case blr_text:
			desc.dsc_dtype = dtype_text;
			desc.dsc_sub_type = ttype_dynamic;
			desc.dsc_length = *blr++;
			desc.dsc_length += (*blr++) << 8;
			break;

		case blr_varying:
			desc.dsc_dtype = dtype_varying;
			desc.dsc_sub_type = ttype_dynamic;
			desc.dsc_length = *blr++ + sizeof(USHORT);
			desc.dsc_length += (*blr++) << 8;
			break;

		case blr_text2:
			desc.dsc_dtype = dtype_text;
			desc.dsc_sub_type = *blr++;
			desc.dsc_sub_type += (*blr++) << 8;
			desc.dsc_length = *blr++;
			desc.dsc_length += (*blr++) << 8;
			break;

		case blr_varying2:
			desc.dsc_dtype = dtype_varying;
			desc.dsc_sub_type = *blr++;
			desc.dsc_sub_type += (*blr++) << 8;
			desc.dsc_length = *blr++ + sizeof(USHORT);
			desc.dsc_length += (*blr++) << 8;
			break;

		case blr_short:
			desc.dsc_dtype = dtype_short;
			desc.dsc_length = sizeof(SSHORT);
			desc.dsc_scale = *blr++;
			break;

		case blr_long:
			desc.dsc_dtype = dtype_long;
			desc.dsc_length = sizeof(SLONG);
			desc.dsc_scale = *blr++;
			break;

		case blr_int64:
			desc.dsc_dtype = dtype_int64;
			desc.dsc_length = sizeof(SINT64);
			desc.dsc_scale = *blr++;
			break;

		case blr_quad:
			desc.dsc_dtype = dtype_quad;
			desc.dsc_length = sizeof(SLONG) * 2;
			desc.dsc_scale = *blr++;
			break;

		case blr_float:
			desc.dsc_dtype = dtype_real;
			desc.dsc_length = sizeof(float);
			break;

		case blr_double:
		case blr_d_float:
			desc.dsc_dtype = dtype_double;
			desc.dsc_length = sizeof(double);
			break;

		case blr_timestamp:
			desc.dsc_dtype = dtype_timestamp;
			desc.dsc_length = sizeof(SLONG) * 2;
			break;

		case blr_sql_date:
			desc.dsc_dtype = dtype_sql_date;
			desc.dsc_length = sizeof(SLONG);
			break;

		case blr_sql_time:
			desc.dsc_dtype = dtype_sql_time;
			desc.dsc_length = sizeof(SLONG);
			break;

		default:
			ERRD_post(isc_sqlerr, isc_arg_number, (SLONG) - 804,
					  isc_arg_gds, isc_dsql_sqlda_err, 0);
		}

		USHORT align = type_alignments[desc.dsc_dtype];
		if (align)
			offset = FB_ALIGN(offset, align);
		desc.dsc_address = (UCHAR*)(IPTR) offset;
		offset += desc.dsc_length;

		if (*blr++ != blr_short || *blr++ != 0)
			ERRD_post(isc_sqlerr, isc_arg_number, (SLONG) - 804,
					  isc_arg_gds, isc_dsql_sqlda_err, 0);

		align = type_alignments[dtype_short];
		if (align)
			offset = FB_ALIGN(offset, align);
		USHORT null_offset = offset;
		offset += sizeof(SSHORT);

		for (dsql_par* parameter = parameters; parameter; parameter = parameter->par_next)
		{
			if (parameter->par_index == index) {
				parameter->par_user_desc = desc;
				dsql_par* null = parameter->par_null;
				if (null) {
					null->par_user_desc.dsc_dtype = dtype_short;
					null->par_user_desc.dsc_scale = 0;
					null->par_user_desc.dsc_length = sizeof(SSHORT);
					null->par_user_desc.dsc_address = (UCHAR*)(IPTR) null_offset;
				}
			}
		}
	}

	if (*blr++ != (UCHAR) blr_end || offset != msg_length)
	{
		ERRD_post(isc_sqlerr, isc_arg_number, (SLONG) - 804,
				  isc_arg_gds, isc_dsql_sqlda_err, 0);
	}

	return count;
}


/**
  
 	prepare
  
    @brief	Prepare a statement for execution.  Return SQL status
 	code.  Note: caller is responsible for pool handling.
 

    @param request
    @param string_length
    @param string
    @param client_dialect
    @param parser_version

 **/
static dsql_req* prepare(thread_db* tdbb,
				   dsql_req* request,
				   USHORT string_length,
				   const TEXT* string,
				   USHORT client_dialect, USHORT parser_version)
{
	ISC_STATUS_ARRAY local_status;
	MOVE_CLEAR(local_status, sizeof(local_status));

	if (client_dialect > SQL_DIALECT_CURRENT)
	{
		ERRD_post(isc_sqlerr, isc_arg_number, (SLONG) - 901,
				  isc_arg_gds, isc_wish_list, 0);
	}

	if (!string_length)
		string_length = strlen(string);

/* Get rid of the trailing ";" if there is one. */

	for (const TEXT* p = string + string_length; p-- > string;)
	{
		if (*p != ' ') {
			if (*p == ';')
				string_length = p - string;
			break;
		}
	}

	// Parse the SQL statement.  If it croaks, return 

	Parser parser(client_dialect, request->req_dbb->dbb_db_SQL_dialect, parser_version,
		string, string_length, tdbb->getAttachment()->att_charset);

	dsql_nod* node = parser.parse();

	if (!node)
	{
		// CVC: Apparently, dsql_ypparse won't return if the command is incomplete,
		// because yyerror() will call ERRD_post().
		// This may be a special case, but we don't know about positions here.
		ERRD_post(isc_sqlerr, isc_arg_number, (SLONG) - 104,
			isc_arg_gds, isc_command_end_err,	// Unexpected end of command
				  0);
	}

// allocate the send and receive messages 

	MemoryPool& pool = *tdbb->getDefaultPool();
	request->req_send = FB_NEW(pool) dsql_msg;
	dsql_msg* message = FB_NEW(pool) dsql_msg;
	request->req_receive = message;
	message->msg_number = 1;

#ifdef SCROLLABLE_CURSORS
	if (request->req_dbb->dbb_base_level >= 5) {
		/* allocate a message in which to send scrolling information
		   outside of the normal send/receive protocol */

		request->req_async = message = FB_NEW(*tdsql->getDefaultPool()) dsql_msg;
		message->msg_number = 2;
	}
#endif

	request->req_type = REQ_SELECT;
	request->req_flags &= ~REQ_cursor_open;

/*
 * No work is done during pass1 for set transaction - like
 * checking for valid table names.  This is because that will
 * require a valid transaction handle.
 * Error will be caught at execute time.
 */

	node = PASS1_statement(request, node, false);
	if (!node)
		return request;

// stop here for requests not requiring code generation 

	if (request->req_type == REQ_DDL && parser.isStmtAmbiguous() &&
		request->req_dbb->dbb_db_SQL_dialect != client_dialect)
	{
		ERRD_post(isc_sqlerr, isc_arg_number, (SLONG) - 817,
				  isc_arg_gds, isc_ddl_not_allowed_by_db_sql_dial,
				  isc_arg_number,
				  (SLONG) request->req_dbb->dbb_db_SQL_dialect, 0);
	}

	if (request->req_type == REQ_COMMIT ||
		request->req_type == REQ_COMMIT_RETAIN ||
		request->req_type == REQ_ROLLBACK ||
		request->req_type == REQ_ROLLBACK_RETAIN)
	{
		return request;
	}

// Work on blob segment requests 

	if (request->req_type == REQ_GET_SEGMENT ||
		request->req_type == REQ_PUT_SEGMENT)
	{
		GEN_port(request, request->req_blob->blb_open_in_msg);
		GEN_port(request, request->req_blob->blb_open_out_msg);
		GEN_port(request, request->req_blob->blb_segment_msg);
		return request;
	}

// Generate BLR, DDL or TPB for request 

// Start transactions takes parameters via a parameter block.
// The request blr string is used for that

	if (request->req_type == REQ_START_TRANS) {
		GEN_start_transaction(request, node);
		return request;
	}

	if (client_dialect > SQL_DIALECT_V5)
		request->req_flags |= REQ_blr_version5;
	else
		request->req_flags |= REQ_blr_version4;

	GEN_request(request, node);
	const USHORT length = request->req_blr_data.getCount();
	
// stop here for ddl requests 

	if (request->req_type == REQ_CREATE_DB ||
		request->req_type == REQ_DDL)
	{
		return request;
	}

// have the access method compile the request 

#ifdef DSQL_DEBUG
	if (DSQL_debug & 64) {
		dsql_trace("Resulting BLR code for DSQL:");
		gds__trace_raw("Statement:\n");
		gds__trace_raw(string, string_length);
		gds__trace_raw("\nBLR:\n");
		gds__print_blr(request->req_blr_data.begin(),
			gds__trace_printer, 0, 0);
	}
#endif

// check for warnings 
	if (tdbb->tdbb_status_vector[2] == isc_arg_warning) {
		// save a status vector 
		memcpy(local_status, tdbb->tdbb_status_vector, sizeof(ISC_STATUS_ARRAY));
	}

	ISC_STATUS status = FB_SUCCESS;

	{	// scope
		Database::Checkout dcoHolder(request->req_dbb->dbb_database);
		status = jrd8_internal_compile_request(tdbb->tdbb_status_vector,
											   &request->req_dbb->dbb_attachment,
											   &request->req_request,
											   length,
											   (const char*)(request->req_blr_data.begin()),
											   string_length,
											   string,
											   request->req_debug_data.getCount(),
											   request->req_debug_data.begin());
	}

// restore warnings (if there are any) 
	if (local_status[2] == isc_arg_warning)
	{
		int indx, len, warning;

		// find end of a status vector 
		PARSE_STATUS(tdbb->tdbb_status_vector, indx, warning);
		if (indx)
			--indx;

		// calculate length of saved warnings 
		PARSE_STATUS(local_status, len, warning);
		len -= 2;

		if ((len + indx - 1) < ISC_STATUS_LENGTH)
			memcpy(&tdbb->tdbb_status_vector[indx], &local_status[2], sizeof(ISC_STATUS) * len);
	}

// free blr memory
	request->req_blr_data.free();

	if (status)
		Firebird::status_exception::raise(tdbb->tdbb_status_vector);

	return request;
}


/**
  
 	put_item
  
    @brief	Put information item in output buffer if there is room, and
 	return an updated pointer.  If there isn't room for the item,
 	indicate truncation and return NULL.
 

    @param item
    @param length
    @param string
    @param ptr
    @param end

 **/
static UCHAR* put_item(	UCHAR	item,
						USHORT	length,
						const UCHAR* string,
						UCHAR*	ptr,
						const UCHAR* const end)
{

	if (ptr + length + 3 >= end) {
		*ptr = isc_info_truncated;
		return NULL;
	}

	*ptr++ = item;

	*ptr++ = (UCHAR)length;
	*ptr++ = length >> 8;

	if (length) {
		do {
			*ptr++ = *string++;
		} while (--length);
	}

	return ptr;
}


/**
  
 	release_request
  
    @brief	Release a dynamic request.
 

    @param request
    @param top_level

 **/
static void release_request(thread_db* tdbb, dsql_req* request, bool drop)
{
	// If request is parent, orphan the children and
	// release a portion of their requests

	for (dsql_req* child = request->req_offspring; child; child = child->req_sibling)
	{
		child->req_flags |= REQ_orphan;
		child->req_parent = NULL;
		Jrd::ContextPoolHolder context(tdbb, &child->req_pool);
		release_request(tdbb, child, false);
	}

	// For requests that are linked to a parent, unlink it

	if (request->req_parent)
	{
		dsql_req* parent = request->req_parent;
		for (dsql_req** ptr = &parent->req_offspring; *ptr; ptr = &(*ptr)->req_sibling)
		{
			if (*ptr == request) {
				*ptr = request->req_sibling;
				break;
			}
		}
		request->req_parent = NULL;
	}

	// If the request had an open cursor, close it

	if (request->req_flags & REQ_cursor_open) {
		close_cursor(request);
	}

	// If request is named, clear it from the hash table

	if (request->req_name) {
		HSHD_remove(request->req_name);
		request->req_name = NULL;
	}

	if (request->req_cursor) {
		HSHD_remove(request->req_cursor);
		request->req_cursor = NULL;
	}

	// If a request has been compiled, release it now

	if (request->req_request) {
		CMP_release(tdbb, request->req_request);
		request->req_request = NULL;
	}

	// free blr memory
	request->req_blr_data.free();

	// Release the entire request if explicitly asked for

	if (drop)
	{
		request->req_dbb->deletePool(&request->req_pool);
	}
}


/**

	sql_info

	@brief	Return DSQL information buffer.

	@param request
	@param item_length
	@param items
	@param info_length
	@param info

 **/

static void sql_info(thread_db* tdbb,
					 dsql_req* request,
					 USHORT item_length,
					 const UCHAR* items,
					 USHORT info_length,
					 UCHAR* info)
{
	if (!item_length || !items || !info_length || !info)
		return;

	UCHAR buffer[BUFFER_SMALL];
	memset(buffer, 0, sizeof(buffer));

	// Pre-initialize buffer. This is necessary because we don't want to transfer rubbish over the wire
	memset(info, 0, info_length); 

	const UCHAR* const end_items = items + item_length;
	const UCHAR* const end_info = info + info_length;
	UCHAR *start_info;
	if (*items == isc_info_length) {
		start_info = info;
		items++;
	}
	else {
		start_info = NULL;
	}

	// CVC: Is it the idea that this pointer remains with its previous value
	// in the loop or should it be made NULL in each iteration?
	dsql_msg** message = NULL;
	USHORT first_index = 0;

	while (items < end_items && *items != isc_info_end)
	{
		USHORT length, number;
		UCHAR item = *items++; // cannot be const
		if (item == isc_info_sql_select || item == isc_info_sql_bind)
		{
			message = (item == isc_info_sql_select) ?
				&request->req_receive : &request->req_send;
			if (info + 1 >= end_info) {
				*info = isc_info_truncated;
				return;
			}
			*info++ = item;
		}
		else if (item == isc_info_sql_stmt_type)
		{
			switch (request->req_type) {
			case REQ_SELECT:
			case REQ_EMBED_SELECT:
				number = isc_info_sql_stmt_select;
				break;
			case REQ_SELECT_UPD:
				number = isc_info_sql_stmt_select_for_upd;
				break;
			case REQ_CREATE_DB:
			case REQ_DDL:
				number = isc_info_sql_stmt_ddl;
				break;
			case REQ_GET_SEGMENT:
				number = isc_info_sql_stmt_get_segment;
				break;
			case REQ_PUT_SEGMENT:
				number = isc_info_sql_stmt_put_segment;
				break;
			case REQ_COMMIT:
			case REQ_COMMIT_RETAIN:
				number = isc_info_sql_stmt_commit;
				break;
			case REQ_ROLLBACK:
			case REQ_ROLLBACK_RETAIN:
				number = isc_info_sql_stmt_rollback;
				break;
			case REQ_START_TRANS:
				number = isc_info_sql_stmt_start_trans;
				break;
			case REQ_INSERT:
				number = isc_info_sql_stmt_insert;
				break;
			case REQ_UPDATE:
			case REQ_UPDATE_CURSOR:
				number = isc_info_sql_stmt_update;
				break;
			case REQ_DELETE:
			case REQ_DELETE_CURSOR:
				number = isc_info_sql_stmt_delete;
				break;
			case REQ_EXEC_PROCEDURE:
				number = isc_info_sql_stmt_exec_procedure;
				break;
			case REQ_SET_GENERATOR:
				number = isc_info_sql_stmt_set_generator;
				break;
			case REQ_SAVEPOINT:
				number = isc_info_sql_stmt_savepoint;
				break;
			case REQ_EXEC_BLOCK: 
				number = isc_info_sql_stmt_exec_procedure;
				break;
			case REQ_SELECT_BLOCK: 
				number = isc_info_sql_stmt_select;
				break;
			default:
				number = 0;
				break;
			}
			length = convert((SLONG) number, buffer);
			info = put_item(item, length, buffer, info, end_info);
			if (!info) {
				return;
			}
		}
		else if (item == isc_info_sql_sqlda_start) {
			length = *items++;
			first_index =
				static_cast<USHORT>(gds__vax_integer(items, length));
			items += length;
		}
		else if (item == isc_info_sql_batch_fetch) {
			if (request->req_flags & REQ_no_batch)
				number = 0;
			else
				number = 1;
			length = convert((SLONG) number, buffer);
			if (!(info = put_item(item, length, buffer, info, end_info))) {
				return;
			}
		}
		else if (item == isc_info_sql_records) {
			length = get_request_info(tdbb, request, (SSHORT) sizeof(buffer), buffer);
			if (length && !(info = put_item(item, length, buffer, info, end_info))) 
			{
				return;
			}
		}
		else if (item == isc_info_sql_get_plan) {
		/* be careful, get_plan_info() will reallocate the buffer to a
		   larger size if it is not big enough */

			UCHAR* buffer_ptr = buffer;
			length =
				get_plan_info(tdbb, request, (SSHORT) sizeof(buffer), reinterpret_cast<SCHAR**>(&buffer_ptr));

			if (length) {
				info = put_item(item, length, buffer_ptr, info, end_info);
			}

			if (length > sizeof(buffer)) {
				gds__free(buffer_ptr);
			}

			if (!info) {
				return;
			}
		}
		else if (!message ||
			 	(item != isc_info_sql_num_variables
			  	&& item != isc_info_sql_describe_vars))
		{
			buffer[0] = item;
			item = isc_info_error;
			length = 1 + convert((SLONG) isc_infunk, buffer + 1);
			if (!(info = put_item(item, length, buffer, info, end_info))) {
				return;
			}
		}
		else
		{
			number = (*message) ? (*message)->msg_index : 0;
			length = convert((SLONG) number, buffer);
			if (!(info = put_item(item, length, buffer, info, end_info))) {
				return;
			}
			if (item == isc_info_sql_num_variables) {
				continue;
			}

			const UCHAR* end_describe = items;
			while (end_describe < end_items &&
			   	*end_describe != isc_info_end &&
			   	*end_describe != isc_info_sql_describe_end) 
			{
				end_describe++;
			}

			info = var_info(*message,
						items,
						end_describe,
						info,
						end_info,
						first_index);
			if (!info) {
				return;
			}

			items = end_describe;
			if (*items == isc_info_sql_describe_end) {
				items++;
			}
		}
	}

	*info++ = isc_info_end;

	if (start_info && (end_info - info >= 7))
	{
		SLONG number = info - start_info;
		memmove(start_info + 7, start_info, number);
		USHORT length = convert(number, buffer);
		put_item(isc_info_length, length, buffer, start_info, end_info);
	}
}


/**
  
 	var_info
  
    @brief	Provide information on an internal message.
 

    @param message
    @param items
    @param end_describe
    @param info
    @param end
    @param first_index

 **/
static UCHAR* var_info(
					   dsql_msg* message,
					   const UCHAR* items,
					   const UCHAR* const end_describe,
					   UCHAR* info, 
					   const UCHAR* const end, 
					   USHORT first_index)
{
	UCHAR buf[128];
	SLONG sql_type, sql_sub_type, sql_scale, sql_len;

	if (!message || !message->msg_index)
		return info;

	Firebird::HalfStaticArray<const dsql_par*, 16> parameters;

	for (const dsql_par* param = message->msg_parameters; param;
		param = param->par_next)
	{
		if (param->par_index)
		{
			if (param->par_index > parameters.getCount())
				parameters.grow(param->par_index);
			fb_assert(!parameters[param->par_index - 1]);
			parameters[param->par_index - 1] = param;
		}
	}

	for (size_t i = 0; i < parameters.getCount(); i++)
	{
		const dsql_par* param = parameters[i];
		fb_assert(param);

		if (param->par_index >= first_index)
		{
			sql_len = param->par_desc.dsc_length;
			sql_sub_type = 0;
			sql_scale = 0;
			switch (param->par_desc.dsc_dtype)
			{
			case dtype_real:
				sql_type = SQL_FLOAT;
				break;
			case dtype_array:
				sql_type = SQL_ARRAY;
				break;

			case dtype_timestamp:
				sql_type = SQL_TIMESTAMP;
				break;
			case dtype_sql_date:
				sql_type = SQL_TYPE_DATE;
				break;
			case dtype_sql_time:
				sql_type = SQL_TYPE_TIME;
				break;

			case dtype_double:
				sql_type = SQL_DOUBLE;
				sql_scale = param->par_desc.dsc_scale;
				break;

			case dtype_text:
				sql_type = SQL_TEXT;
				sql_sub_type = param->par_desc.dsc_sub_type;
				break;

			case dtype_blob:
				sql_type = SQL_BLOB;
				sql_sub_type = param->par_desc.dsc_sub_type;
				sql_scale = param->par_desc.dsc_scale;
				break;

			case dtype_varying:
				sql_type = SQL_VARYING;
				sql_len -= sizeof(USHORT);
				sql_sub_type = param->par_desc.dsc_sub_type;
				break;

			case dtype_short:
			case dtype_long:
			case dtype_int64:
				if (param->par_desc.dsc_dtype == dtype_short)
					sql_type = SQL_SHORT;
				else if (param->par_desc.dsc_dtype == dtype_long)
					sql_type = SQL_LONG;
				else
					sql_type = SQL_INT64;
				sql_scale = param->par_desc.dsc_scale;
				if (param->par_desc.dsc_sub_type)
					sql_sub_type = param->par_desc.dsc_sub_type;
				break;

			case dtype_quad:
				sql_type = SQL_QUAD;
				sql_scale = param->par_desc.dsc_scale;
				break;

			default:
				ERRD_post(isc_sqlerr, isc_arg_number, (SLONG) - 804,
						  isc_arg_gds, isc_dsql_datatype_err, 0);
			}

			if (sql_type && (param->par_desc.dsc_flags & DSC_nullable))
				sql_type++;

			for (const UCHAR* describe = items; describe < end_describe;)
			{
				USHORT length;
				const TEXT* name;
				const UCHAR* buffer = buf;
				UCHAR item = *describe++;
				switch (item) {
				case isc_info_sql_sqlda_seq:
					length = convert((SLONG) param->par_index, buf);
					break;

				case isc_info_sql_message_seq:
					length = 0;
					break;

				case isc_info_sql_type:
					length = convert((SLONG) sql_type, buf);
					break;

				case isc_info_sql_sub_type:
					length = convert((SLONG) sql_sub_type, buf);
					break;

				case isc_info_sql_scale:
					length = convert((SLONG) sql_scale, buf);
					break;

				case isc_info_sql_length:
					length = convert((SLONG) sql_len, buf);
					break;

				case isc_info_sql_null_ind:
					length = convert((SLONG) (sql_type & 1), buf);
					break;

				case isc_info_sql_field:
					if (name = param->par_name) {
						length = strlen(name);
						buffer = reinterpret_cast<const UCHAR*>(name);
					}
					else
						length = 0;
					break;

				case isc_info_sql_relation:
					if (name = param->par_rel_name) {
						length = strlen(name);
						buffer = reinterpret_cast<const UCHAR*>(name);
					}
					else
						length = 0;
					break;

				case isc_info_sql_owner:
					if (name = param->par_owner_name) {
						length = strlen(name);
						buffer = reinterpret_cast<const UCHAR*>(name);
					}
					else
						length = 0;
					break;

				case isc_info_sql_relation_alias:
					if (name = param->par_rel_alias) {
						length = strlen(name);
						buffer = reinterpret_cast<const UCHAR*>(name);
					}
					else
						length = 0;
					break;

				case isc_info_sql_alias:
					if (name = param->par_alias) {
						length = strlen(name);
						buffer = reinterpret_cast<const UCHAR*>(name);
					}
					else
						length = 0;
					break;

				default:
					buf[0] = item;
					item = isc_info_error;
					length = 1 + convert((SLONG) isc_infunk, buf + 1);
					break;
				}

				if (!(info = put_item(item, length, buffer, info, end)))
					return info;
			}

			if (info + 1 >= end) {
				*info = isc_info_truncated;
				return NULL;
			}
			*info++ = isc_info_sql_describe_end;
		} // if()
	} // for()

	return info;
}
