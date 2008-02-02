//____________________________________________________________
//
//		PROGRAM:	Alice (All Else) Utility
//		MODULE:		alice.cpp
//		DESCRIPTION:	Neo-Debe (does everything but eat)
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
// 2001.07.06 Sean Leyne - Code Cleanup, removed "#ifdef READONLY_DATABASE"
//                         conditionals, as the engine now fully supports
//                         readonly databases.
//
// 2002.10.29 Sean Leyne - Removed support for obsolete "Netware" port
//
// 2002.10.30 Sean Leyne - Removed support for obsolete "PC_PLATFORM" define
//
//

#include "firebird.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#include "../jrd/ibase.h"
#include "../jrd/common.h"
#include "../jrd/license.h"
#include "../alice/alice.h"
#include "../alice/aliceswi.h"
#include "../alice/exe_proto.h"
#include "../jrd/msg_encode.h"
#include "../jrd/gds_proto.h"
#include "../jrd/svc.h"
#include "../alice/alice_proto.h"
#include "../common/utils_proto.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef WIN_NT
#include <io.h>
#endif

#include "../utilities/common/cmd_util_proto.h"

using MsgFormat::SafeArg;


static const USHORT val_err_table[] = {
	0,
	55,				// msg 55: \n\tNumber of record level errors\t: %ld
	56,				// msg 56: \tNumber of Blob page errors\t: %ld
	57,				// msg 57: \tNumber of data page errors\t: %ld
	58,				// msg 58: \tNumber of index page errors\t: %ld
	59,				// msg 59: \tNumber of pointer page errors\t: %ld
	60,				// msg 60: \tNumber of transaction page errors\t: %ld
	61				// msg 61: \tNumber of database page errors\t: %ld
};


const int ALICE_MSG_FAC = 3;

void ALICE_exit(int code, AliceGlobals* tdgbl)
{
	tdgbl->exit_code = code;
    Firebird::LongJump::raise();
}

static void expand_filename(const TEXT*, TEXT*);
static void alice_output(const SCHAR*, ...) ATTRIBUTE_FORMAT(1,2);



//____________________________________________________________
//
//	Entry point for GFIX in case of service manager.
//

THREAD_ENTRY_DECLARE ALICE_main(THREAD_ENTRY_PARAM arg)
{
	Firebird::UtilSvc* uSvc = (Firebird::UtilSvc*) arg;
	const int exit_code = alice(uSvc);

	uSvc->finish();

	return (THREAD_ENTRY_RETURN)(IPTR) exit_code;
}

//____________________________________________________________
//
//		Routine called by command line utility, and server manager
//		Parse switches and do work
//

