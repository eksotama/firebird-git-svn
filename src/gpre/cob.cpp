//____________________________________________________________
//  
//		PROGRAM:	General preprocessor
//		MODULE:		cob.cpp
//		DESCRIPTION:	COBOL text generator
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
//  Solaris x86 changes - Konstantin Kuznetsov, Neil McCalden
//  8-Mar-2002 FSG (Frank Schlottmann-G�dde) tiny cobol support
//             fixed Bug No. 526204*
//  
//
//____________________________________________________________
//
//	$Id: cob.cpp,v 1.53 2004-11-10 04:32:11 robocop Exp $
//
// 2002.10.27 Sean Leyne - Completed removal of obsolete "DG_X86" port
// 2002.10.27 Sean Leyne - Code Cleanup, removed obsolete "UNIXWARE" port
//
// 2002.10.28 Sean Leyne - Completed removal of obsolete "DGUX" port
// 2002.10.28 Sean Leyne - Completed removal of obsolete "HP700" port
//
//

#include "firebird.h"
#include <stdio.h>
#include "../jrd/common.h"
#include <stdarg.h>
#include "../jrd/ibase.h"
#include "../jrd/gds_proto.h"
#include "../gpre/gpre.h"
#include "../gpre/pat.h"
#include "../gpre/cmp_proto.h"
#include "../gpre/lang_proto.h"
#include "../gpre/pat_proto.h"
#include "../gpre/prett_proto.h"
#include "../gpre/gpre_proto.h"

#ifdef HAVE_STRING_H
#include <string.h>
#endif

//  vms requires all caps in function call names; hp requires lc 

#ifdef VMS
#define GIVING_SUPPORTED
static const char* const COMMIT			= "COMMIT";
static const char* const ROLLBACK		= "ROLLBACK";
static const char* const OMITTED 		= "OMITTED";
static const char* const RAW_BLR_TEMPLATE = "03  %s%d%s%d PIC S9(9) USAGE COMP VALUE IS %"SLONGFORMAT".";
static const char* const RAW_TPB_TEMPLATE = "03  %s%d%s%d PIC S9(9) USAGE COMP VALUE IS %"SLONGFORMAT".";
static const char* const BY_VALUE		= "BY VALUE ";
static const char* const END_VALUE		= "";
static const char* const BY_REF			= "BY REFERENCE ";
static const char* const BY_DESC			= "BY DESCRIPTOR ";
static const char* const ISC_BLOB		= "ISC_%s_BLOB";
static const char* const CLOSE			= "CLOSE";
static const char* const CANCEL			= "CANCEL";
static const char* const COMP_VALUE		= "COMP";
static const char* const ISC_CANCEL_BLOB				= "ISC_CANCEL_BLOB";
static const char* const ISC_COMPILE_REQUEST			= "ISC_COMPILE_REQUEST";
static const char* const ISC_CREATE_DATABASE			= "ISC_CREATE_DATABASE";
static const char* const ISC_DDL						= "ISC_DDL";
static const char* const ISC_COMMIT_TRANSACTION		= "ISC_COMMIT_TRANSACTION";
static const char* const ISC_ROLLBACK_TRANSACTION	= "ISC_ROLLBACK_TRANSACTION";
static const char* const ISC_DROP_DATABASE 			= "ISC_DROP_DATABASE";
static const char* const ISC_CLOSE 					= "ISC_EMBED_DSQL_CLOSE";
static const char* const ISC_DECLARE 		= "ISC_EMBED_DSQL_DECLARE";
static const char* const ISC_DESCRIBE 		= "ISC_EMBED_DSQL_DESCRIBE";
static const char* const ISC_DESCRIBE_BIND	= "ISC_EMBED_DSQL_DESCRIBE_BIND";
static const char* const ISC_EXECUTE 		= "ISC_EMBED_DSQL_EXECUTE";
static const char* const ISC_EXECUTE2 		= "ISC_EMBED_DSQL_EXECUTE2";
static const char* const ISC_EXECUTE_IMMEDIATE	= "ISC_EMBED_DSQL_EXECUTE_IMMED_D";
static const char* const ISC_EXECUTE_IMMEDIATE2	= "ISC_EMBED_DSQL_EXECUTE_IMMED2_D";
static const char* const ISC_FETCH 			= "ISC_EMBED_DSQL_FETCH";
static const char* const ISC_INSERT 			= "ISC_EMBED_DSQL_INSERT";
static const char* const ISC_OPEN			= "ISC_EMBED_DSQL_OPEN";
static const char* const ISC_OPEN2			= "ISC_EMBED_DSQL_OPEN2";
static const char* const ISC_PREPARE	 		= "ISC_EMBED_DSQL_PREPARE_D";
static const char* const ISC_DSQL_ALLOCATE	= "ISC_DSQL_ALLOC_STATEMENT2";
static const char* const ISC_DSQL_EXECUTE	= "ISC_DSQL_EXECUTE_M";
static const char* const ISC_DSQL_FREE		= "ISC_DSQL_FREE_STATEMENT";
static const char* const ISC_DSQL_SET_CURSOR	= "ISC_DSQL_SET_CURSOR_NAME";
static const char* const ISC_SQLCODE_CALL	= "ISC_SQLCODE";
static const char* const ISC_DETACH_DATABASE = "ISC_DETACH_DATABASE";
static const char* const ISC_GET_SLICE 		= "ISC_GET_SLICE";
static const char* const ISC_PUT_SLICE 		= "ISC_PUT_SLICE";
static const char* const ISC_GET_SEGMENT 	= "ISC_GET_SEGMENT";
static const char* const ISC_PUT_SEGMENT 	= "ISC_PUT_SEGMENT";
static const char* const ISC_RECEIVE 		= "ISC_RECEIVE";
static const char* const ISC_RELEASE_REQUEST	= "ISC_RELEASE_REQUEST";
static const char* const ISC_UNWIND_REQUEST 	= "ISC_UNWIND_REQUEST";
static const char* const ISC_SEND 			= "ISC_SEND";
static const char* const ISC_START_TRANSACTION 	= "ISC_START_TRANSACTION";
static const char* const ISC_START_AND_SEND 	= "ISC_START_AND_SEND";
static const char* const ISC_START_REQUEST 	= "ISC_START_REQUEST";
static const char* const ISC_TRANSACT_REQUEST 	= "ISC_TRANSACT_REQUEST";
static const char* const ISC_COMMIT_RETAINING 	= "ISC_COMMIT_RETAINING";
static const char* const ISC_ATTACH_DATABASE_D 	= "ISC_ATTACH_DATABASE_D";
static const char* const ISC_ATTACH_DATABASE 	= "ISC_ATTACH_DATABASE";
static const char* const ISC_MODIFY_DPB 			= "ISC_MODIFY_DPB";
static const char* const ISC_FREE				= "ISC_FREE";
static const char* const ISC_PREPARE_TRANSACTION	= "ISC_PREPARE_TRANSACTION";
static const char* const ISC_EVENT_BLOCK		= "ISC_EVENT_BLOCK_A";
static const char* const ISC_EVENT_COUNTS	= "ISC_EVENT_COUNTS";
static const char* const ISC_EVENT_WAIT		= "ISC_EVENT_WAIT";
static const char* const ISC_BADDRESS		= "ISC_BADDRESS";

#else // VMS

static const char* const COMMIT			= "commit";
static const char* const ROLLBACK		= "rollback";

#if defined AIX || defined AIX_PPC || defined sparc || defined SOLX86 || defined HP10 || defined HP11 || defined SINIXZ || defined LINUX || defined DARWIN || defined FREEBSD || defined NETBSD || defined WIN_NT
static const char* const OMITTED 		= "BY VALUE 0";
static const char* const BY_VALUE		= "BY VALUE ";
static const char* const END_VALUE		= "";
static const char* const BY_REF			= "BY REFERENCE ";
static const char* const BY_DESC 		= "BY REFERENCE ";
static const char* const RAW_BLR_TEMPLATE	= "03  %s%d%s%d PIC XXXX USAGE COMP-X VALUE IS %"ULONGFORMAT".";
static const char* const RAW_TPB_TEMPLATE	= "03  %s%d%s%d PIC XXXX USAGE COMP-X VALUE IS %"ULONGFORMAT".";
static const char* const COMP_VALUE		= "COMP-5";
#else
static const char* const COMP_VALUE		= "COMP";
#endif // MICROFOCUS

static const char* const STRING_LENGTH	= "\"isc_embed_dsql_length\"";

static const char* const ISC_BLOB		= "isc_%s_blob";
static const char* const CLOSE			= "close";
static const char* const CANCEL			= "cancel";
static const char* const ISC_CANCEL_BLOB		= "isc_cancel_blob";
static const char* const ISC_COMPILE_REQUEST	= "isc_compile_request";
static const char* const ISC_CREATE_DATABASE	= "isc_create_database";
static const char* const ISC_DDL 			= "isc_ddl";
static const char* const ISC_COMMIT_TRANSACTION		= "isc_commit_transaction";
static const char* const ISC_ROLLBACK_TRANSACTION	= "isc_rollback_transaction";
static const char* const ISC_DROP_DATABASE 	= "isc_drop_database";
static const char* const ISC_CLOSE 			= "isc_embed_dsql_close";
static const char* const ISC_DECLARE 		= "isc_embed_dsql_declare";
static const char* const ISC_DESCRIBE 		= "isc_embed_dsql_describe";
static const char* const ISC_DESCRIBE_BIND	= "isc_embed_dsql_describe_bind";
static const char* const ISC_EXECUTE 		= "isc_embed_dsql_execute";
static const char* const ISC_EXECUTE2 		= "isc_embed_dsql_execute2";
static const char* const ISC_EXECUTE_IMMEDIATE	= "isc_embed_dsql_execute_immed";
static const char* const ISC_EXECUTE_IMMEDIATE2	= "isc_embed_dsql_execute_immed2";
static const char* const ISC_INSERT 			= "isc_embed_dsql_insert";
static const char* const ISC_OPEN			= "isc_embed_dsql_open";
static const char* const ISC_OPEN2			= "isc_embed_dsql_open2";
static const char* const ISC_PREPARE	 		= "isc_embed_dsql_prepare";
static const char* const ISC_DSQL_ALLOCATE	= "isc_dsql_alloc_statement2";
static const char* const ISC_DSQL_EXECUTE	= "isc_dsql_execute_m";
static const char* const ISC_DSQL_FREE		= "isc_dsql_free_statement";
static const char* const ISC_DSQL_SET_CURSOR	= "isc_dsql_set_cursor_name";
static const char* const ISC_COMMIT_ROLLBACK_TRANSACTION	= "isc_%s_transaction";
static const char* const ISC_DETACH_DATABASE	= "isc_detach_database";
static const char* const ISC_GET_SLICE 		= "isc_get_slice";
static const char* const ISC_PUT_SLICE 		= "isc_put_slice";
static const char* const ISC_GET_SEGMENT 	= "isc_get_segment";
static const char* const ISC_PUT_SEGMENT 	= "isc_put_segment";
static const char* const ISC_RECEIVE 		= "isc_receive";
static const char* const ISC_RELEASE_REQUEST	= "isc_release_request";
static const char* const ISC_UNWIND_REQUEST	= "isc_unwind_request";
static const char* const ISC_SEND 			= "isc_send";
static const char* const ISC_START_TRANSACTION	= "isc_start_transaction";
static const char* const ISC_START_AND_SEND 	= "isc_start_and_send";
static const char* const ISC_START_REQUEST 	= "isc_start_request";
static const char* const ISC_TRANSACT_REQUEST 	= "isc_transact_request";
static const char* const ISC_COMMIT_RETAINING 	= "isc_commit_retaining";
static const char* const ISC_ATTACH_DATABASE_D 	= "isc_attach_database";
static const char* const ISC_ATTACH_DATABASE 	= "isc_attach_database";
static const char* const ISC_MODIFY_DPB		= "isc_modify_dpb";
static const char* const ISC_FREE			= "isc_free";
#ifdef GIVING_SUPPORTED
static const char* const ISC_SQLCODE_CALL	= "isc_sqlcode";
static const char* const ISC_FETCH 			= "isc_embed_dsql_fetch";
static const char* const ISC_EVENT_BLOCK		= "isc_event_block_a";
#else
static const char* const ISC_SQLCODE_CALL 	= "isc_sqlcode_s";
static const char* const ISC_FETCH 			= "isc_embed_dsql_fetch_a";
static const char* const ISC_EVENT_BLOCK		= "isc_event_block_s";
#endif
static const char* const ISC_PREPARE_TRANSACTION	= "isc_prepare_transaction";
static const char* const ISC_EVENT_WAIT		= "isc_wait_for_event";
static const char* const ISC_EVENT_COUNTS	= "isc_event_counts";
#ifdef GIVING_SUPPORTED
static const char* const ISC_BADDRESS		= "isc_baddress";
#else
static const char* const ISC_BADDRESS		= "isc_baddress_s";
#endif

#endif // VMS

#ifdef GIVING_SUPPORTED
static const char* const FETCH_CALL_TEMPLATE		= "CALL \"%s\" USING %s, %s%s, %s%d%s, %s%s GIVING SQLCODE";
static const char* const GET_LEN_CALL_TEMPLATE	= "CALL %s USING %s GIVING %s";
static const char* const EVENT_MOVE_TEMPLATE		= "CALL \"%s\" USING %s(%d) GIVING %s(%d)";
static const char* const GET_SEG_CALL_TEMPLATE	= "%sCALL \"%s\" USING %s, %s%d, %s%d, %s%d%s, %s%s%d GIVING %s (2)\n";
static const char* const PUT_SEG_CALL_TEMPLATE	= "%sCALL \"%s\" USING %s, %s%s%d, %s%s%d%s, %s%s%d GIVING %s (2)\n"
static const char* const SQLCODE_CALL_TEMPLATE	= "CALL \"%s\" USING %s GIVING SQLCODE";
#else
static const char* const FETCH_CALL_TEMPLATE		= "CALL \"%s\" USING %s, BY REFERENCE SQLCODE, %s%s, %s%d%s, %s%s";
static const char* const GET_LEN_CALL_TEMPLATE	= "CALL %s USING %s, %s";
static const char* const EVENT_MOVE_TEMPLATE		= "CALL \"%s\" USING %s(%d), %s(%d)";
static const char* const GET_SEG_CALL_TEMPLATE	= "%sCALL \"%s\" USING %s, %s%d, %s%d, %s%d%s, %s%s%d\n";
static const char* const PUT_SEG_CALL_TEMPLATE	= "%sCALL \"%s\" USING %s, %s%s%d, %s%s%d%s, %s%s%d\n";
static const char* const SQLCODE_CALL_TEMPLATE	= "CALL \"%s\" USING %s, BY REFERENCE SQLCODE";
#endif // GIVING_SUPPORTED

#if defined AIX || defined AIX_PPC || defined sparc || defined SOLX86 || defined HP10 || defined HP11
static const char* const USAGE_COMP			= " USAGE IS COMP";
#else
static const char* const USAGE_COMP			= " USAGE IS COMP";
#endif

#ifndef FLOATS_COMPS_DECLARED
static const char* const DCL_FLOAT			= "USAGE IS COMP-1";
static const char* const DCL_DOUBLE			= "USAGE IS COMP-2";
#endif

#ifdef NOT_USED_OR_REPLACED
static void	align(int column);
#endif
static void	asgn_from (const act*, REF);
static void	asgn_to (const act*, REF);
static void	asgn_to_proc (REF);
static void	gen_any (const act*);
static void	gen_at_end (const act*);
static void	gen_based (const act*);
static void	gen_blob_close (const act*);
static void	gen_blob_end (const act*);
static void	gen_blob_for (const act*);
static void	gen_blob_open (const act*);
static void	gen_blr (void*, SSHORT, const char*);
static void	gen_clear_handles (const act*);
static void	gen_compile (const act*);
static void	gen_create_database (const act*);
static void	gen_cursor_close (const act*, gpre_req*);
static void	gen_cursor_init (const act*);
static void	gen_cursor_open (const act*, gpre_req*);
static void	gen_database (const act*);
static void	gen_ddl (const act*);
static void	gen_dyn_close (const act*);
static void	gen_dyn_declare (const act*);
static void	gen_dyn_describe(const act*, bool);
static void	gen_dyn_execute (const act*);
static void	gen_dyn_fetch (const act*);
static void	gen_dyn_immediate (const act*);
static void	gen_dyn_insert (const act*);
static void	gen_dyn_open (const act*);
static void	gen_dyn_prepare (const act*);
static void	gen_emodify (const act*);
static void	gen_estore (const act*);
static void	gen_end_fetch (const act*);
static void	gen_endfor (const act*);
static void	gen_erase (const act*);
static SSHORT	gen_event_block (const act*);
static void	gen_event_init (const act*);
static void	gen_event_wait (const act*);
static void	gen_fetch (const act*);
static void	gen_finish (const act*);
static void	gen_for (const act*);
static void	gen_function (const act*);
static void	gen_get_or_put_slice(const act*, REF, bool);
static void	gen_get_segment (const act*);
static void	gen_loop (const act*);
static TEXT* gen_name(TEXT*, const ref*, bool);
static void	gen_on_error (const act*);
static void	gen_procedure (const act*);
static void	gen_put_segment (const act*);
static void	gen_raw (UCHAR *, enum req_t, int, int);
static void	gen_ready (const act*);
static void	gen_receive (const act*, gpre_port*);
static void	gen_release (const act*);
static void	gen_request (gpre_req*);
static void	gen_s_end (const act*);
static void	gen_s_fetch (const act*);
static void	gen_s_start (const act*);
static void	gen_segment (const act*);
static void	gen_select (const act*);
static void	gen_send (const act*, gpre_port*);
static void	gen_slice (const act*);
static void	gen_start (const act*, gpre_port*);
static void	gen_store (const act*);
static void	gen_t_start (const act*);
static void	gen_tpb (tpb*);
static void	gen_trans (const act*);
static void	gen_type (const act*);
static void	gen_update (const act*);
static void	gen_variable (const act*);
static void	gen_whenever (const swe*);
static void	make_array_declaration (REF);
static TEXT* make_name (TEXT*, const gpre_sym*);
static TEXT* make_name_formatted (TEXT*, const TEXT*, const gpre_sym*);
static void	make_port (const gpre_port*);
static void	make_ready (const dbb*, const TEXT*, const TEXT*, const gpre_req*,
	USHORT);
static void	printa(const TEXT*, bool, const TEXT*, ...) ATTRIBUTE_FORMAT(3,4);
#ifdef NOT_USED_OR_REPLACED
static void	printb (const TEXT*, ... ) ATTRIBUTE_FORMAT(1,2);
#endif
static const TEXT* request_trans (const act*, const gpre_req*);
static void	set_sqlcode (const act*);
static const TEXT* status_vector (const act*);
static void	t_start_auto (const gpre_req*, const TEXT*, const act*, bool);

static TEXT output_buffer[512];
static bool global_first_flag = false;
static const TEXT** names;

