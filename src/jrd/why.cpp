/*
 *	PROGRAM:	JRD Access Method
 *	MODULE:		why.cpp
 *	DESCRIPTION:	Universal Y-valve
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
 *
 * 23-Feb-2002 Dmitry Yemanov - Events wildcarding
 *
 *
 * 2002-02-23 Sean Leyne - Code Cleanup, removed old Win3.1 port (Windows_Only)
 * 2002-02-23 Sean Leyne - Code Cleanup, removed old M88K and NCR3000 port
 *
 * 2002.10.27 Sean Leyne - Completed removal of obsolete "IMP" port
 * 2002.10.27 Sean Leyne - Completed removal of obsolete "DG_X86" port
 *
 * 2002.10.28 Sean Leyne - Code cleanup, removed obsolete "MPEXL" port
 * 2002.10.28 Sean Leyne - Code cleanup, removed obsolete "DecOSF" port
 * 2002.10.28 Sean Leyne - Code cleanup, removed obsolete "SGI" port
 *
 * 2002.10.29 Sean Leyne - Removed obsolete "Netware" port
 *
 * 2002.10.30 Sean Leyne - Removed support for obsolete "PC_PLATFORM" define
 *
 * 2002.12.10 Alex Peshkoff: 1. Moved struct hndl declaration to h-file
 * 							 2. renamed all specific objects to WHY_*
 *
 */
/*
$Id: why.cpp,v 1.18 2003-03-01 17:59:41 brodsom Exp $
*/

#include "firebird.h"

#include <stdlib.h>
#include <string.h>
#include "../jrd/common.h"
#include <stdarg.h>

#include "../jrd/ib_stdio.h"
#include <assert.h>

#include "../jrd/y_handle.h"
#include "gen/codes.h"
#include "../jrd/msg_encode.h"
#include "gen/msg_facs.h"
#include "../jrd/acl.h"
#include "../jrd/inf.h"
#include "../jrd/thd.h"
#include "../jrd/isc.h"
#include "../jrd/fil.h"

/* includes specific for DSQL */

#include "../dsql/sqlda.h"

/* end DSQL-specific includes */

#include "../jrd/flu_proto.h"
#include "../jrd/gds_proto.h"
#include "../jrd/isc_proto.h"
#include "../jrd/isc_f_proto.h"
#ifndef REQUESTER
#include "../jrd/isc_i_proto.h"
#include "../jrd/isc_s_proto.h"
#include "../jrd/sch_proto.h"
#endif
#include "../jrd/thd_proto.h"
#include "../jrd/utl_proto.h"
#include "../dsql/dsql_proto.h"
#include "../dsql/prepa_proto.h"
#include "../dsql/utld_proto.h"
#include "../jrd/why_proto.h"

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_FLOCK
#include <sys/file.h>			/* for flock() */
#endif

#ifdef WIN_NT
#include <windows.h>
#ifdef TEXT
#undef TEXT
#endif
#define TEXT		SCHAR
#endif

#ifndef F_OK
#define F_OK		0
#endif

#ifndef F_TLOCK
#define F_TLOCK		2
#endif

#ifdef SHLIB_DEFS
#define lockf		(*_libgds_lockf)
#define _assert		(*_libgds__assert)
#define strchr		(*_libgds_strchr)
#define access		(*_libgds_access)

extern int lockf();
extern void _assert();
extern SCHAR *strchr();
extern int access();
#endif

#define IO_RETRY	20

#ifdef DEV_BUILD
#define CHECK_STATUS(v)		check_status_vector(v, !FB_SUCCESS)
#define CHECK_STATUS_SUCCESS(v)	check_status_vector(v, FB_SUCCESS)
#endif

#ifndef CHECK_STATUS
#define CHECK_STATUS(v)			/* nothing */
#define CHECK_STATUS_SUCCESS(v)	/* nothing */
#endif

#define INIT_STATUS(vector)		vector [0] = gds_arg_gds;\
					vector [1] = FB_SUCCESS;\
					vector [2] = gds_arg_end

#define IS_NETWORK_ERROR(vector) \
	(vector[1] == isc_network_error || \
	 vector[1] == isc_net_write_err || \
	 vector[1] == isc_net_read_err)

#define CHECK_HANDLE(blk, blk_type, code) if (!(blk) || (blk)->type != blk_type) \
	return bad_handle (user_status, code)

#define NULL_CHECK(ptr, code, type)	if (*ptr) return bad_handle (user_status, code)
#define GET_STATUS			{ if (!(status = user_status)) status = local; INIT_STATUS(status); }
#define RETURN_SUCCESS			{ subsystem_exit(); CHECK_STATUS_SUCCESS (status); return FB_SUCCESS; }

#ifdef REQUESTER
#define NO_LOCAL_DSQL
#endif

#ifdef SUPERCLIENT
#define NO_LOCAL_DSQL
#endif

#if defined (SERVER_SHUTDOWN) && !defined (SUPERCLIENT) && !defined (REQUESTER)
static BOOLEAN shutdown_flag = FALSE;
#endif /* SERVER_SHUTDOWN && !SUPERCLIENT && !REQUESTER */

typedef STATUS(*PTR) (STATUS * user_status, ...);

/* Database cleanup handlers */

typedef struct clean
{
	struct clean*	clean_next;
	union {
		DatabaseCleanupRoutine*	DatabaseRoutine;
		TransactionCleanupRoutine *TransactionRoutine;
	};
	SLONG			clean_arg;
} *CLEAN;

/* Transaction element block */

typedef struct teb
{
	WHY_ATT *teb_database;
	int teb_tpb_length;
	UCHAR *teb_tpb;
} TEB;

static WHY_HNDL allocate_handle(int, int);
inline WHY_HNDL allocate_handle(int implementation, why_hndl *h, int handle_type) {
	WHY_HNDL handle = allocate_handle(implementation, handle_type);
	handle->handle.h_why = h;
	return handle;
}
inline WHY_HNDL allocate_handle(int implementation, dsql_req *h, int handle_type) {
	WHY_HNDL handle = allocate_handle(implementation, handle_type);
	handle->handle.h_dsql = h;
	return handle;
}
inline WHY_HNDL allocate_handle(int implementation, class jrd_tra *h, int handle_type) {
	WHY_HNDL handle = allocate_handle(implementation, handle_type);
	handle->handle.h_tra = h;
	return handle;
}