int alice(Firebird::UtilSvc* uSvc)
{
	AliceGlobals gblInstance(uSvc);
	AliceGlobals* tdgbl = &gblInstance;
	AliceGlobals::putSpecific(tdgbl);
	int exit_code = FINI_ERROR;

	try {

//  Perform some special handling when run as a Firebird service.  The
//  first switch can be "-svc" (lower case!) or it can be "-svc_re" followed
//  by 3 file descriptors to use in re-directing stdin, stdout, and stderr.

	tdgbl->ALICE_data.ua_user = NULL;
	tdgbl->ALICE_data.ua_password = NULL;
#ifdef TRUSTED_AUTH
	tdgbl->ALICE_data.ua_trusted = false;
#endif
	tdgbl->ALICE_data.ua_tr_user = NULL;
	tdgbl->ALICE_data.ua_tr_role = false;
	if (uSvc->isService())
	{
		tdgbl->status = uSvc->getStatus();
	}

//  Start by parsing switches

	bool error = false;
	ULONG switches = 0;
	tdgbl->ALICE_data.ua_shutdown_delay = 0;
	const TEXT* database = NULL;
	TEXT	string[512];

	const char** argv = uSvc->argv.begin();
	int argc = uSvc->argv.getCount();
	++argv;
	
	// tested outside the loop
	const in_sw_tab_t* table = alice_in_sw_table;

	while (--argc > 0)
	{
		if ((*argv)[0] != '-')
		{
			if (database) 
			{
				ALICE_error(1, SafeArg() << database);
				// msg 1: "data base file name (%s) already given",
			}
			database = *argv++;

#if defined (WIN95)
//  There is a small problem with SuperServer on NT, since it is a
//  Windows app, it uses the ANSI character set.  All the console
//  apps use the OEM character set.  We need to pass the database
//  name in the correct character set.
// if (GetConsoleCP != GetACP())
//    OemToAnsi(database, database);
//
#else
#endif
			continue;
		}
		ALICE_down_case(*argv++, string, sizeof(string));
		if (!string[1]) {
			continue;
		}
		for (table = alice_in_sw_table; true; ++table)
		{
			const TEXT* p = (TEXT*) table->in_sw_name;
			if (!p) {
				ALICE_print(2, SafeArg() << (*--argv));	// msg 2: invalid switch %s
				error = true;
				break;
			}

			TEXT* q = &string[1];
			while (*q && *p++ == *q) {
				q++;
			}
			if (!*q) {
				break;
			}
		}
		if (error) {
			break;
		}
		if (*table->in_sw_name == 'x') {
			tdgbl->ALICE_data.ua_debug++;
		}
		if (strcmp(table->in_sw_name, "trusted_svc") == 0) {
			uSvc->checkService();
			if (--argc <= 0) {
				ALICE_error(13);	// msg 13: user name required
			}
			tdgbl->ALICE_data.ua_tr_user = *argv++;
			continue;
		}
		if (strcmp(table->in_sw_name, "trusted_role") == 0) {
			uSvc->checkService();
			tdgbl->ALICE_data.ua_tr_role = true;
			continue;
		}
#ifdef TRUSTED_AUTH
		if (strcmp(table->in_sw_name, "trusted") == 0) {
			tdgbl->ALICE_data.ua_trusted = true;
			continue;
		}
#endif
		if (table->in_sw_value == sw_z) {
			ALICE_print(3, SafeArg() << GDS_VERSION);	// msg 3: gfix version %s
		}
		if ((table->in_sw_incompatibilities & switches) ||
			(table->in_sw_requires && !(table->in_sw_requires & switches)))
		{
			ALICE_print(4);	// msg 4: incompatible switch combination
			error = true;
			break;
		}
		switches |= table->in_sw_value;

		if ((table->in_sw_value & (sw_shut | sw_online)) && (argc > 1))
		{
			ALICE_down_case(*argv, string, sizeof(string));
			bool found = true;
			if (strcmp(string, "normal") == 0)
				tdgbl->ALICE_data.ua_shutdown_mode = SHUT_NORMAL;
			else if (strcmp(string, "multi") == 0)
				tdgbl->ALICE_data.ua_shutdown_mode = SHUT_MULTI;
			else if (strcmp(string, "single") == 0)
				tdgbl->ALICE_data.ua_shutdown_mode = SHUT_SINGLE;
			else if (strcmp(string, "full") == 0)
				tdgbl->ALICE_data.ua_shutdown_mode = SHUT_FULL;
			else
				found = false;
			// Consume argument only if we identified mode
			// Let's hope that database with names of modes above are unusual
			if (found) {
				argv++;
				argc--;
			}
		}

		if (table->in_sw_value & sw_begin_log) {
			if (--argc <= 0) {
				ALICE_error(5);	// msg 5: replay log pathname required
			}
			expand_filename(*argv++, tdgbl->ALICE_data.ua_log_file);
		}

		if (table->in_sw_value & (sw_buffers)) {
			if (--argc <= 0) {
				ALICE_error(6);	// msg 6: number of page buffers for cache required
			}
			ALICE_down_case(*argv++, string, sizeof(string));
			if ((!(tdgbl->ALICE_data.ua_page_buffers = atoi(string)))
				&& (strcmp(string, "0")))
			{
				ALICE_error(7);	// msg 7: numeric value required
			}
			if (tdgbl->ALICE_data.ua_page_buffers < 0) {
				ALICE_error(114);	// msg 114: positive or zero numeric value required
			}
		}

		if (table->in_sw_value & (sw_housekeeping)) {
			if (--argc <= 0) {
				ALICE_error(9);	// msg 9: number of transactions per sweep required
			}
			ALICE_down_case(*argv++, string, sizeof(string));
			if ((!(tdgbl->ALICE_data.ua_sweep_interval = atoi(string)))
				&& (strcmp(string, "0")))
			{
				ALICE_error(7);	// msg 7: numeric value required
			}
			if (tdgbl->ALICE_data.ua_sweep_interval < 0) {
				ALICE_error(114);	// msg 114: positive or zero numeric value required
			}
		}

		if (table->in_sw_value & (sw_set_db_dialect)) {
			if (--argc <= 0) {
				ALICE_error(113);	// msg 113: dialect number required
			}

			ALICE_down_case(*argv++, string, sizeof(string));

			if ((!(tdgbl->ALICE_data.ua_db_SQL_dialect = atoi(string))) &&
				(strcmp(string, "0")))
			{
				ALICE_error(7);	// msg 7: numeric value required
			}

			// JMB: Removed because tdgbl->ALICE_data.ua_db_SQL_dialect is
			//		an unsigned number.  Therefore this check is useless.
			// if (tdgbl->ALICE_data.ua_db_SQL_dialect < 0)
			// {
			//	ALICE_error(114);	/* msg 114: positive or zero numeric value
			//									   required */
			// }
		}

		if (table->in_sw_value & (sw_commit | sw_rollback | sw_two_phase)) {
			if (--argc <= 0) {
				ALICE_error(10);	// msg 10: transaction number or "all" required
			}
			ALICE_down_case(*argv++, string, sizeof(string));
			if (!(tdgbl->ALICE_data.ua_transaction = atoi(string))) {
				if (strcmp(string, "all")) {
					ALICE_error(10);	// msg 10: transaction number or "all" required
				}
				else {
					switches |= sw_list;
				}
			}
		}

		if (table->in_sw_value & sw_write) {
			if (--argc <= 0) {
				ALICE_error(11);	// msg 11: "sync" or "async" required
			}
			ALICE_down_case(*argv++, string, sizeof(string));
			if (!strcmp(string, ALICE_SW_SYNC)) {
				tdgbl->ALICE_data.ua_force = true;
			}
			else if (!strcmp(string, ALICE_SW_ASYNC)) {
				tdgbl->ALICE_data.ua_force = false;
			}
			else {
				ALICE_error(11);	// msg 11: "sync" or "async" required
			}
		}

		if (table->in_sw_value & sw_use) {
			if (--argc <= 0) {
				ALICE_error(12);	// msg 12: "full" or "reserve" required
			}
			ALICE_down_case(*argv++, string, sizeof(string));
			if (!strcmp(string, "full")) {
				tdgbl->ALICE_data.ua_use = true;
			}
			else if (!strcmp(string, "reserve")) {
				tdgbl->ALICE_data.ua_use = false;
			}
			else {
				ALICE_error(12);	// msg 12: "full" or "reserve" required
			}
		}

		if (table->in_sw_value & sw_user) {
			if (--argc <= 0) {
				ALICE_error(13);	// msg 13: user name required
			}
			tdgbl->ALICE_data.ua_user = *argv++;
		}

		if (table->in_sw_value & sw_password) {
			if (--argc <= 0) {
				ALICE_error(14);	// msg 14: password required
			}
			uSvc->hidePasswd(uSvc->argv, argv - uSvc->argv.begin());
			tdgbl->ALICE_data.ua_password = *argv++;
		}

		if (table->in_sw_value & sw_disable) {
			if (--argc <= 0) {
				ALICE_error(15);	// msg 15: subsystem name
			}
			ALICE_down_case(*argv++, string, sizeof(string));
			if (strcmp(string, "wal")) {
				ALICE_error(16);	// msg 16: "wal" required
			}
		}

		if (table->in_sw_value & (sw_attach | sw_force | sw_tran | sw_cache)) {
			if (--argc <= 0) {
				ALICE_error(17);	// msg 17: number of seconds required
			}
			ALICE_down_case(*argv++, string, sizeof(string));
			if ((!(tdgbl->ALICE_data.ua_shutdown_delay = atoi(string)))
				&& (strcmp(string, "0")))
			{
				ALICE_error(7);	// msg 7: numeric value required
			}
			if (tdgbl->ALICE_data.ua_shutdown_delay < 0
				|| tdgbl->ALICE_data.ua_shutdown_delay > 32767)
			{
				ALICE_error(18);	// msg 18: numeric value between 0 and 32767 inclusive required
			}
		}

		if (table->in_sw_value & sw_mode) {
			if (--argc <= 0) {
				ALICE_error(110);	// msg 110: "read_only" or "read_write" required
			}
			ALICE_down_case(*argv++, string, sizeof(string));
			if (!strcmp(string, ALICE_SW_MODE_RO)) {
				tdgbl->ALICE_data.ua_read_only = true;
			}
			else if (!strcmp(string, ALICE_SW_MODE_RW)) {
				tdgbl->ALICE_data.ua_read_only = false;
			}
			else {
				ALICE_error(110);	// msg 110: "read_only" or "read_write" required
			}
		}

	}

//  put this here since to put it above overly complicates the parsing
//  can't use tbl_requires since it only looks backwards on command line
	if ((switches & sw_shut)
		&& !(switches & ((sw_attach | sw_force | sw_tran | sw_cache))))
	{
		ALICE_error(19);	// msg 19: must specify type of shutdown
	}

//  catch the case where -z is only command line option
//  switches is unset since sw_z == 0
	if (!switches && !error && table->in_sw_value == sw_z) {
		ALICE_exit(FINI_OK, tdgbl);
	}

	if (!switches || !(switches & ~(sw_user | sw_password))) 
	{
		if (!uSvc->isService())
		{
			ALICE_print(20);	// msg 20: please retry, specifying an option
		}
		error = true;
	}

	if (error) 
	{
		if (uSvc->isService())
		{
			uSvc->stuffStatus(ALICE_MSG_FAC, 20, MsgFormat::SafeArg());
			uSvc->started();
		}
		else
		{
			ALICE_print(21);	// msg 21: plausible options are:\n
			for (table = alice_in_sw_table; table->in_sw_msg; table++)
			{
				if (table->in_sw_msg)
					ALICE_print(table->in_sw_msg);
			}
			ALICE_print(22);	// msg 22: \n    qualifiers show the major option in parenthesis
		}
		ALICE_exit(FINI_ERROR, tdgbl);
	}

	if (!database) {
		ALICE_error(23);	// msg 23: please retry, giving a database name
	}

	//  generate the database parameter block for the attach,
	//  based on the various switches

	USHORT ret;

	if (switches & (sw_list | sw_commit | sw_rollback | sw_two_phase))
	{
		ret = EXE_two_phase(database, switches);
	}
	else
	{
		ret = EXE_action(database, switches);

		const SLONG* ua_val_errors = tdgbl->ALICE_data.ua_val_errors;

		if (!ua_val_errors[VAL_INVALID_DB_VERSION])
		{
			bool any_error = false;

			for (int i = 0; i < MAX_VAL_ERRORS; ++i) {
				if (ua_val_errors[i]) {
					any_error = true;
					break;
				}
			}

			if (any_error) {
				ALICE_print(24);	// msg 24: Summary of validation errors\n

				for (int i = 0; i < MAX_VAL_ERRORS; ++i) {
					if (ua_val_errors[i]) {
						ALICE_print(val_err_table[i], SafeArg() << ua_val_errors[i]);
					}
				}
			}
		}
	}

	if (ret == FINI_ERROR) {
		ALICE_print_status(tdgbl->status);
		ALICE_exit(FINI_ERROR, tdgbl);
	}

	ALICE_exit(FINI_OK, tdgbl);

	}	// try

	catch (const Firebird::LongJump&)
	{
		// All "calls" to ALICE_exit(), normal and error exits, wind up here
		exit_code = tdgbl->exit_code;
	}

	catch (const Firebird::Exception& e)
	{
		// Non-alice exception was caught
		e.stuff_exception(tdgbl->status_vector);
		ALICE_print_status(tdgbl->status_vector);
	}

	tdgbl->uSvc->started();

	AliceGlobals::restoreSpecific();

#if defined(DEBUG_GDS_ALLOC)
	if (!uSvc->isService())
	{
		gds_alloc_report(0, __FILE__, __LINE__);
	}
#endif

	return exit_code;
}