static const TEXT* vnames[] = {
	"_",
	"ISC_",
	"isc_",
	"isc_blob_null",
	"ISC_TPB_",
	"ISC_TRANS",
	"ISC_STATUS_VECTOR",
	"ISC_STATUS",
	"ISC_STATUS_VECTOR2",
	"ISC_STATUS2",
	"ISC_WINDOW",
	"ISC_WIDTH",
	"ISC_HEIGHT",
	"RDB$K_DB_TYPE_GDS",
	"ISC_ARRAY_LENGTH",
	"     ",					// column 
	"*    ",					// comment 
	"-    ",					// continue 
	"-      \"",				// continue quote 
	"-      \'",				// continue single quote 
	"",							// column0 
	"        ",					// column indent 
	"ISC_SQLCODE",
	"ISC_EVENTS_VECTOR",
	"ISC_EVENTS",
	"ISC_EVENT_NAMES_VECTOR",
	"ISC_EVENT_NAMES",
	"ISC_EVENT_NAMES_VECTOR2",
	"ISC_EVENT_NAMES2",
	NULL
};

static const TEXT* anames[] = {
	"-",
	"ISC-",
	"isc-",
	"isc-blob-null",
	"ISC-TPB-",
	"ISC-TRANS",
	"ISC-STATUS-VECTOR",
	"ISC-STATUS",
	"ISC-STATUS-VECTOR2",
	"ISC-STATUS2",
	"ISC-WINDOW",
	"ISC-WIDTH",
	"ISC-HEIGHT",
	"RDB-K-DB-TYPE-GDS",
	"ISC-ARRAY-LENGTH",
	"           ",				// column 
	"      *    ",				// comment 
	"            ",				// continue 
	"      -      \"",			// continue quote 
	"      -      \'",			// continue single quote 
	"           ",				// column0 
	"                ",			// column indent 
	"ISC-SQL-CODE",
	"ISC-EVENTS-VECTOR",
	"ISC-EVENTS",
	"ISC-EVENT-NAMES-VECTOR",
	"ISC-EVENT-NAMES",
	"ISC-EVENT-NAMES-VECTOR2",
	"ISC-EVENT-NAMES2",
	NULL
};

enum {
	UNDER,
	isc_a_pos,
	isc_b_pos,
	isc_blob_null_pos,
	isc_tpb_pos,
	isc_trans_pos,
	isc_status_vector_pos,
	isc_status_pos,
	isc_status_vector2_pos,
	ISC_STATUS2,
	ISC_WINDOW,
	ISC_WIDTH,
	ISC_HEIGHT,
	RDB$K_DB_TYPE_GDS,
	ISC_ARRAY_LENGTH,
	COLUMN,
	COMMENT,
	CONTINUE,
	CONTINUE_QUOTE,
	CONTINUE_SINGLE_QUOTE,
	COLUMN_0,
	COLUMN_INDENT,
	ISC_SQLCODE,
	ISC_EVENTS_VECTOR,
	ISC_EVENTS,
	ISC_EVENT_NAMES_VECTOR,
	ISC_EVENT_NAMES,
	ISC_EVENT_NAMES_VECTOR2,
	ISC_EVENT_NAMES2
};

static const char* const INDENT		= "   ";


//____________________________________________________________
//  
//  

void COB_action(const act* action, int column)
{

	if (action->act_flags & ACT_break)
		global_first_flag = false;

	switch (action->act_type) {
	case ACT_alter_database:
	case ACT_alter_domain:
	case ACT_alter_table:
	case ACT_alter_index:
	case ACT_create_domain:
	case ACT_create_generator:
	case ACT_create_index:
	case ACT_create_shadow:
	case ACT_create_table:
	case ACT_create_view:
	case ACT_declare_filter:
	case ACT_declare_udf:
	case ACT_statistics:
	case ACT_drop_domain:
	case ACT_drop_filter:
	case ACT_drop_index:
	case ACT_drop_shadow:
	case ACT_drop_table:
	case ACT_drop_udf:
	case ACT_drop_view:
		gen_ddl(action);
		break;
	case ACT_any:
		gen_any(action);
		return;
	case ACT_at_end:
		gen_at_end(action);
		return;
	case ACT_commit:
		gen_trans(action);
		break;
	case ACT_commit_retain_context:
		gen_trans(action);
		break;
	case ACT_basedon:
		gen_based(action);
		return;
	case ACT_b_declare:
		gen_database(action);
		return;
	case ACT_blob_cancel:
		gen_blob_close(action);
		return;
	case ACT_blob_close:
		gen_blob_close(action);
		break;
	case ACT_blob_create:
		gen_blob_open(action);
		break;
	case ACT_blob_for:
		gen_blob_for(action);
		return;
	case ACT_blob_handle:
		gen_segment(action);
		return;
	case ACT_blob_open:
		gen_blob_open(action);
		break;
	case ACT_clear_handles:
		gen_clear_handles(action);
		break;
	case ACT_close:
		gen_s_end(action);
		break;
	case ACT_create_database:
		gen_create_database(action);
		break;
	case ACT_cursor:
		gen_cursor_init(action);
		return;
	case ACT_database:
		gen_database(action);
		return;
	case ACT_disconnect:
		gen_finish(action);
		break;
	case ACT_dyn_close:
		gen_dyn_close(action);
		break;
	case ACT_dyn_cursor:
		gen_dyn_declare(action);
		break;
	case ACT_dyn_describe:
		gen_dyn_describe(action, false);
		break;
	case ACT_dyn_describe_input:
		gen_dyn_describe(action, true);
		break;
	case ACT_dyn_execute:
		gen_dyn_execute(action);
		break;
	case ACT_dyn_fetch:
		gen_dyn_fetch(action);
		break;
	case ACT_dyn_grant:
		gen_ddl(action);
		break;
	case ACT_dyn_immediate:
		gen_dyn_immediate(action);
		break;
	case ACT_dyn_insert:
		gen_dyn_insert(action);
		break;
	case ACT_dyn_open:
		gen_dyn_open(action);
		break;
	case ACT_dyn_prepare:
		gen_dyn_prepare(action);
		break;
	case ACT_dyn_revoke:
		gen_ddl(action);
		break;
	case ACT_endblob:
		gen_blob_end(action);
		return;
	case ACT_enderror:
		sprintf(output_buffer, "%sEND-IF", names[COLUMN]);
		COB_print_buffer(output_buffer, false);
		return;
	case ACT_endfor:
		gen_endfor(action);
		break;
	case ACT_endmodify:
		gen_emodify(action);
		break;
	case ACT_endstore:
		gen_estore(action);
		break;
	case ACT_erase:
		gen_erase(action);
		return;
	case ACT_event_init:
		gen_event_init(action);
		return;
	case ACT_event_wait:
		gen_event_wait(action);
		return;
	case ACT_fetch:
		gen_fetch(action);
		break;
	case ACT_finish:
		gen_finish(action);
		break;
	case ACT_for:
		gen_for(action);
		return;
	case ACT_function:
		gen_function(action);
		return;
	case ACT_get_segment:
		gen_get_segment(action);
		break;
	case ACT_get_slice:
		gen_slice(action);
		return;
	case ACT_hctef:
		gen_end_fetch(action);
		return;
	case ACT_insert:
		gen_s_start(action);
		break;
	case ACT_loop:
		gen_loop(action);
		break;
	case ACT_open:
		gen_s_start(action);
		break;
	case ACT_on_error:
		gen_on_error(action);
		return;
	case ACT_prepare:
		gen_trans(action);
		break;
	case ACT_procedure:
		gen_procedure(action);
		break;
	case ACT_put_segment:
		gen_put_segment(action);
		break;
	case ACT_put_slice:
		gen_slice(action);
		return;
	case ACT_ready:
		gen_ready(action);
		return;
	case ACT_release:
		gen_release(action);
		break;
	case ACT_rfinish:
		gen_finish(action);
		return;
	case ACT_rollback:
		gen_trans(action);
		break;
	case ACT_routine:
		return;
	case ACT_s_end:
		gen_s_end(action);
		return;
	case ACT_s_fetch:
		gen_s_fetch(action);
		break;
	case ACT_s_start:
		gen_s_start(action);
		break;
	case ACT_select:
		gen_select(action);
		break;
	case ACT_segment_length:
		gen_segment(action);
		return;
	case ACT_segment:
		gen_segment(action);
		return;
	case ACT_sql_dialect:
		gpreGlob.sw_sql_dialect = ((SDT) action->act_object)->sdt_dialect;
		return;
	case ACT_start:
		gen_t_start(action);
		break;
	case ACT_store:
		gen_store(action);
		break;
	case ACT_type_number:
		gen_type(action);
		return;
	case ACT_update:
		gen_update(action);
		break;
	case ACT_variable:
		gen_variable(action);
		return;
	default:
		return;
	}

	if ((action->act_flags & ACT_sql) && action->act_whenever)
		gen_whenever(action->act_whenever);
	else
		fprintf(gpreGlob.out_file, names[COLUMN]);
}


//____________________________________________________________
//  
//		point 'names' at the appropriate list
//		ANSI has no underscores.
//  

void COB_name_init(bool ansi)
{

	names = ansi ? anames : vnames;
}


//____________________________________________________________
//  
//		Print a statment, breaking it into
//		reasonable 80 character hunks.  This
//		function now works for COBOL statements
//		which are function calls and non-function
//		calls.
//  

void COB_print_buffer(TEXT* output_bufferL,
					  bool function_call)
{
	TEXT s[80];
	bool open_quote = false;
	bool single_quote = false;
	bool save_open_quote;
	bool save_single_quote;
	USHORT max_line;

	if (gpreGlob.sw_ansi)
		max_line = 72;
	else
		max_line = 79;

	TEXT* p = s;

	for (const TEXT* q = output_bufferL; *q; q++) {
		*p++ = *q;

		/*  If we have a single or double quote, toggle the
		   quote switch and indicate single or double quote  */
		if (*q == '\"') {
			open_quote = !open_quote;
			single_quote = false;
		}
		else if (*q == '\'') {
			open_quote = !open_quote;
			single_quote = true;
		}

		if ((p - s) > max_line) {
			const TEXT* tempq = q;
			save_open_quote = open_quote;
			save_single_quote = single_quote;
			if (function_call) {
				//  Back up until we reach a comma  
				for (p--; (p > s); p--, q--) {
					if (*(p + 1) == '\"' || *(p + 1) == '\'') {
						/*  If we have a single or double quote, toggle the
						   quote switch and indicate single or double quote  */
						open_quote = !open_quote;
						if (open_quote)
							single_quote = (*(p + 1) == '\'');
						else
							single_quote = false;
					}
					if (!open_quote && (*p == ','))
						break;
				}
				/* if p == s, this is a call with no commas. back up to a blank */
				if (p == s) {
					q = tempq;
					p = s + max_line;
					open_quote = save_open_quote;
					single_quote = save_single_quote;
					for (p--; p > s; p--, q--) {
						if (*(p + 1) == '\"' || *(p + 1) == '\'') {
							/*  If we have a single or double quote, toggle the
							   quote switch and indicate single or double quote  */
							open_quote = !open_quote;
							if (open_quote)
								single_quote = (*(p + 1) == '\'');
							else
								single_quote = false;
						}
						if (!open_quote && (*p == ' '))
							break;
					}
					q--;
					if (p == s) {
						q = tempq - 1;
						p = s + max_line;
						open_quote = save_open_quote;
						single_quote = save_single_quote;
					}
					*p = 0;
				}
				else
					*++p = 0;
			}
			else {
				// back up to a blank 
				for (p--; p > s; p--, q--) {
					if (*(p + 1) == '\"' || *(p + 1) == '\'') {
						/* If we have a single or double quote, toggle the
						   quote switch and indicate single or double quote  */
						open_quote = !open_quote;
						if (open_quote)
							single_quote = (*(p + 1) == '\'');
						else
							single_quote = false;
					}
					if (!open_quote && (*p == ' '))
						break;
				}
				q--;
				if (p == s) {
					q = tempq - 1;
					p = s + max_line;
					open_quote = save_open_quote;
					single_quote = save_single_quote;
				}
				*p = 0;
			}

			fprintf(gpreGlob.out_file, "%s\n", s);
			if (open_quote) {
				if (single_quote)
					strcpy(s, names[CONTINUE_SINGLE_QUOTE]);
				else
					strcpy(s, names[CONTINUE_QUOTE]);
			}
			else
				strcpy(s, names[CONTINUE]);
			for (p = s; *p; p++);
		}
	}
	*p = 0;
	fprintf(gpreGlob.out_file, "%s", s);
	output_bufferL[0] = 0;
}


#ifdef NOT_USED_OR_REPLACED
//____________________________________________________________
//  
//       Align output to a specific column for output.  If the
//       column is negative, don't do anything.
//  

static void align( int column)
{
	int i;

	if (column < 0)
		return;

	putc('\n', gpreGlob.out_file);

	for (i = column / 8; i; --i)
		putc('\t', gpreGlob.out_file);

	for (i = column % 8; i; --i)
		putc(' ', gpreGlob.out_file);
}
#endif


//____________________________________________________________
//  
//		Build an assignment from a host language variable to
//		a port variable.
//  

static void asgn_from( const act* action, REF reference)
{
	gpre_fld* field;
	TEXT name[64], variable[20], temp[20];
	const char* value;

	for (; reference; reference = reference->ref_next) {
		field = reference->ref_field;
		if (field->fld_array_info)
			if (!(reference->ref_flags & REF_array_elem)) {
				printa(names[COLUMN], true, "CALL \"isc_qtoq\" USING %s, %s",
					   names[isc_blob_null_pos], gen_name(name, reference, true));
				gen_get_or_put_slice(action, reference, false);
				continue;
			}

		if (!reference->ref_source && !reference->ref_value)
			continue;
		gen_name(variable, reference, true);
		if (reference->ref_source)
			value = gen_name(temp, reference->ref_source, true);
		else
			value = reference->ref_value;
		if (!reference->ref_master || (reference->ref_flags & REF_literal))
			sprintf(output_buffer, "%sMOVE %s TO %s\n",
					names[COLUMN], value, variable);
		else {
			sprintf(output_buffer, "%sIF %s < 0 THEN\n",
					names[COLUMN], value);
			COB_print_buffer(output_buffer, false);
			sprintf(output_buffer, "%sMOVE -1 TO %s\n", names[COLUMN_INDENT],
					variable);
			COB_print_buffer(output_buffer, false);
			sprintf(output_buffer, "%sELSE\n", names[COLUMN]);
			COB_print_buffer(output_buffer, false);
			sprintf(output_buffer, "%sMOVE 0 TO %s\n", names[COLUMN_INDENT],
					variable);
			COB_print_buffer(output_buffer, false);
			sprintf(output_buffer, "%sEND-IF\n", names[COLUMN]);
		}
		COB_print_buffer(output_buffer, false);
	}
}

//____________________________________________________________
//  
//		Build an assignment to a host language variable from
//		a port variable.
//  

static void asgn_to( const act* action, REF reference)
{
	gpre_fld* field;
	REF source;
	TEXT s[64];

	source = reference->ref_friend;
	field = source->ref_field;
	gen_name(s, source, true);

	if (field->fld_array_info) {
		source->ref_value = reference->ref_value;
		gen_get_or_put_slice(action, source, true);
		return;
	}

	field = reference->ref_field;
	sprintf(output_buffer, "%sMOVE %s TO %s\n",
			names[COLUMN], s, reference->ref_value);
	COB_print_buffer(output_buffer, false);

//  Pick up NULL value if one is there 

	if (reference = reference->ref_null) {
		sprintf(output_buffer, "%sMOVE %s TO %s\n",
				names[COLUMN], gen_name(s, reference, true),
				reference->ref_value);
		COB_print_buffer(output_buffer, false);
	}
}


//____________________________________________________________
//  
//		Build an assignment to a host language variable from
//		a port variable.
//  

static void asgn_to_proc( REF reference)
{
	gpre_fld* field;
	TEXT s[64];

	for (; reference; reference = reference->ref_next) {
		if (!reference->ref_value)
			continue;
		field = reference->ref_field;
		gen_name(s, reference, true);
		sprintf(output_buffer, "%sMOVE %s TO %s\n",
				names[COLUMN], s, reference->ref_value);
		COB_print_buffer(output_buffer, false);
	}
}


//____________________________________________________________
//  
//		Generate a function call for free standing ANY.  Somebody else
//		will need to generate the actual function.
//  

static void gen_any( const act* action)
{
	gpre_req* request;
	gpre_port* port;
	REF reference;

	request = action->act_request;

	fprintf(gpreGlob.out_file, "%s_r (&%s, &%s",
			   request->req_handle, request->req_handle, request->req_trans);

	if (port = request->req_vport)
		for (reference = port->por_references; reference;
			 reference = reference->ref_next)
				fprintf(gpreGlob.out_file, ", %s", reference->ref_value);

	fprintf(gpreGlob.out_file, ")");
}


//____________________________________________________________
//  
//		Generate code for AT END clause of FETCH.
//  

static void gen_at_end( const act* action)
{
	gpre_req* request;
	TEXT s[20];

	request = action->act_request;
	printa(names[COLUMN], false,
		   "IF %s = 0 THEN", gen_name(s, request->req_eof, true));
	fprintf(gpreGlob.out_file, names[COLUMN]);
}


//____________________________________________________________
//  
//		Substitute for a BASED ON <field name> clause.
//  

static void gen_based( const act* action)
{
	gpre_fld* field;
	TEXT s[64];
	USHORT datatype;
	SLONG length;
	SSHORT digits;

	bas* based_on = (bas*) action->act_object;
	field = based_on->bas_field;

	if (based_on->bas_flags & BAS_segment) {
		datatype = dtype_text;
		if (!(length = field->fld_seg_length))
			length = 256;
	}
	else if (field->fld_array_info) {
		CPR_error("Based on currently not implemented for arrays.");
		return; // silence non initialized warning
//  
//     TBD - messy
//   datatype = field->fld_array_info->ary_dtype;
//   for (dimension = field->fld_array_info->ary_dimension; dimension;
//  dimension = dimension->dim_next)
// { 
// fprintf (gpreGlob.out_file, "
//  
	}
	else
		datatype = field->fld_dtype;

	switch (datatype) {
	case dtype_short:
	case dtype_long:
		digits = (datatype == dtype_short) ? 4 : 9;
		fprintf(gpreGlob.out_file, "%sPIC S", names[COLUMN]);
		if (field->fld_scale >= -digits && field->fld_scale <= 0) {
			if (field->fld_scale > -digits)
				fprintf(gpreGlob.out_file, "9(%d)", digits + field->fld_scale);
			if (field->fld_scale)
				fprintf(gpreGlob.out_file, "V9(%d)",
						   digits - (digits + field->fld_scale));
			fprintf(gpreGlob.out_file, USAGE_COMP);
		}
		else if (field->fld_scale > 0)
			fprintf(gpreGlob.out_file, "9(%d)P(%d)", digits, field->fld_scale);
		else
			fprintf(gpreGlob.out_file, "VP(%d)9(%d)", -(field->fld_scale + digits),
					   digits);
		break;

	case dtype_date:
	case dtype_blob:
		fprintf(gpreGlob.out_file, "%sPIC S9(18)%s", names[COLUMN], USAGE_COMP);
		break;

	case dtype_quad:
		fprintf(gpreGlob.out_file, "%sPIC S9(", names[COLUMN]);
		fprintf(gpreGlob.out_file, "%d)", 18 + field->fld_scale);
		if (field->fld_scale < 0)
			fprintf(gpreGlob.out_file, "V9(%d)", -field->fld_scale);
		else if (field->fld_scale > 0)
			fprintf(gpreGlob.out_file, "P(%d)", field->fld_scale);
		fprintf(gpreGlob.out_file, USAGE_COMP);
		break;

	case dtype_text:
		fprintf(gpreGlob.out_file, "%sPIC X(%d)", names[COLUMN], field->fld_length);
		break;

	case dtype_real:
		fprintf(gpreGlob.out_file, "%s%s", names[COLUMN], DCL_FLOAT);
		break;

	case dtype_double:
		fprintf(gpreGlob.out_file, "%s%s", names[COLUMN], DCL_DOUBLE);
		break;

	default:
		sprintf(s, "datatype %d unknown\n", field->fld_dtype);
		CPR_error(s);
		return;
	}

	if (*based_on->bas_terminator == '.')
		fprintf(gpreGlob.out_file, "%s\n", based_on->bas_terminator);
}