extern "C" {
static STATUS bad_handle(STATUS *, STATUS);
static SCHAR *alloc(SLONG);

#ifdef DEV_BUILD
static void check_status_vector(STATUS *, STATUS);
#endif
static STATUS error(STATUS *, STATUS *);
static STATUS error2(STATUS *, STATUS *);
static void event_ast(UCHAR *, USHORT, UCHAR *);
static void exit_handler(EVENT);
static WHY_TRA find_transaction(WHY_DBB, WHY_TRA);
static void free_block(void*);
static int get_database_info(STATUS *, WHY_TRA, TEXT **);
static const PTR get_entrypoint(int, int);
static SCHAR *get_sqlda_buffer(SCHAR *, USHORT, XSQLDA *, USHORT, USHORT *);
static STATUS get_transaction_info(STATUS *, WHY_TRA, TEXT **);

static void iterative_sql_info(STATUS *, WHY_STMT *, SSHORT, const SCHAR *, SSHORT,
							   SCHAR *, USHORT, XSQLDA *);
static STATUS no_entrypoint(STATUS * user_status, ...);
static STATUS open_blob(STATUS *, WHY_ATT *, WHY_TRA *, WHY_BLB *, SLONG *, USHORT,
						UCHAR *, SSHORT, SSHORT);
#ifdef UNIX
static STATUS open_marker_file(STATUS *, TEXT *, TEXT *);
#endif
static STATUS prepare(STATUS *, WHY_TRA);
static void release_dsql_support(DASUP);
static void release_handle(WHY_HNDL);
static void save_error_string(STATUS *);
static void subsystem_enter(void);
static void subsystem_exit(void);

#ifndef REQUESTER
static EVENT_T why_event[1];
static SSHORT why_initialized = 0;
#endif
static SLONG why_enabled = 0;

/* subsystem_usage is used to count how many active ATTACHMENTs are 
 * running though the why valve.  For the first active attachment
 * request we reset the InterBase FPE handler.
 * This counter is incremented for each ATTACH DATABASE, ATTACH SERVER,
 * or CREATE DATABASE.  This counter is decremented for each 
 * DETACH DATABASE, DETACH SERVER, or DROP DATABASE.
 *
 * A client-only API call, isc_reset_fpe() also controls the re-setting of
 * the FPE handler.
 *	isc_reset_fpe (0);		(default)
 *		Initialize the FPE handler the first time the gds
 *		library is made active.
 *	isc_reset_fpe (1);
 *		Initialize the FPE handler the NEXT time an API call is
 *		invoked.
 *	isc_reset_fpe (2);
 *		Revert to InterBase pre-V4.1.0 behavior, reset the FPE
 *		handler on every API call.
 */

#define FPE_RESET_INIT_ONLY		0x0	/* Don't reset FPE after init */
#define FPE_RESET_NEXT_API_CALL		0x1	/* Reset FPE on next gds call */
#define FPE_RESET_ALL_API_CALL		0x2	/* Reset FPE on all gds call */

#if !(defined REQUESTER || defined SUPERCLIENT || defined SUPERSERVER)
extern ULONG isc_enter_count;
static ULONG subsystem_usage = 0;
static USHORT subsystem_FPE_reset = FPE_RESET_INIT_ONLY;
#define SUBSYSTEM_USAGE_INCR	subsystem_usage++
#define SUBSYSTEM_USAGE_DECR	subsystem_usage--
#endif

#ifndef SUBSYSTEM_USAGE_INCR
#define SUBSYSTEM_USAGE_INCR	/* nothing */
#define SUBSYSTEM_USAGE_DECR	/* nothing */
#endif

#ifdef UNIX
static TEXT marker_failures[1024];
static TEXT *marker_failures_ptr = marker_failures;
#endif

/* 
 * Global array to store string from the status vector in 
 * save_error_string.
 */

#define MAXERRORSTRINGLENGTH	250
static TEXT glbstr1[MAXERRORSTRINGLENGTH];
static const TEXT glbunknown[10] = "<unknown>";

#ifdef VMS
#define CALL(proc, handle)	(*get_entrypoint(proc, handle))
#else
#define CALL(proc, handle)	(get_entrypoint(proc, handle))
#endif


#define GDS_ATTACH_DATABASE		isc_attach_database
#define GDS_BLOB_INFO			isc_blob_info
#define GDS_CANCEL_BLOB			isc_cancel_blob
#define GDS_CANCEL_EVENTS		isc_cancel_events
#define GDS_CANCEL_OPERATION	gds__cancel_operation
#define GDS_CLOSE_BLOB			isc_close_blob
#define GDS_COMMIT				isc_commit_transaction
#define GDS_COMMIT_RETAINING	isc_commit_retaining
#define GDS_COMPILE				isc_compile_request
#define GDS_COMPILE2			isc_compile_request2
#define GDS_CREATE_BLOB			isc_create_blob
#define GDS_CREATE_BLOB2		isc_create_blob2
#define GDS_CREATE_DATABASE		isc_create_database
#define GDS_DATABASE_INFO		isc_database_info
#define GDS_DDL					isc_ddl
#define GDS_DETACH				isc_detach_database
#define GDS_DROP_DATABASE		isc_drop_database
#define GDS_EVENT_WAIT			gds__event_wait
#define GDS_GET_SEGMENT			isc_get_segment
#define GDS_GET_SLICE			isc_get_slice
#define GDS_OPEN_BLOB			isc_open_blob
#define GDS_OPEN_BLOB2			isc_open_blob2
#define GDS_PREPARE				isc_prepare_transaction
#define GDS_PREPARE2			isc_prepare_transaction2
#define GDS_PUT_SEGMENT			isc_put_segment
#define GDS_PUT_SLICE			isc_put_slice
#define GDS_QUE_EVENTS			isc_que_events
#define GDS_RECONNECT			isc_reconnect_transaction
#define GDS_RECEIVE				isc_receive

#ifdef SCROLLABLE_CURSORS
#define GDS_RECEIVE2			isc_receive2
#endif

#define GDS_RELEASE_REQUEST		isc_release_request
#define GDS_REQUEST_INFO		isc_request_info
#define GDS_ROLLBACK			isc_rollback_transaction
#define GDS_ROLLBACK_RETAINING	isc_rollback_retaining
#define GDS_SEEK_BLOB			isc_seek_blob
#define GDS_SEND				isc_send
#define GDS_START_AND_SEND		isc_start_and_send
#define GDS_START				isc_start_request
#define GDS_START_MULTIPLE		isc_start_multiple
#define GDS_START_TRANSACTION	isc_start_transaction
#define GDS_TRANSACT_REQUEST	isc_transact_request
#define GDS_TRANSACTION_INFO	isc_transaction_info
#define GDS_UNWIND				isc_unwind_request

#define GDS_DSQL_ALLOCATE		isc_dsql_allocate_statement
#define GDS_DSQL_ALLOC			isc_dsql_alloc_statement
#define GDS_DSQL_ALLOC2			isc_dsql_alloc_statement2
#define GDS_DSQL_EXECUTE		isc_dsql_execute
#define GDS_DSQL_EXECUTE2		isc_dsql_execute2
#define GDS_DSQL_EXECUTE_M		isc_dsql_execute_m
#define GDS_DSQL_EXECUTE2_M		isc_dsql_execute2_m
#define GDS_DSQL_EXECUTE_IMMED	isc_dsql_execute_immediate
#define GDS_DSQL_EXECUTE_IMM_M	isc_dsql_execute_immediate_m
#define GDS_DSQL_EXEC_IMMED		isc_dsql_exec_immediate
#define GDS_DSQL_EXEC_IMMED2	isc_dsql_exec_immed2
#define GDS_DSQL_EXEC_IMM_M		isc_dsql_exec_immediate_m
#define GDS_DSQL_EXEC_IMM2_M	isc_dsql_exec_immed2_m
#define GDS_DSQL_EXEC_IMM3_M    isc_dsql_exec_immed3_m
#define GDS_DSQL_FETCH			isc_dsql_fetch
#define GDS_DSQL_FETCH2			isc_dsql_fetch2
#define GDS_DSQL_FETCH_M		isc_dsql_fetch_m
#define GDS_DSQL_FETCH2_M		isc_dsql_fetch2_m
#define GDS_DSQL_FREE			isc_dsql_free_statement
#define GDS_DSQL_INSERT			isc_dsql_insert
#define GDS_DSQL_INSERT_M		isc_dsql_insert_m
#define GDS_DSQL_PREPARE		isc_dsql_prepare
#define GDS_DSQL_PREPARE_M		isc_dsql_prepare_m
#define GDS_DSQL_SET_CURSOR		isc_dsql_set_cursor_name
#define GDS_DSQL_SQL_INFO		isc_dsql_sql_info

#define GDS_SERVICE_ATTACH		isc_service_attach
#define GDS_SERVICE_DETACH		isc_service_detach
#define GDS_SERVICE_QUERY		isc_service_query
#define GDS_SERVICE_START		isc_service_start

/*****************************************************
 *  IMPORTANT IMPORTANT IMPORTANT IMPORTANT IMPORTANT
 *
 *  The order in which these defines appear MUST match
 *  the order in which the entrypoint appears in
 *  source/jrd/entry.h.  Failure to do so will result in
 *  much frustration
 ******************************************************/

#define PROC_ATTACH_DATABASE	0
#define PROC_BLOB_INFO			1
#define PROC_CANCEL_BLOB		2
#define PROC_CLOSE_BLOB			3
#define PROC_COMMIT				4
#define PROC_COMPILE			5
#define PROC_CREATE_BLOB		6
#define PROC_CREATE_DATABASE	7
#define PROC_DATABASE_INFO		8
#define PROC_DETACH				9
#define PROC_GET_SEGMENT		10
#define PROC_OPEN_BLOB			11
#define PROC_PREPARE			12
#define PROC_PUT_SEGMENT		13
#define PROC_RECONNECT			14
#define PROC_RECEIVE			15
#define PROC_RELEASE_REQUEST	16
#define PROC_REQUEST_INFO		17
#define PROC_ROLLBACK			18
#define PROC_SEND				19
#define PROC_START_AND_SEND		20
#define PROC_START				21
#define PROC_START_MULTIPLE		22
#define PROC_START_TRANSACTION	23
#define PROC_TRANSACTION_INFO	24
#define PROC_UNWIND				25
#define PROC_COMMIT_RETAINING	26
#define PROC_QUE_EVENTS			27
#define PROC_CANCEL_EVENTS		28
#define PROC_DDL				29
#define PROC_OPEN_BLOB2			30
#define PROC_CREATE_BLOB2		31
#define PROC_GET_SLICE			32
#define PROC_PUT_SLICE			33
#define PROC_SEEK_BLOB			34
#define PROC_TRANSACT_REQUEST	35
#define PROC_DROP_DATABASE		36

#define PROC_DSQL_ALLOCATE		37
#define PROC_DSQL_EXECUTE		38
#define PROC_DSQL_EXECUTE2		39
#define PROC_DSQL_EXEC_IMMED	40
#define PROC_DSQL_EXEC_IMMED2	41
#define PROC_DSQL_FETCH			42
#define	PROC_DSQL_FREE			43
#define PROC_DSQL_INSERT		44
#define PROC_DSQL_PREPARE		45
#define PROC_DSQL_SET_CURSOR	46
#define PROC_DSQL_SQL_INFO		47

#define PROC_SERVICE_ATTACH		48
#define PROC_SERVICE_DETACH		49
#define PROC_SERVICE_QUERY		50
#define PROC_SERVICE_START		51

#define PROC_ROLLBACK_RETAINING 52
#define PROC_CANCEL_OPERATION	53

#define PROC_count				54

typedef struct
{
	TEXT *name;
	PTR address;
} ENTRY;

typedef struct
{
	TEXT *name;
	TEXT *path;
} IMAGE;

/* Define complicated table for multi-subsystem world */

#ifdef VMS
#define RDB
#endif

#ifndef GDS_A_PATH
#define GDS_A_PATH	"GDS_A"
#define GDS_B_PATH	"GDS_B"
#define GDS_C_PATH	"GDS_C"
#define GDS_D_PATH	"GDS_D"
#endif

#ifdef SUPERCLIENT
#define ENTRYPOINT(gen,cur,bridge,rem,os2_rem,csi,rdb,pipe,bridge_pipe,win,winipi)	extern STATUS	rem(STATUS * user_status, ...);
#else
#define ENTRYPOINT(gen,cur,bridge,rem,os2_rem,csi,rdb,pipe,bridge_pipe,win,winipi)	extern STATUS	rem(STATUS * user_status, ...), cur(STATUS * user_status, ...);
#endif

#include "../jrd/entry.h"

#ifdef RDB
#define ENTRYPOINT(gen,cur,bridge,rem,os2_rem,csi,rdb,pipe,bridge_pipe,win,winipi)	extern STATUS	rdb(STATUS * user_status, ...);
#include "../jrd/entry.h"
#endif

#ifdef IPSERV
#ifndef XNET
#define ENTRYPOINT(gen,cur,bridge,rem,os2_rem,csi,rdb,pipe,bridge_pipe,win,winipi)	extern STATUS	winipi(STATUS * user_status, ...);
#include "../jrd/entry.h"
#endif
#endif

static const IMAGE images[] =
{
	{"REMINT", "REMINT"},			/* Remote */

#ifndef REQUESTER
#ifndef SUPERCLIENT
	{"GDSSHR", "GDSSHR"},			/* Primary access method */
#endif
#endif

#ifdef RDB
	{"GDSRDB", "GDSRDB"},			/* Rdb Interface */
#endif

#ifndef SINIXZ
	{"GDS_A", GDS_A_PATH},
	{"GDS_B", GDS_B_PATH},
	{"GDS_C", GDS_C_PATH},
	{"GDS_D", GDS_D_PATH},
#endif
#ifdef IPSERV
#ifndef XNET
		{"WINIPI", "WINIPI"},
#endif
#endif

};

#define SUBSYSTEMS		sizeof (images) / (sizeof (IMAGE))

static const ENTRY entrypoints[PROC_count * SUBSYSTEMS] =
{

#ifndef EMBEDDED
#define ENTRYPOINT(gen,cur,bridge,rem,os2_rem,csi,rdb,pipe,bridge_pipe,win,winipi)	{NULL, rem},
#include "../jrd/entry.h"
#endif

#ifndef REQUESTER
#ifndef SUPERCLIENT
#define ENTRYPOINT(gen,cur,bridge,rem,os2_rem,csi,rdb,pipe,bridge_pipe,win,winipi)	{NULL, cur},
#include "../jrd/entry.h"
#endif
#endif

#ifdef RDB
#define ENTRYPOINT(gen,cur,bridge,rem,os2_rem,csi,rdb,pipe,bridge_pipe,win,winipi)	{NULL, rdb},
#include "../jrd/entry.h"
#endif

#ifdef IPSERV
#ifndef XNET
#define ENTRYPOINT(gen,cur,bridge,rem,os2_rem,csi,rdb,pipe,bridge_pipe,win,winipi)	{NULL, winipi},
#include "../jrd/entry.h"
#endif
#endif

};

static const TEXT *generic[] = {
#define ENTRYPOINT(gen,cur,bridge,rem,os2_rem,csi,rdb,pipe,bridge_pipe,win,winipi)	gen,
#include "../jrd/entry.h"
};


/* Information items for two phase commit */

static const UCHAR prepare_tr_info[] =
{
	gds__info_tra_id,
	gds__info_end
};

/* Information items for DSQL prepare */

static const SCHAR sql_prepare_info[] =
{
	gds__info_sql_select,
	gds__info_sql_describe_vars,
	gds__info_sql_sqlda_seq,
	gds__info_sql_type,
	gds__info_sql_sub_type,
	gds__info_sql_scale,
	gds__info_sql_length,
	gds__info_sql_field,
	gds__info_sql_relation,
	gds__info_sql_owner,
	gds__info_sql_alias,
	gds__info_sql_describe_end
};

/* Information items for SQL info */

static const SCHAR describe_select_info[] =
{
	gds__info_sql_select,
	gds__info_sql_describe_vars,
	gds__info_sql_sqlda_seq,
	gds__info_sql_type,
	gds__info_sql_sub_type,
	gds__info_sql_scale,
	gds__info_sql_length,
	gds__info_sql_field,
	gds__info_sql_relation,
	gds__info_sql_owner,
	gds__info_sql_alias,
	gds__info_sql_describe_end
};

static const SCHAR describe_bind_info[] =
{
	gds__info_sql_bind,
	gds__info_sql_describe_vars,
	gds__info_sql_sqlda_seq,
	gds__info_sql_type,
	gds__info_sql_sub_type,
	gds__info_sql_scale,
	gds__info_sql_length,
	gds__info_sql_field,
	gds__info_sql_relation,
	gds__info_sql_owner,
	gds__info_sql_alias,
	gds__info_sql_describe_end
};


STATUS API_ROUTINE GDS_ATTACH_DATABASE(STATUS*	user_status,
									   SSHORT	GDS_VAL(file_length),
									   TEXT*	file_name,
									   WHY_ATT*		handle,
									   SSHORT	GDS_VAL(dpb_length),
									   SCHAR*	dpb)
{
/**************************************
 *
 *	g d s _ $ a t t a c h _ d a t a b a s e
 *
 **************************************
 *
 * Functional description
 *	Attach a database through the first subsystem
 *	that recognizes it.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status, *ptr, temp[ISC_STATUS_LENGTH];
	USHORT n, length, org_length, temp_length;
	WHY_DBB database;
	UCHAR *current_dpb_ptr, *last_dpb_ptr;
	TEXT expanded_filename[MAXPATHLEN], temp_filebuf[MAXPATHLEN];
	TEXT *p, *q, *temp_filename;
#if defined(UNIX)
	TEXT single_user[5];
#endif

	GET_STATUS;

	NULL_CHECK(handle, isc_bad_db_handle, HANDLE_database);
	if (!file_name)
	{
		status[0] = isc_arg_gds;
		status[1] = isc_bad_db_format;
		status[2] = isc_arg_string;
		status[3] = (STATUS) "";
		status[4] = isc_arg_end;
		return error2(status, local);
	}

	if (GDS_VAL(dpb_length) > 0 && !dpb)
	{
		status[0] = isc_arg_gds;
		status[1] = isc_bad_dpb_form;
		status[2] = isc_arg_end;
		return error2(status, local);
	}

#if defined (SERVER_SHUTDOWN) && !defined (SUPERCLIENT) && !defined (REQUESTER)
	if (shutdown_flag)
	{
		status[0] = isc_arg_gds;
		status[1] = isc_shutwarn;
		status[2] = isc_arg_end;
		return error2(status, local);
	}
#endif /* SERVER_SHUTDOWN && !SUPERCLIENT && !REQUESTER */

	subsystem_enter();
	SUBSYSTEM_USAGE_INCR;

	temp_filename = temp_filebuf;

	ptr = status;

	org_length = GDS_VAL(file_length);

	if (org_length)
	{
		p = file_name + org_length - 1;
		while (*p == ' ')
		{
			p--;
		}
		org_length = p - file_name + 1;
	}

/* copy the file name to a temp buffer, since some of the following
   utilities can modify it */

	temp_length = org_length ? org_length : strlen(file_name);
	memcpy(temp_filename, file_name, temp_length);
	temp_filename[temp_length] = '\0';

	if (isc_set_path(temp_filename, org_length, expanded_filename))
	{
		temp_filename = expanded_filename;
		org_length = strlen(temp_filename);
	}
	else
	{
		ISC_expand_filename(temp_filename, org_length, expanded_filename);
	}

	current_dpb_ptr = (UCHAR *) dpb;

#ifdef UNIX
/* added so that only the pipe_server goes in here */
	single_user[0] = 0;
	if (open_marker_file(status, expanded_filename, single_user))
	{
		SUBSYSTEM_USAGE_DECR;
		return error(status, local);
	}

	if (single_user[0]) {
		isc_set_single_user(&current_dpb_ptr, &dpb_length, single_user);
	}
#endif

/* Special handling of dpb pointers to handle multiple extends of the dpb */
	last_dpb_ptr = current_dpb_ptr;
	isc_set_login(&current_dpb_ptr, &dpb_length);
	if ((current_dpb_ptr != last_dpb_ptr) && (last_dpb_ptr != (UCHAR *)dpb))
		gds__free((SLONG *) last_dpb_ptr);

	for (n = 0; n < SUBSYSTEMS; n++)
	{
		if (why_enabled && !(why_enabled & (1 << n)))
		{
			continue;
		}
		if (!CALL(PROC_ATTACH_DATABASE, n) (ptr,
											org_length,
											temp_filename,
											handle,
											GDS_VAL(dpb_length),
											current_dpb_ptr,
											expanded_filename))
		{
			length = strlen(expanded_filename);
			database = allocate_handle(n, *handle, HANDLE_database);
			if (database)
			{
				database->db_path = (TEXT*) alloc((SLONG) (length + 1));
			}
			if (!database || !database->db_path)
			{
				/* No memory. Make a half-hearted to detach and get out. */

				if (database)
					release_handle(database);
				CALL(PROC_DETACH, n) (ptr, handle);
				*handle = 0;
				status[0] = isc_arg_gds;
				status[1] = isc_virmemexh;
				status[2] = isc_arg_end;
				break;
			}

			*handle = database;
			p = database->db_path;
			for (q = expanded_filename; length; length--)
			{
				*p++ = *q++;
			}
			*p = 0;

			if (current_dpb_ptr != (UCHAR *) dpb) {
				gds__free((SLONG *) current_dpb_ptr);
			}

			database->cleanup = NULL;
			status[0] = isc_arg_gds;
			status[1] = 0;

			/* Check to make sure that status[2] is not a warning.  If it is, then
			 * the rest of the vector should be untouched.  If it is not, then make
			 * this element isc_arg_end
			 *
			 * This cleanup is done to remove any erroneous errors from trying multiple
			 * protocols for a database attach
			 */
			if (status[2] != isc_arg_warning) {
				status[2] = isc_arg_end;
			}

			subsystem_exit();
			CHECK_STATUS_SUCCESS(status);
			return status[1];
		}
		if (ptr[1] != isc_unavailable) {
			ptr = temp;
		}
	}

	if (current_dpb_ptr != (UCHAR *) dpb) {
		gds__free((SLONG *) current_dpb_ptr);
	}

	SUBSYSTEM_USAGE_DECR;
	return error(status, local);
}


STATUS API_ROUTINE GDS_BLOB_INFO(STATUS*	user_status,
								 WHY_BLB*		blob_handle,
								 SSHORT		GDS_VAL(item_length),
								 SCHAR*		items,
								 SSHORT		GDS_VAL(buffer_length),
								 SCHAR*		buffer)
{
/**************************************
 *
 *	g d s _ $ b l o b _ i n f o
 *
 **************************************
 *
 * Functional description
 *	Provide information on blob object.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status;
	WHY_BLB blob;

	GET_STATUS;
	blob = *blob_handle;
	CHECK_HANDLE(blob, HANDLE_blob, isc_bad_segstr_handle);
	subsystem_enter();

	CALL(PROC_BLOB_INFO, blob->implementation) (status,
												&blob->handle,
												GDS_VAL(item_length),
												items,
												GDS_VAL(buffer_length),
												buffer);

	if (status[1])
		return error(status, local);

	RETURN_SUCCESS;
}


STATUS API_ROUTINE GDS_CANCEL_BLOB(STATUS * user_status, WHY_BLB * blob_handle)
{
/**************************************
 *
 *	g d s _ $ c a n c e l _ b l o b
 *
 **************************************
 *
 * Functional description
 *	Abort a partially completed blob.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status;
	WHY_BLB blob, *ptr;
	WHY_ATT dbb;

	if (!*blob_handle) {
		if (user_status) {
			user_status[0] = gds_arg_gds;
			user_status[1] = 0;
			user_status[2] = gds_arg_end;
			CHECK_STATUS_SUCCESS(user_status);
		}
		return FB_SUCCESS;
	}

	GET_STATUS;
	blob = *blob_handle;
	CHECK_HANDLE(blob, HANDLE_blob, isc_bad_segstr_handle);
	subsystem_enter();

	if (CALL(PROC_CANCEL_BLOB, blob->implementation) (status, &blob->handle))
		return error(status, local);

/* Get rid of connections to database */

	dbb = blob->parent;

	for (ptr = &dbb->blobs; *ptr; ptr = &(*ptr)->next)
		if (*ptr == blob) {
			*ptr = blob->next;
			break;
		}

	release_handle(blob);
	*blob_handle = NULL;

	RETURN_SUCCESS;
}


STATUS API_ROUTINE GDS_CANCEL_EVENTS(STATUS * user_status,
									 WHY_ATT * handle, SLONG * id)
{
/**************************************
 *
 *	g d s _ $ c a n c e l _ e v e n t s
 *
 **************************************
 *
 * Functional description
 *	Try to cancel an event.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status;
	WHY_ATT database;

	GET_STATUS;
	database = *handle;
	CHECK_HANDLE(database, HANDLE_database, isc_bad_db_handle);
	subsystem_enter();

	if (CALL(PROC_CANCEL_EVENTS, database->implementation) (status,
															&database->handle,
															id))
			return error(status, local);

	RETURN_SUCCESS;
}


#ifdef CANCEL_OPERATION
STATUS API_ROUTINE GDS_CANCEL_OPERATION(STATUS * user_status,
										WHY_ATT * handle, USHORT option)
{
/**************************************
 *
 *	g d s _ $ c a n c e l _ o p e r a t i o n
 *
 **************************************
 *
 * Functional description
 *	Try to cancel an operation.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status;
	WHY_ATT database;

	GET_STATUS;
	database = *handle;
	CHECK_HANDLE(database, HANDLE_database, isc_bad_db_handle);
	subsystem_enter();

	if (CALL(PROC_CANCEL_OPERATION, database->implementation) (status,
															   &database->
															   handle,
															   option)) return
			error(status, local);

	RETURN_SUCCESS;
}
#endif


STATUS API_ROUTINE GDS_CLOSE_BLOB(STATUS * user_status, WHY_BLB * blob_handle)
{
/**************************************
 *
 *	g d s _ $ c l o s e _ b l o b
 *
 **************************************
 *
 * Functional description
 *	Abort a partially completed blob.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status;
	WHY_BLB blob, *ptr;
	WHY_ATT dbb;

	GET_STATUS;
	blob = *blob_handle;
	CHECK_HANDLE(blob, HANDLE_blob, isc_bad_segstr_handle);
	subsystem_enter();

	CALL(PROC_CLOSE_BLOB, blob->implementation) (status, &blob->handle);

	if (status[1])
		return error(status, local);

/* Get rid of connections to database */

	dbb = blob->parent;

	for (ptr = &dbb->blobs; *ptr; ptr = &(*ptr)->next)
		if (*ptr == blob) {
			*ptr = blob->next;
			break;
		}

	release_handle(blob);
	*blob_handle = NULL;

	RETURN_SUCCESS;
}


STATUS API_ROUTINE GDS_COMMIT(STATUS * user_status, WHY_TRA * tra_handle)
{
/**************************************
 *
 *	g d s _ $ c o m m i t
 *
 **************************************
 *
 * Functional description
 *	Commit a transaction.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status;
	WHY_TRA transaction, sub;
	CLEAN clean;

	GET_STATUS;
	transaction = *tra_handle;
	CHECK_HANDLE(transaction, HANDLE_transaction, isc_bad_trans_handle);
	subsystem_enter();

/* Handle single transaction case */

	if (transaction->implementation != SUBSYSTEMS) {
		if (CALL(PROC_COMMIT, transaction->implementation) (status,
															&transaction->
															handle)) return
				error(status, local);
	}
	else {
		/* Handle two phase commit.  Start by putting everybody into
		   limbo.  If anybody fails, punt */

		if (!(transaction->flags & HANDLE_TRANSACTION_limbo))
			if (prepare(status, transaction))
				return error(status, local);

		/* Everybody is in limbo, now commit everybody.
		   In theory, this can't fail */

		for (sub = transaction->next; sub; sub = sub->next)
			if (CALL(PROC_COMMIT, sub->implementation) (status, &sub->handle))
				return error(status, local);
	}

	subsystem_exit();

/* Call the associated cleanup handlers */

	while (clean = transaction->cleanup) {
		transaction->cleanup = clean->clean_next;
		clean->TransactionRoutine(transaction, clean->clean_arg);
		free_block(clean);
	}

	while (sub = transaction) {
		transaction = sub->next;
		release_handle(sub);
	}
	*tra_handle = NULL;

	CHECK_STATUS_SUCCESS(status);
	return FB_SUCCESS;
}


STATUS API_ROUTINE GDS_COMMIT_RETAINING(STATUS * user_status,
										WHY_TRA * tra_handle)
{
/**************************************
 *
 *	g d s _ $ c o m m i t _ r e t a i n i n g
 *
 **************************************
 *
 * Functional description
 *	Do a commit retaining.
 *
 * N.B., the transaction cleanup handlers are NOT
 * called here since, conceptually, the transaction
 * is still running.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status;
	WHY_TRA transaction, sub;

	GET_STATUS;
	transaction = *tra_handle;
	CHECK_HANDLE(transaction, HANDLE_transaction, isc_bad_trans_handle);
	subsystem_enter();

	for (sub = transaction; sub; sub = sub->next)
		if (sub->implementation != SUBSYSTEMS &&
			CALL(PROC_COMMIT_RETAINING, sub->implementation) (status,
															  &sub->handle))
				return error(status, local);

	transaction->flags |= HANDLE_TRANSACTION_limbo;

	RETURN_SUCCESS;
}


STATUS API_ROUTINE GDS_COMPILE(STATUS * user_status,
							   WHY_ATT * db_handle,
							   WHY_REQ * req_handle,
							   USHORT GDS_VAL(blr_length), SCHAR * blr)
{
/**************************************
 *
 *	g d s _ $ c o m p i l e
 *
 **************************************
 *
 * Functional description
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status;
	WHY_ATT dbb;
	WHY_REQ request;

	GET_STATUS;
	NULL_CHECK(req_handle, isc_bad_req_handle, HANDLE_request);
	dbb = *db_handle;
	CHECK_HANDLE(dbb, HANDLE_database, isc_bad_db_handle);
	subsystem_enter();

	if (CALL(PROC_COMPILE, dbb->implementation) (status,
												 &dbb->handle,
												 req_handle,
												 GDS_VAL(blr_length),
												 blr))
			return error(status, local);

	request = allocate_handle(dbb->implementation, *req_handle, HANDLE_request);
	if (!request) {
		/* No memory. Make a half-hearted attempt to release request. */

		CALL(PROC_RELEASE_REQUEST, dbb->implementation) (status, req_handle);
		*req_handle = 0;
		status[0] = isc_arg_gds;
		status[1] = isc_virmemexh;
		status[2] = isc_arg_end;
		return error(status, local);
	}

	*req_handle = request;
	request->parent = dbb;
	request->next = dbb->requests;
	dbb->requests = request;

	RETURN_SUCCESS;
}


STATUS API_ROUTINE GDS_COMPILE2(STATUS * user_status,
								WHY_ATT * db_handle,
								WHY_REQ * req_handle,
								USHORT GDS_VAL(blr_length), SCHAR * blr)
{
/**************************************
 *
 *	g d s _ $ c o m p i l e 2
 *
 **************************************
 *
 * Functional description
 *
 **************************************/

	if (GDS_COMPILE(user_status, db_handle, req_handle, blr_length, blr))
		/* Note: if user_status == NULL then GDS_COMPILE handled it */
		return user_status[1];

	(*req_handle)->user_handle = req_handle;

	return FB_SUCCESS;
}


STATUS API_ROUTINE GDS_CREATE_BLOB(STATUS * user_status,
								   WHY_ATT * db_handle,
								   WHY_TRA * tra_handle,
								   WHY_BLB * blob_handle, SLONG * blob_id)
{
/**************************************
 *
 *	g d s _ $ c r e a t e _ b l o b
 *
 **************************************
 *
 * Functional description
 *	Open an existing blob.
 *
 **************************************/

	return open_blob(user_status, db_handle, tra_handle, blob_handle, blob_id,
					 0, 0, PROC_CREATE_BLOB, PROC_CREATE_BLOB2);
}


STATUS API_ROUTINE GDS_CREATE_BLOB2(STATUS * user_status,
									WHY_ATT * db_handle,
									WHY_TRA * tra_handle,
									WHY_BLB * blob_handle,
									SLONG * blob_id,
									SSHORT GDS_VAL(bpb_length), UCHAR * bpb)
{
/**************************************
 *
 *	g d s _ $ c r e a t e _ b l o b 2
 *
 **************************************
 *
 * Functional description
 *	Open an existing blob.
 *
 **************************************/

	return open_blob(user_status, db_handle, tra_handle, blob_handle, blob_id,
					 GDS_VAL(bpb_length), bpb, PROC_CREATE_BLOB,
					 PROC_CREATE_BLOB2);
}