//____________________________________________________________
//
//		Copy a string, down casing as we go.
//

void ALICE_down_case(const TEXT* in, TEXT* out, const size_t buf_size)
{
	const TEXT* const end = out + buf_size - 1;
	for (TEXT c = *in++; c && out < end; c = *in++) {
		*out++ = (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
	}
	*out = 0;
}


//____________________________________________________________
//
//		Display a formatted error message
//

void ALICE_print(USHORT	number,
				 const SafeArg& arg)
{
	TEXT buffer[256];

	fb_msg_format(0, ALICE_MSG_FAC, number, sizeof(buffer), buffer, arg);
	alice_output("%s\n", buffer);
}


//____________________________________________________________
//
//		Print error message. Use fb_interpret
//		to allow redirecting output.
//

void ALICE_print_status(const ISC_STATUS* status_vector)
{
	if (status_vector)
	{
		const ISC_STATUS* vector = status_vector;
		AliceGlobals* tdgbl = AliceGlobals::getSpecific();
		tdgbl->uSvc->stuffStatus(status_vector);

		SCHAR s[1024];
		if (fb_interpret(s, sizeof(s), &vector))
		{
			alice_output("%s\n", s);

			// Continuation of error
			s[0] = '-';
			while (fb_interpret(s + 1, sizeof(s) - 1, &vector)) {
				alice_output("%s\n", s);
			}
		}
	}
}


//____________________________________________________________
//
//		Format and print an error message, then punt.
//

void ALICE_error(USHORT	number,
				 const SafeArg& arg)
{
	AliceGlobals* tdgbl = AliceGlobals::getSpecific();
	TEXT buffer[256];

	tdgbl->uSvc->stuffStatus(ALICE_MSG_FAC, number, arg);

	fb_msg_format(0, ALICE_MSG_FAC, number, sizeof(buffer), buffer, arg);
	alice_output("%s\n", buffer);
	ALICE_exit(FINI_ERROR, tdgbl);
}


//____________________________________________________________
//
//		Platform independent output routine.
//

static void alice_output(const SCHAR* format, ...)
{

	AliceGlobals* tdgbl = AliceGlobals::getSpecific();

	va_list arglist;
	va_start(arglist, format);
	Firebird::string buf;
	buf.vprintf(format, arglist);
	va_end(arglist);

	tdgbl->uSvc->output(buf.c_str());
}


//____________________________________________________________
//
//		Fully expand a file name.  If the file doesn't exist, do something
//		intelligent.
//      CVC: The above comment is either a joke or a copy/paste.

static void expand_filename(const TEXT* filename, TEXT* expanded_name)
{
	strcpy(expanded_name, filename);
}