//____________________________________________________________
//  
//		Make a blob FOR loop.
//  

static void gen_blob_close( const act* action)
{
	blb* blob;

	if (action->act_flags & ACT_sql) {
		gen_cursor_close(action, action->act_request);
		blob = (blb*) action->act_request->req_blobs;
	}
	else
		blob = (blb*) action->act_object;

	const TEXT* command = (action->act_type == ACT_blob_cancel) ? CANCEL : CLOSE;
	TEXT buffer[80];
	sprintf(buffer, ISC_BLOB, command);

	printa(names[COLUMN], true, "CALL \"%s\" USING %s, %s%s%d",
		   buffer,
		   status_vector(action), BY_REF, names[isc_a_pos], blob->blb_ident);

	set_sqlcode(action);
}


//____________________________________________________________
//  
//		End a blob FOR loop.
//  

static void gen_blob_end( const act* action)
{
	blb* blob;

	blob = (blb*) action->act_object;
	gen_get_segment(action);
	printa(names[COLUMN], false, "END-PERFORM");
	if (action->act_error)
		printa(names[COLUMN], true,
			   "%sCALL \"%s\" USING %s%s, %s%s%d",
			   INDENT,
			   ISC_CANCEL_BLOB,
			   BY_REF, names[isc_status_vector2_pos],
			   BY_REF, names[isc_a_pos], blob->blb_ident);
	else
		printa(names[COLUMN], true,
			   "%sCALL \"%s\" USING %s, %s%s%d",
			   INDENT, ISC_CANCEL_BLOB,
			   status_vector(0), BY_REF, names[isc_a_pos], blob->blb_ident);
}


//____________________________________________________________
//  
//		Make a blob FOR loop.
//  

static void gen_blob_for( const act* action)
{

	gen_blob_open(action);
	gen_get_segment(action);
	printa(names[COLUMN], false, "PERFORM UNTIL");

	printa(names[COLUMN], false, "%s%s(2) NOT = 0 AND %s(2) NOT = 335544366",
		   INDENT, names[isc_status_pos], names[isc_status_pos]);
}


//____________________________________________________________
//  
//		Generate the call to open (or create) a blob.
//  

static void gen_blob_open( const act* action)
{
#ifdef VMS
	const TEXT* pattern1 =
		"CALL \"ISC_%IFCREATE%ELOPEN%EN_BLOB2\" USING %V1, %RF%DH%RE, %RF%RT%RE, %RF%BH%RE, %RF%FR%RE, %VF%N1%VE, %RF%I1%RE\n";
	const TEXT* pattern2 =
		"CALL \"ISC_%IFCREATE%ELOPEN%EN_BLOB2\" USING %V1, %RF%DH%RE, %RF%RT%RE, %RF%BH%RE, %RF%FR%RE, %VF0%VE, %VF0%VE\n";
#else
	const TEXT* pattern1 =
		"CALL \"isc_%IFcreate%ELopen%EN_blob2\" USING %V1, %RF%DH%RE, %RF%RT%RE, %RF%BH%RE, %RF%FR%RE, %VF%N1%VE, %RF%I1%RE\n";
	const TEXT* pattern2 =
		"CALL \"isc_%IFcreate%ELopen%EN_blob2\" USING %V1, %RF%DH%RE, %RF%RT%RE, %RF%BH%RE, %RF%FR%RE, %VF0%VE, %VF0%VE\n";
#endif

	if (gpreGlob.sw_auto && (action->act_flags & ACT_sql)) {
		t_start_auto(action->act_request, status_vector(action), action, true);
		printa(names[COLUMN], false, "IF %s NOT = 0 THEN",
			   request_trans(action, action->act_request));
	}

    TEXT s[20];
	blb* blob;
	const ref* reference;
	if (action->act_flags & ACT_sql) {
		gen_cursor_open(action, action->act_request);
		blob = (blb*) action->act_request->req_blobs;
		reference = ((open_cursor*) action->act_object)->opn_using;
		gen_name(s, reference, true);
	}
	else {
		blob = (blb*) action->act_object;
		reference = blob->blb_reference;
	}

	const USHORT column = strlen(names[COLUMN]);
	PAT args;
	args.pat_condition = (action->act_type == ACT_blob_create);	// open or create blob
	args.pat_vector1 = status_vector(action);	// status vector
	args.pat_database = blob->blb_request->req_database;	// database handle
	args.pat_request = blob->blb_request;	// transaction handle
	args.pat_blob = blob;		// blob handle
	args.pat_reference = reference;	// blob identifier
	args.pat_ident1 = blob->blb_bpb_ident;	// blob parameter block

	if ((action->act_flags & ACT_sql) && action->act_type == ACT_blob_open)
		printa(names[COLUMN], false, "MOVE %s TO %s",
			   reference->ref_value, s);

	if (args.pat_value1 = blob->blb_bpb_length)
		PATTERN_expand(column, pattern1, &args);
	else
		PATTERN_expand(column, pattern2, &args);

	if (action->act_flags & ACT_sql) {
		printa(names[COLUMN], false, "END-IF");
		printa(names[COLUMN], false, "END-IF");
		printa(names[COLUMN], false, "END-IF");
		printa(names[COLUMN], false, "END-IF");
		if (gpreGlob.sw_auto)
			printa(names[COLUMN], false, "END-IF");
		set_sqlcode(action);
		if (action->act_type == ACT_blob_create) {
			printa(names[COLUMN], false, "IF SQLCODE = 0 THEN");
			printa(names[COLUMN], false, "MOVE %s TO %s",
				   s, reference->ref_value);
			printa(names[COLUMN], false, "END-IF");
		}
	}
}


//____________________________________________________________
//  
//		Callback routine for BLR pretty printer.
//  

static void gen_blr(void* user_arg, SSHORT offset, const char* string)
{
	int indent, length, comment, i, max_line, max_diff;
	const char *p, *q;
	bool open_quote;
	bool first_line = true;

	if (gpreGlob.sw_ansi) {
		max_line = 70;
		max_diff = 7;
	}
	else {
		max_line = 255;
		max_diff = 30;
	}
	comment = strlen(names[COMMENT]);
	indent = 2 * strlen(INDENT);
	p = string;
	while (*p == ' ') {
		p++;
		indent++;
	}
	length = strlen(p);

	if (comment + indent > max_line)
		indent = max_line - comment;

	while (length > 0 && length + indent + comment > max_line) {
		/* if we did not find somewhere to break between the 200th and 256th
		   character just print out 256 characters */

		for (q = p, open_quote = false; (q - p + indent + comment) < max_line;
			 q++) {
			if ((q - p + indent + comment) > max_line - max_diff && *q == ','
				&& !open_quote)
			{
				break;
			}
			if (*q == '\'' && *(q - 1) != '\\')
				open_quote = !open_quote;
		}
		fprintf(gpreGlob.out_file, "%s", names[COMMENT]);
		for (i = 0; i < indent; i++)
			fputc(' ', gpreGlob.out_file);
		q++;
		char buffer[256];
		strncpy(buffer, p, q - p);
		buffer[q - p] = 0;
		fprintf(gpreGlob.out_file, "%s\n", buffer);
		length = length - (q - p);
		p = q;
		if (first_line) {
			first_line = false;
			indent += strlen(INDENT);
			if (comment + indent > max_line)
				indent = max_line - comment;
		}
	}

	fprintf(gpreGlob.out_file, "%s", names[COMMENT]);
	for (i = 0; i < indent; i++)
		fputc(' ', gpreGlob.out_file);
	fprintf(gpreGlob.out_file, "%s\n", p);
}


//____________________________________________________________
//  
//		Zap all know handles.
//  

static void gen_clear_handles( const act* action)
{
	gpre_req* request;

	for (request = gpreGlob.requests; request; request = request->req_next) {
		if (!(request->req_flags & REQ_exp_hand))
			printa(names[COLUMN], true, "%s = 0;", request->req_handle);
	}
}


//____________________________________________________________
//  
//		Generate text to compile a request.
//  

static void gen_compile( const act* action)
{
	const gpre_req* request = action->act_request;
	const dbb* db = request->req_database;
	const gpre_sym* symbol = db->dbb_name;

//  generate automatic ready if appropriate 

	if (gpreGlob.sw_auto)
		t_start_auto(request, status_vector(action), action, true);

//  
//  always generate a compile, a test for the success of the compile,
//  and an end to the 'if not compiled test
//  

//  generate an 'if not compiled' 

	printa(names[COLUMN], false, "IF %s = 0 THEN", request->req_handle);

	if (gpreGlob.sw_auto && action->act_error)
		printa(names[COLUMN], false, "IF %s NOT = 0 THEN",
			   request_trans(action, request));

	sprintf(output_buffer,
			"%sCALL \"%s%s\" USING %s, %s%s, %s%s, %s%d%s, %s%s%d\n",
			names[COLUMN], ISC_COMPILE_REQUEST,
			(request->req_flags & REQ_exp_hand) ? "" : "2",
			status_vector(action), BY_REF, symbol->sym_string, BY_REF,
			request->req_handle, BY_VALUE, request->req_length, END_VALUE,
			BY_REF, names[isc_a_pos], request->req_ident);

	COB_print_buffer(output_buffer, true);
	if (gpreGlob.sw_auto && action->act_error)
		printa(names[COLUMN], false, "END-IF");

	set_sqlcode(action);
	printa(names[COLUMN], false, "END-IF");

//  If blobs are present, zero out all of the blob handles.  After this
//  point, the handles are the user's responsibility 

	const blb* blob = request->req_blobs;
	if (blob)
		for (; blob; blob = blob->blb_next) {
			sprintf(output_buffer, "%sMOVE 0 TO %s%d\n",
					names[COLUMN], names[isc_a_pos], blob->blb_ident);

			COB_print_buffer(output_buffer, false);
		}
}


//____________________________________________________________
//  
//		Generate a call to create a database.
//  

static void gen_create_database( const act* action)
{
	DBB dbisc;
	TEXT s1[32], s1Tmp[32], s2[32], s2Tmp[32], db_name[128];
	bool save_sw_auto;

	gpre_req* request = ((mdbb*) action->act_object)->mdbb_dpb_request;
	DBB db = (DBB) request->req_database;
	if (request) {
		sprintf(s1, "%s%dL", names[isc_b_pos], request->req_ident);
		if (request->req_flags & REQ_extend_dpb)
			sprintf(s2, "%s%dp", names[isc_b_pos], request->req_ident);
		else
			sprintf(s2, "%s%d", names[isc_b_pos], request->req_ident);

		/* if the dpb needs to be extended at runtime to include items
		   in host variables, do so here; this assumes that there is 
		   always a request generated for runtime variables */

		if (request->req_flags & REQ_extend_dpb) {
			if (request->req_length) {
				sprintf(output_buffer, "%sMOVE isc_%d to %s\n",
						names[COLUMN], request->req_ident, s2);
			}
			if (db->dbb_r_user) {
				sprintf(output_buffer,
						"%sCALL \"%s\" USING %s%s, %s%s, BY VALUE 28, %s %s, %s%d%s\n",
						names[COLUMN],
						ISC_MODIFY_DPB,
						BY_REF, s2,
						BY_REF, s1,
						BY_DESC, db->dbb_r_user,
						BY_VALUE, strlen(db->dbb_r_user) - 2, END_VALUE);
				COB_print_buffer(output_buffer, true);
			}
			if (db->dbb_r_password) {
				sprintf(output_buffer,
						"%sCALL \"%s\" USING %s%s, %s%s, BY VALUE 29,  %s %s, %s%d%s\n",
						names[COLUMN],
						ISC_MODIFY_DPB,
						BY_REF, s2,
						BY_REF, s1,
						BY_DESC, db->dbb_r_password,
						BY_VALUE, strlen(db->dbb_r_password) - 2, END_VALUE);
				COB_print_buffer(output_buffer, true);
			}

			/*
			   ** ==================================================
			   ** ==
			   ** ==   Process Role Name, isc_dpb_sql_role_name/60
			   ** ==
			   ** ==================================================
			 */

			if (db->dbb_r_sql_role) {
				sprintf(output_buffer,
						"%sCALL \"%s\" USING %s%s, %s%s, BY VALUE 60,  %s %s, %s%d%s\n",
						names[COLUMN],
						ISC_MODIFY_DPB,
						BY_REF, s2,
						BY_REF, s1,
						BY_DESC, db->dbb_r_sql_role,
						BY_VALUE, strlen(db->dbb_r_sql_role) - 2, END_VALUE);
				COB_print_buffer(output_buffer, true);
			}

			if (db->dbb_r_lc_messages) {
				sprintf(output_buffer,
						"%sCALL \"%s\" USING %s%s, %s%s, BY VALUE 47,  %s %s, %s%d%s\n",
						names[COLUMN],
						ISC_MODIFY_DPB,
						BY_REF, s2,
						BY_REF, s1,
						BY_REF, db->dbb_r_lc_messages,
						BY_VALUE, strlen(db->dbb_r_lc_messages) - 2,
						END_VALUE);
				COB_print_buffer(output_buffer, true);
			}
			if (db->dbb_r_lc_ctype) {
				sprintf(output_buffer,
						"%sCALL \"%s\" USING %s%s %s%s, BY VALUE 48,  %s %s, %s%d%s\n",
						names[COLUMN],
						ISC_MODIFY_DPB,
						BY_REF, s2,
						BY_REF, s1,
						BY_REF, db->dbb_r_lc_ctype,
						BY_VALUE, strlen(db->dbb_r_lc_ctype) - 2, END_VALUE);
				COB_print_buffer(output_buffer, true);
			}
		}

		if (request->req_flags & REQ_extend_dpb) {
			sprintf(s1Tmp, "%s%s%s", BY_VALUE, s1, END_VALUE);
			sprintf(s2Tmp, "%s%s%s", BY_VALUE, s2, END_VALUE);
		}
		else {
			sprintf(s2Tmp, "%s%s", BY_REF, s2);
			sprintf(s1Tmp, "%s%d%s", BY_VALUE, request->req_length,
					END_VALUE);
		}
	}

#ifdef VMS
	sprintf(db_name, "\"%s\"", db->dbb_filename);
#else
	for (dbisc = gpreGlob.isc_databases; dbisc; dbisc = dbisc->dbb_next)
		if (strcmp(dbisc->dbb_filename, db->dbb_filename) == 0)
			db->dbb_id = dbisc->dbb_id;
	sprintf(db_name, "isc-%ddb", db->dbb_id);
#endif

	sprintf(output_buffer,
			"%sCALL \"%s\" USING %s, %s%d%s, %s%s, %s%s, %s, %s, %s0%s\n",
			names[COLUMN],
			ISC_CREATE_DATABASE,
			status_vector(action),
			BY_VALUE, strlen(db->dbb_filename), END_VALUE,
			BY_REF, db_name,
			BY_REF, db->dbb_name->sym_string,
			(request->req_length) ? s1Tmp : OMITTED,
			(request->req_length) ? s2Tmp : OMITTED, BY_VALUE, END_VALUE);

	COB_print_buffer(output_buffer, true);
//  if the dpb was extended, free it here 

	if (request && request->req_flags & REQ_extend_dpb) {
		if (request->req_length) {
			sprintf(output_buffer,
					"if (%s != isc_%d)", s2, request->req_ident);
			COB_print_buffer(output_buffer, true);
		}

		sprintf(output_buffer,
				"%sCALL \"%s\" USING %s\n", names[COLUMN], ISC_FREE, s2Tmp);
		COB_print_buffer(output_buffer, true);

		// reset the length of the dpb 
		sprintf(output_buffer, "%sMOVE %d to %s",
				names[COLUMN], request->req_length, s1);
		COB_print_buffer(output_buffer, true);
	}
	save_sw_auto = gpreGlob.sw_auto;
	gpreGlob.sw_auto = true;
	printa(names[COLUMN], false, "IF %s(2) = 0 THEN", names[isc_status_pos]);
	gen_ddl(action);
	gpreGlob.sw_auto = save_sw_auto;
	printa(names[COLUMN], false, "END-IF");
	set_sqlcode(action);
}


//____________________________________________________________
//  
//		Generate substitution text for END_STREAM.
//  

static void gen_cursor_close( const act* action, gpre_req* request)
{

	printa(names[COLUMN], false, "IF %s%dS NOT = 0 THEN",
		   names[isc_a_pos], request->req_ident);
	printa(names[COLUMN], true,
		   "CALL \"%s\" USING %s, %s%s%dS, %s%d%s",
		   ISC_DSQL_FREE,
		   status_vector(action),
		   BY_REF, names[isc_a_pos], request->req_ident, BY_VALUE, 1, END_VALUE);
	printa(names[COLUMN], false, "IF %s(2) = 0 THEN", names[isc_status_pos]);
}


//____________________________________________________________
//  
//		Generate text to initialize a cursor.
//  

static void gen_cursor_init( const act* action)
{

//  If blobs are present, zero out all of the blob handles.  After this
//  point, the handles are the user's responsibility 

	if (action->act_request->
		req_flags & (REQ_sql_blob_open | REQ_sql_blob_create))
			printa(names[COLUMN], false, "MOVE 0 TO %s%d", names[isc_a_pos],
				   action->act_request->req_blobs->blb_ident);
}


//____________________________________________________________
//  
//		Generate text to open an embedded SQL cursor.
//  