STATUS API_ROUTINE GDS_CREATE_DATABASE(STATUS * user_status,
									   USHORT GDS_VAL(file_length),
									   TEXT * file_name,
									   WHY_ATT * handle,
									   SSHORT GDS_VAL(dpb_length),
									   UCHAR * dpb, USHORT GDS_VAL(db_type))
{
/**************************************
 *
 *	g d s _ $ c r e a t e _ d a t a b a s e
 *
 **************************************
 *
 * Functional description
 *	Create a nice, squeaky clean database, uncorrupted by user data.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status, temp[ISC_STATUS_LENGTH], *ptr;
	USHORT n, length, org_length, temp_length;
	WHY_DBB database;
	TEXT expanded_filename[MAXPATHLEN], temp_filebuf[MAXPATHLEN];
	TEXT *p, *q, *temp_filename;
	UCHAR *current_dpb_ptr, *last_dpb_ptr;
#ifdef UNIX
	TEXT single_user[5];
#endif

	GET_STATUS;
	NULL_CHECK(handle, isc_bad_db_handle, HANDLE_database);
	if (!file_name)
	{
		status[0] = isc_arg_gds;
		status[1] = isc_bad_db_format;
		status[2] = isc_arg_string;
		status[3] = (STATUS) "";
		status[4] = isc_arg_end;
		return error2(status, local);
	}

	if (dpb_length > 0 && !dpb)
	{
		status[0] = isc_arg_gds;
		status[1] = isc_bad_dpb_form;
		status[2] = isc_arg_end;
		return error2(status, local);
	}

#if defined (SERVER_SHUTDOWN) && !defined (SUPERCLIENT) && !defined (REQUESTER)
	if (shutdown_flag)
	{
		status[0] = isc_arg_gds;
		status[1] = isc_shutwarn;
		status[2] = isc_arg_end;
		return error2(status, local);
	}
#endif /* SERVER_SHUTDOWN && !SUPERCLIENT && !REQUESTER */

	subsystem_enter();
	SUBSYSTEM_USAGE_INCR;

	temp_filename = temp_filebuf;

	ptr = status;

	org_length = file_length;

	if (org_length)
	{
		p = file_name + org_length - 1;
		while (*p == ' ')
			p--;
		org_length = p - file_name + 1;
	}

/* copy the file name to a temp buffer, since some of the following
   utilities can modify it */

	if (org_length)
		temp_length = org_length;
	else
		temp_length = strlen(file_name);
	memcpy(temp_filename, file_name, temp_length);
	temp_filename[temp_length] = '\0';

	if (isc_set_path(temp_filename, org_length, expanded_filename))
	{
		temp_filename = expanded_filename;
		org_length = strlen(temp_filename);
	}
	else
		ISC_expand_filename(temp_filename, org_length, expanded_filename);

	current_dpb_ptr = dpb;

#ifdef UNIX
	single_user[0] = 0;
	if (open_marker_file(status, expanded_filename, single_user))
	{
		SUBSYSTEM_USAGE_DECR;
		return error(status, local);
	}

	if (single_user[0])
		isc_set_single_user(&current_dpb_ptr, &dpb_length, single_user);
#endif

/* Special handling of dpb pointers to handle multiple extends of the dpb */
	last_dpb_ptr = current_dpb_ptr;
	isc_set_login(&current_dpb_ptr, &dpb_length);
	if ((current_dpb_ptr != last_dpb_ptr) && (last_dpb_ptr != dpb))
		gds__free(last_dpb_ptr);

	for (n = 0; n < SUBSYSTEMS; n++)
	{
		if (why_enabled && !(why_enabled & (1 << n)))
			continue;
		if (!CALL(PROC_CREATE_DATABASE, n) (
					ptr,
					org_length,
					temp_filename,
					handle,
					GDS_VAL(dpb_length),
					current_dpb_ptr,
					0,
					expanded_filename))
		{
#ifdef WIN_NT
            /*
              if (!(length = (*handle)->att_database->dbb_filename->str_length))
              length = strlen ((*handle)->att_database->dbb_filename->str_data);
            */
            /* Now we can expand, the file exists. */
            ISC_expand_filename (temp_filename, org_length, expanded_filename);
            length = strlen (expanded_filename);
#else
			length = org_length;
			if (!length) {
				length = strlen(temp_filename);
			}
#endif

			database = allocate_handle(n, *handle, HANDLE_database);
			if (database)
			{
				database->db_path = (TEXT *) alloc((SLONG) (length + 1));
			}
			if (!database || !database->db_path)
			{
				/* No memory. Make a half-hearted to drop database. The
				   database was successfully created but the user wouldn't
				   be able to tell. */

				if (database)
					release_handle(database);
				CALL(PROC_DROP_DATABASE, n) (ptr, handle);
				*handle = 0;
				status[0] = isc_arg_gds;
				status[1] = isc_virmemexh;
				status[2] = isc_arg_end;
				break;
			}

			assert(database);
			assert(database->db_path);

			*handle = database;
			p = database->db_path;

#ifdef WIN_NT
            /* for (q = (*handle)->dbb_filename->str_data; length; length--) */
            for (q = expanded_filename; length; length--)
                *p++ = *q++;
#else
            for (q = temp_filename; length; length--)
                *p++ = *q++;
#endif
			*p = 0;

			if (current_dpb_ptr != dpb)
				gds__free((SLONG *) current_dpb_ptr);
			database->cleanup = NULL;
			status[0] = isc_arg_gds;
			status[1] = 0;
			if (status[2] != isc_arg_warning)
				status[2] = isc_arg_end;
			subsystem_exit();
			CHECK_STATUS_SUCCESS(status);
			return status[1];
		}
		if (ptr[1] != isc_unavailable)
			ptr = temp;
	}

	if (current_dpb_ptr != dpb)
		gds__free((SLONG *) current_dpb_ptr);

	SUBSYSTEM_USAGE_DECR;
	return error(status, local);
}


STATUS API_ROUTINE gds__database_cleanup(STATUS * user_status,
										 WHY_ATT * handle,
										 DatabaseCleanupRoutine * routine, SLONG arg)
{
/**************************************
 *
 *	g d s _ $ d a t a b a s e _ c l e a n u p
 *
 **************************************
 *
 * Functional description
 *	Register a database specific cleanup handler.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status;
	WHY_ATT database;
	CLEAN clean;

	GET_STATUS;
	database = *handle;
	CHECK_HANDLE(database, HANDLE_database, isc_bad_db_handle);

	if (!(clean = (CLEAN) alloc((SLONG) sizeof(struct clean)))) {
		status[0] = isc_arg_gds;
		status[1] = isc_virmemexh;
		status[2] = isc_arg_end;
		return error2(status, local);
	}

	clean->clean_next = database->cleanup;
	database->cleanup = clean;
	clean->DatabaseRoutine = routine;
	clean->clean_arg = arg;

	status[0] = gds_arg_gds;
	status[1] = 0;
	status[2] = gds_arg_end;
	CHECK_STATUS_SUCCESS(status);
	return FB_SUCCESS;
}


STATUS API_ROUTINE GDS_DATABASE_INFO(STATUS * user_status,
									 WHY_ATT * handle,
									 SSHORT GDS_VAL(item_length),
									 SCHAR * items,
									 SSHORT GDS_VAL(buffer_length),
									 SCHAR * buffer)
{
/**************************************
 *
 *	g d s _ $ d a t a b a s e _ i n f o
 *
 **************************************
 *
 * Functional description
 *	Provide information on database object.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status;
	WHY_ATT database;

	GET_STATUS;
	database = *handle;
	CHECK_HANDLE(database, HANDLE_database, isc_bad_db_handle);
	subsystem_enter();

	if (CALL(PROC_DATABASE_INFO, database->implementation) (status,
															&database->handle,
															GDS_VAL
															(item_length),
															items,
															GDS_VAL
															(buffer_length),
															buffer)) return
			error(status, local);

	RETURN_SUCCESS;
}


STATUS API_ROUTINE GDS_DDL(STATUS * user_status,
						   WHY_ATT * db_handle,
						   WHY_TRA * tra_handle,
						   USHORT GDS_VAL(length), UCHAR * ddl)
{
/**************************************
 *
 *	g d s _ $ d d l
 *
 **************************************
 *
 * Functional description
 *	Do meta-data update.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status;
	WHY_ATT database;
	WHY_TRA transaction;

#if !defined(SUPERCLIENT)
	TEXT *image;
	PTR entrypoint;
#endif

	GET_STATUS;
	database = *db_handle;
	CHECK_HANDLE(database, HANDLE_database, isc_bad_db_handle);
	transaction = find_transaction(database, *tra_handle);
	CHECK_HANDLE(transaction, HANDLE_transaction, isc_bad_trans_handle);
	subsystem_enter();

	if (get_entrypoint(PROC_DDL, database->implementation) != no_entrypoint)
	{
		if (!CALL(PROC_DDL, database->implementation) (status,
													   &database->handle,
													   &transaction->handle,
													   GDS_VAL(length),
													   ddl))
		{
			RETURN_SUCCESS;
		}
		if (status[1] != isc_unavailable)
		{
			return error(status, local);
		}
	}

	subsystem_exit();

/* Assume that we won't find an entrypoint to call. */

	no_entrypoint(status);

#ifndef SUPERCLIENT
	char DYN_ddl[] = "DYN_ddl";
	if ((image = images[database->implementation].path) != NULL &&
		((entrypoint = (PTR) ISC_lookup_entrypoint(image, DYN_ddl, NULL)) !=
		 NULL ||
		 FALSE) &&
		!((*entrypoint) (status, db_handle, tra_handle, length, ddl))) {
		CHECK_STATUS_SUCCESS(status);
		return FB_SUCCESS;
	}
#endif

	return error2(status, local);
}


STATUS API_ROUTINE GDS_DETACH(STATUS * user_status, WHY_ATT * handle)
{
/**************************************
 *
 *	g d s _ $ d e t a c h
 *
 **************************************
 *
 * Functional description
 *	Close down a database.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status;
	WHY_ATT dbb;
	WHY_REQ request;
	WHY_STMT statement;
	WHY_BLB blob;
	CLEAN clean;

	GET_STATUS;

	dbb = *handle;

#ifdef WIN_NT
/* This code was added to fix an IDAPI problem where our DLL exit handler
 * was getting called before theirs. Our exit handler frees the attachment
 * but does not NULL the handle.  Their exit handler trys to detach, causing
 * a GPF.
 * We should check with IDAPI periodically to see if we still need this.
 */
	if (IsBadReadPtr(dbb, sizeof(WHY_ATT)))
		return bad_handle(user_status, isc_bad_db_handle);
#endif /* WIN_NT */

	CHECK_HANDLE(dbb, HANDLE_database, isc_bad_db_handle);

#ifdef SUPERSERVER

/* Drop all DSQL statements to reclaim DSQL memory pools. */

	while (statement = dbb->statements) {
		if (!statement->user_handle)
			statement->user_handle = &statement;

		if (GDS_DSQL_FREE(status, statement->user_handle, DSQL_drop))
			return error2(status, local);
	}
#endif

	subsystem_enter();

	if (CALL(PROC_DETACH, dbb->implementation) (status, &dbb->handle))
		return error(status, local);

/* Release associated request handles */

	if (dbb->db_path)
		free_block(dbb->db_path);

	while (request = dbb->requests) {
		dbb->requests = request->next;
		if (request->user_handle)
			*request->user_handle = NULL;
		release_handle(request);
	}

#ifndef SUPERSERVER
	while (statement = dbb->statements) {
		dbb->statements = statement->next;
		if (statement->user_handle) {
			*statement->user_handle = NULL;
		}
		release_dsql_support(statement->das);
		release_handle(statement);
	}
#endif

	while (blob = dbb->blobs) {
		dbb->blobs = blob->next;
		if (blob->user_handle)
			*blob->user_handle = NULL;
		release_handle(blob);
	}

	SUBSYSTEM_USAGE_DECR;
	subsystem_exit();

/* Call the associated cleanup handlers */

	while (clean = dbb->cleanup) {
		dbb->cleanup = clean->clean_next;
		clean->DatabaseRoutine(handle, clean->clean_arg);
		free_block(clean);
	}

	release_handle(dbb);
	*handle = NULL;

	CHECK_STATUS_SUCCESS(status);
	return FB_SUCCESS;
}


int API_ROUTINE gds__disable_subsystem(TEXT * subsystem)
{
/**************************************
 *
 *	g d s _ $ d i s a b l e _ s u b s y s t e m 
 *
 **************************************
 *
 * Functional description
 *	Disable access to a specific subsystem.  If no subsystem
 *	has been explicitly disabled, all are available.
 *
 **************************************/
	const IMAGE* sys;
	const IMAGE* end;

	for (sys = images, end = sys + SUBSYSTEMS; sys < end; sys++)
	{
		if (!strcmp(sys->name, subsystem)) {
			if (!why_enabled)
				why_enabled = ~why_enabled;
			why_enabled &= ~(1 << (sys - images));
			return TRUE;
		}
	}

	return FALSE;
}


STATUS API_ROUTINE GDS_DROP_DATABASE(STATUS * user_status, WHY_ATT * handle)
{
/**************************************
 *
 *	i s c _ d r o p _ d a t a b a s e
 *
 **************************************
 *
 * Functional description
 *	Close down a database and then purge it.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status;
	WHY_ATT dbb;
	WHY_REQ request;
	WHY_STMT statement;
	WHY_BLB blob;
	CLEAN clean;

	GET_STATUS;
	dbb = *handle;
	CHECK_HANDLE(dbb, HANDLE_database, isc_bad_db_handle);

#ifdef SUPERSERVER

/* Drop all DSQL statements to reclaim DSQL memory pools. */

	while (statement = dbb->statements) {
		if (!statement->user_handle)
			statement->user_handle = &statement;

		if (GDS_DSQL_FREE(status, statement->user_handle, DSQL_drop))
			return error2(status, local);
	}
#endif

	subsystem_enter();

	(CALL(PROC_DROP_DATABASE, dbb->implementation) (status, &dbb->handle));

	if (status[1] && (status[1] != isc_drdb_completed_with_errs))
		return error(status, local);

/* Release associated request handles */

	if (dbb->db_path)
		free_block(dbb->db_path);

	while (request = dbb->requests) {
		dbb->requests = request->next;
		if (request->user_handle)
			*request->user_handle = NULL;
		release_handle(request);
	}

#ifndef SUPERSERVER
	while (statement = dbb->statements) {
		dbb->statements = statement->next;
		if (statement->user_handle)
			*statement->user_handle = NULL;
		release_dsql_support(statement->das);
		release_handle(statement);
	}
#endif

	while (blob = dbb->blobs) {
		dbb->blobs = blob->next;
		if (blob->user_handle)
			*blob->user_handle = NULL;
		release_handle(blob);
	}

	SUBSYSTEM_USAGE_DECR;
	subsystem_exit();

/* Call the associated cleanup handlers */

	while ((clean = dbb->cleanup) != NULL) {
		dbb->cleanup = clean->clean_next;
		clean->DatabaseRoutine(handle, clean->clean_arg);
		free_block(clean);
	}

	release_handle(dbb);
	*handle = NULL;

	if (status[1])
		return error2(status, local);

	CHECK_STATUS_SUCCESS(status);
	return FB_SUCCESS;
}


STATUS API_ROUTINE GDS_DSQL_ALLOC(STATUS * user_status,
								  WHY_ATT * db_handle, WHY_STMT * stmt_handle)
{
/**************************************
 *
 *	i s c _ d s q l _ a l l o c _ s t a t e m e n t
 *
 **************************************/

	return GDS_DSQL_ALLOCATE(user_status, db_handle, stmt_handle);
}


STATUS API_ROUTINE GDS_DSQL_ALLOC2(STATUS * user_status,
								   WHY_ATT * db_handle, WHY_STMT * stmt_handle)
{
/**************************************
 *
 *	i s c _ d s q l _ a l l o c _ s t a t e m e n t 2
 *
 **************************************
 *
 * Functional description
 *	Allocate a statement handle.
 *
 **************************************/

	if (GDS_DSQL_ALLOCATE(user_status, db_handle, stmt_handle))
		return user_status[1];

	(*stmt_handle)->user_handle = stmt_handle;

	return FB_SUCCESS;
}


STATUS API_ROUTINE GDS_DSQL_ALLOCATE(STATUS * user_status,
									 WHY_ATT * db_handle, WHY_STMT * stmt_handle)
{
/**************************************
 *
 *	i s c _ d s q l _ a l l o c a t e _ s t a t e m e n t
 *
 **************************************
 *
 * Functional description
 *	Allocate a statement handle.
 *
 **************************************/
	STATUS s, *status, local[ISC_STATUS_LENGTH];
	WHY_STMT statement;
	WHY_ATT dbb;
	UCHAR flag;
	PTR entry;
#ifndef NO_LOCAL_DSQL
	dsql_req *dstatement = 0;
#endif
	GET_STATUS;

/* check the statement handle to make sure it's NULL and then initialize it. */

	NULL_CHECK(stmt_handle, isc_bad_stmt_handle, HANDLE_statement);
	dbb = *db_handle;
	CHECK_HANDLE(dbb, HANDLE_database, isc_bad_db_handle);

/* Attempt to have the implementation which processed the database attach
   process the allocate statement.  This may not be feasible (e.g., the 
   server doesn't support remote DSQL because it's the wrong version or 
   something) in which case, execute the functionality locally (and hence 
   remotely through the original Y-valve). */

	s = isc_unavailable;
	entry = get_entrypoint(PROC_DSQL_ALLOCATE, dbb->implementation);
	if (entry != no_entrypoint) {
		subsystem_enter();
		s = (*entry) (status, &dbb->handle, stmt_handle);
		subsystem_exit();
	}
	flag = 0;
#ifndef NO_LOCAL_DSQL
	if (s == isc_unavailable) {
		/* if the entry point didn't exist or if the routine said the server
		   didn't support the protocol... do it locally */

		flag = HANDLE_STATEMENT_local;

		subsystem_enter();
		s = dsql8_allocate_statement(status, db_handle, &dstatement);
		subsystem_exit();
	}
#endif

	if (status[1])
		return error2(status, local);

	statement =
#ifndef NO_LOCAL_DSQL
		(flag & HANDLE_STATEMENT_local) ?
			allocate_handle(dbb->implementation, dstatement, HANDLE_statement) :
#endif
			allocate_handle(dbb->implementation, *stmt_handle, HANDLE_statement);

	if (!statement) {
		/* No memory. Make a half-hearted attempt to drop statement. */

		subsystem_enter();

#ifndef NO_LOCAL_DSQL
		if (flag & HANDLE_STATEMENT_local)
			dsql8_free_statement(status, &dstatement, DSQL_drop);
		else
#endif
			CALL(PROC_DSQL_FREE, dbb->implementation) (status, stmt_handle,
													   DSQL_drop);

		subsystem_exit();

		*stmt_handle = 0;
		status[0] = isc_arg_gds;
		status[1] = isc_virmemexh;
		status[2] = isc_arg_end;
		return error2(status, local);
	}

	*stmt_handle = statement;
	statement->parent = dbb;
	statement->next = dbb->statements;
	dbb->statements = statement;
	statement->flags = flag;

	CHECK_STATUS_SUCCESS(status);
	return FB_SUCCESS;
}


STATUS API_ROUTINE isc_dsql_describe(STATUS * user_status,
									 WHY_STMT * stmt_handle,
									 USHORT dialect, XSQLDA * sqlda)
{
/**************************************
 *
 *	i s c _ d s q l _ d e s c r i b e
 *
 **************************************
 *
 * Functional description
 *	Describe output parameters for a prepared statement.
 *
 **************************************/
	STATUS *status, local[ISC_STATUS_LENGTH];
	USHORT buffer_len;
	SCHAR *buffer, local_buffer[512];

	GET_STATUS;
	CHECK_HANDLE(*stmt_handle, HANDLE_statement, isc_bad_stmt_handle);

	if (!(buffer = get_sqlda_buffer(local_buffer, sizeof(local_buffer), sqlda,
									dialect, &buffer_len))) {
		status[0] = isc_arg_gds;
		status[1] = isc_virmemexh;
		status[2] = isc_arg_end;
		return error2(status, local);
	}

	if (!GDS_DSQL_SQL_INFO(	status,
							stmt_handle,
							sizeof(describe_select_info),
							describe_select_info,
							buffer_len,
							buffer))
	{
		iterative_sql_info(	status,
							stmt_handle,
							sizeof(describe_select_info),
							describe_select_info,
							buffer_len,
							buffer,
							dialect,
							sqlda);
	}

	if (buffer != local_buffer) {
		free_block(buffer);
	}

	if (status[1]) {
		return error2(status, local);
	}

	CHECK_STATUS_SUCCESS(status);
	return FB_SUCCESS;
}


STATUS API_ROUTINE isc_dsql_describe_bind(STATUS * user_status,
										  WHY_STMT * stmt_handle,
										  USHORT dialect, XSQLDA * sqlda)
{
/**************************************
 *
 *	i s c _ d s q l _ d e s c r i b e _ b i n d
 *
 **************************************
 *
 * Functional description
 *	Describe input parameters for a prepared statement.
 *
 **************************************/
	STATUS *status, local[ISC_STATUS_LENGTH];
	USHORT buffer_len;
	SCHAR *buffer, local_buffer[512];

	GET_STATUS;
	CHECK_HANDLE(*stmt_handle, HANDLE_statement, isc_bad_stmt_handle);

	if (!(buffer = get_sqlda_buffer(local_buffer, sizeof(local_buffer), sqlda,
									dialect, &buffer_len))) {
		status[0] = isc_arg_gds;
		status[1] = isc_virmemexh;
		status[2] = isc_arg_end;
		return error2(status, local);
	}

	if (!GDS_DSQL_SQL_INFO(	status,
							stmt_handle,
							sizeof(describe_bind_info),
							describe_bind_info,
							buffer_len,
							buffer))
	{
		iterative_sql_info(	status,
							stmt_handle,
							sizeof(describe_bind_info),
							describe_bind_info,
							buffer_len,
							buffer,
							dialect,
							sqlda);
	}

	if (buffer != local_buffer) {
		free_block(buffer);
	}

	if (status[1]) {
		return error2(status, local);
	}

	CHECK_STATUS_SUCCESS(status);
	return FB_SUCCESS;
}


STATUS API_ROUTINE GDS_DSQL_EXECUTE(STATUS * user_status,
									WHY_TRA * tra_handle,
									WHY_STMT * stmt_handle,
									USHORT dialect, XSQLDA * sqlda)
{
/**************************************
 *
 *	i s c _ d s q l _ e x e c u t e
 *
 **************************************
 *
 * Functional description
 *	Execute a non-SELECT dynamic SQL statement.
 *
 **************************************/

	return GDS_DSQL_EXECUTE2(user_status, tra_handle, stmt_handle, dialect,
							 sqlda, NULL);
}


