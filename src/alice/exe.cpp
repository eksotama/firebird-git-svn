//____________________________________________________________
//
//		PROGRAM:	Alice (All Else) Utility
//		MODULE:		exe.cpp
//		DESCRIPTION:	Does the database calls
//
//  The contents of this file are subject to the Interbase Public
//  License Version 1.0 (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy
//  of the License at http://www.Inprise.com/IPL.html
//
//  Software distributed under the License is distributed on an
//  "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express
//  or implied. See the License for the specific language governing
//  rights and limitations under the License.
//
//  The Original Code was created by Inprise Corporation
//  and its predecessors. Portions created by Inprise Corporation are
//  Copyright (C) Inprise Corporation.
//
//  All Rights Reserved.
//  Contributor(s): ______________________________________.
//
//
//____________________________________________________________
//
//	$Id: exe.cpp,v 1.20 2003-10-16 08:50:54 robocop Exp $
//
// 2001.07.06 Sean Leyne - Code Cleanup, removed "#ifdef READONLY_DATABASE"
//                         conditionals, as the engine now fully supports
//                         readonly databases.
//
// 2002.10.30 Sean Leyne - Removed obsolete "PC_PLATFORM" define
//

#include "firebird.h"
#include "../jrd/ib_stdio.h"
#include <stdlib.h>
#include <string.h>
#include "../jrd/gds.h"
#include "../jrd/common.h"
#include "../jrd/ibsetjmp.h"
#include "../alice/alice.h"
#include "../alice/alice_proto.h"
#include "../alice/aliceswi.h"
#include "../alice/all.h"
#include "../alice/all_proto.h"
#include "../alice/alice_meta.h"
#include "../alice/tdr_proto.h"
#include "../jrd/gds_proto.h"
#include "../jrd/thd_proto.h"


static USHORT build_dpb(UCHAR *, ULONG);
static void extract_db_info(UCHAR *);

static TEXT val_errors[] =
{
	isc_info_page_errors, isc_info_record_errors, isc_info_bpage_errors,
	isc_info_dpage_errors, isc_info_ipage_errors, isc_info_ppage_errors,
	isc_info_tpage_errors, gds_info_end
};

static inline void stuff_dpb(UCHAR **d, int blr)
{
	UCHAR *ptr = *d;
	*ptr++ = (UCHAR)blr;
	*d = ptr;
}

static inline void stuff_dpb_long(UCHAR **d, int blr)
{
	stuff_dpb(d, blr);
	stuff_dpb(d, blr >> 8);
	stuff_dpb(d, blr >> 16);
	stuff_dpb(d, blr >> 24);
}



//____________________________________________________________
//
//

int EXE_action(TEXT * database, ULONG switches)
{
	UCHAR dpb[128];
	TGBL tdgbl = GET_THREAD_DATA;

	ALLA_init();

	for (USHORT i = 0; i < MAX_VAL_ERRORS; i++)
		tdgbl->ALICE_data.ua_val_errors[i] = 0;

//  generate the database parameter block for the attach,
//  based on the various switches

	const USHORT dpb_length = build_dpb(dpb, switches);

	bool error = false;
	FRBRD* handle = NULL;
	gds__attach_database(tdgbl->status, 0, database, &handle, dpb_length,
						 reinterpret_cast<SCHAR*>(dpb));

	SVC_STARTED(tdgbl->service_blk);

	if (tdgbl->status[1])
		error = true;

	if (tdgbl->status[2] == isc_arg_warning)
		ALICE_print_status(tdgbl->status);

	if (handle != NULL) {
		UCHAR error_string[128];
		if ((switches & sw_validate) && (tdgbl->status[1] != isc_bug_check)) {
			gds__database_info(tdgbl->status, &handle, sizeof(val_errors),
							   val_errors, sizeof(error_string),
							   reinterpret_cast<char*>(error_string));

			extract_db_info(error_string);
		}

		if (switches & sw_disable)
			MET_disable_wal(tdgbl->status, handle);

		gds__detach_database(tdgbl->status, &handle);
	}

	ALLA_fini();

	return ((error) ? FINI_ERROR : FINI_OK);
}


//____________________________________________________________
//
//