static void gen_cursor_open( const act* action, gpre_req* request)
{
	TEXT s[64];

	if (action->act_type != ACT_open)
		printa(names[COLUMN], false, "IF %s%dS = 0 THEN",
			   names[isc_a_pos], request->req_ident);
	else
		printa(names[COLUMN], false, "IF (%s%dS = 0) AND %s NOT = 0 THEN",
			   names[isc_a_pos], request->req_ident, request->req_handle);
	if (gpreGlob.sw_auto)
		printa(names[COLUMN], false, "IF %s NOT = 0 THEN",
			   request->req_database->dbb_name->sym_string);
	printa(names[COLUMN], true, "CALL \"%s\" USING %s, %s%s, %s%s%dS",
		   ISC_DSQL_ALLOCATE,
		   status_vector(action),
		   BY_REF, request->req_database->dbb_name->sym_string,
		   BY_REF, names[isc_a_pos], request->req_ident);
	if (gpreGlob.sw_auto)
		printa(names[COLUMN], false, "END-IF");
	printa(names[COLUMN], false, "END-IF");

	printa(names[COLUMN], false, "IF %s%dS NOT = 0 THEN",
		   names[isc_a_pos], request->req_ident);
	if (gpreGlob.sw_auto)
		printa(names[COLUMN], false, "IF %s NOT = 0 THEN",
			   request_trans(action, request));
#ifndef VMS
	make_name_formatted(s, "ISC-CONST-%s",
						((open_cursor*) action->act_object)->opn_cursor);
#else
	make_name(s, ((open_cursor*) action->act_object)->opn_cursor);
#endif
	printa(names[COLUMN], true,
		   "CALL \"%s\" USING %s, %s%s%dS, %s%s, %s0%s",
		   ISC_DSQL_SET_CURSOR,
		   status_vector(action),
		   BY_REF, names[isc_a_pos], request->req_ident,
		   BY_REF, s, BY_VALUE, END_VALUE);
	printa(names[COLUMN], false, "IF %s(2) = 0 THEN", names[isc_status_pos]);
	printa(names[COLUMN], true,
		   "CALL \"%s\" USING %s, %s%s, %s%s%dS, %s0%s, %s, %s-1%s, %s0%s, %s",
		   ISC_DSQL_EXECUTE,
		   status_vector(action),
		   BY_REF, request_trans(action, request),
		   BY_REF, names[isc_a_pos], request->req_ident,
		   BY_VALUE, END_VALUE,
		   OMITTED, BY_VALUE, END_VALUE, BY_VALUE, END_VALUE, OMITTED);
	printa(names[COLUMN], false, "IF %s(2) = 0 THEN", names[isc_status_pos]);
}


//____________________________________________________________
//  
//		Generate insertion text for the database statement.
//  

static void gen_database( const act* action)
{
	DBB db;
	gpre_req* request;
	TEXT fname[80], s1[40];
	USHORT count;
	tpb* tpb_iterator;
	gpre_port* port;
	blb* blob;
	rdy* ready;
	gpre_sym* symbol;
	gpre_sym* cur_stmt;
	gpre_prc* procedure;
	gpre_lls* stack_ptr;

	if (global_first_flag)
		return;
	global_first_flag = true;

	sprintf(output_buffer, "\n%s**** GDS Preprocessor Definitions ****\n\n",
			names[COMMENT]);
	COB_print_buffer(output_buffer, false);

	printa(names[COLUMN_0], false, "01  %s PIC S9(18) USAGE COMP VALUE IS 0.",
		   names[isc_blob_null_pos]);
	printa(names[COLUMN_0], false, "01  %s PIC S9(9) USAGE COMP EXTERNAL.",
		   names[ISC_SQLCODE]);

	bool all_static = true;
	bool all_extern = true;
	for (db = gpreGlob.isc_databases, count = 0; db; db = db->dbb_next) {
		all_static = all_static && (db->dbb_scope == DBB_STATIC);
		all_extern = all_extern && (db->dbb_scope == DBB_EXTERN);
		const TEXT* name = db->dbb_name->sym_string;
		printa(names[COLUMN_0], false, "01  %s%s PIC S9(9) USAGE COMP%s.",
			   name,
			   (all_static) ? "" : (all_extern) ? " IS EXTERNAL" :
			   " IS GLOBAL", (all_extern) ? "" : " VALUE IS 0");

		// generate variables to hold database name strings for attach call 

#ifndef VMS
		db->dbb_id = ++count;
		if (db->dbb_runtime) {
			printa(names[COLUMN_0], false,
				   "01  %s%ddb PIC X(%d) VALUE IS \"%s\".", names[isc_b_pos],
				   db->dbb_id, strlen(db->dbb_runtime), db->dbb_runtime);
		}
		else if (db->dbb_filename) {
			printa(names[COLUMN_0], false,
				   "01  %s%ddb PIC X(%d) VALUE IS \"%s\".", names[isc_b_pos],
				   db->dbb_id, strlen(db->dbb_filename), db->dbb_filename);
		}
#endif

		for (tpb_iterator = db->dbb_tpbs;
			 tpb_iterator;
			 tpb_iterator = tpb_iterator->tpb_dbb_next)
		{
			gen_tpb(tpb_iterator);
		}
	}

#ifndef VMS
//  loop through actions: find readys to generate vars for quoted strings 

	bool dyn_immed = false;
	for (const act* local_act = action; local_act; local_act = local_act->act_rest)
	{
		if (local_act->act_type == ACT_create_database) {
			// no statement;
		}
		else if (local_act->act_type == ACT_ready) {
			for (ready = (rdy*) local_act->act_object; ready; ready = ready->rdy_next)
			{
				TEXT* s = ready->rdy_filename;
				if (s && ((*s == '\'') || (*s == '\"')))
				{
					strcpy(fname, ++s);
					s = fname + strlen(fname) - 1;
					*s = 0;
					ready->rdy_id = ++count;
					printa(names[COLUMN_0], false,
						   "01  %s%ddb PIC X(%d) VALUE IS \"%s\".",
						   names[isc_b_pos], ready->rdy_id, strlen(fname), fname);
				}
			}
		}
		else if ((local_act->act_flags & ACT_sql) &&
				 (local_act->act_type == ACT_dyn_cursor ||
				  local_act->act_type == ACT_dyn_prepare ||
				  local_act->act_type == ACT_open ||
				  local_act->act_type == ACT_blob_open ||
				  local_act->act_type == ACT_blob_create))
		{
			if (local_act->act_type == ACT_dyn_cursor)
				cur_stmt = ((DYN) local_act->act_object)->dyn_cursor_name;
			else if (local_act->act_type == ACT_dyn_prepare)
				cur_stmt = ((DYN) local_act->act_object)->dyn_statement_name;
			else
				cur_stmt = ((open_cursor*) local_act->act_object)->opn_cursor;

			// Only generate one declaration per cursor or statement name 

			const act* chck_dups;
			for (chck_dups = local_act->act_rest; chck_dups;
				 chck_dups = chck_dups->act_rest) 
			{
				const gpre_sym* dup;
				if (chck_dups->act_type == ACT_dyn_cursor)
					dup = ((DYN) chck_dups->act_object)->dyn_cursor_name;
				else if (chck_dups->act_type == ACT_dyn_prepare)
					dup = ((DYN) chck_dups->act_object)->dyn_statement_name;
				else if ((chck_dups->act_flags & ACT_sql) &&
						 (chck_dups->act_type == ACT_open ||
						  chck_dups->act_type == ACT_blob_open ||
						  chck_dups->act_type == ACT_blob_create))
				{
					dup = ((open_cursor*) chck_dups->act_object)->opn_cursor;
				}
				else
					continue;

				if (!strcmp(dup->sym_string, cur_stmt->sym_string))
					break;
			}

			if (!chck_dups) {
				make_name(s1, cur_stmt);
				printa(names[COLUMN_0], false,
					   "01  ISC-CONST-%s PIC X(%d) VALUE IS \"%s \".",
					   s1, strlen(s1) + 1, s1);
				printa(names[COLUMN_0], false,
					   "01  ISC-CONST-%sL PIC S9(4) USAGE %s.", s1,
					   COMP_VALUE);
			}
		}
		else if (local_act->act_type == ACT_dyn_immediate) {
			if (!dyn_immed) {
				dyn_immed = true;
				printa(names[COLUMN_0], false,
					   "01  ISC-CONST-DYN-IMMEDL PIC S9(4) USAGE %s.",
					   COMP_VALUE);
			}
		}
		else if (local_act->act_type == ACT_procedure) {
			request = local_act->act_request;
			procedure = (gpre_prc*) local_act->act_object;
			symbol = procedure->prc_symbol;
			const char* sname = symbol->sym_string;
			printa(names[COLUMN_0], false,
				   "01  %s%dprc PIC X(%d) VALUE IS \"%s\".",
				   names[isc_b_pos], request->req_ident, strlen(sname), sname);
		}
	}
#endif

	printa(names[COLUMN_0], false, "01  %s%s PIC S9(9) USAGE COMP%s.",
		   names[isc_trans_pos],
		   (all_static) ? "" : (all_extern) ? " IS EXTERNAL" : " IS GLOBAL",
		   (all_extern) ? "" : " VALUE IS 0");
	printa(names[COLUMN_0], false, "01  %s%s.",
		   names[isc_status_vector_pos],
		   (all_static) ? "" : (all_extern) ? " IS EXTERNAL" : " IS GLOBAL");
	printa(names[COLUMN], false,
		   "03  %s PIC S9(9) USAGE COMP OCCURS 20 TIMES.", names[isc_status_pos]);
	printa(names[COLUMN_0], false, "01  %s%s.", names[isc_status_vector2_pos],
		   (all_static) ? "" : (all_extern) ? " IS EXTERNAL" : " IS GLOBAL");
	printa(names[COLUMN], false,
		   "03  %s PIC S9(9) USAGE COMP OCCURS 20 TIMES.",
		   names[ISC_STATUS2]);
	printa(names[COLUMN_0], false, "01  %s PIC S9(9) USAGE COMP.",
		   names[ISC_ARRAY_LENGTH]);

	printa(names[COLUMN_0], false, "01  SQLCODE%s PIC S9(9) USAGE %s%s.",
		   (all_static) ? "" : (all_extern) ? " IS EXTERNAL" : " IS GLOBAL",
		   COMP_VALUE, (all_extern) ? "" : " VALUE IS 0");

	for (request = gpreGlob.requests; request; request = request->req_next) {
		gen_request(request);
		for (port = request->req_ports; port; port = port->por_next)
			make_port(port);
		for (blob = request->req_blobs; blob; blob = blob->blb_next) {
			printa(names[COLUMN_0], false, "01  %s%d PIC S9(9) USAGE COMP.",
				   names[isc_a_pos], blob->blb_ident);
			printa(names[COLUMN_0], false, "01  %s%d PIC X(%d).",
				   names[isc_a_pos], blob->blb_buff_ident, blob->blb_seg_length);
			printa(names[COLUMN_0], false, "01  %s%d PIC S9(4) USAGE %s.",
				   names[isc_a_pos], blob->blb_len_ident, COMP_VALUE);
		}

		//  Array declarations  

		if (port = request->req_primary)
			for (REF reference = port->por_references; reference;
				 reference = reference->ref_next)
			{
					if (reference->ref_field->fld_array_info)
						make_array_declaration(reference);
			}
	}

//  Generate event parameter block for each event 

	USHORT max_count = 0;
	for (stack_ptr = gpreGlob.events; stack_ptr; stack_ptr = stack_ptr->lls_next) {
		count = gen_event_block((const act*) stack_ptr->lls_object);
		max_count = MAX(count, max_count);
	}

	if (max_count) {
		printa(names[COLUMN_0], false, "01  %s.", names[ISC_EVENTS_VECTOR]);
		printa(names[COLUMN], false,
			   "03  %s PIC S9(9) USAGE COMP OCCURS %d TIMES.",
			   names[ISC_EVENTS], max_count);
		printa(names[COLUMN_0], false, "01  %s.",
			   names[ISC_EVENT_NAMES_VECTOR]);
		printa(names[COLUMN], false,
			   "03  %s PIC S9(9) USAGE COMP OCCURS %d TIMES.",
			   names[ISC_EVENT_NAMES], max_count);
		printa(names[COLUMN_0], false, "01  %s.",
			   names[ISC_EVENT_NAMES_VECTOR2]);
		printa(names[COLUMN], false, "03  %s PIC X(31) OCCURS %d TIMES.",
			   names[ISC_EVENT_NAMES2], max_count);
	}

	printa(names[COMMENT], false, "**** end of GPRE definitions ****\n");
}


//____________________________________________________________
//  
//		Generate a call to update metadata.
//  

static void gen_ddl( const act* action)
{
//  Set up command type for call to RDB$DDL

	const gpre_req* request = action->act_request;

	if (gpreGlob.sw_auto) {
		t_start_auto(0, status_vector(action), action, true);
		printa(names[COLUMN], false, "IF %s NOT = 0 THEN", names[isc_trans_pos]);
	}


	sprintf(output_buffer,
			"%sCALL \"%s\" USING %s, %s%s, %s%s, %s%d%s, %s%s%d\n",
			names[COLUMN], ISC_DDL, status_vector(action), BY_REF,
			request->req_database->dbb_name->sym_string, BY_REF,
			names[isc_trans_pos], BY_VALUE, request->req_length, END_VALUE,
			BY_REF, names[isc_a_pos], request->req_ident);

	COB_print_buffer(output_buffer, true);

	if (gpreGlob.sw_auto) {
		printa(names[COLUMN], false, "END-IF");
		printa(names[COLUMN], false, "IF %s(2) = 0 THEN", names[isc_status_pos]);
		printa(names[COLUMN], true,
			   "CALL \"%s\" USING %s, %s%s", ISC_COMMIT_TRANSACTION,
			   status_vector(action), BY_REF, names[isc_trans_pos]);
		printa(names[COLUMN], false, "END-IF");
		printa(names[COLUMN], false, "IF %s(2) NOT = 0 THEN",
			   names[isc_status_pos]);
		printa(names[COLUMN], true,
			   "CALL \"%s\" USING %s, %s%s",
			   ISC_ROLLBACK_TRANSACTION, OMITTED, BY_REF, names[isc_trans_pos]);
		printa(names[COLUMN], false, "END-IF");
	}
	COB_print_buffer(output_buffer, true);
	set_sqlcode(action);
}


//____________________________________________________________
//  
//		Generate a dynamic SQL statement.
//  

static void gen_dyn_close( const act* action)
{
	DYN statement;
	TEXT s[64];

	statement = (DYN) action->act_object;
#ifndef VMS
	make_name_formatted(s, "ISC-CONST-%s", statement->dyn_cursor_name);
#else
	make_name(s, statement->dyn_cursor_name);
#endif
	printa(names[COLUMN], true,
		   "CALL \"%s\" USING %s, %s%s",
		   ISC_CLOSE, status_vector(action), BY_REF, s);
	set_sqlcode(action);
}


//____________________________________________________________
//  
//		Generate a dynamic SQL statement.
//  

static void gen_dyn_declare( const act* action)
{
	DYN statement;
	TEXT s1[64], s2[64];

	statement = (DYN) action->act_object;

#ifndef VMS
	make_name_formatted(s1, "ISC-CONST-%s", statement->dyn_statement_name);
	make_name_formatted(s2, "ISC-CONST-%s", statement->dyn_cursor_name);
#else
	make_name(s1, statement->dyn_statement_name);
	make_name(s2, statement->dyn_cursor_name);
#endif

	printa(names[COLUMN], true,
		   "CALL \"%s\" USING %s, %s%s, %s%s",
		   ISC_DECLARE, status_vector(action), BY_REF, s1, BY_REF, s2);
	set_sqlcode(action);
}


//____________________________________________________________
//  
//		Generate a dynamic SQL statement.
//  

static void gen_dyn_describe(const act* action,
							 bool bind_flag)
{
	DYN statement;
	TEXT s[64];

	statement = (DYN) action->act_object;

#ifndef VMS
	make_name_formatted(s, "ISC-CONST-%s", statement->dyn_statement_name);
#else
	make_name(s, statement->dyn_statement_name);
#endif

	printa(names[COLUMN], true,
		   "CALL \"%s\" USING %s, %s%s, %s%d%s, %s%s",
		   bind_flag ? ISC_DESCRIBE_BIND : ISC_DESCRIBE,
		   status_vector(action),
		   BY_REF, s,
		   BY_VALUE, gpreGlob.sw_sql_dialect, END_VALUE, BY_REF, statement->dyn_sqlda);
	set_sqlcode(action);
}


//____________________________________________________________
//  
//		Generate a dynamic SQL statement.
//  

static void gen_dyn_execute( const act* action)
{
	TEXT s[64];
	gpre_req* request;
	gpre_req req_const;

	DYN statement = (DYN) action->act_object;
	const TEXT* transaction;
	if (statement->dyn_trans) {
		transaction = statement->dyn_trans;
		request = &req_const;
		request->req_trans = transaction;
	}
	else {
		transaction = names[isc_trans_pos];
		request = NULL;
	}

	if (gpreGlob.sw_auto) {
		t_start_auto(request, status_vector(action), action, true);
		printa(names[COLUMN], false, "IF %s NOT = 0 THEN", transaction);
	}

#ifndef VMS
	make_name_formatted(s, "ISC-CONST-%s", statement->dyn_statement_name);
#else
	make_name(s, statement->dyn_statement_name);
#endif

	printa(names[COLUMN], true,
		   (statement->dyn_sqlda2) ?
		   "CALL \"%s\" USING %s, %s%s, %s%s, %s%d%s, %s%s, %s%s" :
		   "CALL \"%s\" USING %s, %s%s, %s%s, %s%d%s, %s%s",
		   (statement->dyn_sqlda2) ? ISC_EXECUTE2 : ISC_EXECUTE,
		   status_vector(action),
		   BY_REF, transaction,
		   BY_REF, s,
		   BY_VALUE, gpreGlob.sw_sql_dialect, END_VALUE,
		   (statement->dyn_sqlda) ? BY_REF : "",
		   (statement->dyn_sqlda) ? statement->dyn_sqlda : OMITTED,
		   (statement->dyn_sqlda2) ? BY_REF : "",
		   (statement->dyn_sqlda2) ? statement->dyn_sqlda2 : OMITTED);

	if (gpreGlob.sw_auto)
		printa(names[COLUMN], false, "END-IF");

	set_sqlcode(action);
}


//____________________________________________________________
//  
//		Generate a dynamic SQL statement.
//  

static void gen_dyn_fetch( const act* action)
{
	DYN statement;
	TEXT s[64];

	statement = (DYN) action->act_object;

#ifndef VMS
	make_name_formatted(s, "ISC-CONST-%s", statement->dyn_cursor_name);
#else
	make_name(s, statement->dyn_cursor_name);
#endif

	printa(names[COLUMN], true, FETCH_CALL_TEMPLATE,
		   ISC_FETCH,
		   status_vector(action),
		   BY_REF, s,
		   BY_VALUE, gpreGlob.sw_sql_dialect, END_VALUE,
		   (statement->dyn_sqlda) ? BY_REF : "",
		   (statement->dyn_sqlda) ? statement->dyn_sqlda : OMITTED);

	printa(names[COLUMN], false, "IF SQLCODE NOT = 100 THEN");

	set_sqlcode(action);

	printa(names[COLUMN], false, "END-IF");
}


//____________________________________________________________
//  
//		Generate code for an EXECUTE IMMEDIATE dynamic SQL statement.
//  