STATUS API_ROUTINE GDS_DSQL_EXECUTE2(STATUS * user_status,
									 WHY_TRA * tra_handle,
									 WHY_STMT * stmt_handle,
									 USHORT dialect,
									 XSQLDA * in_sqlda, XSQLDA * out_sqlda)
{
/**************************************
 *
 *	i s c _ d s q l _ e x e c u t e 2
 *
 **************************************
 *
 * Functional description
 *	Execute a non-SELECT dynamic SQL statement.
 *
 **************************************/
	STATUS *status, local[ISC_STATUS_LENGTH];
	WHY_STMT statement;
	USHORT in_blr_length, in_msg_type, in_msg_length,
		out_blr_length, out_msg_type, out_msg_length;
	DASUP dasup;

	GET_STATUS;
	statement = *stmt_handle;
	CHECK_HANDLE(statement, HANDLE_statement, isc_bad_stmt_handle);
	if (*tra_handle)
		CHECK_HANDLE(*tra_handle, HANDLE_transaction, isc_bad_trans_handle);

	if (! (dasup = statement->das))
		return bad_handle(user_status, isc_unprepared_stmt);

	if (UTLD_parse_sqlda(status, dasup, &in_blr_length, &in_msg_type,
						 &in_msg_length, dialect, in_sqlda,
						 DASUP_CLAUSE_bind)) return error2(status, local);
	if (UTLD_parse_sqlda
		(status, dasup, &out_blr_length, &out_msg_type, &out_msg_length,
		 dialect, out_sqlda, DASUP_CLAUSE_select))
		return error2(status, local);

	if (GDS_DSQL_EXECUTE2_M(status, tra_handle, stmt_handle,
							in_blr_length,
							dasup->dasup_clauses[DASUP_CLAUSE_bind].dasup_blr,
							in_msg_type, in_msg_length,
							dasup->dasup_clauses[DASUP_CLAUSE_bind].dasup_msg,
							out_blr_length,
							dasup->dasup_clauses[DASUP_CLAUSE_select].
							dasup_blr, out_msg_type, out_msg_length,
							dasup->dasup_clauses[DASUP_CLAUSE_select].
							dasup_msg)) return error2(status, local);

	if (UTLD_parse_sqlda(status, dasup, NULL, NULL, NULL,
						 dialect, out_sqlda, DASUP_CLAUSE_select))
			return error2(status, local);

	CHECK_STATUS_SUCCESS(status);
	return FB_SUCCESS;
}


STATUS API_ROUTINE GDS_DSQL_EXECUTE_M(STATUS * user_status,
									  WHY_TRA * tra_handle,
									  WHY_STMT * stmt_handle,
									  USHORT blr_length,
									  SCHAR * blr,
									  USHORT msg_type,
									  USHORT msg_length, SCHAR * msg)
{
/**************************************
 *
 *	i s c _ d s q l _ e x e c u t e _ m
 *
 **************************************
 *
 * Functional description
 *	Execute a non-SELECT dynamic SQL statement.
 *
 **************************************/

	return GDS_DSQL_EXECUTE2_M(user_status, tra_handle, stmt_handle,
							   blr_length, blr, msg_type, msg_length, msg,
							   0, NULL, 0, 0, NULL);
}


STATUS API_ROUTINE GDS_DSQL_EXECUTE2_M(STATUS * user_status,
									   WHY_TRA * tra_handle,
									   WHY_STMT * stmt_handle,
									   USHORT in_blr_length,
									   SCHAR * in_blr,
									   USHORT in_msg_type,
									   USHORT in_msg_length,
									   SCHAR * in_msg,
									   USHORT out_blr_length,
									   SCHAR * out_blr,
									   USHORT out_msg_type,
									   USHORT out_msg_length, 
									   SCHAR * out_msg)
{
/**************************************
 *
 *	i s c _ d s q l _ e x e c u t e 2 _ m
 *
 **************************************
 *
 * Functional description
 *	Execute a non-SELECT dynamic SQL statement.
 *
 **************************************/
	STATUS *status, local[ISC_STATUS_LENGTH];
	WHY_STMT statement;
	WHY_TRA transaction, handle = NULL;
	PTR entry;
	CLEAN clean;

	GET_STATUS;
	statement = *stmt_handle;
	CHECK_HANDLE(statement, HANDLE_statement, isc_bad_stmt_handle);
	if (transaction = *tra_handle)
		CHECK_HANDLE(transaction, HANDLE_transaction, isc_bad_trans_handle);

#ifndef NO_LOCAL_DSQL
	if (statement->flags & HANDLE_STATEMENT_local) {
		subsystem_enter();
		dsql8_execute(status, tra_handle, &statement->handle.h_dsql,
					  in_blr_length, in_blr, in_msg_type, in_msg_length,
					  in_msg, out_blr_length, out_blr, out_msg_type,
					  out_msg_length, out_msg);
		subsystem_exit();
	}
	else
#endif
	{
		if (transaction) {
			handle = find_transaction(statement->parent, transaction);
			CHECK_HANDLE(handle, HANDLE_transaction, isc_bad_trans_handle);
			handle = handle->handle.h_why;
		}
		subsystem_enter();
		entry = get_entrypoint(PROC_DSQL_EXECUTE2, statement->implementation);
		if (entry != no_entrypoint &&
			(*entry) (status,
					  &handle,
					  &statement->handle,
					  in_blr_length,
					  in_blr,
					  in_msg_type,
					  in_msg_length,
					  in_msg,
					  out_blr_length,
					  out_blr,
					  out_msg_type,
					  out_msg_length, out_msg) != isc_unavailable);
		else if (!out_blr_length && !out_msg_type && !out_msg_length)
			CALL(PROC_DSQL_EXECUTE, statement->implementation) (status,
																&handle,
																&statement->
																handle,
																in_blr_length,
																in_blr,
																in_msg_type,
																in_msg_length,
																in_msg);
		else
			no_entrypoint(status);
		subsystem_exit();

		if (!status[1])
		{
			if (transaction && !handle) {
				/* Call the associated cleanup handlers */

				while (clean = transaction->cleanup) {
					transaction->cleanup = clean->clean_next;
					clean->TransactionRoutine(transaction, clean->clean_arg);
					free_block(clean);
				}

				release_handle(transaction);
				*tra_handle = NULL;
			}
			else if (!transaction && handle)
			{
				*tra_handle = allocate_handle(	statement->implementation,
												handle,
												HANDLE_transaction);
				if (*tra_handle) {
					(*tra_handle)->parent = statement->parent;
				} else {
					status[0] = isc_arg_gds;
					status[1] = isc_virmemexh;
					status[2] = isc_arg_end;
				}
			}
		}
	}

	if (status[1]) {
		return error2(status, local);
	}

	CHECK_STATUS_SUCCESS(status);
	return FB_SUCCESS;
}


STATUS API_ROUTINE GDS_DSQL_EXEC_IMMED(STATUS * user_status,
									   WHY_ATT * db_handle,
									   WHY_TRA * tra_handle,
									   USHORT length,
									   SCHAR * string,
									   USHORT dialect, XSQLDA * sqlda)
{
/**************************************
 *
 *	i s c _ d s q l _ e x e c _ i m m e d
 *
 **************************************
 *
 * Functional description
 *
 **************************************/

	return GDS_DSQL_EXECUTE_IMMED(user_status,
								  db_handle, tra_handle, length, string,
								  dialect, sqlda);
}


STATUS API_ROUTINE GDS_DSQL_EXECUTE_IMMED(STATUS * user_status,
										  WHY_ATT * db_handle,
										  WHY_TRA * tra_handle,
										  USHORT length,
										  SCHAR * string,
										  USHORT dialect, XSQLDA * sqlda)
{
/**************************************
 *
 *	i s c _ d s q l _ e x e c u t e _ i m m e d i a t e
 *
 **************************************
 *
 * Functional description
 *	Prepare a statement for execution.
 *
 **************************************/

	return GDS_DSQL_EXEC_IMMED2(user_status,
								db_handle, tra_handle, length, string,
								dialect, sqlda, NULL);
}


STATUS API_ROUTINE GDS_DSQL_EXEC_IMMED2(STATUS * user_status,
										WHY_ATT * db_handle,
										WHY_TRA * tra_handle,
										USHORT length,
										SCHAR * string,
										USHORT dialect,
										XSQLDA * in_sqlda, XSQLDA * out_sqlda)
{
/**************************************
 *
 *	i s c _ d s q l _ e x e c _ i m m e d 2
 *
 **************************************
 *
 * Functional description
 *	Prepare a statement for execution.
 *
 **************************************/
	STATUS s, *status, local[ISC_STATUS_LENGTH];
	USHORT in_blr_length, in_msg_type, in_msg_length,
		out_blr_length, out_msg_type, out_msg_length;
	struct dasup dasup;

	GET_STATUS;

	if (*db_handle)
		CHECK_HANDLE(*db_handle, HANDLE_database, isc_bad_db_handle);
	if (*tra_handle)
		CHECK_HANDLE(*tra_handle, HANDLE_transaction, isc_bad_trans_handle);

	memset(&dasup, 0, sizeof(struct dasup));
	if (UTLD_parse_sqlda(status, &dasup, &in_blr_length, &in_msg_type,
						 &in_msg_length, dialect, in_sqlda,
						 DASUP_CLAUSE_bind)) return error2(status, local);
	if (UTLD_parse_sqlda
		(status, &dasup, &out_blr_length, &out_msg_type, &out_msg_length,
		 dialect, out_sqlda, DASUP_CLAUSE_select))
		return error2(status, local);

	if (!(s = GDS_DSQL_EXEC_IMM2_M(status, db_handle, tra_handle,
								   length, string, dialect,
								   in_blr_length,
								   dasup.dasup_clauses[DASUP_CLAUSE_bind].
								   dasup_blr, in_msg_type, in_msg_length,
								   dasup.dasup_clauses[DASUP_CLAUSE_bind].
								   dasup_msg, out_blr_length,
								   dasup.dasup_clauses[DASUP_CLAUSE_select].
								   dasup_blr, out_msg_type, out_msg_length,
								   dasup.dasup_clauses[DASUP_CLAUSE_select].
								   dasup_msg))) s =
			UTLD_parse_sqlda(status, &dasup, NULL, NULL, NULL, dialect,
							 out_sqlda, DASUP_CLAUSE_select);

	if (dasup.dasup_clauses[DASUP_CLAUSE_bind].dasup_blr)
		gds__free((SLONG *)
				  (dasup.dasup_clauses[DASUP_CLAUSE_bind].dasup_blr));
	if (dasup.dasup_clauses[DASUP_CLAUSE_bind].dasup_msg)
		gds__free((SLONG *)
				  (dasup.dasup_clauses[DASUP_CLAUSE_bind].dasup_msg));
	if (dasup.dasup_clauses[DASUP_CLAUSE_select].dasup_blr)
		gds__free((SLONG *)
				  (dasup.dasup_clauses[DASUP_CLAUSE_select].dasup_blr));
	if (dasup.dasup_clauses[DASUP_CLAUSE_select].dasup_msg)
		gds__free((SLONG *)
				  (dasup.dasup_clauses[DASUP_CLAUSE_select].dasup_msg));

	CHECK_STATUS(status);
	return s;
}


STATUS API_ROUTINE GDS_DSQL_EXEC_IMM_M(STATUS * user_status,
									   WHY_ATT * db_handle,
									   WHY_TRA * tra_handle,
									   USHORT length,
									   SCHAR * string,
									   USHORT dialect,
									   USHORT blr_length,
									   USHORT msg_type,
									   USHORT msg_length,
									   SCHAR * blr, SCHAR * msg)
{
/**************************************
 *
 *	i s c _ d s q l _ e x e c _ i m m e d _ m
 *
 **************************************
 *
 * Functional description
 *
 **************************************/

	return GDS_DSQL_EXECUTE_IMM_M(user_status, db_handle, tra_handle,
								  length, string, dialect, blr_length, blr,
								  msg_type, msg_length, msg);
}


STATUS API_ROUTINE GDS_DSQL_EXECUTE_IMM_M(STATUS * user_status,
										  WHY_ATT * db_handle,
										  WHY_TRA * tra_handle,
										  USHORT length,
										  SCHAR * string,
										  USHORT dialect,
										  USHORT blr_length,
										  SCHAR * blr,
										  USHORT msg_type,
										  USHORT msg_length, SCHAR * msg)
{
/**************************************
 *
 *	i s c _ d s q l _ e x e c u t e _ i m m e d i a t e _ m
 *
 **************************************
 *
 * Functional description
 *	Prepare a statement for execution.
 *
 **************************************/

	return GDS_DSQL_EXEC_IMM2_M(user_status, db_handle, tra_handle,
								length, string, dialect, blr_length, blr,
								msg_type, msg_length, msg, 0, NULL, 0, 0,
								NULL);
}


STATUS API_ROUTINE GDS_DSQL_EXEC_IMM2_M(STATUS * user_status,
										WHY_ATT * db_handle,
										WHY_TRA * tra_handle,
										USHORT length,
										SCHAR * string,
										USHORT dialect,
										USHORT in_blr_length,
										SCHAR * in_blr,
										USHORT in_msg_type,
										USHORT in_msg_length,
										SCHAR * in_msg,
										USHORT out_blr_length,
										SCHAR * out_blr,
										USHORT out_msg_type,
										USHORT out_msg_length,
										SCHAR * out_msg)
{
/**************************************
 *
 *	i s c _ d s q l _ e x e c _ i m m 2 _ m
 *
 **************************************
 *
 * Functional description
 *	Prepare a statement for execution.
 *
 **************************************/
	WHY_TRA crdb_trans_handle;
	STATUS temp_status[ISC_STATUS_LENGTH];
	STATUS local[ISC_STATUS_LENGTH], *status, *s;
	BOOLEAN stmt_eaten;
	SCHAR buffer[16];
	SCHAR ch;
	BOOLEAN ret_v3_error;

	GET_STATUS;

	if (PREPARSE_execute(	status,
							db_handle,
							tra_handle,
							length,
							string,
							&stmt_eaten,
							dialect))
	{
		if (status[1])
			return error2(status, local);

		crdb_trans_handle = NULL;
		if (GDS_START_TRANSACTION(status, &crdb_trans_handle, 1,
								  db_handle, 0, 0)) {
			save_error_string(status);
			GDS_DROP_DATABASE(temp_status, db_handle);
			*db_handle = NULL;
			return error2(status, local);
		}

		ret_v3_error = FALSE;
		if (!stmt_eaten) {
			/* Check if against < 4.0 database */

			ch = gds__info_base_level;
			if (!GDS_DATABASE_INFO(status, db_handle, 1, &ch, sizeof(buffer),
								   buffer)) {
				if ((buffer[0] != gds__info_base_level) || (buffer[4] > 3))
					GDS_DSQL_EXEC_IMM3_M(status, db_handle,
										 &crdb_trans_handle, length, string,
										 dialect, in_blr_length, in_blr,
										 in_msg_type, in_msg_length, in_msg,
										 out_blr_length, out_blr,
										 out_msg_type, out_msg_length,
										 out_msg);
				else
					ret_v3_error = TRUE;
			}
		}

		if (status[1]) {
			GDS_ROLLBACK(temp_status, &crdb_trans_handle);
			save_error_string(status);
			GDS_DROP_DATABASE(temp_status, db_handle);
			*db_handle = NULL;
			return error2(status, local);
		}
		else {
			if (GDS_COMMIT(status, &crdb_trans_handle)) {
				GDS_ROLLBACK(temp_status, &crdb_trans_handle);
				save_error_string(status);
				GDS_DROP_DATABASE(temp_status, db_handle);
				*db_handle = NULL;
				return error2(status, local);
			}
		}

		if (ret_v3_error) {
			s = status;
			*s++ = isc_arg_gds;
			*s++ = isc_srvr_version_too_old;
			*s = isc_arg_end;
			return error2(status, local);
		}
		CHECK_STATUS_SUCCESS(status);
		return FB_SUCCESS;
	}
	else
		return GDS_DSQL_EXEC_IMM3_M(user_status, db_handle, tra_handle,
									length, string, dialect,
									in_blr_length, in_blr, in_msg_type,
									in_msg_length, in_msg, out_blr_length,
									out_blr, out_msg_type, out_msg_length,
									out_msg);
}


STATUS API_ROUTINE GDS_DSQL_EXEC_IMM3_M(STATUS * user_status,
										WHY_ATT * db_handle,
										WHY_TRA * tra_handle,
										USHORT length,
										SCHAR * string,
										USHORT dialect,
										USHORT in_blr_length,
										SCHAR * in_blr,
										USHORT in_msg_type,
										USHORT in_msg_length,
										SCHAR * in_msg,
										USHORT out_blr_length,
										SCHAR * out_blr,
										USHORT out_msg_type,
										USHORT out_msg_length,
										SCHAR * out_msg)
{
/**************************************
 *
 *	i s c _ d s q l _ e x e c _ i m m 3 _ m
 *
 **************************************
 *
 * Functional description
 *	Prepare a statement for execution.
 *
 **************************************/
	STATUS s, *status, local[ISC_STATUS_LENGTH];
	WHY_ATT dbb;
	WHY_TRA transaction, handle = NULL;
	PTR entry;
	CLEAN clean;

/* If we haven't been initialized yet, do it now */

	GET_STATUS;

	dbb = *db_handle;
	CHECK_HANDLE(dbb, HANDLE_database, isc_bad_db_handle);
	if (transaction = *tra_handle) {
		CHECK_HANDLE(transaction, HANDLE_transaction, isc_bad_trans_handle);
		handle = find_transaction(dbb, transaction);
		CHECK_HANDLE(handle, HANDLE_transaction, isc_bad_trans_handle);
		handle = handle->handle.h_why;
	}

/* Attempt to have the implementation which processed the database attach
   process the prepare statement.  This may not be feasible (e.g., the 
   server doesn't support remote DSQL because it's the wrong version or 
   something) in which case, execute the functionality locally (and hence 
   remotely through the original Y-valve). */

	s = isc_unavailable;
	entry = get_entrypoint(PROC_DSQL_EXEC_IMMED2, dbb->implementation);
	if (entry != no_entrypoint) {
		subsystem_enter();
		s = (*entry) (status,
					  &dbb->handle,
					  &handle,
					  length,
					  string,
					  dialect,
					  in_blr_length,
					  in_blr,
					  in_msg_type,
					  in_msg_length,
					  in_msg,
					  out_blr_length,
					  out_blr, out_msg_type, out_msg_length, out_msg);
		subsystem_exit();
	}

	if (s == isc_unavailable && !out_msg_length) {
		entry = get_entrypoint(PROC_DSQL_EXEC_IMMED, dbb->implementation);
		if (entry != no_entrypoint)
		{
			subsystem_enter();
			s = (*entry) (status,
						  &dbb->handle,
						  &handle,
						  length,
						  string,
						  dialect,
						  in_blr_length,
						  in_blr, in_msg_type, in_msg_length, in_msg);
			subsystem_exit();
		}
	}

	if (s != isc_unavailable && !status[1])
		if (transaction && !handle) {
			/* Call the associated cleanup handlers */

			while (clean = transaction->cleanup) {
				transaction->cleanup = clean->clean_next;
				clean->TransactionRoutine(transaction, clean->clean_arg);
				free_block(clean);
			}

			release_handle(transaction);
			*tra_handle = NULL;
		}
		else if (!transaction && handle) {
			if (*tra_handle =
				allocate_handle(dbb->implementation, handle,
								HANDLE_transaction))
				(*tra_handle)->parent = dbb;
			else {
				status[0] = isc_arg_gds;
				status[1] = isc_virmemexh;
				status[2] = isc_arg_end;
				return error2(status, local);
			}
		}

#ifndef NO_LOCAL_DSQL
	if (s == isc_unavailable) {
		/* if the entry point didn't exist or if the routine said the server
		   didn't support the protocol... do it locally */

		subsystem_enter();
		dsql8_execute_immediate(status, db_handle, tra_handle,
								length, string, dialect,
								in_blr_length, in_blr, in_msg_type,
								in_msg_length, in_msg, out_blr_length,
								out_blr, out_msg_type, out_msg_length,
								out_msg);
		subsystem_exit();
	}
#endif

	if (status[1])
		return error2(status, local);

	CHECK_STATUS_SUCCESS(status);
	return FB_SUCCESS;
}


STATUS API_ROUTINE GDS_DSQL_FETCH(STATUS * user_status,
								  WHY_STMT * stmt_handle,
								  USHORT dialect, XSQLDA * sqlda)
{
/**************************************
 *
 *	i s c _ d s q l _ f e t c h
 *
 **************************************
 *
 * Functional description
 *	Fetch next record from a dynamic SQL cursor
 *
 **************************************/
	STATUS s, *status, local[ISC_STATUS_LENGTH];
	WHY_STMT statement;
	USHORT blr_length, msg_type, msg_length;
	DASUP dasup;

	GET_STATUS;
	statement = *stmt_handle;
	CHECK_HANDLE(statement, HANDLE_statement, isc_bad_stmt_handle);

	if (!sqlda) {
		status[0] = isc_arg_gds;
		status[1] = isc_dsql_sqlda_err;
		status[2] = isc_arg_end;
		return error2(status, local);
	}

	if (!(dasup = statement->das))
		return bad_handle(user_status, isc_unprepared_stmt);

	if (UTLD_parse_sqlda(status, dasup, &blr_length, &msg_type, &msg_length,
						 dialect, sqlda, DASUP_CLAUSE_select))
		return error2(status, local);

	if ((s = GDS_DSQL_FETCH_M(status, stmt_handle, blr_length,
							  dasup->dasup_clauses[DASUP_CLAUSE_select].
							  dasup_blr, 0, msg_length,
							  dasup->dasup_clauses[DASUP_CLAUSE_select].
							  dasup_msg)) && s != 101) {
		CHECK_STATUS(status);
		return s;
	}

	if (UTLD_parse_sqlda(status, dasup, NULL, NULL, NULL,
						 dialect, sqlda, DASUP_CLAUSE_select))
			return error2(status, local);

	CHECK_STATUS(status);
	return s;
}