int EXE_two_phase(TEXT* database, ULONG switches)
{
	UCHAR dpb[128];
	TGBL tdgbl = GET_THREAD_DATA;

	ALLA_init();

	for (USHORT i = 0; i < MAX_VAL_ERRORS; i++)
		tdgbl->ALICE_data.ua_val_errors[i] = 0;

//  generate the database parameter block for the attach,
//  based on the various switches

	const USHORT dpb_length = build_dpb(dpb, switches);

	bool error = false;
	FRBRD* handle = NULL;
	gds__attach_database(tdgbl->status, 0, database, &handle,
						 dpb_length,  reinterpret_cast<char*>(dpb));

	SVC_STARTED(tdgbl->service_blk);

	if (tdgbl->status[1])
		error = true;
	else if (switches & sw_list)
		TDR_list_limbo((handle), database, switches);
	else if (switches & (sw_commit | sw_rollback | sw_two_phase))
		error = TDR_reconnect_multiple((handle),
									   tdgbl->ALICE_data.ua_transaction, database,
									   switches);

	if (handle)
		gds__detach_database(tdgbl->status, &handle);

	ALLA_fini();

	return ((error) ? FINI_ERROR : FINI_OK);
}

//____________________________________________________________
//
//
//  generate the database parameter block for the attach,
//  based on the various switches
//

static USHORT build_dpb(UCHAR* dpb, ULONG switches)
{
	TGBL tdgbl = GET_THREAD_DATA;

	UCHAR* dpb2 = dpb;
	*dpb2++ = gds_dpb_version1;
	*dpb2++ = isc_dpb_gfix_attach;
	*dpb2++ = 0;

	if (switches & sw_sweep) {
		*dpb2++ = gds_dpb_sweep;
		*dpb2++ = 1;
		*dpb2++ = gds_dpb_records;
	}
	else if (switches & sw_activate) {
		*dpb2++ = gds_dpb_activate_shadow;
		*dpb2++ = 0;
	}
	else if (switches & sw_validate) {
		*dpb2++ = gds_dpb_verify;
		*dpb2++ = 1;
		*dpb2 = gds_dpb_pages;
		if (switches & sw_full)
			*dpb2 |= gds_dpb_records;
		if (switches & sw_no_update)
			*dpb2 |= gds_dpb_no_update;
		if (switches & sw_mend)
			*dpb2 |= gds_dpb_repair;
		if (switches & sw_ignore)
			*dpb2 |= gds_dpb_ignore;
		dpb2++;
	}
	else if (switches & sw_housekeeping) {
		*dpb2++ = gds_dpb_sweep_interval;
		*dpb2++ = 4;
		for (int i = 0; i < 4; i++, (tdgbl->ALICE_data.ua_sweep_interval >>= 8))
		{
			// TMN: Here we should really have the following assert 
			// assert(tdgbl->ALICE_data.ua_sweep_interval <= MAX_UCHAR);
			*dpb2++ = (UCHAR) tdgbl->ALICE_data.ua_sweep_interval;
		}
	}
	else if (switches & sw_begin_log) {
		*dpb2++ = gds_dpb_begin_log;
		*dpb2++ = strlen(tdgbl->ALICE_data.ua_log_file);
		for (const char* q = tdgbl->ALICE_data.ua_log_file; *q;)
			*dpb2++ = *q++;
	}
	else if (switches & sw_buffers) {
		*dpb2++ = isc_dpb_set_page_buffers;
		*dpb2++ = 4;
		for (int i = 0; i < 4; i++, (tdgbl->ALICE_data.ua_page_buffers >>= 8))
		{
			// TMN: Here we should really have the following assert 
			// assert(tdgbl->ALICE_data.ua_page_buffers <= MAX_UCHAR);
			*dpb2++ = (UCHAR) tdgbl->ALICE_data.ua_page_buffers;
		}
	}
	else if (switches & sw_quit_log) {
		*dpb2++ = gds_dpb_quit_log;
		*dpb2++ = 0;
	}
	else if (switches & sw_kill) {
		*dpb2++ = gds_dpb_delete_shadow;
		*dpb2++ = 0;
	}
	else if (switches & sw_write) {
		*dpb2++ = gds_dpb_force_write;
		*dpb2++ = 1;
		*dpb2++ = tdgbl->ALICE_data.ua_force ? 1 : 0;
	}
	else if (switches & sw_use) {
		*dpb2++ = gds_dpb_no_reserve;
		*dpb2++ = 1;
		*dpb2++ = tdgbl->ALICE_data.ua_use ? 1 : 0;
	}

	else if (switches & sw_mode) {
		*dpb2++ = isc_dpb_set_db_readonly;
		*dpb2++ = 1;
		*dpb2++ = (tdgbl->ALICE_data.ua_read_only) ? 1 : 0;
	}
	else if (switches & sw_shut) {
		*dpb2++ = gds_dpb_shutdown;
		*dpb2++ = 1;
		*dpb2 = 0;
		if (switches & sw_attach)
			*dpb2 |= gds_dpb_shut_attachment;
		else if (switches & sw_cache)
			*dpb2 |= gds_dpb_shut_cache;
		else if (switches & sw_force)
			*dpb2 |= gds_dpb_shut_force;
		else if (switches & sw_tran)
			*dpb2 |= gds_dpb_shut_transaction;
		dpb2++;
		*dpb2++ = gds_dpb_shutdown_delay;
		*dpb2++ = 2;				// Build room for shutdown delay 
		// TMN: Here we should really have the following assert 
		// assert(tdgbl->ALICE_data.ua_page_buffers <= MAX_USHORT);
		// or maybe even compare with MAX_SSHORT 
		*dpb2++ = (UCHAR) tdgbl->ALICE_data.ua_shutdown_delay;
		*dpb2++ = (UCHAR) (tdgbl->ALICE_data.ua_shutdown_delay >> 8);
	}
	else if (switches & sw_online) {
		*dpb2++ = gds_dpb_online;
		*dpb2++ = 0;
	}
	else if (switches & sw_disable) {
		*dpb2++ = isc_dpb_disable_wal;
		*dpb2++ = 0;
	}
	else if (switches & (sw_list | sw_commit | sw_rollback | sw_two_phase)) {
		*dpb2++ = gds_dpb_no_garbage_collect;
		*dpb2++ = 0;
	}
	else if (switches & sw_set_db_dialect) {
		stuff_dpb(&dpb2, isc_dpb_set_db_sql_dialect);
		stuff_dpb(&dpb2, 4);
		stuff_dpb_long(&dpb2, tdgbl->ALICE_data.ua_db_SQL_dialect);
	}

	if (tdgbl->ALICE_data.ua_user) {
		*dpb2++ = gds_dpb_user_name;
		*dpb2++ = strlen(reinterpret_cast<const char*>(tdgbl->ALICE_data.ua_user));
		for (const UCHAR* q = tdgbl->ALICE_data.ua_user; *q;)
			*dpb2++ = *q++;
	}

	if (tdgbl->ALICE_data.ua_password) {
		if (!tdgbl->sw_service)
			*dpb2++ = gds_dpb_password;
		else
			*dpb2++ = gds_dpb_password_enc;
		*dpb2++ = strlen(reinterpret_cast<const char*>(tdgbl->ALICE_data.ua_password));
		for (const UCHAR* q = tdgbl->ALICE_data.ua_password; *q;)
			*dpb2++ = *q++;
	}

	USHORT dpb_length = dpb2 - dpb;
	if (dpb_length == 1)
		dpb_length = 0;

	return dpb_length;
}