static void gen_dyn_immediate( const act* action)
{
	TEXT s[64];
	gpre_req* request;
	gpre_req req_const;

	DYN statement = (DYN) action->act_object;
	const TEXT* transaction;
	if (statement->dyn_trans) {
		transaction = statement->dyn_trans;
		request = &req_const;
		request->req_trans = transaction;
	}
	else {
		transaction = names[isc_trans_pos];
		request = NULL;
	}

	const dbb* database = statement->dyn_database;

#ifndef VMS
	const TEXT* s2 = "ISC-CONST-DYN-IMMEDL";
	printa(names[COLUMN], true, GET_LEN_CALL_TEMPLATE,
		   STRING_LENGTH, statement->dyn_string, s2);
	sprintf(s, " %s%s%s,", BY_VALUE, s2, END_VALUE);
#else
	s[0] = 0;
#endif

	if (gpreGlob.sw_auto) {
		t_start_auto(request, status_vector(action), action, true);
		printa(names[COLUMN], false, "IF %s NOT = 0 THEN", transaction);
	}

	printa(names[COLUMN], true,
		   (statement->dyn_sqlda2) ?
		    "CALL \"%s\" USING %s, %s%s, %s%s,%s %s%s, %s%d%s, %s%s, %s%s" :
		    "CALL \"%s\" USING %s, %s%s, %s%s,%s %s%s, %s%d%s, %s%s",
		   (statement->dyn_sqlda2) ?
				ISC_EXECUTE_IMMEDIATE2 : ISC_EXECUTE_IMMEDIATE,
		   status_vector(action), BY_REF, database->dbb_name->sym_string,
		   BY_REF, transaction, s, BY_DESC, statement->dyn_string, BY_VALUE,
		   gpreGlob.sw_sql_dialect, END_VALUE, (statement->dyn_sqlda) ? BY_REF : "",
		   (statement->dyn_sqlda) ? statement->dyn_sqlda : OMITTED,
		   (statement->dyn_sqlda2) ? BY_REF : "",
		   (statement->dyn_sqlda2) ? statement->dyn_sqlda2 : OMITTED);

	if (gpreGlob.sw_auto)
		printa(names[COLUMN], false, "END-IF");

	set_sqlcode(action);
}


//____________________________________________________________
//  
//		Generate a dynamic SQL statement.
//  

static void gen_dyn_insert( const act* action)
{
	DYN statement;
	TEXT s[64];

	statement = (DYN) action->act_object;

#ifndef VMS
	make_name_formatted(s, "ISC-CONST-%s", statement->dyn_cursor_name);
#else
	make_name(s, statement->dyn_cursor_name);
#endif

	printa(names[COLUMN], true,
		   "CALL \"%s\" USING %s, %s%s, %s%d%s, %s%s",
		   ISC_INSERT,
		   status_vector(action),
		   BY_REF, s,
		   BY_VALUE, gpreGlob.sw_sql_dialect, END_VALUE,
		   (statement->dyn_sqlda) ? BY_REF : "",
		   (statement->dyn_sqlda) ? statement->dyn_sqlda : OMITTED);

	set_sqlcode(action);
}


//____________________________________________________________
//  
//		Generate a dynamic SQL statement.
//  

static void gen_dyn_open( const act* action)
{
	TEXT s[64];
	gpre_req* request;
	gpre_req req_const;

	DYN statement = (DYN) action->act_object;
	const TEXT* transaction;
	if (statement->dyn_trans) {
		transaction = statement->dyn_trans;
		request = &req_const;
		request->req_trans = transaction;
	}
	else {
		transaction = names[isc_trans_pos];
		request = NULL;
	}

#ifndef VMS
	make_name_formatted(s, "ISC-CONST-%s", statement->dyn_cursor_name);
#else
	make_name(s, statement->dyn_cursor_name);
#endif

	if (gpreGlob.sw_auto) {
		t_start_auto(request, status_vector(action), action, true);
		printa(names[COLUMN], false, "IF %s NOT = 0 THEN", transaction);
	}

	printa(names[COLUMN], true,
		   (statement->dyn_sqlda2) ?
		   "CALL \"%s\" USING %s, %s%s, %s%s, %s%d%s, %s%s, %s%s" :
		   "CALL \"%s\" USING %s, %s%s, %s%s, %s%d%s, %s%s",
		   (statement->dyn_sqlda2) ? ISC_OPEN2 : ISC_OPEN,
		   status_vector(action),
		   BY_REF, transaction,
		   BY_REF, s,
		   BY_VALUE, gpreGlob.sw_sql_dialect, END_VALUE,
		   (statement->dyn_sqlda) ? BY_REF : "",
		   (statement->dyn_sqlda) ? statement->dyn_sqlda : OMITTED,
		   (statement->dyn_sqlda2) ? BY_REF : "",
		   (statement->dyn_sqlda2) ? statement->dyn_sqlda2 : OMITTED);

	if (gpreGlob.sw_auto)
		printa(names[COLUMN], false, "END-IF");

	set_sqlcode(action);
}


//____________________________________________________________
//  
//		Generate a dynamic SQL statement.
//  

static void gen_dyn_prepare( const act* action)
{
	TEXT s[64], s2[64], s3[64];
	gpre_req* request;
	gpre_req req_const;

	DYN statement = (DYN) action->act_object;
	const dbb* database = statement->dyn_database;

	const TEXT* transaction;
	if (statement->dyn_trans) {
		transaction = statement->dyn_trans;
		request = &req_const;
		request->req_trans = transaction;
	}
	else {
		transaction = names[isc_trans_pos];
		request = NULL;
	}

#ifndef VMS
	make_name_formatted(s, "ISC-CONST-%s", statement->dyn_statement_name);
	sprintf(s2, "%sL", s);
	printa(names[COLUMN], true, GET_LEN_CALL_TEMPLATE,
		   STRING_LENGTH, statement->dyn_string, s2);
	sprintf(s3, " %s%s%s,", BY_VALUE, s2, END_VALUE);
#else
	make_name(s, statement->dyn_statement_name);
	s3[0] = 0;
#endif

	if (gpreGlob.sw_auto) {
		t_start_auto(request, status_vector(action), action, true);
		printa(names[COLUMN], false, "IF %s NOT = 0 THEN", transaction);
	}

	printa(names[COLUMN], true,
		   "CALL \"%s\" USING %s, %s%s, %s%s, %s%s,%s %s%s, %s%d%s, %s%s",
		   ISC_PREPARE,
		   status_vector(action),
		   BY_REF, database->dbb_name->sym_string,
		   BY_REF, transaction,
		   BY_REF, s,
		   s3,
		   BY_DESC, statement->dyn_string,
		   BY_VALUE, gpreGlob.sw_sql_dialect, END_VALUE,
		   (statement->dyn_sqlda) ? BY_REF : "",
		   (statement->dyn_sqlda) ? statement->dyn_sqlda : OMITTED);

	if (gpreGlob.sw_auto)
		printa(names[COLUMN], false, "END-IF");

	set_sqlcode(action);
}


//____________________________________________________________
//  
//		Generate substitution text for END_MODIFY.
//  

static void gen_emodify( const act* action)
{
	upd* modify;
	REF reference, source;
	gpre_fld* field;
	TEXT s1[20], s2[20];

	modify = (upd*) action->act_object;

	for (reference = modify->upd_port->por_references; reference;
		 reference = reference->ref_next) {
		if (!(source = reference->ref_source))
			continue;
		field = reference->ref_field;
		gen_name(s1, source, true);
		gen_name(s2, reference, true);
		if (field->fld_dtype == dtype_blob ||
			field->fld_dtype == dtype_quad || field->fld_dtype == dtype_date)
			sprintf(output_buffer, "%sCALL \"isc_qtoq\" USING %s, %s\n",
					names[COLUMN], s1, s2);
		else
			sprintf(output_buffer, "%sMOVE %s TO %s\n",
					names[COLUMN], s1, s2);
		COB_print_buffer(output_buffer, true);
		if (field->fld_array_info)
			gen_get_or_put_slice(action, reference, false);
	}

	gen_send(action, modify->upd_port);
}


//____________________________________________________________
//  
//		Generate substitution text for END_STORE.
//  

static void gen_estore( const act* action)
{
	gpre_req* request;

	request = action->act_request;
	gen_start(action, request->req_primary);
}


//____________________________________________________________
//  
//		Generate end-if for AT_END if statement
//  

static void gen_end_fetch( const act* action)
{

	printa(names[COLUMN], false, "END-IF");

}


//____________________________________________________________
//  
//		Generate definitions associated with a single request.
//  

static void gen_endfor( const act* action)
{
	gpre_req* request;

	request = action->act_request;

	if (request->req_sync)
		gen_send(action, request->req_sync);
	gen_receive(action, request->req_primary);
	printa(names[COLUMN], false, "END-PERFORM");
}


//____________________________________________________________
//  
//		Generate substitution text for ERASE.
//  

static void gen_erase( const act* action)
{
	upd* erase;

	erase = (upd*) action->act_object;
	gen_send(action, erase->upd_port);
}


//____________________________________________________________
//  
//		Generate event parameter blocks for use
//		with a particular call to isc_event_wait.
//  

static SSHORT gen_event_block( const act* action)
{
	GPRE_NOD init, list;
	int ident;

	init = (GPRE_NOD) action->act_object;

	ident = CMP_next_ident();
	init->nod_arg[2] = (GPRE_NOD) ident;

	printa(names[COLUMN_0], false, "01  %s%dA PIC S9(9) USAGE COMP.",
		   names[isc_a_pos], ident);
	printa(names[COLUMN_0], false, "01  %s%dB PIC S9(9) USAGE COMP.",
		   names[isc_a_pos], ident);
	printa(names[COLUMN_0], false, "01  %s%dL PIC S9(4) USAGE COMP.",
		   names[isc_a_pos], ident);

	list = init->nod_arg[1];

	return list->nod_count;
}


//____________________________________________________________
//  
//		Generate substitution text for EVENT_INIT.
//  

static void gen_event_init( const act* action)
{
	GPRE_NOD init, event_list, *ptr, *end, node;
	REF reference;
	PAT args;
	TEXT variable[20];
	SSHORT column, count;
#ifdef GIVING_SUPPORTED
	const TEXT *pattern1 =
		"CALL \"%S1\" USING %RF%S4%N1A%RE, %RF%S4%N1B%RE, %VF%N2%VE, %RF%S6%RE GIVING %S4%N1L";
#else
	const TEXT *pattern1 =
		"CALL \"%S1\" USING %RF%S4%N1A%RE, %RF%S4%N1B%RE, %VF%N2%VE, %RF%S6%RE, %RF%S4%N1L%RE";
#endif
	const TEXT *pattern2 =
		"CALL \"%S2\" USING %V1, %RF%DH%RE, %VF%S4%N1L%VE, %VF%S4%N1A%VE, %VF%S4%N1B%VE";
	const TEXT *pattern3 =
		"CALL \"%S3\" USING %S5, %VF%S4%N1L%VE, %VF%S4%N1A%VE, %VF%S4%N1B%VE";

	init = (GPRE_NOD) action->act_object;
	event_list = init->nod_arg[1];

	column = strlen(names[COLUMN]);

	args.pat_database = (DBB) init->nod_arg[3];
	args.pat_vector1 = status_vector(action);
	args.pat_value1 = (int) init->nod_arg[2];
	args.pat_value2 = (int) event_list->nod_count;
	args.pat_string1 = ISC_EVENT_BLOCK;
	args.pat_string2 = ISC_EVENT_WAIT;
	args.pat_string3 = ISC_EVENT_COUNTS;
	args.pat_string4 = names[isc_a_pos];
	args.pat_string5 = names[ISC_EVENTS_VECTOR];
	args.pat_string6 = names[ISC_EVENT_NAMES_VECTOR];

//  generate call to dynamically generate event blocks 

	for (ptr = event_list->nod_arg, count = 0, end =
		 ptr + event_list->nod_count; ptr < end; ptr++)
	{
		count++;
		node = *ptr;
		if (node->nod_type == nod_field) {
			reference = (REF) node->nod_arg[0];
			gen_name(variable, reference, true);
			printa(names[COLUMN], false, "MOVE %s TO %s(%d)", variable,
				   names[ISC_EVENT_NAMES2], count);
		}
		else
			printa(names[COLUMN], false, "MOVE %s TO %s(%d)",
				   (TEXT *) node->nod_arg[0], names[ISC_EVENT_NAMES2], count);

		printa(names[COLUMN], true, EVENT_MOVE_TEMPLATE, ISC_BADDRESS,
				names[ISC_EVENT_NAMES2], count, names[ISC_EVENT_NAMES], count);
	}

	PATTERN_expand(column, pattern1, &args);

//  generate actual call to event_wait 

	PATTERN_expand(column, pattern2, &args);

//  get change in event counts, copying event parameter block for reuse 

	PATTERN_expand(column, pattern3, &args);
	set_sqlcode(action);
}


//____________________________________________________________
//  
//		Generate substitution text for EVENT_WAIT.
//  

static void gen_event_wait( const act* action)
{
	PAT args;
	GPRE_NOD event_init;
	gpre_sym* event_name;
	gpre_sym* stack_name;
	DBB database = NULL;
	gpre_lls* stack_ptr;
	const act* event_action;
	SSHORT column;
	int ident;
	TEXT s[64];
	const TEXT *pattern1 =
		"CALL \"%S2\" USING %V1, %RF%DH%RE, %VF%S4%N1L%VE, %VF%S4%N1A%VE, %VF%S4%N1B%VE";
	const TEXT *pattern2 =
		"CALL \"%S3\" USING %S5, %VF%S4%N1L%VE, %VF%S4%N1A%VE, %VF%S4%N1B%VE";

	event_name = (gpre_sym*) action->act_object;

//  go through the stack of gpreGlob.events, checking to see if the
//  event has been initialized and getting the event identifier 

	ident = -1;
	for (stack_ptr = gpreGlob.events; stack_ptr; stack_ptr = stack_ptr->lls_next) {
		event_action = (const act*) stack_ptr->lls_object;
		event_init = (GPRE_NOD) event_action->act_object;
		stack_name = (gpre_sym*) event_init->nod_arg[0];
		if (!strcmp(event_name->sym_string, stack_name->sym_string)) {
			ident = (int) event_init->nod_arg[2];
			database = (DBB) event_init->nod_arg[3];
		}
	}

	if (ident < 0) {
		sprintf(s, "event handle \"%s\" not found", event_name->sym_string);
		CPR_error(s);
		return; // silence non initialized warning
	}

	column = strlen(names[COLUMN]);

	args.pat_database = database;
	args.pat_vector1 = status_vector(action);
	args.pat_value1 = (int) ident;
	args.pat_string2 = ISC_EVENT_WAIT;
	args.pat_string3 = ISC_EVENT_COUNTS;
	args.pat_string4 = names[isc_a_pos];
	args.pat_string5 = names[ISC_EVENTS_VECTOR];

//  generate calls to wait on the event and to fill out the gpreGlob.events array 

	PATTERN_expand(column, pattern1, &args);
	PATTERN_expand(column, pattern2, &args);
	set_sqlcode(action);
}


//____________________________________________________________
//  
//		Generate replacement text for the SQL FETCH statement.  The
//		epilog FETCH statement is handled by GEN_S_FETCH (generate
//		stream fetch).
//  

static void gen_fetch( const act* action)
{
	gpre_req* request;
	GPRE_NOD var_list;
	int i;
	SCHAR s[20];
#ifdef SCROLLABLE_CURSORS
	gpre_port* port;
	REF reference;
	const char *direction, *offset;
	VAL value;
#endif

	request = action->act_request;

#ifdef SCROLLABLE_CURSORS
	if (port = request->req_aport) {
		/* set up the reference to point to the correct value 
		   in the linked list of values, and prepare for the 
		   next FETCH statement if applicable */

		for (reference = port->por_references; reference;
			 reference = reference->ref_next) {
			value = reference->ref_values;
			reference->ref_value = value->val_value;
			reference->ref_values = value->val_next;
		}

		// find the direction and offset parameters 

		reference = port->por_references;
		offset = reference->ref_value;
		reference = reference->ref_next;
		direction = reference->ref_value;

		/* the direction in which the engine will scroll is sticky, so check to see 
		   the last direction passed to the engine; if the direction is the same and 
		   the offset is 1, then there is no need to pass the message; this prevents 
		   extra packets and allows for batch fetches in either direction */

		printa(names[COLUMN], false,
			   "IF %s%dDI MOD 2 NOT = %s || %s NOT = 1 THEN", names[isc_a_pos],
			   request->req_ident, direction, offset);

		/* assign the direction and offset parameters to the appropriate message, 
		   then send the message to the engine */

		asgn_from(action, port->por_references);
		gen_send(action, port);
		printa(names[COLUMN], false, "MOVE %s TO %s%dDI",
			   names[isc_a_pos], direction, request->req_ident);
		printa(names[COLUMN], false, "END-IF");

		printa(names[COLUMN], false, "IF SQLCODE NOT = 0 THEN");
	}
#endif

	if (request->req_sync)
		gen_send(action, request->req_sync);

	gen_receive(action, request->req_primary);
	printa(names[COLUMN], false,
		   "IF %s NOT =  0 THEN", gen_name(s, request->req_eof, true));
	printa(names[COLUMN], false, "MOVE 0 TO SQLCODE");
	if (var_list = (GPRE_NOD) action->act_object)
		for (i = 0; i < var_list->nod_count; i++) {
			asgn_to(action, (REF) var_list->nod_arg[i]);
		}
	printa(names[COLUMN], false, "ELSE");
	printa(names[COLUMN], false, "MOVE 100 TO SQLCODE");
	printa(names[COLUMN], false, "END-IF");

#ifdef SCROLLABLE_CURSORS
	if (port)
		printa(names[COLUMN], false, "END-IF");
#endif
}


//____________________________________________________________
//  
//		Generate substitution text for FINISH
//  

static void gen_finish( const act* action)
{
	DBB db;
	rdy* ready;

	if (gpreGlob.sw_auto || ((action->act_flags & ACT_sql) &&
					(action->act_type != ACT_disconnect)))
	{
		printa(names[COLUMN], false, "IF %s NOT = 0 THEN", names[isc_trans_pos]);
		printa(names[COLUMN], true,
			   "    CALL \"%s\" USING %s, %s%s",
			   (action->act_type !=
				ACT_rfinish) ? ISC_COMMIT_TRANSACTION :
			   ISC_ROLLBACK_TRANSACTION, status_vector(action), BY_REF,
			   names[isc_trans_pos]);
		printa(names[COLUMN], false, "END-IF");
	}

//  the user may have supplied one or more handles 

	db = NULL;
	for (ready = (rdy*) action->act_object; ready; ready = ready->rdy_next) {
		db = ready->rdy_database;
		printa(names[COLUMN], false,
			   "IF %s NOT = 0 THEN", db->dbb_name->sym_string);
		printa(names[COLUMN], true,
			   "CALL \"%s\" USING %s, %s%s",
			   ISC_DETACH_DATABASE,
			   status_vector(action), BY_REF, db->dbb_name->sym_string);
		printa(names[COLUMN], false, "END-IF");
	}

//  no handles, so finish all known databases 

	if (!db)
		for (db = gpreGlob.isc_databases; db; db = db->dbb_next) {
			printa(names[COLUMN], false,
				   "IF %s NOT = 0 THEN", db->dbb_name->sym_string);
			printa(names[COLUMN], true,
				   "CALL \"%s\" USING %s, %s%s",
				   ISC_DETACH_DATABASE,
				   status_vector(action), BY_REF, db->dbb_name->sym_string);
			printa(names[COLUMN], false, "END-IF");
		}
	set_sqlcode(action);
}


//____________________________________________________________
//  
//		Generate substitution text for FOR statement.
//  

static void gen_for( const act* action)
{
	gpre_req* request;
	TEXT s[20];
	REF reference;
	gpre_port* port;

	gen_s_start(action);
	request = action->act_request;
	gen_receive(action, request->req_primary);
	printa(names[COLUMN], false, "PERFORM UNTIL %s = 0",
		   gen_name(s, request->req_eof, true));

	if (port = action->act_request->req_primary)
		for (reference = port->por_references; reference;
			 reference = reference->ref_next)
		{
			if (reference->ref_field->fld_array_info)
				gen_get_or_put_slice(action, reference, true);
		}
}