#ifdef SCROLLABLE_CURSORS
STATUS API_ROUTINE GDS_DSQL_FETCH2(STATUS * user_status,
								   WHY_STMT * stmt_handle,
								   USHORT dialect,
								   XSQLDA * sqlda,
								   USHORT direction, SLONG offset)
{
/**************************************
 *
 *	i s c _ d s q l _ f e t c h 2
 *
 **************************************
 *
 * Functional description
 *	Fetch next record from a dynamic SQL cursor
 *
 **************************************/
	STATUS s, *status, local[ISC_STATUS_LENGTH];
	WHY_STMT statement;
	USHORT blr_length, msg_type, msg_length;
	DASUP dasup;

	GET_STATUS;
	statement = *stmt_handle;
	CHECK_HANDLE(statement, HANDLE_statement, isc_bad_stmt_handle);

	if (!(dasup = statement->das))
		return bad_handle(user_status, isc_unprepared_stmt);

	if (UTLD_parse_sqlda(status, dasup, &blr_length, &msg_type, &msg_length,
						 dialect, sqlda, DASUP_CLAUSE_select))
		return error2(status, local);

	if ((s = GDS_DSQL_FETCH2_M(status, stmt_handle, blr_length,
							   dasup->dasup_clauses[DASUP_CLAUSE_select].
							   dasup_blr, 0, msg_length,
							   dasup->dasup_clauses[DASUP_CLAUSE_select].
							   dasup_msg, direction, offset)) && s != 101) {
		CHECK_STATUS(status);
		return s;
	}

	if (UTLD_parse_sqlda(status, dasup, NULL, NULL, NULL,
						 dialect, sqlda, DASUP_CLAUSE_select))
			return error2(status, local);

	CHECK_STATUS(status);
	return s;
}
#endif


STATUS API_ROUTINE GDS_DSQL_FETCH_M(STATUS * user_status,
									WHY_STMT * stmt_handle,
									USHORT blr_length,
									SCHAR * blr,
									USHORT msg_type,
									USHORT msg_length, SCHAR * msg)
{
/**************************************
 *
 *	i s c _ d s q l _ f e t c h _ m
 *
 **************************************
 *
 * Functional description
 *	Fetch next record from a dynamic SQL cursor
 *
 **************************************/
	STATUS s, *status, local[ISC_STATUS_LENGTH];
	WHY_STMT statement;

	GET_STATUS;
	statement = *stmt_handle;
	CHECK_HANDLE(statement, HANDLE_statement, isc_bad_stmt_handle);

	subsystem_enter();

#ifndef NO_LOCAL_DSQL
	if (statement->flags & HANDLE_STATEMENT_local)
		s = dsql8_fetch(status,
						&statement->handle.h_dsql, blr_length, blr, msg_type,
						msg_length, msg
#ifdef	SCROLLABLE_CURSORS
						, (USHORT) 0, (ULONG) 1);
#else
			);
#endif
	else
#endif
		s = CALL(PROC_DSQL_FETCH, statement->implementation) (status,
															  &statement->
															  handle,
															  blr_length, blr,
															  msg_type,
															  msg_length, msg
#ifdef SCROLLABLE_CURSORS
															  ,
															  (USHORT) 0,
															  (ULONG) 1);
#else
			);
#endif

	subsystem_exit();

	CHECK_STATUS(status);
	if (s == 100 || s == 101)
		return s;
	else if (s)
		return error2(status, local);

	return FB_SUCCESS;
}


#ifdef SCROLLABLE_CURSORS
STATUS API_ROUTINE GDS_DSQL_FETCH2_M(STATUS * user_status,
									 WHY_STMT * stmt_handle,
									 USHORT blr_length,
									 SCHAR * blr,
									 USHORT msg_type,
									 USHORT msg_length,
									 SCHAR * msg,
									 USHORT direction, SLONG offset)
{
/**************************************
 *
 *	i s c _ d s q l _ f e t c h 2 _ m
 *
 **************************************
 *
 * Functional description
 *	Fetch next record from a dynamic SQL cursor
 *
 **************************************/
	STATUS s, *status, local[ISC_STATUS_LENGTH];
	WHY_STMT statement;

	GET_STATUS;
	statement = *stmt_handle;
	CHECK_HANDLE(statement, HANDLE_statement, isc_bad_stmt_handle);

	subsystem_enter();

#ifndef NO_LOCAL_DSQL
	if (statement->flags & HANDLE_STATEMENT_local)
		s = dsql8_fetch(status,
						&statement->handle, blr_length, blr, msg_type,
						msg_length, msg, direction, offset);
	else
#endif
		s = CALL(PROC_DSQL_FETCH, statement->implementation) (status,
															  &statement->
															  handle,
															  blr_length, blr,
															  msg_type,
															  msg_length, msg,
															  direction,
															  offset);

	subsystem_exit();

	CHECK_STATUS(status);
	if (s == 100 || s == 101)
		return s;
	else if (s)
		return error2(status, local);

	return FB_SUCCESS;
}
#endif


STATUS API_ROUTINE GDS_DSQL_FREE(STATUS * user_status,
								 WHY_STMT * stmt_handle, USHORT option)
{
/*****************************************
 *
 *	i s c _ d s q l _ f r e e _ s t a t e m e n t
 *
 *****************************************
 *
 * Functional Description
 *	release request for an sql statement
 *
 *****************************************/
	STATUS *status, local[ISC_STATUS_LENGTH];
	WHY_STMT statement;
	WHY_DBB dbb, *ptr;

	GET_STATUS;
	statement = *stmt_handle;
	CHECK_HANDLE(statement, HANDLE_statement, isc_bad_stmt_handle);

	subsystem_enter();

#ifndef NO_LOCAL_DSQL
	if (statement->flags & HANDLE_STATEMENT_local)
		dsql8_free_statement(status, &statement->handle.h_dsql, option);
	else
#endif
		CALL(PROC_DSQL_FREE, statement->implementation) (status,
														 &statement->handle,
														 option);

	subsystem_exit();

	if (status[1])
		return error2(status, local);

/* Release the handle and any request hanging off of it. */

	if (option & DSQL_drop) {
		/* Get rid of connections to database */

		dbb = statement->parent;
		for (ptr = &dbb->statements; *ptr; ptr = &(*ptr)->next)
			if (*ptr == statement) {
				*ptr = statement->next;
				break;
			}

		release_dsql_support(statement->das);
		release_handle(statement);
		*stmt_handle = NULL;
	}

	CHECK_STATUS_SUCCESS(status);
	return FB_SUCCESS;
}


STATUS API_ROUTINE GDS_DSQL_INSERT(STATUS * user_status,
								   WHY_STMT * stmt_handle,
								   USHORT dialect, XSQLDA * sqlda)
{
/**************************************
 *
 *	i s c _ d s q l _ i n s e r t
 *
 **************************************
 *
 * Functional description
 *	Insert next record into a dynamic SQL cursor
 *
 **************************************/
	STATUS *status, local[ISC_STATUS_LENGTH];
	WHY_STMT statement;
	USHORT blr_length, msg_type, msg_length;
	DASUP dasup;

	GET_STATUS;
	statement = *stmt_handle;
	CHECK_HANDLE(statement, HANDLE_statement, isc_bad_stmt_handle);

	if (!(dasup = statement->das))
		return bad_handle(user_status, isc_unprepared_stmt);

	if (UTLD_parse_sqlda(status, dasup, &blr_length, &msg_type, &msg_length,
						 dialect, sqlda, DASUP_CLAUSE_bind))
		return error2(status, local);

	return GDS_DSQL_INSERT_M(status, stmt_handle, blr_length,
							 dasup->dasup_clauses[DASUP_CLAUSE_bind].
							 dasup_blr, 0, msg_length,
							 dasup->dasup_clauses[DASUP_CLAUSE_bind].
							 dasup_msg);
}


STATUS API_ROUTINE GDS_DSQL_INSERT_M(STATUS * user_status,
									 WHY_STMT * stmt_handle,
									 USHORT blr_length,
									 SCHAR * blr,
									 USHORT msg_type,
									 USHORT msg_length,
									 SCHAR * msg)
{
/**************************************
 *
 *	i s c _ d s q l _ i n s e r t _ m
 *
 **************************************
 *
 * Functional description
 *	Insert next record into a dynamic SQL cursor
 *
 **************************************/
	STATUS s, *status, local[ISC_STATUS_LENGTH];
	WHY_STMT statement;

	GET_STATUS;
	statement = *stmt_handle;
	CHECK_HANDLE(statement, HANDLE_statement, isc_bad_stmt_handle);

	subsystem_enter();

#ifndef NO_LOCAL_DSQL
	if (statement->flags & HANDLE_STATEMENT_local)
		s = dsql8_insert(status,
						 &statement->handle.h_dsql, blr_length, blr, msg_type,
						 msg_length, msg);
	else
#endif
		s = CALL(PROC_DSQL_INSERT, statement->implementation) (status,
															   &statement->
															   handle,
															   blr_length,
															   blr, msg_type,
															   msg_length,
															   msg);

	subsystem_exit();

	if (s)
		return error2(status, local);

	CHECK_STATUS_SUCCESS(status);
	return FB_SUCCESS;
}


STATUS API_ROUTINE GDS_DSQL_PREPARE(STATUS * user_status,
									WHY_TRA * tra_handle,
									WHY_STMT * stmt_handle,
									USHORT length,
									SCHAR * string,
									USHORT dialect, XSQLDA * sqlda)
{
/**************************************
 *
 *	i s c _ d s q l _ p r e p a r e
 *
 **************************************
 *
 * Functional description
 *	Prepare a statement for execution.
 *
 **************************************/
	STATUS *status, local[ISC_STATUS_LENGTH];
	USHORT buffer_len;
	SCHAR *buffer, local_buffer[BUFFER_MEDIUM];
	DASUP dasup;

	GET_STATUS;
	CHECK_HANDLE(*stmt_handle, HANDLE_statement, isc_bad_stmt_handle);
	if (*tra_handle)
		CHECK_HANDLE(*tra_handle, HANDLE_transaction, isc_bad_trans_handle);

	if (!(buffer = get_sqlda_buffer(local_buffer, sizeof(local_buffer), sqlda,
									dialect, &buffer_len))) {
		status[0] = isc_arg_gds;
		status[1] = isc_virmemexh;
		status[2] = isc_arg_end;
		return error2(status, local);
	}

	if (!GDS_DSQL_PREPARE_M(status,
							tra_handle,
							stmt_handle,
							length,
							string,
							dialect,
							sizeof(sql_prepare_info),
							sql_prepare_info,
							buffer_len,
							buffer))
	{
		release_dsql_support((*stmt_handle)->das);

		if (!(dasup = (DASUP) alloc((SLONG) sizeof(struct dasup)))) {
			(*stmt_handle)->requests = 0;
			status[0] = isc_arg_gds;
			status[1] = isc_virmemexh;
			status[2] = isc_arg_end;
		}
		else {
			(*stmt_handle)->das = dasup;
			dasup->dasup_dialect = dialect;

			iterative_sql_info(status, stmt_handle, sizeof(sql_prepare_info),
							   sql_prepare_info, buffer_len, buffer, dialect,
							   sqlda);
		}
	}

	if (buffer != local_buffer)
		free_block(buffer);

	if (status[1])
		return error2(status, local);

	CHECK_STATUS_SUCCESS(status);
	return FB_SUCCESS;
}


STATUS API_ROUTINE GDS_DSQL_PREPARE_M(STATUS * user_status,
									  WHY_TRA * tra_handle,
									  WHY_STMT * stmt_handle,
									  USHORT length,
									  SCHAR * string,
									  USHORT dialect,
									  USHORT GDS_VAL(item_length),
									  const SCHAR * items,
									  USHORT GDS_VAL(buffer_length),
									  SCHAR * buffer)
{
/**************************************
 *
 *	i s c _ d s q l _ p r e p a r e _ m
 *
 **************************************
 *
 * Functional description
 *	Prepare a statement for execution.
 *
 **************************************/
	STATUS *status, local[ISC_STATUS_LENGTH];
	WHY_STMT statement;
	WHY_TRA handle = NULL, transaction;

	GET_STATUS;

	statement = *stmt_handle;
	CHECK_HANDLE(statement, HANDLE_statement, isc_bad_stmt_handle);
	if (transaction = *tra_handle) {
		CHECK_HANDLE(transaction, HANDLE_transaction, isc_bad_trans_handle);
		handle = find_transaction(statement->parent, transaction);
		CHECK_HANDLE(handle, HANDLE_transaction, isc_bad_trans_handle);
		handle = handle->handle.h_why;
	}

	subsystem_enter();

#ifndef NO_LOCAL_DSQL
	if (statement->flags & HANDLE_STATEMENT_local)
		dsql8_prepare(status, tra_handle, &statement->handle.h_dsql,
					  length, string, dialect, item_length, items,
					  buffer_length, buffer);
	else
#endif
	{
		CALL(PROC_DSQL_PREPARE, statement->implementation) (status,
															&handle,
															&statement->
															handle, length,
															string, dialect,
															item_length,
															items,
															buffer_length,
															buffer);
	}

	subsystem_exit();

	if (status[1])
		return error2(status, local);

	CHECK_STATUS_SUCCESS(status);
	return FB_SUCCESS;
}


STATUS API_ROUTINE GDS_DSQL_SET_CURSOR(STATUS * user_status,
									   WHY_STMT * stmt_handle,
									   SCHAR * cursor, USHORT type)
{
/**************************************
 *
 *	i s c _ d s q l _ s e t _ c u r s o r
 *
 **************************************
 *
 * Functional description
 *	Set a cursor name for a dynamic request.
 *
 **************************************/
	STATUS *status, local[ISC_STATUS_LENGTH];
	WHY_STMT statement;

	GET_STATUS;
	statement = *stmt_handle;
	CHECK_HANDLE(statement, HANDLE_statement, isc_bad_stmt_handle);

	subsystem_enter();

#ifndef NO_LOCAL_DSQL
	if (statement->flags & HANDLE_STATEMENT_local)
		dsql8_set_cursor(status, &statement->handle.h_dsql, cursor, type);
	else
#endif
		CALL(PROC_DSQL_SET_CURSOR, statement->implementation) (status,
															   &statement->
															   handle, cursor,
															   type);

	subsystem_exit();

	if (status[1])
		return error2(status, local);

	CHECK_STATUS_SUCCESS(status);
	return FB_SUCCESS;
}


STATUS API_ROUTINE GDS_DSQL_SQL_INFO(STATUS * user_status,
									 WHY_STMT * stmt_handle,
									 SSHORT GDS_VAL(item_length),
									 const SCHAR * items,
									 SSHORT GDS_VAL(buffer_length),
									 SCHAR * buffer)
{
/**************************************
 *
 *	i s c _ d s q l _ s q l _ i n f o
 *
 **************************************
 *
 * Functional description
 *	Provide information on sql statement.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status;
	WHY_STMT statement;

	GET_STATUS;
	statement = *stmt_handle;
	CHECK_HANDLE(statement, HANDLE_statement, isc_bad_stmt_handle);

	subsystem_enter();

#ifndef NO_LOCAL_DSQL
	if (statement->flags & HANDLE_STATEMENT_local)
		dsql8_sql_info(status, &statement->handle.h_dsql, item_length, items,
					   buffer_length, buffer);
	else
#endif
		CALL(PROC_DSQL_SQL_INFO, statement->implementation) (status,
															 &statement->
															 handle,
															 item_length,
															 items,
															 buffer_length,
															 buffer);

	subsystem_exit();

	if (status[1])
		return error2(status, local);

	CHECK_STATUS_SUCCESS(status);
	return FB_SUCCESS;
}


int API_ROUTINE gds__enable_subsystem(TEXT * subsystem)
{
/**************************************
 *
 *	g d s _ $ e n a b l e _ s u b s y s t e m 
 *
 **************************************
 *
 * Functional description
 *	Enable access to a specific subsystem.  If no subsystem
 *	has been explicitly enabled, all are available.
 *
 **************************************/
	const IMAGE *sys, *end;

	for (sys = images, end = sys + SUBSYSTEMS; sys < end; sys++)
		if (!strcmp(sys->name, subsystem)) {
			if (!~why_enabled)
				why_enabled = 0;
			why_enabled |= (1 << (sys - images));
			return TRUE;
		}

	return FALSE;
}


#ifndef REQUESTER
STATUS API_ROUTINE GDS_EVENT_WAIT(STATUS * user_status,
								  WHY_ATT * handle,
								  USHORT GDS_VAL(length),
								  UCHAR * events, UCHAR * buffer)
{
/**************************************
 *
 *	g d s _ $ e v e n t _ w a i t
 *
 **************************************
 *
 * Functional description
 *	Que request for event notification.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status;
	SLONG value, id;
	EVENT event_ptr;

	GET_STATUS;

	if (!why_initialized) {
		gds__register_cleanup((FPTR_VOID_PTR) exit_handler, why_event);
		why_initialized = TRUE;
		ISC_event_init(why_event, 0, 0);
	}

	value = ISC_event_clear(why_event);

	if (GDS_QUE_EVENTS
		(status, handle, &id, length, events, event_ast,
		 buffer)) return error2(status, local);

	event_ptr = why_event;
	ISC_event_wait(1, &event_ptr, &value, -1, 0, NULL_PTR);

	CHECK_STATUS_SUCCESS(status);
	return FB_SUCCESS;
}
#endif


STATUS API_ROUTINE GDS_GET_SEGMENT(STATUS * user_status,
								   WHY_BLB * blob_handle,
								   USHORT * length,
								   USHORT GDS_VAL(buffer_length),
								   UCHAR * buffer)
{
/**************************************
 *
 *	g d s _ $ g e t _ s e g m e n t
 *
 **************************************
 *
 * Functional description
 *	Abort a partially completed blob.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status, code;
	WHY_BLB blob;

	GET_STATUS;
	blob = *blob_handle;
	CHECK_HANDLE(blob, HANDLE_blob, isc_bad_segstr_handle);
	subsystem_enter();

	code = CALL(PROC_GET_SEGMENT, blob->implementation) (status,
														 &blob->handle,
														 length,
														 GDS_VAL
														 (buffer_length),
														 buffer);

	if (code) {
		if (code == isc_segstr_eof || code == isc_segment) {
			subsystem_exit();
			CHECK_STATUS(status);
			return code;
		}
		return error(status, local);
	}

	RETURN_SUCCESS;
}


STATUS API_ROUTINE GDS_GET_SLICE(STATUS * user_status,
								 WHY_ATT * db_handle,
								 WHY_TRA * tra_handle,
								 SLONG * array_id,
								 USHORT GDS_VAL(sdl_length),
								 UCHAR * sdl,
								 USHORT GDS_VAL(param_length),
								 UCHAR * param,
								 SLONG GDS_VAL(slice_length),
								 UCHAR * slice, SLONG * return_length)
{
/**************************************
 *
 *	g d s _ $ g e t _ s l i c e
 *
 **************************************
 *
 * Functional description
 *	Snatch a slice of an array.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status;
	WHY_ATT dbb;
	WHY_TRA transaction;

	GET_STATUS;
	dbb = *db_handle;
	CHECK_HANDLE(dbb, HANDLE_database, isc_bad_db_handle);
	transaction = find_transaction(dbb, *tra_handle);
	CHECK_HANDLE(transaction, HANDLE_transaction, isc_bad_trans_handle);
	subsystem_enter();

	if (CALL(PROC_GET_SLICE, dbb->implementation) (status,
												   &dbb->handle,
												   &transaction->handle,
												   array_id,
												   GDS_VAL(sdl_length),
												   sdl,
												   GDS_VAL(param_length),
												   param,
												   GDS_VAL(slice_length),
												   slice,
												   return_length))
			return error(status, local);

	RETURN_SUCCESS;
}


STATUS gds__handle_cleanup(STATUS * user_status, WHY_HNDL * user_handle)
{
/**************************************
 *
 *	g d s _ $ h a n d l e _ c l e a n u p
 *
 **************************************
 *
 * Functional description
 *	Clean up a dangling y-valve handle.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status;
	WHY_HNDL handle;
	WHY_TRA transaction, sub;
	CLEAN clean;

	GET_STATUS;

	if (!(handle = *user_handle)) {
		status[0] = isc_arg_gds;
		status[1] = isc_bad_db_handle;
		status[2] = isc_arg_end;
		return error2(status, local);
	}

	switch (handle->type) {
	case HANDLE_transaction:

		/* Call the associated cleanup handlers */

		transaction = (WHY_TRA) handle;
		while (clean = transaction->cleanup) {
			transaction->cleanup = clean->clean_next;
			clean->TransactionRoutine(transaction, clean->clean_arg);
			free_block(clean);
		}
		while (sub = transaction) {
			transaction = sub->next;
			release_handle(sub);
		}
		break;

	default:
		status[0] = isc_arg_gds;
		status[1] = isc_bad_db_handle;
		status[2] = isc_arg_end;
		return error2(status, local);
	}

	CHECK_STATUS_SUCCESS(status);
	return FB_SUCCESS;
}