//____________________________________________________________
//
//		Extract database info from string
//

static void extract_db_info(UCHAR* db_info_buffer)
{
	UCHAR item;
	TGBL tdgbl = GET_THREAD_DATA;

	UCHAR* p = db_info_buffer;

	while ((item = *p++) != gds_info_end) {
		const SLONG length = gds__vax_integer(p, 2);
		p += 2;

		// TMN: Here we should really have the following assert 
		// assert(length <= MAX_SSHORT);
		// for all cases that use 'length' as input to 'gds__vax_integer' 
		switch (item) {
		case isc_info_page_errors:
			tdgbl->ALICE_data.ua_val_errors[VAL_PAGE_ERRORS] =
				gds__vax_integer(p, (SSHORT) length);
			p += length;
			break;

		case isc_info_record_errors:
			tdgbl->ALICE_data.ua_val_errors[VAL_RECORD_ERRORS] =
				gds__vax_integer(p, (SSHORT) length);
			p += length;
			break;

		case isc_info_bpage_errors:
			tdgbl->ALICE_data.ua_val_errors[VAL_BLOB_PAGE_ERRORS] =
				gds__vax_integer(p, (SSHORT) length);
			p += length;
			break;

		case isc_info_dpage_errors:
			tdgbl->ALICE_data.ua_val_errors[VAL_DATA_PAGE_ERRORS] =
				gds__vax_integer(p, (SSHORT) length);
			p += length;
			break;

		case isc_info_ipage_errors:
			tdgbl->ALICE_data.ua_val_errors[VAL_INDEX_PAGE_ERRORS] =
				gds__vax_integer(p, (SSHORT) length);
			p += length;
			break;

		case isc_info_ppage_errors:
			tdgbl->ALICE_data.ua_val_errors[VAL_POINTER_PAGE_ERRORS] =
				gds__vax_integer(p, (SSHORT) length);
			p += length;
			break;

		case isc_info_tpage_errors:
			tdgbl->ALICE_data.ua_val_errors[VAL_TIP_PAGE_ERRORS] =
				gds__vax_integer(p, (SSHORT) length);
			p += length;
			break;

		case isc_info_error:
			/* has to be a < V4 database. */

			tdgbl->ALICE_data.ua_val_errors[VAL_INVALID_DB_VERSION] = 1;
			return;

		default:
			;
		}
	}
}