//____________________________________________________________
//  
//		Generate a function for free standing ANY or statistical.
//  

static void gen_function( const act* function)
{
	REF reference;
	gpre_fld* field;
	TEXT s[64];
	const char* dtype;

	const act* action = (const act*) function->act_object;

	if (action->act_type != ACT_any) {
		CPR_error("can't generate function");
		return;
	}

	gpre_req* request = action->act_request;

	fprintf(gpreGlob.out_file, "static %s_r (request, transaction", request->req_handle);

	gpre_port* port = request->req_vport;
	if (port)
		for (reference = port->por_references; reference;
			 reference = reference->ref_next)
		{
			fprintf(gpreGlob.out_file, ", %s",
					   gen_name(s, reference->ref_source, true));
		}

	fprintf(gpreGlob.out_file,
			   ")\n    isc_req_handle\trequest;\n    isc_tr_handle\ttransaction;\n");

	if (port)
		for (reference = port->por_references; reference;
			 reference = reference->ref_next)
		{
			field = reference->ref_field;
			switch (field->fld_dtype) {
			case dtype_short:
				dtype = "short";
				break;

			case dtype_long:
				dtype = "long";
				break;

			case dtype_cstring:
			case dtype_text:
				dtype = "char*";
				break;

			case dtype_quad:
				dtype = "ISC_QUAD";
				break;

			case dtype_date:
			case dtype_blob:
				dtype = "ISC_QUAD";
				break;

			case dtype_real:
				dtype = "float";
				break;

			case dtype_double:
				dtype = "double";
				break;

			default:
				CPR_error("gen_function: unsupported datatype");
				return;
			}
			fprintf(gpreGlob.out_file, "    %s\t%s;\n", dtype,
					   gen_name(s, reference->ref_source, true));
		}

	fprintf(gpreGlob.out_file, "{\n");
	for (port = request->req_ports; port; port = port->por_next)
		make_port(port);

	fprintf(gpreGlob.out_file, "\n\n");
	gen_s_start(action);
	gen_receive(action, request->req_primary);

	for (port = request->req_ports; port; port = port->por_next)
		for (reference = port->por_references; reference;
			 reference = reference->ref_next)
		{
			if (reference->ref_field->fld_array_info)
				gen_get_or_put_slice(action, reference, true);
		}

	port = request->req_primary;
	fprintf(gpreGlob.out_file, "\nreturn %s;\n}\n",
			   gen_name(s, port->por_references, true));
}


//____________________________________________________________
//  
//		Generate a call to isc_get_slice
//		or isc_put_slice for an array.
//  

static void gen_get_or_put_slice(const act* action,
								 REF reference,
								 bool get)
{
	int column;
	PAT args;
	TEXT s1[25], s2[10], s4[10];
	const TEXT* pattern1 =
		"CALL \"%S7\" USING %V1, %RF%DH%RE, %RF%S1%RE, %RF%S2%RE, %VF%N1%VE, %RF%S3%RE, %VF%N2%VE, %VF%S4%VE, %VF%L1%VE, %RF%S5%RE, %RF%S6%RE";
	const TEXT* pattern2 =
		"CALL \"%S7\" USING %V1, %RF%DH%RE, %RF%S1%RE, %RF%S2%RE, %VF%N1%VE, %RF%S3%RE, %VF%N2%VE, %VF%S4%VE, %VF%L1%VE, %RF%S5%RE";

	if (!(reference->ref_flags & REF_fetch_array))
		return;

	column = strlen(names[COLUMN]);

	args.pat_condition = get;	//  get or put slice
	args.pat_vector1 = status_vector(action);	//  status vector
	args.pat_database = action->act_request->req_database;	// database handle
	args.pat_string1 = action->act_request->req_trans;	//  transaction handle

	gen_name(s1, reference, true);	//  blob handle
	args.pat_string2 = s1;

	args.pat_value1 = reference->ref_sdl_length;	//  slice descr length 

	sprintf(s2, "%s%d", names[isc_a_pos], reference->ref_sdl_ident);	//  slice description  
	args.pat_string3 = s2;

	args.pat_value2 = 0;		//  parameter length  

	args.pat_string4 = "0";		//  parameter  

	args.pat_long1 = reference->ref_field->fld_array_info->ary_size;
	//  slice size  
	if (action->act_flags & ACT_sql) {
		args.pat_string5 = reference->ref_value;
	}
	else {
		sprintf(s4, "%s%dL", names[isc_a_pos],
				reference->ref_field->fld_array_info->ary_ident);
		args.pat_string5 = s4;	//  array name  
	}

	args.pat_string6 = names[ISC_ARRAY_LENGTH];	//  return length 
	args.pat_string7 = (get) ? ISC_GET_SLICE : ISC_PUT_SLICE;

	PATTERN_expand(column, (get) ? pattern1 : pattern2, &args);
	fprintf(gpreGlob.out_file, "\n");
}


//____________________________________________________________
//  
//		Generate the code to do a get segment.
//  

static void gen_get_segment( const act* action)
{
	blb* blob;
	REF into;
	TEXT buffer[128];

	if (action->act_flags & ACT_sql)
		blob = (blb*) action->act_request->req_blobs;
	else
		blob = (blb*) action->act_object;

	strcpy(buffer, GET_SEG_CALL_TEMPLATE);
	sprintf(output_buffer,
			buffer,
			names[COLUMN],
			ISC_GET_SEGMENT,
			names[isc_status_vector_pos],
			names[isc_a_pos], blob->blb_ident,
			names[isc_a_pos], blob->blb_len_ident,
			BY_VALUE, blob->blb_seg_length, END_VALUE,
			BY_REF, names[isc_a_pos], blob->blb_buff_ident, names[isc_status_pos]);

	COB_print_buffer(output_buffer, true);

	if (action->act_flags & ACT_sql) {
		into = action->act_object;
		set_sqlcode(action);
		printa(names[COLUMN], false, "IF SQLCODE = 0 OR SQLCODE = 101 THEN ");
		printa(names[COLUMN], false, "MOVE ISC-%d TO %s",
			   blob->blb_buff_ident, into->ref_value);
		if (into->ref_null_value)
			printa(names[COLUMN], false, "MOVE ISC-%d TO %s",
				   blob->blb_len_ident, into->ref_null_value);
		printa(names[COLUMN], false, "END-IF");
	}
}


//____________________________________________________________
//  
//		Generate text to compile and start a SQL mass update. 
//		
//  

static void gen_loop( const act* action)
{
	gpre_req* request;
	gpre_port* port;
	TEXT name[20];

	gen_s_start(action);
	request = action->act_request;
	printa(names[COLUMN], false, "IF SQLCODE = 0 THEN");
	port = request->req_primary;
	gen_receive(action, port);
	gen_name(name, port->por_references, true);
	printa(names[COLUMN], false, "IF SQLCODE = 0 AND %s = 0 THEN ", name);
	printa(names[COLUMN], false, "MOVE 100 TO SQLCODE");
	printa(names[COLUMN], false, "END-IF");
	printa(names[COLUMN], false, "END-IF");
}


//____________________________________________________________
//  
//		Generate a name for a reference.  Name is constructed from
//		port and parameter idents.
//  

static TEXT* gen_name(TEXT* string,
					  const ref* reference,
					  bool as_blob)
{

	if (reference->ref_field->fld_array_info && !as_blob)
		sprintf(string, "%s%d", names[isc_a_pos],
				reference->ref_field->fld_array_info->ary_ident);
	else
		sprintf(string, "%s%d", names[isc_b_pos], reference->ref_ident);

	return string;
}


//____________________________________________________________
//  
//		Generate a block to handle errors.
//  

static void gen_on_error( const act* action)
{

	printa(names[COLUMN], false, "IF %s (2) NOT = 0 THEN", names[isc_status_pos]);
	fprintf(gpreGlob.out_file, names[COLUMN]);
}


//____________________________________________________________
//  
//		Generate code for an EXECUTE PROCEDURE.
//  

static void gen_procedure( const act* action)
{
	gpre_req* request = action->act_request;
	gpre_port* in_port = request->req_vport;
	gpre_port* out_port = request->req_primary;

	PAT args;
	args.pat_database = request->req_database;
	args.pat_request = action->act_request;
	args.pat_vector1 = status_vector(action);
	args.pat_request = request;
	args.pat_port = in_port;
	args.pat_port2 = out_port;
	const TEXT* pattern;
#ifndef VMS
	if (in_port && in_port->por_length)
		pattern =
			"CALL \"isc_transact_request\" USING %V1, %RF%DH%RE, %RF%RT%RE, %VF%RS%VE, %RF%RI%RE, %VF%PL%VE, %RF%PI%RE, %VF%QL%VE, %RF%QI%RE\n";
	else
		pattern =
			"CALL \"isc_transact_request\" USING %V1, %RF%DH%RE, %RF%RT%RE, %VF%RS%VE, %RI, %VF0%VE, 0, %VF%QL%VE, %RF%QI%RE\n";
#else
	if (in_port && in_port->por_length)
		pattern =
			"CALL \"isc_transact_request\" USING %V1, %RF%DH%RE, %RF%RT%RE, %VF%RS%VE, %RF%RI%RE, %VF%PL%VE, %RF%PI%RE, %VF%QL%VE, %RF%QI%RE\n";
	else
		pattern =
			"CALL \"isc_transact_request\" USING %V1, %RF%DH%RE, %RF%RT%RE, %VF%RS%VE, %RI, %VF0%VE, 0, %VF%QL%VE, %RF%QI%RE\n";
#endif

//  Get database attach and transaction started 

	if (gpreGlob.sw_auto)
		t_start_auto(0, status_vector(action), action, true);

//  Move in input values 

	asgn_from(action, request->req_values);

//  Execute the procedure 

	const USHORT column = strlen(names[COLUMN]);
	PATTERN_expand(column, pattern, &args);

	set_sqlcode(action);

	printa(names[COLUMN], false, "IF SQLCODE = 0 THEN");

//  Move out output values 

	asgn_to_proc(request->req_references);
	printa(names[COLUMN], false, "END-IF");
}


//____________________________________________________________
//  
//		Generate the code to do a put segment.
//  

static void gen_put_segment( const act* action)
{
	blb* blob;
	REF from;
	TEXT buffer[128];

	if (action->act_flags & ACT_sql) {
		blob = (blb*) action->act_request->req_blobs;
		from = action->act_object;
		printa(names[COLUMN], false, "MOVE %s TO ISC-%d",
			   from->ref_value, blob->blb_buff_ident);
		printa(names[COLUMN], false, "MOVE %s TO ISC-%d",
			   from->ref_null_value, blob->blb_len_ident);
	}
	else
		blob = (blb*) action->act_object;

	strcpy(buffer, PUT_SEG_CALL_TEMPLATE);
	sprintf(output_buffer,
			buffer,
			names[COLUMN],
			ISC_PUT_SEGMENT,
			status_vector(action),
			BY_REF, names[isc_a_pos], blob->blb_ident,
			BY_VALUE, names[isc_a_pos], blob->blb_len_ident, END_VALUE,
			BY_REF, names[isc_a_pos], blob->blb_buff_ident, names[isc_status_pos]);
	COB_print_buffer(output_buffer, true);

	set_sqlcode(action);
}


//____________________________________________________________
//  
//		Generate BLR in raw, numeric form.  Ughly but dense.
//  

static void gen_raw(
			   UCHAR * blr,
			   enum req_t request_type, int request_length, int ident)
{
	UCHAR *c;
	TEXT s[256];
	int blr_length, length;
	union {
#ifdef VMS
		SCHAR bytewise_blr[4];
		SLONG longword_blr;
#else
		UCHAR bytewise_blr[4];
		ULONG longword_blr;
#endif
	} blr_hunk;

	length = 1;
	blr_length = request_length;

	while (blr_length) {
		s[0] = 0;
		blr_hunk.longword_blr = 0;
		for (c = blr_hunk.bytewise_blr;
			 c < blr_hunk.bytewise_blr + sizeof(SLONG); c++)
		{
			if (--blr_length)
				*c = *blr++;
			else {
				if (request_type == REQ_slice)
					*c = isc_sdl_eoc;
				else if ((request_type == REQ_ddl) ||
						 (request_type == REQ_create_database))
				{
					*c = *blr++;
				}
				else
					*c = blr_eoc;
				break;
			}
		}

		strcat(s, names[COLUMN]);
		strcat(s, RAW_BLR_TEMPLATE);
		strcat(s, "\n");
		sprintf(output_buffer, s, names[isc_a_pos], ident, names[UNDER], length++,
				blr_hunk.longword_blr);
		COB_print_buffer(output_buffer, false);
	}
}


//____________________________________________________________
//  
//		Generate substitution text for READY
//		This becomes baroque for mpexl where we
//		must generate a variable if the user gives us
//		a string literal. mpexl cobol doesn't take
//		string literals as  CALL parameters.
//  

static void gen_ready( const act* action)
{
	rdy* ready;
	DBB db, dbisc;
	TEXT dbname[96];
	USHORT namelength;

	const TEXT* vector = status_vector(action);

	for (ready = (rdy*) action->act_object; ready; ready = ready->rdy_next) {
		db = ready->rdy_database;
		dbisc = (DBB) db->dbb_name->sym_object;
		const TEXT* filename = ready->rdy_filename;
		if (!filename) {
			filename = db->dbb_runtime;
			if (filename) {
				namelength = strlen(filename);
#ifndef VMS
				sprintf(dbname, "isc-%ddb", dbisc->dbb_id);
				filename = dbname;
#endif
			}
			else
				namelength = 0;
		}
		else
			namelength = strlen(filename);
#ifndef VMS
		/* string literal or user defined variable? */

		if (ready->rdy_id) {
			sprintf(dbname, "isc-%ddb", ready->rdy_id);
			filename = dbname;
			namelength -= 2;
		}
		else
			namelength = 0;
#endif
		make_ready(db, filename, vector, ready->rdy_request, namelength);
		set_sqlcode(action);
	}

	fprintf(gpreGlob.out_file, names[COLUMN]);
}


//____________________________________________________________
//  
//		Generate substitution text for RELEASE_REQUESTS
//		For active databases, call isc_release_request.
//		for all others, just zero the handle.  For the
//		release request calls, ignore error returns, which
//		are likely if the request was compiled on a database
//		which has been released and re-readied.  If there is
//		a serious error, it will be caught on the next statement.
//  

static void gen_release( const act* action)
{
	DBB db, exp_db;
	gpre_req* request;

	exp_db = (DBB) action->act_object;

	for (request = gpreGlob.requests; request; request = request->req_next) {
		db = request->req_database;
		if (exp_db && db != exp_db)
			continue;
		if (!(request->req_flags & REQ_exp_hand)) {
			printa(names[COLUMN], false, "IF %s = 0 THEN",
				   db->dbb_name->sym_string);
			printa(names[COLUMN], true, "CALL \"%s\" USING %s, %s%s",
				   ISC_RELEASE_REQUEST, status_vector(action), BY_REF,
				   request->req_handle);
			printa(names[COLUMN], false, "END-IF");
			printa(names[COLUMN], false, "MOVE 0 to %s", request->req_handle);
		}
	}
}


//____________________________________________________________
//  
//		Generate a send or receive call for a port.
//  

static void gen_receive( const act* action, gpre_port* port)
{
	gpre_req* request;

	request = action->act_request;
	sprintf(output_buffer,
			"%sCALL \"%s\" USING %s, %s%s, %s%d%s, %s%d%s, %s%s%d, %s%s%s\n",
			names[COLUMN],
			ISC_RECEIVE,
			status_vector(action),
			BY_REF, request->req_handle,
			BY_VALUE, port->por_msg_number, END_VALUE,
			BY_VALUE, port->por_length, END_VALUE,
			BY_REF, names[isc_a_pos], port->por_ident,
			BY_VALUE, request->req_request_level, END_VALUE);
	COB_print_buffer(output_buffer, true);

	set_sqlcode(action);
}


//____________________________________________________________
//  
//		Generate definitions associated with a single request.
//  	Requests are generated as raw BLR in longword chunks 
//  	because COBOL is a miserable excuse for a language
//  	and doesn't allow byte value assignments to character
//  	fields.
//  

static void gen_request( gpre_req* request)
{
	REF reference;
	blb* blob;
	gpre_port* port;

	if (!(request->req_flags & (REQ_exp_hand | REQ_sql_blob_open 
		| REQ_sql_blob_create)) && request->req_type != REQ_slice
		&& request->req_type != REQ_procedure)
	{
		printa(names[COLUMN_0], false,
			   "01  %s PIC S9(9) USAGE COMP VALUE IS 0.",
			   request->req_handle);
	}

	if (request->req_type == REQ_ready)
		printa(names[COLUMN_0], false,
			   "01  %s%dL PIC S9(4) USAGE %s VALUE IS %d.", names[isc_a_pos],
			   request->req_ident, COMP_VALUE, request->req_length);

//  check the case where we need to extend the dpb dynamically at runtime,
//  in which case we need dpb length and a pointer to be defined even if 
//  there is no static dpb defined 

	if (request->req_flags & REQ_extend_dpb) {
		printa(names[COLUMN_0], false,
			   "01  ISC-%dP PIC S9(9) USAGE COMP-5 VALUE IS 0.",
			   request->req_ident);
	}

	if (request->req_flags & (REQ_sql_blob_open | REQ_sql_blob_create))
		printa(names[COLUMN_0], false,
			   "01  %s%dS PIC S9(9) USAGE COMP VALUE IS 0.", names[isc_a_pos],
			   request->req_ident);

//  generate the request as BLR long words 

	if (request->req_length) {
		if (request->req_flags & REQ_sql_cursor)
			printa(names[COLUMN_0], false,
				   "01  %s%dS PIC S9(9) USAGE COMP VALUE IS 0.", names[isc_a_pos],
				   request->req_ident);
#ifdef SCROLLABLE_CURSORS
		if (request->req_flags & REQ_scroll)
			printa(names[COLUMN_0], false,
				   "01  %s%dDI PIC S9(4) USAGE COMP VALUE IS 0.", names[isc_a_pos],
				   request->req_ident);
#endif
		printa(names[COLUMN_0], false, "01  %s%d.",
			   names[isc_a_pos], request->req_ident);
		gen_raw(request->req_blr, request->req_type, request->req_length,
				request->req_ident);
				
		const char* string_type;
		if (!(gpreGlob.sw_raw)) {
			printa(names[COMMENT], false, " ");
			printa(names[COMMENT], false, "FORMATTED REQUEST BLR FOR %s%d = ",
				   names[isc_a_pos], request->req_ident);
			switch (request->req_type) {
			case REQ_create_database:
			case REQ_ready:
				string_type = "DPB";
				if (PRETTY_print_cdb(request->req_blr, gen_blr, 0, 0))
					CPR_error("internal error during parameter generation");
				break;


			case REQ_ddl:
				string_type = "DYN";
				if (PRETTY_print_dyn(request->req_blr, gen_blr, 0, 0))
					CPR_error("internal error during dynamic DDL generation");
				break;
			case REQ_slice:
				string_type = "SDL";
				if (PRETTY_print_sdl(request->req_blr, gen_blr, 0, 0))
					CPR_error("internal error during SDL generation");
				break;

			default:
				string_type = "BLR";
				if (gds__print_blr(request->req_blr, gen_blr, 0, 0))
					CPR_error("internal error during BLR generation");
			}
		}
		else {
			switch (request->req_type) {
			case REQ_create_database:
			case REQ_ready:
				string_type = "DPB";
				break;

			case REQ_ddl:
				string_type = "DYN";
				break;
			case REQ_slice:
				string_type = "SDL";
				break;

			default:
				string_type = "BLR";
			}
		}
		printa(names[COMMENT], false, " ");
		printa(names[COMMENT], false, "END OF %s STRING FOR REQUEST %s%d\n",
			   string_type, names[isc_a_pos], request->req_ident);
	}

//   Print out slice description language if there are arrays associated with request  

	for (port = request->req_ports; port; port = port->por_next)
		for (reference = port->por_references; reference;
			 reference = reference->ref_next)
		{
			if (reference->ref_sdl) {
				printa(names[COLUMN_0], false, "01  %s%d.", names[isc_a_pos],
					   reference->ref_sdl_ident);
				gen_raw(reference->ref_sdl, REQ_slice,
						reference->ref_sdl_length, reference->ref_sdl_ident);
				if (!gpreGlob.sw_raw)
					if (PRETTY_print_sdl(reference->ref_sdl, gen_blr, 0, 0))
						CPR_error("internal error during SDL generation");

				printa(names[COMMENT], false, " ");
				printa(names[COMMENT], false,
					   "END OF SDL STRING FOR %s%d */\n", names[isc_a_pos],
					   reference->ref_sdl_ident);
			}
		}

//  Print out any blob parameter blocks required 
	for (blob = request->req_blobs; blob; blob = blob->blb_next)
		if (blob->blb_const_from_type || blob->blb_const_to_type) {
			printa(names[COLUMN_0], false, "01  %s%d.",
				   names[isc_a_pos], blob->blb_bpb_ident);
			gen_raw(blob->blb_bpb, request->req_type, blob->blb_bpb_length,
					(int) request);
			printa(names[COMMENT], false, " ");
		}
//  If this is a GET_SLICE/PUT_slice, allocate some variables 

	if (request->req_type == REQ_slice) {
		printa(names[COLUMN_0], false, "01  %s%dv.", names[isc_b_pos],
			   request->req_ident);
		printa(names[COLUMN], false,
			   "    03 %s%dv_3 PIC S9(9) USAGE COMP OCCURS %d TIMES.",
			   names[isc_b_pos], request->req_ident, MAX(1,
													request->req_slice->
													slc_parameters));
		printa(names[COLUMN_0], false, "01  %s%ds PIC S9(9) USAGE COMP.",
			   names[isc_b_pos], request->req_ident);
	}
}