STATUS API_ROUTINE GDS_OPEN_BLOB(STATUS * user_status,
								 WHY_ATT * db_handle,
								 WHY_TRA * tra_handle,
								 WHY_BLB * blob_handle, SLONG * blob_id)
{
/**************************************
 *
 *	g d s _ $ o p e n _ b l o b
 *
 **************************************
 *
 * Functional description
 *	Open an existing blob.
 *
 **************************************/

	return open_blob(user_status, db_handle, tra_handle, blob_handle, blob_id,
					 0, 0, PROC_OPEN_BLOB, PROC_OPEN_BLOB2);
}


STATUS API_ROUTINE GDS_OPEN_BLOB2(STATUS * user_status,
								  WHY_ATT * db_handle,
								  WHY_TRA * tra_handle,
								  WHY_BLB * blob_handle,
								  SLONG * blob_id,
								  SSHORT GDS_VAL(bpb_length), UCHAR * bpb)
{
/**************************************
 *
 *	g d s _ $ o p e n _ b l o b 2
 *
 **************************************
 *
 * Functional description
 *	Open an existing blob (extended edition).
 *
 **************************************/

	return open_blob(user_status, db_handle, tra_handle, blob_handle, blob_id,
					 GDS_VAL(bpb_length), bpb, PROC_OPEN_BLOB,
					 PROC_OPEN_BLOB2);
}


STATUS API_ROUTINE GDS_PREPARE(STATUS * user_status, WHY_TRA * tra_handle)
{
/**************************************
 *
 *	g d s _ $ p r e p a r e
 *
 **************************************
 *
 * Functional description
 *	Prepare a transaction for commit.  First phase of a two
 *	phase commit.
 *
 **************************************/
	return GDS_PREPARE2(user_status, tra_handle, 0, 0);
}


STATUS API_ROUTINE GDS_PREPARE2(STATUS * user_status,
								WHY_TRA * tra_handle,
								USHORT GDS_VAL(msg_length), UCHAR * msg)
{
/**************************************
 *
 *	g d s _ $ p r e p a r e 2
 *
 **************************************
 *
 * Functional description
 *	Prepare a transaction for commit.  First phase of a two
 *	phase commit.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status;
	WHY_TRA transaction, sub;

	GET_STATUS;
	transaction = *tra_handle;
	CHECK_HANDLE(transaction, HANDLE_transaction, isc_bad_trans_handle);
	subsystem_enter();

	for (sub = transaction; sub; sub = sub->next)
		if (sub->implementation != SUBSYSTEMS &&
			CALL(PROC_PREPARE, sub->implementation) (status,
													 &sub->handle,
													 GDS_VAL(msg_length),
													 msg))
				return error(status, local);

	transaction->flags |= HANDLE_TRANSACTION_limbo;

	RETURN_SUCCESS;
}


STATUS API_ROUTINE GDS_PUT_SEGMENT(STATUS * user_status,
								   WHY_BLB * blob_handle,
								   USHORT GDS_VAL(buffer_length),
								   UCHAR * buffer)
{
/**************************************
 *
 *	g d s _ $ p u t _ s e g m e n t
 *
 **************************************
 *
 * Functional description
 *	Abort a partially completed blob.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status;
	WHY_BLB blob;

	GET_STATUS;
	blob = *blob_handle;
	CHECK_HANDLE(blob, HANDLE_blob, isc_bad_segstr_handle);
	subsystem_enter();

	if (CALL(PROC_PUT_SEGMENT, blob->implementation) (status,
													  &blob->handle,
													  GDS_VAL(buffer_length),
													  buffer))
			return error(status, local);

	RETURN_SUCCESS;
}


STATUS API_ROUTINE GDS_PUT_SLICE(STATUS * user_status,
								 WHY_ATT * db_handle,
								 WHY_TRA * tra_handle,
								 SLONG * array_id,
								 USHORT GDS_VAL(sdl_length),
								 UCHAR * sdl,
								 USHORT GDS_VAL(param_length),
								 UCHAR * param,
								 SLONG GDS_VAL(slice_length), UCHAR * slice)
{
/**************************************
 *
 *	g d s _ $ p u t _ s l i c e
 *
 **************************************
 *
 * Functional description
 *	Snatch a slice of an array.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status;
	WHY_ATT dbb;
	WHY_TRA transaction;

	GET_STATUS;
	dbb = *db_handle;
	CHECK_HANDLE(dbb, HANDLE_database, isc_bad_db_handle);
	transaction = find_transaction(dbb, *tra_handle);
	CHECK_HANDLE(transaction, HANDLE_transaction, isc_bad_trans_handle);
	subsystem_enter();

	if (CALL(PROC_PUT_SLICE, dbb->implementation) (status,
												   &dbb->handle,
												   &transaction->handle,
												   array_id,
												   GDS_VAL(sdl_length),
												   sdl,
												   GDS_VAL(param_length),
												   param,
												   GDS_VAL(slice_length),
												   slice))
			return error(status, local);

	RETURN_SUCCESS;
}


STATUS API_ROUTINE GDS_QUE_EVENTS(STATUS * user_status,
								  WHY_ATT * handle,
								  SLONG * id,
								  USHORT GDS_VAL(length),
								  UCHAR * events,
								  event_ast_routine * ast, void *arg)
{
/**************************************
 *
 *	g d s _ $ q u e _ e v e n t s
 *
 **************************************
 *
 * Functional description
 *	Que request for event notification.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status;
	WHY_ATT database;

	GET_STATUS;
	database = *handle;
	CHECK_HANDLE(database, HANDLE_database, isc_bad_db_handle);

	subsystem_enter();

	if (CALL(PROC_QUE_EVENTS, database->implementation) (status,
														 &database->handle,
														 id,
														 GDS_VAL(length),
														 events,
														 GDS_VAL(ast),
														 arg))
			return error(status, local);

	RETURN_SUCCESS;
}


STATUS API_ROUTINE GDS_RECEIVE(STATUS * user_status,
							   WHY_REQ * req_handle,
							   USHORT GDS_VAL(msg_type),
							   USHORT GDS_VAL(msg_length),
							   SCHAR * msg, SSHORT GDS_VAL(level))
{
/**************************************
 *
 *	g d s _ $ r e c e i v e
 *
 **************************************
 *
 * Functional description
 *	Get a record from the host program.
 *
 **************************************/

#ifdef SCROLLABLE_CURSORS
	return GDS_RECEIVE2(user_status, req_handle, msg_type, msg_length,
						msg, level, (USHORT) blr_continue,	/* means continue in same direction as before */
						(ULONG) 1);
#else
	STATUS local[ISC_STATUS_LENGTH], *status;
	WHY_REQ request;

	GET_STATUS;
	request = *req_handle;
	CHECK_HANDLE(request, HANDLE_request, isc_bad_req_handle);
	subsystem_enter();

	if (CALL(PROC_RECEIVE, request->implementation) (status,
													 &request->handle,
													 GDS_VAL(msg_type),
													 GDS_VAL(msg_length),
													 msg,
													 GDS_VAL(level)))
			return error(status, local);

	RETURN_SUCCESS;
#endif
}


#ifdef SCROLLABLE_CURSORS
STATUS API_ROUTINE GDS_RECEIVE2(STATUS * user_status,
								WHY_REQ * req_handle,
								USHORT GDS_VAL(msg_type),
								USHORT GDS_VAL(msg_length),
								SCHAR * msg,
								SSHORT GDS_VAL(level),
								USHORT direction, ULONG offset)
{
/**************************************
 *
 *	i s c _ r e c e i v e 2
 *
 **************************************
 *
 * Functional description
 *	Scroll through the request output stream, 
 *	then get a record from the host program.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status;
	WHY_REQ request;

	GET_STATUS;
	request = *req_handle;
	CHECK_HANDLE(request, HANDLE_request, isc_bad_req_handle);
	subsystem_enter();

	if (CALL(PROC_RECEIVE, request->implementation) (status,
													 &request->handle,
													 GDS_VAL(msg_type),
													 GDS_VAL(msg_length),
													 msg,
													 GDS_VAL(level),
													 direction,
													 offset))
			return error(status, local);

	RETURN_SUCCESS;
}
#endif


STATUS API_ROUTINE GDS_RECONNECT(STATUS * user_status,
								 WHY_ATT * db_handle,
								 WHY_TRA * tra_handle,
								 SSHORT GDS_VAL(length), UCHAR * id)
{
/**************************************
 *
 *	g d s _ $ r e c o n n e c t
 *
 **************************************
 *
 * Functional description
 *	Connect to a transaction in limbo.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status;
	WHY_ATT database;

	GET_STATUS;
	NULL_CHECK(tra_handle, isc_bad_trans_handle, HANDLE_transaction);
	database = *db_handle;
	CHECK_HANDLE(database, HANDLE_database, isc_bad_db_handle);
	subsystem_enter();

	if (CALL(PROC_RECONNECT, database->implementation) (status,
														&database->handle,
														tra_handle,
														GDS_VAL(length),
														id))
			return error(status, local);

	*tra_handle = allocate_handle(	database->implementation,
									*tra_handle,
									HANDLE_transaction);
	if (*tra_handle) {
		(*tra_handle)->parent = database;
	} else {
		status[0] = isc_arg_gds;
		status[1] = isc_virmemexh;
		status[2] = isc_arg_end;
		return error(status, local);
	}

	RETURN_SUCCESS;
}


STATUS API_ROUTINE GDS_RELEASE_REQUEST(STATUS * user_status, WHY_REQ * req_handle)
{
/**************************************
 *
 *	g d s _ $ r e l e a s e _ r e q u e s t
 *
 **************************************
 *
 * Functional description
 *	Release a request.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status;
	WHY_REQ request, *ptr;
	WHY_DBB dbb;

	GET_STATUS;
	request = *req_handle;
	CHECK_HANDLE(request, HANDLE_request, isc_bad_req_handle);
	subsystem_enter();

	if (CALL(PROC_RELEASE_REQUEST, request->implementation) (status,
															 &request->
															 handle)) return
			error(status, local);

/* Get rid of connections to database */

	dbb = request->parent;

	for (ptr = &dbb->requests; *ptr; ptr = &(*ptr)->next)
		if (*ptr == request) {
			*ptr = request->next;
			break;
		}

	release_handle(request);
	*req_handle = NULL;

	RETURN_SUCCESS;
}


STATUS API_ROUTINE GDS_REQUEST_INFO(STATUS * user_status,
									WHY_REQ * req_handle,
									SSHORT GDS_VAL(level),
									SSHORT GDS_VAL(item_length),
									SCHAR * items,
									SSHORT GDS_VAL(buffer_length),
									SCHAR * buffer)
{
/**************************************
 *
 *	g d s _ $ r e q u e s t _ i n f o
 *
 **************************************
 *
 * Functional description
 *	Provide information on blob object.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status;
	WHY_REQ request;

	GET_STATUS;
	request = *req_handle;
	CHECK_HANDLE(request, HANDLE_request, isc_bad_req_handle);
	subsystem_enter();

	if (CALL(PROC_REQUEST_INFO, request->implementation) (status,
														  &request->handle,
														  GDS_VAL(level),
														  GDS_VAL
														  (item_length),
														  items,
														  GDS_VAL
														  (buffer_length),
														  buffer)) return
			error(status, local);

	RETURN_SUCCESS;
}


SLONG API_ROUTINE isc_reset_fpe(USHORT fpe_status)
{
/**************************************
 *
 *	i s c _ r e s e t _ f p e
 *
 **************************************
 *
 * Functional description
 *	API to be used to tell InterBase to reset it's
 *	FPE handler - eg: client has an FPE of it's own
 *	and just changed it.
 *
 * Returns
 *	Prior setting of the FPE reset flag
 *
 **************************************/
#if !(defined REQUESTER || defined SUPERCLIENT || defined SUPERSERVER)
	SLONG prior;
	prior = (SLONG) subsystem_FPE_reset;
	switch (fpe_status) {
	case FPE_RESET_INIT_ONLY:
		subsystem_FPE_reset = fpe_status;
		break;
	case FPE_RESET_NEXT_API_CALL:
		subsystem_FPE_reset = fpe_status;
		break;
	case FPE_RESET_ALL_API_CALL:
		subsystem_FPE_reset = fpe_status;
		break;
	default:
		break;
	}
	return prior;
#else
	return FPE_RESET_INIT_ONLY;
#endif
}


STATUS API_ROUTINE GDS_ROLLBACK_RETAINING(STATUS * user_status,
										  WHY_TRA * tra_handle)
{
/**************************************
 *
 *	i s c _ r o l l b a c k _ r e t a i n i n g
 *
 **************************************
 *
 * Functional description
 *	Abort a transaction, but keep all cursors open.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status;
	WHY_TRA transaction, sub;

	GET_STATUS;
	transaction = *tra_handle;
	CHECK_HANDLE(transaction, HANDLE_transaction, isc_bad_trans_handle);
	subsystem_enter();

	for (sub = transaction; sub; sub = sub->next)
		if (sub->implementation != SUBSYSTEMS &&
			CALL(PROC_ROLLBACK_RETAINING, sub->implementation) (status,
																&sub->handle))
				return error(status, local);

	transaction->flags |= HANDLE_TRANSACTION_limbo;

	RETURN_SUCCESS;
}


STATUS API_ROUTINE GDS_ROLLBACK(STATUS * user_status, WHY_TRA * tra_handle)
{
/**************************************
 *
 *	g d s _ $ r o l l b a c k
 *
 **************************************
 *
 * Functional description
 *	Abort a transaction.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status;
	WHY_TRA transaction, sub;
	CLEAN clean;

	GET_STATUS;
	transaction = *tra_handle;
	CHECK_HANDLE(transaction, HANDLE_transaction, isc_bad_trans_handle);
	subsystem_enter();

	for (sub = transaction; sub; sub = sub->next)
		if (sub->implementation != SUBSYSTEMS &&
			CALL(PROC_ROLLBACK, sub->implementation) (status, &sub->handle)) {
			if (!IS_NETWORK_ERROR(status))
				return error(status, local);
		}

	if (IS_NETWORK_ERROR(status))
		INIT_STATUS(status);

	subsystem_exit();

/* Call the associated cleanup handlers */

	while (clean = transaction->cleanup) {
		transaction->cleanup = clean->clean_next;
		clean->TransactionRoutine(transaction, clean->clean_arg);
		free_block(clean);
	}

	while (sub = transaction) {
		transaction = sub->next;
		release_handle(sub);
	}
	*tra_handle = NULL;

	CHECK_STATUS_SUCCESS(status);
	return FB_SUCCESS;
}


STATUS API_ROUTINE GDS_SEEK_BLOB(STATUS * user_status,
								 WHY_BLB * blob_handle,
								 SSHORT GDS_VAL(mode),
								 SLONG GDS_VAL(offset), SLONG * result)
{
/**************************************
 *
 *	g d s _ $ s e e k _ b l o b
 *
 **************************************
 *
 * Functional description
 *	Seek a blob.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status;
	WHY_BLB blob;

	GET_STATUS;
	blob = *blob_handle;
	CHECK_HANDLE(blob, HANDLE_blob, isc_bad_segstr_handle);
	subsystem_enter();

/***
if (blob->flags & HANDLE_BLOB_filter)
    {
    subsystem_exit();
    BLF_close_blob (status, &blob->handle);
    subsystem_enter();
    }
else
***/
	CALL(PROC_SEEK_BLOB, blob->implementation) (status,
												&blob->handle,
												GDS_VAL(mode),
												GDS_VAL(offset), result);

	if (status[1])
		return error(status, local);

	RETURN_SUCCESS;
}


STATUS API_ROUTINE GDS_SEND(STATUS * user_status,
							WHY_REQ * req_handle,
							USHORT GDS_VAL(msg_type),
							USHORT GDS_VAL(msg_length),
							SCHAR * msg, SSHORT GDS_VAL(level))
{
/**************************************
 *
 *	g d s _ $ s e n d
 *
 **************************************
 *
 * Functional description
 *	Get a record from the host program.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status;
	WHY_REQ request;

	GET_STATUS;
	request = *req_handle;
	CHECK_HANDLE(request, HANDLE_request, isc_bad_req_handle);
	subsystem_enter();

	if (CALL(PROC_SEND, request->implementation) (status,
												  &request->handle,
												  GDS_VAL(msg_type),
												  GDS_VAL(msg_length),
												  msg,
												  GDS_VAL(level)))
			return error(status, local);

	RETURN_SUCCESS;
}


STATUS API_ROUTINE GDS_SERVICE_ATTACH(STATUS * user_status,
									  USHORT service_length,
									  TEXT * service_name,
									  WHY_SVC * handle,
									  USHORT spb_length, SCHAR * spb)
{
/**************************************
 *
 *	i s c _ s e r v i c e _ a t t a c h
 *
 **************************************
 *
 * Functional description
 *	Attach a service through the first subsystem
 *	that recognizes it.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status, *ptr, temp[ISC_STATUS_LENGTH];
	USHORT n, org_length;
	WHY_SVC service;
	TEXT *p;

	GET_STATUS;
	NULL_CHECK(handle, isc_bad_svc_handle, HANDLE_service);

	if (!service_name) {
		status[0] = isc_arg_gds;
		status[1] = isc_service_att_err;
		status[2] = isc_arg_gds;
		status[3] = isc_svc_name_missing;
		status[4] = isc_arg_end;
		return error2(status, local);
	}

	if (GDS_VAL(spb_length) > 0 && !spb) {
		status[0] = isc_arg_gds;
		status[1] = isc_bad_spb_form;
		status[2] = isc_arg_end;
		return error2(status, local);
	}

#if defined (SERVER_SHUTDOWN) && !defined (SUPERCLIENT) && !defined (REQUESTER)
	if (shutdown_flag) {
		status[0] = isc_arg_gds;
		status[1] = isc_shutwarn;
		status[2] = isc_arg_end;
		return error2(status, local);
	}
#endif /* SERVER_SHUTDOWN && !SUPERCLIENT && !REQUESTER */

	subsystem_enter();
	SUBSYSTEM_USAGE_INCR;

	ptr = status;

	org_length = service_length;

	if (org_length) {
		p = service_name + org_length - 1;
		while (*p == ' ')
			p--;
		org_length = p - service_name + 1;
	}

	for (n = 0; n < SUBSYSTEMS; n++) {
		if (why_enabled && !(why_enabled & (1 << n)))
			continue;
		if (!CALL(PROC_SERVICE_ATTACH, n) (ptr,
										   org_length,
										   service_name,
										   handle, spb_length, spb))
		{
			service = allocate_handle(n, *handle, HANDLE_service);
			if (!service)
			{
				/* No memory. Make a half-hearted attempt to detach service. */

				CALL(PROC_SERVICE_DETACH, n) (ptr, handle);
				*handle = 0;
				status[0] = isc_arg_gds;
				status[1] = isc_virmemexh;
				status[2] = isc_arg_end;
				break;
			}

			*handle = service;
			service->cleanup = NULL;
			status[0] = isc_arg_gds;
			status[1] = 0;
			if (status[2] != isc_arg_warning)
				status[2] = isc_arg_end;
			subsystem_exit();
			CHECK_STATUS_SUCCESS(status);
			return status[1];
		}
		if (ptr[1] != isc_unavailable)
			ptr = temp;
	}

	SUBSYSTEM_USAGE_DECR;
	if (status[1] == isc_unavailable)
		status[1] = isc_service_att_err;
	return error(status, local);
}


STATUS API_ROUTINE GDS_SERVICE_DETACH(STATUS * user_status, WHY_SVC * handle)
{
/**************************************
 *
 *	i s c _ s e r v i c e _ d e t a c h
 *
 **************************************
 *
 * Functional description
 *	Close down a service.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status;
	WHY_SVC service;
	CLEAN clean;

	GET_STATUS;
	service = *handle;
	CHECK_HANDLE(service, HANDLE_service, isc_bad_svc_handle);
	subsystem_enter();

	if (CALL(PROC_SERVICE_DETACH, service->implementation) (status,
															&service->handle))
			return error(status, local);

	SUBSYSTEM_USAGE_DECR;
	subsystem_exit();

/* Call the associated cleanup handlers */

	while ((clean = service->cleanup) != NULL) {
		service->cleanup = clean->clean_next;
		clean->DatabaseRoutine(handle, clean->clean_arg);
		free_block(clean);
	}

	release_handle(service);
	*handle = NULL;

	CHECK_STATUS_SUCCESS(status);
	return FB_SUCCESS;
}