//____________________________________________________________
//  
//		Generate substitution text for END_STREAM.
//  

static void gen_s_end( const act* action)
{
	gpre_req* request;

	request = action->act_request;

	if (action->act_type == ACT_close)
		gen_cursor_close(action, request);

	printa(names[COLUMN], true,
		   "CALL \"%s\" USING %s, %s%s, %s%s%s",
		   ISC_UNWIND_REQUEST,
		   status_vector(action),
		   BY_REF, request->req_handle,
		   BY_VALUE, request->req_request_level, END_VALUE);

	if (action->act_type == ACT_close) {
		printa(names[COLUMN], false, "END-IF");
		printa(names[COLUMN], false, "END-IF");
	}

	set_sqlcode(action);
}


//____________________________________________________________
//  
//		Generate substitution text for FETCH.
//  

static void gen_s_fetch( const act* action)
{
	gpre_req* request;

	request = action->act_request;
	if (request->req_sync)
		gen_send(action, request->req_sync);

	gen_receive(action, request->req_primary);
}


//____________________________________________________________
//  
//		Generate text to compile and start a stream.  This is
//		used both by START_STREAM and FOR
//  

static void gen_s_start( const act* action)
{
	gpre_req* request;
	gpre_port* port;

	request = action->act_request;

	gen_compile(action);

	if (port = request->req_vport)
		asgn_from(action, port->por_references);

	if (action->act_type == ACT_open)
		gen_cursor_open(action, request);

//  Do not call "gen_start" in case if "gen_compile" failed 

	if (action->act_error || (action->act_flags & ACT_sql)) {
		if (gpreGlob.sw_auto)
			printa(names[COLUMN], false, "IF %s NOT = 0 AND %s NOT = 0 THEN",
				   request_trans(action, request), request->req_handle);
		else
			printa(names[COLUMN], false, "IF %s NOT = 0 THEN",
				   request->req_handle);
	}

	gen_start(action, port);

	if (action->act_error || (action->act_flags & ACT_sql))
		printa(names[COLUMN], false, "END-IF");

	if (action->act_type == ACT_open) {
		printa(names[COLUMN], false, "END-IF");
		printa(names[COLUMN], false, "END-IF");
		if (gpreGlob.sw_auto)
			printa(names[COLUMN], false, "END-IF");
		printa(names[COLUMN], false, "END-IF");
		set_sqlcode(action);
	}
}


//____________________________________________________________
//  
//		Generate a send call for a port.
//  

static void gen_send( const act* action, gpre_port* port)
{
	gpre_req* request = action->act_request;

	sprintf(output_buffer,
			"%sCALL \"%s\" USING %s, %s%s, %s%d%s, %s%d%s, %s%s%d, %s%s%s\n",
			names[COLUMN],
			ISC_SEND,
			status_vector(action),
			BY_REF, request->req_handle,
			BY_VALUE, port->por_msg_number, END_VALUE,
			BY_VALUE, port->por_length, END_VALUE,
			BY_REF, names[isc_a_pos], port->por_ident,
			BY_VALUE, request->req_request_level, END_VALUE);

	COB_print_buffer(output_buffer, true);
	set_sqlcode(action);
}


//____________________________________________________________
//  
//		Generate support for get/put slice statement.
//  

static void gen_slice( const act* action)
{
	REF reference, upper, lower;
	slc* slice;
	USHORT column;
	PAT args;
	slc::slc_repeat *tail, *end;
	const TEXT *pattern1 =
		"CALL \"%S7\" USING %V1, %RF%DH%RE, %RF%RT%RE, %RF%FR%RE, %VF%N1%VE, %RF%I1%RE, %VF%N2%VE, %RF%I1v%RE, %VF%I1s%VE, %RF%S5%RE, %RF%S6%RE";
	const TEXT *pattern2 =
		"CALL \"%S7\" USING %V1, %RF%DH%RE, %RF%RT%RE, %RF%FR%RE, %VF%N1%VE, %RF%I1%RE, %VF%N2%VE, %RF%I1v%RE, %VF%I1s%VE, %RF%S5%RE";

	column = strlen(names[COLUMN]);
	gpre_req* request = action->act_request;
	slice = (slc*) action->act_object;
	gpre_req* parent_request = slice->slc_parent_request;

//  Compute array size 

	fprintf(gpreGlob.out_file, "    COMPUTE %s%ds = %d",
			   names[isc_b_pos],
			   request->req_ident, slice->slc_field->fld_array->fld_length);

	for (tail = slice->slc_rpt, end = tail + slice->slc_dimensions;
		 tail < end; ++tail)
		if (tail->slc_upper != tail->slc_lower) {
			lower = (REF) tail->slc_lower->nod_arg[0];
			upper = (REF) tail->slc_upper->nod_arg[0];
			if (lower->ref_value)
				fprintf(gpreGlob.out_file, " * ( %s - %s + 1)", upper->ref_value,
						   lower->ref_value);
			else
				fprintf(gpreGlob.out_file, " * ( %s + 1)", upper->ref_value);
		}
	fprintf(gpreGlob.out_file, "\n");

//  Make assignments to variable vector 

	for (reference = request->req_values; reference;
		 reference = reference->ref_next)
	{
		printa(names[COLUMN], false, "MOVE %s TO %s%dv (%d)",
			   reference->ref_value, names[isc_a_pos],
			   request->req_ident, reference->ref_id);
	}

	args.pat_reference = slice->slc_field_ref;
	args.pat_request = parent_request;	// blob id request 
	args.pat_vector1 = status_vector(action);	// status vector 
	args.pat_database = parent_request->req_database;	// database handle 
	args.pat_string1 = action->act_request->req_trans;	// transaction handle 
	args.pat_value1 = request->req_length;	// slice descr. length 
	args.pat_ident1 = request->req_ident;	// request name 
	args.pat_value2 = slice->slc_parameters * sizeof(SLONG);	// parameter length 

	reference = (REF) slice->slc_array->nod_arg[0];
	args.pat_string5 = reference->ref_value;	// array name 
	args.pat_string6 = names[ISC_ARRAY_LENGTH];
	args.pat_string7 =
		(action->act_type == ACT_get_slice) ? ISC_GET_SLICE : ISC_PUT_SLICE;

	PATTERN_expand(column,
				   (action->act_type == ACT_get_slice) ? pattern1 : pattern2,
				   &args);
}


//____________________________________________________________
//  
//		Substitute for a segment, segment length, or blob handle.
//  

static void gen_segment( const act* action)
{
	blb* blob;

	blob = (blb*) action->act_object;

	fprintf(gpreGlob.out_file, "%s%d",
			   names[isc_a_pos],
			   (action->act_type == ACT_segment) ? blob->blb_buff_ident :
			   (action->act_type ==
				ACT_segment_length) ? blob->blb_len_ident : blob->blb_ident);
}


//____________________________________________________________
//  
//  

static void gen_select( const act* action)
{
	gpre_req* request;
	gpre_port* port;
	GPRE_NOD var_list;
	int i;
	TEXT name[20];

	request = action->act_request;
	port = request->req_primary;
	gen_name(name, request->req_eof, true);

	gen_s_start(action);

//  BUG8321: Do not call "receive" in case if SQLCODE is not equal 0 
	printa(names[COLUMN], false, "IF SQLCODE = 0 THEN");

	gen_receive(action, port);

	printa(names[COLUMN], false, "IF %s NOT = 0 THEN", name);
	if (var_list = (GPRE_NOD) action->act_object)
		for (i = 0; i < var_list->nod_count; i++) {
			asgn_to(action, (REF) var_list->nod_arg[i]);
		}

	if (request->req_database->dbb_flags & DBB_v3) {
		gen_receive(action, port);
		printa(names[COLUMN], false, "IF %s NOT = 0 THEN", name);
		printa(names[COLUMN], false, "MOVE -1 TO SQLCODE");
		printa(names[COLUMN], false, "ELSE");
		printa(names[COLUMN], false, "MOVE 0 TO SQLCODE");
		printa(names[COLUMN], false, "END-IF");
	}

	printa(names[COLUMN], false, "ELSE");
	printa(names[COLUMN], false, "MOVE 100 TO SQLCODE");
	printa(names[COLUMN], false, "END-IF");
	printa(names[COLUMN], false, "END-IF");
}


//____________________________________________________________
//  
//		Generate either a START or START_AND_SEND depending
//		on whether or a not a port is present.
//  

static void gen_start( const act* action, gpre_port* port)
{
	gpre_req* request;
	REF reference;

	request = action->act_request;
	const TEXT* vector = status_vector(action);

	if (port) {
		for (reference = port->por_references; reference;
			 reference = reference->ref_next)
		{
			if (reference->ref_field->fld_array_info)
				gen_get_or_put_slice(action, reference, false);
		}

		sprintf(output_buffer,
				"%sCALL \"%s\" USING %s, %s%s, %s%s, %s%d%s, %s%d%s, %s%s%d, %s%s%s\n",
				names[COLUMN],
				ISC_START_AND_SEND,
				vector,
				BY_REF, request->req_handle,
				BY_REF, request_trans(action, request),
				BY_VALUE, port->por_msg_number, END_VALUE,
				BY_VALUE, port->por_length, END_VALUE,
				BY_REF, names[isc_a_pos], port->por_ident,
				BY_VALUE, request->req_request_level, END_VALUE);
	}
	else
		sprintf(output_buffer,
				"%sCALL \"%s\" USING %s, %s%s, %s%s, %s%s%s\n",
				names[COLUMN],
				ISC_START_REQUEST,
				vector,
				BY_REF, request->req_handle,
				BY_REF, request_trans(action, request),
				BY_VALUE, request->req_request_level, END_VALUE);

	COB_print_buffer(output_buffer, true);

	set_sqlcode(action);
}


//____________________________________________________________
//  
//		Generate text for STORE statement.  This includes the compile
//		call and any variable initialization required.
//  

static void gen_store( const act* action)
{
	gpre_req* request;
	REF reference;
	gpre_fld* field;
	gpre_port* port;
	TEXT name[64];

	request = action->act_request;
	gen_compile(action);

//  Initialize any blob fields 

	port = request->req_primary;
	for (reference = port->por_references; reference;
		 reference = reference->ref_next) {
		field = reference->ref_field;
		if (field->fld_flags & FLD_blob)
			printa(names[COLUMN], true, "CALL \"isc_qtoq\" USING %s, %s",
				   names[isc_blob_null_pos], gen_name(name, reference, true));
	}
}


//____________________________________________________________
//  
//		Generate substitution text for START_TRANSACTION.
//  

static void gen_t_start( const act* action)
{
	TEXT dbname[80];

//  if this is a purely default transaction, just let it through 

	const gpre_tra* trans;
	if (!action || !(trans = (gpre_tra*) action->act_object)) {
		t_start_auto(0, status_vector(action), action, false);
		return;
	}

//  build a complete statement, including tpb's.  Ready db's as gpre_req. 

	const tpb* tpb_iterator;
	if (gpreGlob.sw_auto)
		for (tpb_iterator = trans->tra_tpb;
			 tpb_iterator;
			 tpb_iterator = tpb_iterator->tpb_tra_next)
		{
			const dbb* db = tpb_iterator->tpb_database;
			const TEXT* filename = db->dbb_runtime;
			if (filename || !(db->dbb_flags & DBB_sqlca)) {
				printa(names[COLUMN], false, "IF %s = 0 THEN",
					   db->dbb_name->sym_string);
				const USHORT namelength = filename ? strlen(filename) : 0;
#ifndef VMS
				if (filename) {
					sprintf(dbname, "isc-%ddb", db->dbb_id);
					filename = dbname;
				}
#endif
				make_ready(db, filename, status_vector(action), 0,
						   namelength);
				set_sqlcode(action);
				printa(names[COLUMN], false, "END-IF");
			}
		}

	printa(names[COLUMN], true,
		   "CALL \"%s\" USING %s, %s%s, %s%d%s",
		   ISC_START_TRANSACTION,
		   status_vector(action),
		   BY_REF, (trans->tra_handle) ? trans->tra_handle : names[isc_trans_pos],
		   BY_VALUE, trans->tra_db_count, END_VALUE);

	for (tpb_iterator = trans->tra_tpb;
		 tpb_iterator;
		 tpb_iterator = tpb_iterator->tpb_tra_next)
	{
		printa(names[CONTINUE], true, ", %s%s, %s%d%s, %s%s%d",
			   BY_REF, tpb_iterator->tpb_database->dbb_name->sym_string,
			   BY_VALUE, tpb_iterator->tpb_length, END_VALUE,
			   BY_REF, names[isc_tpb_pos], tpb_iterator->tpb_ident);
	}

	set_sqlcode(action);

}


//____________________________________________________________
//  
//		Initialize a TPB in the output file
//  

static void gen_tpb(tpb* tpb_buffer)
{
	union {
#ifdef VMS
		SCHAR bytewise_tpb[4];
#else
		UCHAR bytewise_tpb[4];
#endif
		SLONG longword_tpb;
	} tpb_hunk;

//  
//  TPBs are generated as raw BLR in longword chunks 
//  because COBOL is a miserable excuse for a language
//  and doesn't allow byte value assignments to character
//  fields.
//  

	printa(names[COLUMN_0], false, "01  %s%d.",
		   names[isc_tpb_pos], tpb_buffer->tpb_ident);

	const UCHAR* text = tpb_buffer->tpb_string;
	int char_len = tpb_buffer->tpb_length;
	int length = 1;

	while (char_len) {
		for (UCHAR* c = tpb_hunk.bytewise_tpb;
			 c < tpb_hunk.bytewise_tpb + sizeof(SLONG); c++)
		{
			*c = *text++;
			if (!(--char_len))
				break;
		}

		printa(names[COLUMN], false, RAW_TPB_TEMPLATE,
			   names[isc_tpb_pos], tpb_buffer->tpb_ident,
			   names[UNDER], length++, tpb_hunk.longword_tpb);
	}

	sprintf(output_buffer, "%sEnd of data for %s%d\n",
			names[COMMENT], names[isc_tpb_pos], tpb_buffer->tpb_ident);
	COB_print_buffer(output_buffer, false);
}


//____________________________________________________________
//  
//		Generate substitution text for COMMIT, ROLLBACK, PREPARE, and SAVE
//  

static void gen_trans( const act* action)
{

	if (action->act_type == ACT_commit_retain_context)
		printa(names[COLUMN], true, "CALL \"%s\" USING %s, %s%s",
			   ISC_COMMIT_RETAINING,
			   status_vector(action),
			   BY_REF,
			   (action->act_object) ? (TEXT *) (action->
												act_object) :
			   names[isc_trans_pos]);
	else
		printa(names[COLUMN], true, "CALL \"%s\" USING %s, %s%s",
			   (action->act_type ==
				ACT_commit) ? ISC_COMMIT_TRANSACTION : (action->act_type ==
														ACT_rollback) ?
			   ISC_ROLLBACK_TRANSACTION : ISC_PREPARE_TRANSACTION,
			   status_vector(action), BY_REF,
			   (action->act_object) ? (TEXT *) (action->
												act_object) :
			   names[isc_trans_pos]);
	set_sqlcode(action);
}


//____________________________________________________________
//  
//		Substitute for a variable reference.
//  

static void gen_type( const act* action)
{

	printa(names[COLUMN], true, "%ld", (SLONG) action->act_object);
}


//____________________________________________________________
//  
//		Generate substitution text for UPDATE ... WHERE CURRENT OF ...
//  

static void gen_update( const act* action)
{
	upd* modify = (upd*) action->act_object;
	gpre_port* port = modify->upd_port;
	asgn_from(action, port->por_references);
	gen_send(action, port);
}


//____________________________________________________________
//  
//		Substitute for a variable reference.
//  

static void gen_variable( const act* action)
{
	TEXT s[20];
	const ref* reference = action->act_object;
	fprintf(gpreGlob.out_file, "\n%s%s",
			   names[COLUMN], gen_name(s, reference, false));
}


//____________________________________________________________
//  
//		Generate tests for any WHENEVER clauses that may have been declared.
//  

static void gen_whenever(const swe* label)
{
	const TEXT* condition;

	while (label) {
		switch (label->swe_condition) {
		case SWE_error:
			condition = "SQLCODE < 0";
			break;

		case SWE_warning:
			condition = "SQLCODE > 0 AND SQLCODE NOT = 100";
			break;

		case SWE_not_found:
			condition = "SQLCODE = 100";
			break;

		default:
			// condition undefined
			fb_assert(false);
			return;
		}
		fprintf(gpreGlob.out_file, "%sIF (%s) THEN GO TO %s",
				   names[COLUMN], condition, label->swe_label);
		if (label->swe_condition == SWE_warning)
			fprintf(gpreGlob.out_file, "\n%s", names[COLUMN_INDENT]);
		label = label->swe_next;
		fprintf(gpreGlob.out_file, " END-IF%s", (label) ? ".\n" : "");
	}
}


//____________________________________________________________
//  
//		Generate a declaration of an array in the
//		output file.
//  

static void make_array_declaration( REF reference)
{
	TEXT space[128], string1[256];
	int dimension_size;

	gpre_fld* field = reference->ref_field;
	const TEXT* name = field->fld_symbol->sym_string;

//  Don't generate multiple declarations for the array.  V3 Bug 569.  

	if (field->fld_array_info->ary_declared)
		return;

	field->fld_array_info->ary_declared = true;

	fprintf(gpreGlob.out_file, "%s01  %s%dL.\n",
			   names[COLUMN_0], names[isc_a_pos],
			   field->fld_array_info->ary_ident);
	strcpy(space, "       ");

//   Print out the dimension part of the declaration  
	const dim* dimension = field->fld_array_info->ary_dimension;
	int i = 3;
	for (; dimension->dim_next; dimension = dimension->dim_next, i += 2)
	{
		dimension_size = dimension->dim_upper - dimension->dim_lower + 1;
		printa(space, false, "%02d  %s%d%s%d OCCURS %d TIMES.",
			   i, names[isc_a_pos], field->fld_array_info->ary_ident,
			   names[UNDER], i, dimension_size);
		strcat(space, "   ");
	}

	TEXT* p = string1;
	dimension_size = dimension->dim_upper - dimension->dim_lower + 1;
	sprintf(p, "%02d  %s%d OCCURS %d TIMES ",
			i, names[isc_a_pos], field->fld_array_info->ary_ident, dimension_size);
	while (*p)
		p++;

	SSHORT digits, scale;
	
	switch (field->fld_array_info->ary_dtype) {
	case dtype_short:
	case dtype_long:
		digits = (field->fld_array_info->ary_dtype == dtype_short) ? 4 : 9;
		scale = field->fld_array->fld_scale;
		strcpy(p, "PIC S");
		while (*p)
			p++;
		if (scale >= -digits && scale <= 0) {
			if (scale > -digits)
				sprintf(p, "9(%d)", digits + scale);
			while (*p)
				p++;
			if (scale)
				sprintf(p, "V9(%d)", digits - (digits + scale));
		}
		else if (scale > 0)
			sprintf(p, "9(%d)P(%d)", digits, scale);
		else
			sprintf(p, "VP(%d)9(%d)", -(scale + digits), digits);
		while (*p)
			p++;
		strcpy(p, USAGE_COMP);
		strcat(p, ".");
		break;

	case dtype_cstring:
	case dtype_text:
	case dtype_varying:
		sprintf(p, "PIC X(%d).", field->fld_array->fld_length);
		break;

	case dtype_date:
		strcpy(p, "PIC S9(18)");
		strcat(p, USAGE_COMP);
		strcat(p, ".");
		break;

	case dtype_quad:
		sprintf(p, "PIC S9(%d)", 18 + field->fld_array->fld_scale);
		while (*p)
			p++;
		if (field->fld_array->fld_scale < 0)
			sprintf(p, "V9(%d)", -field->fld_array->fld_scale);
		else if (field->fld_array->fld_scale > 0)
			sprintf(p, "P(%d)", field->fld_array->fld_scale);
		while (*p)
			p++;
		strcpy(p, USAGE_COMP);
		strcat(p, ".");
		break;

	case dtype_real:
		strcpy(p, DCL_FLOAT);
		strcat(p, ".");
		break;

	case dtype_double:
		strcpy(p, DCL_DOUBLE);
		strcat(p, ".");
		break;

	default:
		sprintf(space, "datatype %d unknown for field %s",
				field->fld_array_info->ary_dtype, name);
		CPR_error(space);
		return;
	}

	printa(space, false, string1);
}


//____________________________________________________________
//  
//		Turn a symbol into a varying string.
//  

static TEXT* make_name(TEXT* string, const gpre_sym* symbol)
{

#ifndef VMS
	sprintf(string, "%s", symbol->sym_string);
	while (*string) {
		if (*string == '_')
			*string = '-';
		string++;
	}
#else
	sprintf(string, "'%s '", symbol->sym_string);
#endif

	return string;
}


//____________________________________________________________
//  
//		Turn a symbol into a varying string.
//  

static TEXT* make_name_formatted(TEXT* string, const TEXT* format,
	const gpre_sym* symbol)
{
	sprintf(string, format, symbol->sym_string);
	for (TEXT* s = string; *s; s++)
		if (*s == '_')
			*s = '-';

	return string;
}


//____________________________________________________________
//  
//		Insert a port record description in output.
//  

static void make_port(const gpre_port* port)
{
	SSHORT digits;

	printa(names[COLUMN_0], false, "01  %s%d.", names[isc_a_pos], port->por_ident);

	for (const ref* reference = port->por_references; reference;
		 reference = reference->ref_next)
	{
		const gpre_fld* field = reference->ref_field;
		const TEXT* name;
		const gpre_sym* symbol = field->fld_symbol;
		if (symbol)
			name = symbol->sym_string;
		else
			name = "<expression>";
		if (reference->ref_value && (reference->ref_flags & REF_array_elem))
			field = field->fld_array;
		switch (field->fld_dtype) {
		case dtype_short:
		case dtype_long:
			digits = (field->fld_dtype == dtype_short) ? 4 : 9;
			fprintf(gpreGlob.out_file, "%s03  %s%d PIC S",
					   names[COLUMN], names[isc_a_pos], reference->ref_ident);
			if (field->fld_scale >= -digits && field->fld_scale <= 0) {
				if (field->fld_scale > -digits)
					fprintf(gpreGlob.out_file, "9(%d)", digits + field->fld_scale);
				if (field->fld_scale)
					fprintf(gpreGlob.out_file, "V9(%d)",
							   digits - (digits + field->fld_scale));
			}
			else if (field->fld_scale > 0)
				fprintf(gpreGlob.out_file, "9(%d)P(%d)", digits, field->fld_scale);
			else
				fprintf(gpreGlob.out_file, "VP(%d)9(%d)",
						   -(field->fld_scale + digits), digits);
			fprintf(gpreGlob.out_file, "%s.\n", USAGE_COMP);
			break;

		case dtype_cstring:
		case dtype_text:
			printa(names[COLUMN], false, "03  %s%d PIC X(%d).",
				   names[isc_a_pos], reference->ref_ident, field->fld_length);
			break;

		case dtype_date:
		case dtype_quad:
		case dtype_blob:
			fprintf(gpreGlob.out_file, "%s03  %s%d PIC S9(",
					   names[COLUMN], names[isc_a_pos], reference->ref_ident);
			fprintf(gpreGlob.out_file, "%d)", 18 + field->fld_scale);
			if (field->fld_scale < 0)
				fprintf(gpreGlob.out_file, "V9(%d)", -field->fld_scale);
			else if (field->fld_scale > 0)
				fprintf(gpreGlob.out_file, "P(%d)", field->fld_scale);
			fprintf(gpreGlob.out_file, "%s.\n", USAGE_COMP);
			break;

		case dtype_real:
			printa(names[COLUMN], false, "03  %s%d %s.",
				   names[isc_a_pos], reference->ref_ident, DCL_FLOAT);
			break;

		case dtype_double:
			printa(names[COLUMN], false, "03  %s%d %s.",
				   names[isc_a_pos], reference->ref_ident, DCL_DOUBLE);
			break;

		default:
		    {
		    	TEXT s[80];
				sprintf(s, "datatype %d unknown for field %s, msg %d",
						field->fld_dtype, name, port->por_msg_number);
				CPR_error(s);
				return;
			}
		}
	}

	printa(names[COLUMN], false, " ");
}


//____________________________________________________________
//  
//		Generate the actual ready call.
//  

static void make_ready(
				  const dbb* db,
				  const TEXT* filename,
				  const TEXT* vector,
				  const gpre_req* request, USHORT namelength)
{
	TEXT s1[32], s1Tmp[32], s2[32], s2Tmp[32], dbname[128];
	const dbb* dbisc = (DBB) db->dbb_name->sym_object;

	if (request) {
		sprintf(s1, "%s%dL", names[isc_b_pos], request->req_ident);
		if (request->req_flags & REQ_extend_dpb)
			sprintf(s2, "%s%dp", names[isc_b_pos], request->req_ident);
		else
			sprintf(s2, "%s%d", names[isc_b_pos], request->req_ident);

		/* if the dpb needs to be extended at runtime to include items
		   in host variables, do so here; this assumes that there is 
		   always a request generated for runtime variables */

		if (request->req_flags & REQ_extend_dpb) {
			if (request->req_length) {
				sprintf(output_buffer, "%sMOVE isc_%d to %s\n",
						names[COLUMN], request->req_ident, s2);

			}
			if (db->dbb_r_user) {
				sprintf(output_buffer,
						"%sCALL \"%s\" USING %s%s, %s%s, BY VALUE 28, %s %s, %s%d%s\n",
						names[COLUMN],
						ISC_MODIFY_DPB,
						BY_REF, s2,
						BY_REF, s1,
						BY_DESC, db->dbb_r_user,
						BY_VALUE, strlen(db->dbb_r_user) - 2, END_VALUE);
				COB_print_buffer(output_buffer, true);
			}
			if (db->dbb_r_password) {
				sprintf(output_buffer,
						"%sCALL \"%s\" USING %s%s, %s%s, BY VALUE 29,  %s %s, %s%d%s\n",
						names[COLUMN],
						ISC_MODIFY_DPB,
						BY_REF, s2,
						BY_REF, s1,
						BY_DESC, db->dbb_r_password,
						BY_VALUE, strlen(db->dbb_r_password) - 2, END_VALUE);
				COB_print_buffer(output_buffer, true);
			}

			/*
			   ** ==================================================
			   ** ==
			   ** ==   Process Role Name, isc_dpb_sql_role_name/60
			   ** ==
			   ** ==================================================
			 */

			if (db->dbb_r_sql_role) {
				sprintf(output_buffer,
						"%sCALL \"%s\" USING %s%s, %s%s, BY VALUE 60,  %s %s, %s%d%s\n",
						names[COLUMN],
						ISC_MODIFY_DPB,
						BY_REF, s2,
						BY_REF, s1,
						BY_DESC, db->dbb_r_sql_role,
						BY_VALUE, strlen(db->dbb_r_sql_role) - 2, END_VALUE);
				COB_print_buffer(output_buffer, true);
			}

			if (db->dbb_r_lc_messages) {
				sprintf(output_buffer,
						"%sCALL \"%s\" USING %s%s, %s%s, BY VALUE 47,  %s %s, %s%d%s\n",
						names[COLUMN],
						ISC_MODIFY_DPB,
						BY_REF, s2,
						BY_REF, s1,
						BY_REF, db->dbb_r_lc_messages,
						BY_VALUE, strlen(db->dbb_r_lc_messages) - 2,
						END_VALUE);
				COB_print_buffer(output_buffer, true);
			}
			if (db->dbb_r_lc_ctype) {
				sprintf(output_buffer,
						"%sCALL \"%s\" USING %s%s %s%s, BY VALUE 48,  %s %s, %s%d%s\n",
						names[COLUMN],
						ISC_MODIFY_DPB,
						BY_REF, s2,
						BY_REF, s1,
						BY_REF, db->dbb_r_lc_ctype,
						BY_VALUE, strlen(db->dbb_r_lc_ctype) - 2, END_VALUE);
				COB_print_buffer(output_buffer, true);
			}
		}

		if (request->req_flags & REQ_extend_dpb) {
			sprintf(s1Tmp, "%s%s%s", BY_VALUE, s1, END_VALUE);
			sprintf(s2Tmp, "%s%s%s", BY_VALUE, s2, END_VALUE);
		}
		else {
			sprintf(s2Tmp, "%s%s", BY_REF, s2);
			sprintf(s1Tmp, "%s%d%s", BY_VALUE, request->req_length,
					END_VALUE);
		}
	}

#ifdef VMS

	if (filename) {
		sprintf(output_buffer,
				"%sCALL \"%s\" USING %s, %s%s, %s%s, %s, %s\n",
				names[COLUMN],
				ISC_ATTACH_DATABASE_D,
				vector,
				BY_DESC, filename,
				BY_REF, db->dbb_name->sym_string,
				request ? s1Tmp : OMITTED, request ? s2Tmp : OMITTED);
	}
	else {
		sprintf(output_buffer,
				"%sCALL \"%s\" USING %s, %s%d%s, %s \"%s\", %s%s, %s, %s\n",
				names[COLUMN],
				ISC_ATTACH_DATABASE,
				vector,
				BY_VALUE, strlen(db->dbb_filename), END_VALUE,
				BY_REF, db->dbb_filename,
				BY_REF, db->dbb_name->sym_string,
				request ? s1Tmp : OMITTED, request ? s2Tmp : OMITTED);
	}

#else

	if (!filename) {
		sprintf(dbname, "%s%ddb", names[isc_b_pos], dbisc->dbb_id);
		filename = dbname;
		namelength = strlen(db->dbb_filename);

	}
	sprintf(output_buffer,
			"%sCALL \"%s\" USING %s, %s%d%s, %s %s, %s%s, %s, %s\n",
			names[COLUMN],
			ISC_ATTACH_DATABASE,
			vector,
			BY_VALUE, namelength, END_VALUE,
			BY_REF, filename,
			BY_REF, db->dbb_name->sym_string,
			request ? s1Tmp : OMITTED, request ? s2Tmp : OMITTED);

#endif

	COB_print_buffer(output_buffer, true);

//  if the dpb was extended, free it here 

	if (request && request->req_flags & REQ_extend_dpb) {
		if (request->req_length) {
			sprintf(output_buffer,
					"if (%s != isc_%d)", s2, request->req_ident);
			COB_print_buffer(output_buffer, true);
		}

		sprintf(output_buffer,
				"%sCALL \"%s\" USING %s\n", names[COLUMN], ISC_FREE, s2Tmp);
		COB_print_buffer(output_buffer, true);

		// reset the length of the dpb 
		sprintf(output_buffer, "%sMOVE %d to %s\n",
				names[COLUMN], request->req_length, s1);
		COB_print_buffer(output_buffer, true);
	}
}


//____________________________________________________________
//  
//		Print a fixed string at a particular COLUMN.
//  

static void printa(const TEXT* column,
				   bool call,
				   const TEXT* string,
				   ...)
{
	va_list ptr;
	TEXT s[256];

	va_start(ptr, string);
	strcpy(s, column);
	strcat(s, string);
	strcat(s, "\n");
	vsprintf(output_buffer, s, ptr);
	va_end(ptr);
	COB_print_buffer(output_buffer, call);
}


#ifdef NOT_USED_OR_REPLACED
//____________________________________________________________
//  
//		Print a fixed string at a particular column.
//  

static void printb(const TEXT* string, ...)
{
	va_list ptr;

	va_start(ptr, string);
	vfprintf(gpreGlob.out_file, string, ptr);
	va_end(ptr);
}
#endif

//____________________________________________________________
//  
//		Generate the appropriate transaction handle.
//  

static const TEXT* request_trans( const act* action, const gpre_req* request)
{
	if (action->act_type == ACT_open) {
		const TEXT* trname = ((open_cursor*) action->act_object)->opn_trans;
		if (!trname)
			trname = names[isc_trans_pos];
		return trname;
	}
	else
		return (request) ? request->req_trans : names[isc_trans_pos];
}


//____________________________________________________________
//  
//		generate a CALL to the appropriate SQLCODE routine.
//		Note that not all COBOLs have the concept of a function.

static void set_sqlcode( const act* action)
{
	TEXT buffer[128];

	if (action && action->act_flags & ACT_sql) {
		strcpy(buffer, SQLCODE_CALL_TEMPLATE);
		printa(names[COLUMN], true,
			   buffer, ISC_SQLCODE_CALL, names[isc_status_vector_pos]);
	}
}


//____________________________________________________________
//  
//		Generate the appropriate status vector parameter for a gds
//		call depending on where or not the action has an error clause.
//  

static const TEXT* status_vector( const act* action)
{

	if (action && (action->act_error || (action->act_flags & ACT_sql)))
		return names[isc_status_vector_pos];

	return (OMITTED);
}


//____________________________________________________________
//  
//		Generate substitution text for START_TRANSACTION,
//		when it's being generated automatically by a compile
//		call.
//  

static void t_start_auto(const gpre_req* request,
						 const TEXT* vector,
						 const act* action,
						 bool test)
{
	DBB db;
	int count;
	USHORT namelength;
	TEXT dbname[80], buffer[256], temp[40];

	const TEXT* trname = request_trans(action, request);

//  find out whether we're using a status vector or not 

	const bool stat = !strcmp(vector, names[isc_status_vector_pos]);

//  this is a default transaction, make sure all databases are ready 

	if (gpreGlob.sw_auto) {
		buffer[0] = 0;
		for (count = 0, db = gpreGlob.isc_databases; db; db = db->dbb_next, count++)
		{
			const TEXT* filename = db->dbb_runtime;
			if (filename || !(db->dbb_flags & DBB_sqlca)) {
				fprintf(gpreGlob.out_file, "%sIF %s = 0", names[COLUMN],
						   db->dbb_name->sym_string);
				if (stat && buffer[0])
					fprintf(gpreGlob.out_file, " AND %s(2) = 0", names[isc_status_pos]);
				fprintf(gpreGlob.out_file, " THEN\n");
				namelength = filename ? strlen(filename) : 0;
#ifndef VMS
				if (filename) {
					sprintf(dbname, "isc-%ddb", db->dbb_id);
					filename = dbname;
				}
#endif
				make_ready(db, filename, vector, 0, namelength);
				printa(names[COLUMN], false, "END-IF");
				if (buffer[0])
					strcat(buffer, ") AND (");
				sprintf(temp, "%s NOT = 0", db->dbb_name->sym_string);
				strcat(buffer, temp);
			}
		}
		if (test)
			if (buffer[0])
				printa(names[COLUMN], false, "IF (%s) AND %s = 0 THEN",
					   buffer, trname);

			else
				printa(names[COLUMN], false, "IF %s = 0 THEN", trname);
		else if (buffer[0])
			printa(names[COLUMN], false, "IF (%s) THEN", buffer);
	}
	else
		for (count = 0, db = gpreGlob.isc_databases; db; db = db->dbb_next, count++);

	const TEXT* col = (stat) ? names[COLUMN_INDENT] : names[COLUMN];

	printa(col, true, "CALL \"%s\" USING %s, %s%s, %s%d%s",
		   ISC_START_TRANSACTION, vector, BY_REF, trname,
		   BY_VALUE, count, END_VALUE);

	for (db = gpreGlob.isc_databases; db; db = db->dbb_next)
		printa(names[CONTINUE], true, ", %s%s, %s, %s",
			   BY_REF, db->dbb_name->sym_string, OMITTED, OMITTED);

	if (gpreGlob.sw_auto && (test || buffer[0]))
		printa(names[COLUMN], false, "END-IF");


	set_sqlcode(action);
}