STATUS API_ROUTINE GDS_SERVICE_QUERY(STATUS * user_status,
									 WHY_SVC * handle,
									 ULONG * reserved,
									 USHORT send_item_length,
									 SCHAR * send_items,
									 USHORT recv_item_length,
									 SCHAR * recv_items,
									 USHORT buffer_length, SCHAR * buffer)
{
/**************************************
 *
 *	i s c _ s e r v i c e _ q u e r y
 *
 **************************************
 *
 * Functional description
 *	Provide information on service object.
 *
 *	NOTE: The parameter RESERVED must not be used
 *	for any purpose as there are networking issues
 *	involved (as with any handle that goes over the
 *	network).  This parameter will be implemented at 
 *	a later date.
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status;
	WHY_SVC service;

	GET_STATUS;
	service = *handle;
	CHECK_HANDLE(service, HANDLE_service, isc_bad_svc_handle);
	subsystem_enter();

	if (CALL(PROC_SERVICE_QUERY, service->implementation) (status, &service->handle, 0,	/* reserved */
														   send_item_length,
														   send_items,
														   recv_item_length,
														   recv_items,
														   buffer_length,
														   buffer))
			return error(status, local);

	RETURN_SUCCESS;
}


STATUS API_ROUTINE GDS_SERVICE_START(STATUS * user_status,
									 WHY_SVC * handle,
									 ULONG * reserved,
									 USHORT spb_length, SCHAR * spb)
{
/**************************************
 *
 *	i s c _ s e r v i c e _ s t a r t
 *
 **************************************
 *
 * Functional description
 *	Starts a service thread
 * 
 *	NOTE: The parameter RESERVED must not be used
 *	for any purpose as there are networking issues
 *	involved (as with any handle that goes over the
 *	network).  This parameter will be implemented at 
 *	a later date.
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status;
	WHY_SVC service;

	GET_STATUS;
	service = *handle;
	CHECK_HANDLE(service, HANDLE_service, isc_bad_svc_handle);
	subsystem_enter();

	if (CALL(PROC_SERVICE_START, service->implementation) (status,
														   &service->handle,
														   NULL,
														   spb_length, spb)) {
		return error(status, local);
	}

	RETURN_SUCCESS;
}


STATUS API_ROUTINE GDS_START_AND_SEND(STATUS * user_status,
									  WHY_REQ * req_handle,
									  WHY_TRA * tra_handle,
									  USHORT GDS_VAL(msg_type),
									  USHORT GDS_VAL(msg_length),
									  SCHAR * msg, SSHORT GDS_VAL(level))
{
/**************************************
 *
 *	g d s _ $ s t a r t _ a n d _ s e n d
 *
 **************************************
 *
 * Functional description
 *	Get a record from the host program.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status;
	WHY_REQ request;
	WHY_TRA transaction;

	GET_STATUS;
	request = *req_handle;
	CHECK_HANDLE(request, HANDLE_request, isc_bad_req_handle);
	transaction = find_transaction(request->parent, *tra_handle);
	CHECK_HANDLE(transaction, HANDLE_transaction, isc_bad_trans_handle);
	subsystem_enter();

	if (CALL(PROC_START_AND_SEND, request->implementation) (status,
															&request->handle,
															&transaction->
															handle,
															GDS_VAL(msg_type),
															GDS_VAL
															(msg_length), msg,
															GDS_VAL(level)))
			return error(status, local);

	RETURN_SUCCESS;
}


STATUS API_ROUTINE GDS_START(STATUS * user_status,
							 WHY_REQ * req_handle,
							 WHY_TRA * tra_handle, SSHORT GDS_VAL(level))
{
/**************************************
 *
 *	g d s _ $ s t a r t
 *
 **************************************
 *
 * Functional description
 *	Get a record from the host program.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status;
	WHY_REQ request;
	WHY_TRA transaction;

	GET_STATUS;
	request = *req_handle;
	CHECK_HANDLE(request, HANDLE_request, isc_bad_req_handle);
	transaction = find_transaction(request->parent, *tra_handle);
	CHECK_HANDLE(transaction, HANDLE_transaction, isc_bad_trans_handle);
	subsystem_enter();

	if (CALL(PROC_START, request->implementation) (status,
												   &request->handle,
												   &transaction->handle,
												   GDS_VAL(level)))
			return error(status, local);

	RETURN_SUCCESS;
}


STATUS API_ROUTINE GDS_START_MULTIPLE(STATUS * user_status,
									  WHY_TRA * tra_handle,
									  USHORT GDS_VAL(count), TEB * vector)
{
/**************************************
 *
 *	g d s _ $ s t a r t _ m u l t i p l e
 *
 **************************************
 *
 * Functional description
 *	Start a transaction.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status, temp[ISC_STATUS_LENGTH], *s;
	WHY_TRA transaction, sub, *ptr;
	WHY_DBB database;
	USHORT n;

	GET_STATUS;
	NULL_CHECK(tra_handle, isc_bad_trans_handle, HANDLE_transaction);
	transaction = NULL;
	subsystem_enter();

	for (n = 0, ptr = &transaction; n < GDS_VAL(count);
		 n++, ptr = &(*ptr)->next, vector++) {
		database = *vector->teb_database;
		if (!database || database->type != HANDLE_database) {
			s = status;
			*s++ = isc_arg_gds;
			*s++ = isc_bad_db_handle;
			*s = isc_arg_end;
			return error(status, local);
		}
		if (CALL(PROC_START_TRANSACTION, database->implementation) (status,
																	ptr,
																	1,
																	&database->
																	handle,
																	vector->
																	teb_tpb_length,
																	vector->
																	teb_tpb))
		{
			while (sub = transaction) {
				transaction = sub->next;
				CALL(PROC_ROLLBACK, sub->implementation) (temp, &sub->handle);
				release_handle(sub);
			}
			return error(status, local);
		}

		sub = allocate_handle(	database->implementation,
								*ptr,
								HANDLE_transaction);
		if (!sub)
		{
			/* No memory. Make a half-hearted attempt to rollback all sub-transactions. */

			CALL(PROC_ROLLBACK, database->implementation) (temp, ptr);
			*ptr = 0;
			while (sub = transaction) {
				transaction = sub->next;
				CALL(PROC_ROLLBACK, sub->implementation) (temp, &sub->handle);
				release_handle(sub);
			}
			status[0] = isc_arg_gds;
			status[1] = isc_virmemexh;
			status[2] = isc_arg_end;
			return error(status, local);
		}

		sub->parent = database;
		*ptr = sub;
	}

	if (transaction->next)
	{
		sub = allocate_handle(SUBSYSTEMS, (class jrd_tra *)0, HANDLE_transaction);
		if (!sub)
		{
			/* No memory. Make a half-hearted attempt to rollback all sub-transactions. */

			while (sub = transaction) {
				transaction = sub->next;
				CALL(PROC_ROLLBACK, sub->implementation) (temp, &sub->handle);
				release_handle(sub);
			}
			status[0] = isc_arg_gds;
			status[1] = isc_virmemexh;
			status[2] = isc_arg_end;
			return error(status, local);
		}

		sub->next = transaction;
		*tra_handle = sub;
	}
	else
		*tra_handle = transaction;

	RETURN_SUCCESS;
}


STATUS API_ROUTINE_VARARG GDS_START_TRANSACTION(STATUS * user_status,
												WHY_TRA * tra_handle,
												SSHORT count, ...)
{
/**************************************
 *
 *	g d s _ $ s t a r t _ t r a n s a c t i o n
 *
 **************************************
 *
 * Functional description
 *	Start a transaction.
 *
 **************************************/
	TEB tebs[16], *teb, *end;
	STATUS status;
	va_list ptr;

	if (GDS_VAL(count) <= FB_NELEM(tebs))
		teb = tebs;
	else
		teb = (TEB *) alloc((SLONG) (sizeof(struct teb) * GDS_VAL(count)));

	if (!teb) {
		user_status[0] = isc_arg_gds;
		user_status[1] = status = isc_virmemexh;
		user_status[2] = isc_arg_end;
		return status;
	}

	end = teb + GDS_VAL(count);
	VA_START(ptr, count);

	for (; teb < end; teb++) {
		teb->teb_database = va_arg(ptr, WHY_ATT *);
		teb->teb_tpb_length = va_arg(ptr, int);
		teb->teb_tpb = va_arg(ptr, UCHAR *);
	}

	teb = end - GDS_VAL(count);

	status = GDS_START_MULTIPLE(user_status, tra_handle, count, teb);

	if (teb != tebs)
		free_block(teb);

	return status;
}


STATUS API_ROUTINE GDS_TRANSACT_REQUEST(STATUS * user_status,
										WHY_ATT * db_handle,
										WHY_TRA * tra_handle,
										USHORT blr_length,
										SCHAR * blr,
										USHORT in_msg_length,
										SCHAR * in_msg,
										USHORT out_msg_length,
										SCHAR * out_msg)
{
/**************************************
 *
 *	i s c _ t r a n s a c t _ r e q u e s t
 *
 **************************************
 *
 * Functional description
 *	Execute a procedure.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status;
	WHY_ATT dbb;
	WHY_TRA transaction;

	GET_STATUS;
	dbb = *db_handle;
	CHECK_HANDLE(dbb, HANDLE_database, isc_bad_db_handle);
	transaction = *tra_handle;
	CHECK_HANDLE(transaction, HANDLE_transaction, isc_bad_trans_handle);
	subsystem_enter();

	if (CALL(PROC_TRANSACT_REQUEST, dbb->implementation) (status,
														  &dbb->handle,
														  &transaction->
														  handle, blr_length,
														  blr, in_msg_length,
														  in_msg,
														  out_msg_length,
														  out_msg)) return
			error(status, local);

	RETURN_SUCCESS;
}


STATUS API_ROUTINE gds__transaction_cleanup(STATUS * user_status,
											WHY_TRA * tra_handle,
											TransactionCleanupRoutine *routine, SLONG arg)
{
/**************************************
 *
 *	g d s _ $ t r a n s a c t i o n _ c l e a n u p
 *
 **************************************
 *
 * Functional description
 *	Register a transaction specific cleanup handler.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status, *s;
	WHY_TRA transaction;
	CLEAN clean;

	GET_STATUS;
	transaction = *tra_handle;
	if (!transaction || transaction->type != HANDLE_transaction) {
		s = status;
		*s++ = isc_arg_gds;
		*s++ = isc_bad_db_handle;
		*s = isc_arg_end;
		return error2(status, local);
	}

/* Only add the cleanup handler if the transaction doesn't already know
   about it. */

	for (clean = transaction->cleanup; clean; clean = clean->clean_next)
	{
		if (clean->TransactionRoutine == routine && clean->clean_arg == arg)
		{
			break;
		}
	}

	if (!clean)
	{
		if (clean = (CLEAN) alloc((SLONG) sizeof(struct clean)))
		{
#ifdef DEBUG_GDS_ALLOC
			/* If client doesn't commit/rollback/detach
			   or drop, this could be left unfreed. */

			gds_alloc_flag_unfreed((void *) clean);
#endif
			clean->clean_next = transaction->cleanup;
			transaction->cleanup = clean;
			clean->TransactionRoutine = routine;
			clean->clean_arg = arg;
		}
		else {
			status[0] = isc_arg_gds;
			status[1] = isc_virmemexh;
			status[2] = isc_arg_end;
			return error2(status, local);
		}
	}

	status[0] = gds_arg_gds;
	status[1] = FB_SUCCESS;
	status[2] = gds_arg_end;
	CHECK_STATUS_SUCCESS(status);
	return FB_SUCCESS;
}


STATUS API_ROUTINE GDS_TRANSACTION_INFO(STATUS * user_status,
										WHY_TRA * tra_handle,
										SSHORT GDS_VAL(item_length),
										SCHAR * items,
										SSHORT GDS_VAL(buffer_length),
										UCHAR * buffer)
{
/**************************************
 *
 *	g d s _ $ t r a n s a c t i o n _ i n f o
 *
 **************************************
 *
 * Functional description
 *	Provide information on transaction object.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status;
	WHY_TRA transaction, sub;
	UCHAR *ptr, *end;
	SSHORT buffer_len, item_len;

	GET_STATUS;
	transaction = *tra_handle;
	CHECK_HANDLE(transaction, HANDLE_transaction, isc_bad_trans_handle);
	subsystem_enter();

	if (transaction->implementation != SUBSYSTEMS) {
		if (CALL(PROC_TRANSACTION_INFO, transaction->implementation) (status,
																	  &transaction->
																	  handle,
																	  GDS_VAL
																	  (item_length),
																	  items,
																	  GDS_VAL
																	  (buffer_length),
																	  buffer))
				return error(status, local);
	}
	else {
		item_len = GDS_VAL(item_length);
		buffer_len = GDS_VAL(buffer_length);
		for (sub = transaction->next; sub; sub = sub->next) {
			if (CALL(PROC_TRANSACTION_INFO, sub->implementation) (status,
																  &sub->
																  handle,
																  item_len,
																  items,
																  buffer_len,
																  buffer))
					return error(status, local);

			ptr = buffer;
			end = buffer + buffer_len;
			while (ptr < end && *ptr == gds__info_tra_id)
				ptr += 3 + gds__vax_integer(ptr + 1, 2);

			if (ptr >= end || *ptr != gds__info_end) {
				RETURN_SUCCESS;
			}

			buffer_len = end - ptr;
			buffer = ptr;
		}
	}

	RETURN_SUCCESS;
}


STATUS API_ROUTINE GDS_UNWIND(STATUS * user_status,
							  WHY_REQ * req_handle, SSHORT GDS_VAL(level))
{
/**************************************
 *
 *	g d s _ $ u n w i n d
 *
 **************************************
 *
 * Functional description
 *	Unwind a running request.  This is potentially nasty since it can be called
 *	asynchronously.
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status;
	WHY_REQ request;

	GET_STATUS;
	request = *req_handle;
	CHECK_HANDLE(request, HANDLE_request, isc_bad_req_handle);
	subsystem_enter();

	if (CALL(PROC_UNWIND, request->implementation) (status,
													&request->handle,
													GDS_VAL(level)))
			return error(status, local);

	RETURN_SUCCESS;
}


static SCHAR *alloc(SLONG length)
{
/**************************************
 *
 *	a l l o c
 *
 **************************************
 *
 * Functional description
 *	Allocate some memory.
 *
 **************************************/
	SCHAR *block;

	if (block = reinterpret_cast<SCHAR *>(gds__alloc((SLONG) (sizeof(SCHAR) * length))))
		memset(block, 0, length);
	return block;
}


static STATUS bad_handle(STATUS * user_status, STATUS code)
{
/**************************************
 *
 *	b a d _ h a n d l e
 *
 **************************************
 *
 * Functional description
 *	Generate an error for a bad handle.
 *
 **************************************/
	STATUS *s, *status, local[ISC_STATUS_LENGTH];

	GET_STATUS;
	s = status;
	*s++ = isc_arg_gds;
	*s++ = code;
	*s = isc_arg_end;

	return error2(status, local);
}


#ifdef DEV_BUILD
static void check_status_vector(STATUS * status, STATUS expected)
 {
/**************************************
 *
 *	c h e c k _ s t a t u s _ v e c t o r
 *
 **************************************
 *
 * Functional description
 *	Validate that a status vector looks valid.
 *
 **************************************/

	STATUS *s, code;
	ULONG length;

#define SV_MSG(x)	{ ib_fprintf (ib_stderr, "%s %d check_status_vector: %s\n", __FILE__, __LINE__, (x)); BREAKPOINT (__LINE__); }

	s = status;
	if (!s) {
		SV_MSG("Invalid status vector");
		return;
	}

	if (*s != gds_arg_gds) {
		SV_MSG("Must start with gds_arg_gds");
		return;
	}

/* Vector [2] could either end the vector, or start a warning 
   in either case the status vector is a success */
	if ((expected == FB_SUCCESS)
		&& (s[1] != FB_SUCCESS
			|| (s[2] != gds_arg_end && s[2] != gds_arg_gds
				&& s[2] !=
				isc_arg_warning))) SV_MSG("Success vector expected");

	while (*s != gds_arg_end) {
		code = *s++;
		switch (code) {
		case isc_arg_warning:
		case gds_arg_gds:
			/* The next element must either be 0 (indicating no error) or a
			 * valid isc error message, let's check */
			if (*s && (*s & ISC_MASK) != ISC_MASK) {
				if (code == isc_arg_warning) {
					SV_MSG("warning code not a valid ISC message");
				}
				else {
					SV_MSG("error code not a valid ISC message");
				}
			}

			/* If the error code is valid, then I better be able to retrieve a
			 * proper facility code from it ... let's find out */
			if (*s && (*s & ISC_MASK) == ISC_MASK) {
				const struct _facilities *facs;
				int fac_code;
				BOOLEAN found = 0;

				facs = facilities;
				fac_code = GET_FACILITY(*s);
				while (facs->facility) {
					if (facs->fac_code == fac_code) {
						found = 1;
						break;
					}
					facs++;
				}
				if (!found)
					if (code == isc_arg_warning) {
						SV_MSG
							("warning code does not contain a valid facility");
					}
					else {
						SV_MSG
							("error code does not contain a valid facility");
					}
			}
			s++;
			break;

		case gds_arg_interpreted:
		case gds_arg_string:
			length = strlen((char *) *s);
			/* This check is heuristic, not deterministic */
			if (length > 1024 - 1)
				SV_MSG("suspect length value");
			if (*((UCHAR *) * s) == 0xCB)
				SV_MSG("string in freed memory");
			s++;
			break;

		case gds_arg_cstring:
			length = (ULONG) * s;
			s++;
			/* This check is heuristic, not deterministic */
			/* Note: This can never happen anyway, as the length is coming
			   from a byte value */
			if (length > 1024 - 1)
				SV_MSG("suspect length value");
			if (*((UCHAR *) * s) == 0xCB)
				SV_MSG("string in freed memory");
			s++;
			break;

		case gds_arg_number:
		case gds_arg_vms:
		case gds_arg_unix:
		case gds_arg_win32:
			s++;
			break;

		default:
			SV_MSG("invalid status code");
			return;
		}
		if ((s - status) >= 20)
			SV_MSG("vector too long");
	}

#undef SV_MSG

}
#endif


static STATUS error(STATUS * user_status, STATUS * local)
{
/**************************************
 *
 *	e r r o r
 *
 **************************************
 *
 * Functional description
 *	An error returned has been trapped.  If the user specified
 *	a status vector, return a status code.  Otherwise print the
 *	error code(s) and abort.
 *
 **************************************/

	subsystem_exit();

	return error2(user_status, local);
}


static STATUS error2(STATUS * user_status, STATUS * local)
{
/**************************************
 *
 *	e r r o r 2
 *
 **************************************
 *
 * Functional description
 *	An error returned has been trapped.  If the user specified
 *	a status vector, return a status code.  Otherwise print the
 *	error code(s) and abort.
 *
 **************************************/

	CHECK_STATUS(user_status);

#ifdef SUPERSERVER
	return user_status[1];
#else
	if (user_status != local)
		return user_status[1];

	gds__print_status(user_status);
	exit((int) user_status[1]);

	return FB_SUCCESS;
#endif
}


#ifndef REQUESTER
static void event_ast(UCHAR * buffer, USHORT length, UCHAR * items)
{
/**************************************
 *
 *	e v e n t _ a s t
 *
 **************************************
 *
 * Functional description
 *	We're had an event complete.
 *
 **************************************/

	while (length--)
		*buffer++ = *items++;
	ISC_event_post(why_event);
}
#endif


#ifndef REQUESTER
static void exit_handler(EVENT why_event)
{
/**************************************
 *
 *	e x i t _ h a n d l e r
 *
 **************************************
 *
 * Functional description
 *	Cleanup shared image.
 *
 **************************************/

#ifdef WIN_NT
	CloseHandle((void *) why_event->event_handle);
#endif

	why_initialized = FALSE;
	why_enabled = 0;
#if !(defined REQUESTER || defined SUPERCLIENT || defined SUPERSERVER)
	isc_enter_count = 0;
	subsystem_usage = 0;
	subsystem_FPE_reset = FPE_RESET_INIT_ONLY;
#endif
}
#endif


static WHY_TRA find_transaction(WHY_DBB dbb, WHY_TRA transaction)
{
/**************************************
 *
 *	f i n d _ t r a n s a c t i o n
 *
 **************************************
 *
 * Functional description
 *	Find the element of a possible multiple database transaction
 *	that corresponds to the current database.
 *
 **************************************/

	for (; transaction; transaction = transaction->next)
		if (transaction->parent == dbb)
			return transaction;

	return NULL;
}


static void free_block(void* block)
{
/**************************************
 *
 *	f r e e _ b l o c k
 *
 **************************************
 *
 * Functional description
 *	Release some memory.
 *
 **************************************/

	gds__free((SLONG *) block);
}


static int get_database_info(STATUS * status, WHY_TRA transaction, TEXT ** ptr)
{
/**************************************
 *
 *	g e t _ d a t a b a s e _ i n f o
 *
 **************************************
 *
 * Functional description
 *	Get the full database pathname
 *	and put it in the transaction
 *	description record.
 *
 **************************************/
	TEXT *p, *q;
	WHY_DBB database;

	p = *ptr;

	database = transaction->parent;
	q = database->db_path;
	*p++ = TDR_DATABASE_PATH;
	*p++ = (TEXT) strlen(reinterpret_cast<SCHAR *>(q));
	while (*q)
		*p++ = *q++;

	*ptr = p;

	return FB_SUCCESS;
}


static const PTR get_entrypoint(int proc, int implementation)
{
/**************************************
 *
 *	g e t _ e n t r y p o i n t
 *
 **************************************
 *
 * Functional description
 *	Lookup entrypoint for procedure.
 *
 **************************************/

#if !defined(SUPERCLIENT)
	const TEXT *name;
	TEXT *image;
#endif

	const ENTRY *ent = entrypoints + implementation * PROC_count + proc;
	const PTR entrypoint = ent->address;

	if (entrypoint)
	{
		return entrypoint;
	}

#ifndef SUPERCLIENT
	image = images[implementation].path;
	name = ent->name;
	if (!name)
	{
		name = generic[proc];
	}

	if (image && name)
	{
#define BufSize 128
		TEXT Buffer[BufSize];
		SLONG NameLength = strlen(name) + 1;
		TEXT *NamePointer = NameLength > BufSize ? 
			reinterpret_cast<TEXT *>(gds__alloc(NameLength)) : Buffer;
		memcpy(NamePointer, name, NameLength);
		PTR entry = (PTR) ISC_lookup_entrypoint(image, NamePointer, NULL);
		if (NameLength > BufSize)
			gds__free(NamePointer);
		if (entry)
		{
			// This const_cast appears to be safe, because:
			// 1. entrypoints table is modified ONLY once for each entry
			// 2. even when some threads try to modify it concurrently,
			//	  they will write SAME results in that table
			*const_cast<PTR*>(&ent->address) = entry;
			return entry;
		}
	}
#endif

	return &no_entrypoint;
}


static SCHAR *get_sqlda_buffer(SCHAR * buffer,
							   USHORT local_buffer_length,
							   XSQLDA * sqlda,
							   USHORT dialect, USHORT * buffer_length)
{
/**************************************
 *
 *	g e t _ s q l d a _ b u f f e r
 *
 **************************************
 *
 * Functional description
 *	Get a buffer that is large enough to store
 *	the info items relating to an SQLDA.
 *
 **************************************/
	USHORT n_variables;
	SLONG length;
	USHORT sql_dialect;

/* If dialect / 10 == 0, then it has not been combined with the
   parser version for a prepare statement.  If it has been combined, then
   the dialect needs to be pulled out to compare to DIALECT_xsqlda
*/

	if ((sql_dialect = dialect / 10) == 0)
		sql_dialect = dialect;

	if (!sqlda)
		n_variables = 0;
	else if (sql_dialect >= DIALECT_xsqlda)
		n_variables = sqlda->sqln;
	else
		n_variables = ((SQLDA *) sqlda)->sqln;

	length = 32 + n_variables * 172;
	*buffer_length = (USHORT)((length > 65500L) ? 65500L : length);
	if (*buffer_length > local_buffer_length)
		buffer = alloc((SLONG) * buffer_length);

	return buffer;
}


static STATUS get_transaction_info(STATUS * status,
								   WHY_TRA transaction, TEXT ** ptr)
{
/**************************************
 *
 *	g e t _ t r a n s a c t i o n _ i n f o
 *
 **************************************
 *
 * Functional description
 *	Put a transaction's id into the transaction
 *	description record.
 *
 **************************************/
	TEXT *p, *q, buffer[16];
	USHORT length;

	p = *ptr;

	if (CALL(PROC_TRANSACTION_INFO, transaction->implementation) (status,
																  &transaction->
																  handle,
																  sizeof
																  (prepare_tr_info),
																  prepare_tr_info,
																  sizeof
																  (buffer),
																  buffer)) {
		CHECK_STATUS(status);
		return status[1];
	}

	q = buffer + 3;
	*p++ = TDR_TRANSACTION_ID;

	length = (USHORT)gds__vax_integer(reinterpret_cast<UCHAR*>(buffer + 1), 2);
	*p++ = length;
	if (length) {
		do {
			*p++ = *q++;
		} while (--length);
	}

	*ptr = p;

	CHECK_STATUS_SUCCESS(status);
	return FB_SUCCESS;
}


static void iterative_sql_info(STATUS * user_status,
							   WHY_STMT * stmt_handle,
							   SSHORT item_length,
							   const SCHAR * items,
							   SSHORT buffer_length,
							   SCHAR * buffer, USHORT dialect, XSQLDA * sqlda)
{
/**************************************
 *
 *	i t e r a t i v e _ s q l _ i n f o
 *
 **************************************
 *
 * Functional description
 *	Turn an sql info buffer into an SQLDA.  If the info
 *	buffer was incomplete, make another request, beginning
 *	where the previous info call left off.
 *
 **************************************/
	USHORT last_index;
	SCHAR new_items[32], *p;

	while (UTLD_parse_sql_info(	user_status,
								dialect,
								buffer,
								sqlda,
								&last_index) && last_index)
	{
		p = new_items;
		*p++ = gds__info_sql_sqlda_start;
		*p++ = 2;
		*p++ = last_index;
		*p++ = last_index >> 8;
		memcpy(p, items, (int) item_length);
		p += item_length;
		if (GDS_DSQL_SQL_INFO(	user_status,
								stmt_handle,
								(SSHORT) (p - new_items),
								new_items,
								buffer_length,
								buffer))
		{
			break;
		}
	}
}

static STATUS open_blob(STATUS * user_status,
						WHY_ATT * db_handle,
						WHY_TRA * tra_handle,
						WHY_BLB * blob_handle,
						SLONG * blob_id,
						USHORT bpb_length,
						UCHAR * bpb, SSHORT proc, SSHORT proc2)
{
/**************************************
 *
 *	o p e n _ b l o b
 *
 **************************************
 *
 * Functional description
 *	Open an existing blob (extended edition).
 *
 **************************************/
	STATUS local[ISC_STATUS_LENGTH], *status;
	WHY_TRA transaction;
	WHY_ATT dbb;
	WHY_BLB blob;
	USHORT from, to;
	USHORT flags;

	GET_STATUS;
	NULL_CHECK(blob_handle, isc_bad_segstr_handle, HANDLE_blob);

	dbb = *db_handle;
	CHECK_HANDLE(dbb, HANDLE_database, isc_bad_db_handle);
	transaction = find_transaction(dbb, *tra_handle);
	CHECK_HANDLE(transaction, HANDLE_transaction, isc_bad_trans_handle);
	subsystem_enter();

	flags = 0;
	gds__parse_bpb(bpb_length, bpb, &from, &to);

	if (get_entrypoint(proc2, dbb->implementation) != no_entrypoint &&
		CALL(proc2, dbb->implementation) (status,
										  &dbb->handle,
										  &transaction->handle,
										  blob_handle,
										  blob_id,
										  bpb_length,
										  bpb) != isc_unavailable) flags = 0;
	else if (!to || from == to)
		CALL(proc, dbb->implementation) (status,
										 &dbb->handle,
										 &transaction->handle,
										 blob_handle, blob_id);

	if (status[1]) {
		return error(status, local);
	}

	blob = allocate_handle(dbb->implementation, *blob_handle, HANDLE_blob);
	if (!blob)
	{
		/* No memory. Make a half-hearted attempt to cancel the blob. */

		CALL(PROC_CANCEL_BLOB, dbb->implementation) (status, blob_handle);
		status[0] = isc_arg_gds;
		status[1] = isc_virmemexh;
		status[2] = isc_arg_end;
		*blob_handle = 0;
		return error(status, local);
	}

	*blob_handle = blob;
	blob->flags |= flags;
	blob->parent = dbb;
	blob->next = dbb->blobs;
	dbb->blobs = blob;

	RETURN_SUCCESS;
}


#ifdef UNIX
static STATUS open_marker_file(STATUS * status,
							   TEXT * expanded_filename, TEXT * single_user)
{
/*************************************
 *
 *	o p e n _ m a r k e r _ f i l e
 *
 *************************************
 *
 * Functional description
 *	Try to open a marker file.  If one is
 *	found, open it, read the absolute path
 *	to the NFS mounted database, lockf()
 *	the marker file to ensure single user
 *	access to the db and write the open marker
 *	file descriptor into the marker file so
 *	that the file can be closed in
 *	close_marker_file located in unix.c.
 *	Return FB_FAILURE if a marker file exists
 *	but something goes wrong.  Return FB_SUCCESS
 *	otherwise.
 *
 *************************************/
	int fd, i, j;
	TEXT marker_filename[MAXPATHLEN], marker_contents[MAXPATHLEN],
		fildes_str[5], *p;
	TEXT *err_routine, buffer[80];
	SLONG bytes, size;

/* Create the marker file name and see if it exists.  If not,
   don't sweat it. */

	strcpy(marker_filename, expanded_filename);
	strcat(marker_filename, "_m");
	if (access(marker_filename, F_OK))	/* Marker file doesn't exist. */
		return FB_SUCCESS;

/* Ensure that writes are ok on the marker file for lockf(). */

	if (!access(marker_filename, W_OK)) {
		for (i = 0; i < IO_RETRY; i++) {
			if ((fd = open(marker_filename, O_RDWR)) == -1) {
				sprintf(buffer,
						"Couldn't open marker file %s\n", marker_filename);
				gds__log(buffer);
				err_routine = "open";
				break;
			}

			/* Place an advisory lock on the marker file. */
#ifdef HAVE_FLOCK
			if (flock(fd, LOCK_EX ) != -1) {
#else
			if (lockf(fd, F_TLOCK, 0) != -1) {
#endif
				size = sizeof(marker_contents);
				for (j = 0; j < IO_RETRY; j++) {
					if ((bytes = read(fd, marker_contents, size)) != -1)
						break;

					if ((bytes == -1) && (!SYSCALL_INTERRUPTED(errno))) {
						err_routine = "read";
						close(fd);
						fd = -1;
					}
				}				/* for (j < IO_RETRY ) */

				p = strchr(marker_contents, '\n');
				*p = 0;
				if (strcmp(expanded_filename, marker_contents))
					close(fd);
				else {
					sprintf(fildes_str, "%d\n", fd);
					strcpy(single_user, "YES");
					size = strlen(fildes_str);
					for (j = 0; j < IO_RETRY; j++) {
						if (lseek(fd, LSEEK_OFFSET_CAST 0L, SEEK_END) == -1) {
							err_routine = "lseek";
							close(fd);
							fd = -1;
						}

						if ((bytes = write(fd, fildes_str, size)) == size)
							break;

						if ((bytes == -1) && (!SYSCALL_INTERRUPTED(errno))) {
							err_routine = "write";
							close(fd);
							fd = -1;
						}
					}			/* for (j < IO_RETRY ) */
				}
			}
			else
			{				/* Else couldn't lockf(). */

				sprintf(buffer,
						"Marker file %s already opened by another user\n",
						marker_filename);
				gds__log(buffer);
				close(fd);
				fd = -1;
			}

			if (!SYSCALL_INTERRUPTED(errno))
			{
				break;
			}
		}						/* for (i < IO_RETRY ) */
	}							/* if (access (...)) */
	else
	{						/* Else the marker file exists, but can't write to it. */

		sprintf(buffer,
				"Must have write permission on marker file %s\n",
				marker_filename);
		gds__log(buffer);
		err_routine = "access";
		fd = -1;
	}

	if (fd != -1)
		return FB_SUCCESS;

/* The following code saves the name of the offending marker
   file in a (sort of) permanent location.  It is totally specific
   because this is the only dynamic string being returned in
   a status vector in this entire module.  Since the marker
   feature will almost never be used, it's not worth saving the
   information in a more general way. */

	if (marker_failures_ptr + strlen(marker_filename) + 1 >
		marker_failures + sizeof(marker_failures) - 1)
		marker_failures_ptr = marker_failures;

	*status++ = isc_arg_gds;
	*status++ = isc_io_error;
	*status++ = isc_arg_string;
	*status++ = (STATUS) err_routine;
	*status++ = isc_arg_string;
	*status++ = (STATUS) marker_failures_ptr;
	*status++ = isc_arg_unix;
	*status++ = errno;
	*status = isc_arg_end;

	strcpy(marker_failures_ptr, marker_filename);
	marker_failures_ptr += strlen(marker_filename) + 1;

	return FB_FAILURE;
}
#endif


static STATUS no_entrypoint(STATUS * user_status, ...)
{
/**************************************
 *
 *	n o _ e n t r y p o i n t
 *
 **************************************
 *
 * Functional description
 *	No_entrypoint is called if there is not entrypoint for a given routine.
 *
 **************************************/

	*user_status++ = isc_arg_gds;
	*user_status++ = isc_unavailable;
	*user_status = isc_arg_end;

	return isc_unavailable;
}


static STATUS prepare(STATUS * status, WHY_TRA transaction)
{
/**************************************
 *
 *	p r e p a r e
 *
 **************************************
 *
 * Functional description
 *	Perform the first phase of a two-phase commit 
 *	for a multi-database transaction.
 *
 **************************************/
	WHY_TRA sub;
	TEXT *p, *description;
	TEXT tdr_buffer[1024];
	USHORT length = 0;

	for (sub = transaction->next; sub; sub = sub->next)
		length += 256;

	description =
		(length >
		 sizeof(tdr_buffer)) ? (TEXT *) gds__alloc((SLONG) length) :
		tdr_buffer;

/* build a transaction description record containing 
   the host site and database/transaction
   information for the target databases. */

	if (!(p = description)) {
		status[0] = isc_arg_gds;
		status[1] = isc_virmemexh;
		status[2] = isc_arg_end;
		CHECK_STATUS(status);
		return status[1];
	}
	*p++ = TDR_VERSION;

	ISC_get_host(p + 2, length - 16);
	*p++ = TDR_HOST_SITE;
	*p = (UCHAR) strlen(reinterpret_cast<SCHAR *>(p) + 1);

	while (*++p);

/* Get database and transaction stuff for each sub-transaction */

	for (sub = transaction->next; sub; sub = sub->next) {
		get_database_info(status, sub, &p);
		get_transaction_info(status, sub, &p);
	}

/* So far so good -- prepare each sub-transaction */

	length = p - description;

	for (sub = transaction->next; sub; sub = sub->next)
		if (CALL(PROC_PREPARE, sub->implementation) (status,
													 &sub->handle,
													 length, description))
		{
			if (description != tdr_buffer) {
				free_block(description);
			}
			CHECK_STATUS(status);
			return status[1];
		}

	if (description != tdr_buffer)
		free_block(description);

	CHECK_STATUS_SUCCESS(status);
	return FB_SUCCESS;
}


static void why_priv_gds__free_if_set(void* pMem)
{
	if (pMem) {
		gds__free(pMem);
	}
}

static void release_dsql_support(DASUP dasup)
{
/**************************************
 *
 *	r e l e a s e _ d s q l _ s u p p o r t
 *
 **************************************
 *
 * Functional description
 *	Release some memory.
 *
 **************************************/

	struct dasup::dasup_clause* pClauses;

	if (!dasup) {
		return;
	}

	/* for C++, add "dasup::" before "dasup_clause" */
	pClauses = dasup->dasup_clauses;

	why_priv_gds__free_if_set(pClauses[DASUP_CLAUSE_bind].dasup_blr);
	why_priv_gds__free_if_set(pClauses[DASUP_CLAUSE_select].dasup_blr);
	why_priv_gds__free_if_set(pClauses[DASUP_CLAUSE_bind].dasup_msg);
	why_priv_gds__free_if_set(pClauses[DASUP_CLAUSE_select].dasup_msg);
	free_block(dasup);
}


static void release_handle(WHY_HNDL handle)
{
/**************************************
 *
 *	r e l e a s e _ h a n d l e
 *
 **************************************
 *
 * Functional description
 *	Release unused and unloved handle.
 *
 **************************************/

	handle->type = HANDLE_invalid;
	free_block(handle);
}


static void save_error_string(STATUS * status)
{
/**************************************
 *
 *	s a v e _ e r r o r _ s t r i n g  
 *
 **************************************
 *
 * Functional description
 *	This is need because there are cases
 *	where the memory allocated for strings 
 *	in the status vector is freed prior to 
 *	surfacing them to the user.  This is an
 *	attempt to save off 1 string to surface to
 *  	the user.  Any other strings will be set to
 *	a standard <Unknown> string.
 *
 **************************************/
	TEXT *p;
	ULONG l, len;

	assert(status != NULL);

	p = glbstr1;
	len = sizeof(glbstr1) - 1;

	while (*status != isc_arg_end)
	{
		switch (*status++)
		{
		case isc_arg_cstring:
			l = (ULONG) * status;
			if (l < len)
			{
				status++;		/* Length is unchanged */
				/* 
				 * This strncpy should really be a memcpy
				 */
				strncpy(p, reinterpret_cast<char*>(*status), l);
				*status++ = (STATUS) p;	/* string in static memory */
				p += l;
				len -= l;
			}
			else {
				*status++ = (STATUS) strlen(glbunknown);
				*status++ = (STATUS) glbunknown;
			}
			break;

		case isc_arg_interpreted:
		case isc_arg_string:
			l = (ULONG) strlen(reinterpret_cast<char*>(*status)) + 1;
			if (l < len)
			{
				strncpy(p, reinterpret_cast<char*>(*status), l);
				*status++ = (STATUS) p;	/* string in static memory */
				p += l;
				len -= l;
			}
			else
			{
				*status++ = (STATUS) glbunknown;
			}
			break;

		default:
			assert(FALSE);
		case isc_arg_gds:
		case isc_arg_number:
		case isc_arg_vms:
		case isc_arg_unix:
		case isc_arg_win32:
			status++;			/* Skip parameter */
			break;
		}
	}
}


static void subsystem_enter(void)
{
/**************************************
 *
 *	s u b s y s t e m _ e n t e r
 *
 **************************************
 *
 * Functional description
 *	Enter subsystem.
 *
 **************************************/

#ifdef EMBEDDED
	THD_INIT;
#endif

	THREAD_ENTER;
#if !(defined REQUESTER || defined SUPERCLIENT || defined SUPERSERVER)
	isc_enter_count++;
	if (subsystem_usage == 0 ||
		(subsystem_FPE_reset &
		 (FPE_RESET_NEXT_API_CALL | FPE_RESET_ALL_API_CALL)))
	{
		ISC_enter();
		subsystem_FPE_reset &= ~FPE_RESET_NEXT_API_CALL;
	}
#endif

#ifdef DEBUG_FPE_HANDLING
	{
/* It's difficult to make a FPE to occur inside the engine - for debugging
 * just force one to occur every-so-often. */
		static ULONG counter = 0;
		if (((counter++) % 10) == 0)
		{
			ib_fprintf(ib_stderr, "Forcing FPE to occur within engine\n");
			(void) kill(getpid(), SIGFPE);
		}
	}
#endif /* DEBUG_FPE_HANDLING */
}


static void subsystem_exit(void)
{
/**************************************
 *
 *	s u b s y s t e m _ e x i t
 *
 **************************************
 *
 * Functional description
 *	Exit subsystem.
 *
 **************************************/

#if !(defined REQUESTER || defined SUPERCLIENT || defined SUPERSERVER)
	if (subsystem_usage == 0 ||
		(subsystem_FPE_reset &
		 (FPE_RESET_NEXT_API_CALL | FPE_RESET_ALL_API_CALL)))
	{
		ISC_exit();
	}
	isc_enter_count--;
#endif
	THREAD_EXIT;
}


#if defined (SERVER_SHUTDOWN) && !defined (SUPERCLIENT) && !defined (REQUESTER)
BOOLEAN WHY_set_shutdown(BOOLEAN flag)
{
/**************************************
 *
 *	W H Y _ s e t _ s h u t d o w n
 *
 **************************************
 *
 * Functional description
 *	Set shutdown_flag to either TRUE or FALSE.
 *		TRUE = accept new connections
 *		FALSE= refuse new connections
 *	Returns the prior state of the flag (server).
 *
 **************************************/

	BOOLEAN old_flag;
	old_flag = shutdown_flag;
	shutdown_flag = flag;
	return old_flag;
}

BOOLEAN WHY_get_shutdown()
{
/**************************************
 *
 *	W H Y _ g e t _ s h u t d o w n
 *
 **************************************
 *
 * Functional description
 *	Returns the current value of shutdown_flag.
 *
 **************************************/

	return shutdown_flag;
}
#endif /* SERVER_SHUTDOWN && !SUPERCLIENT && !REQUESTER */

} // "C"

static WHY_HNDL allocate_handle(int		implementation,
								int		handle_type)
{
/**************************************
 *
 *	a l l o c a t e _ h a n d l e
 *
 **************************************
 *
 * Functional description
 *	Allocate an indirect handle.
 *
 **************************************/
	WHY_HNDL handle;

	if (handle = (WHY_HNDL) alloc((SLONG) sizeof(why_hndl)))
	{
		handle->implementation = implementation;
		handle->type = handle_type;

#ifdef DEBUG_GDS_ALLOC
/* As the memory for the handle is handed back to the client, InterBase
 * cannot free the memory unless the client returns to us.  As a result,
 * the memory allocator might try to report this as unfreed, but it
 * ain't our fault.  So flag it to make the allocator be happy.
 */
		gds_alloc_flag_unfreed((void *) handle);
#endif
	}

	return handle;
}

