/*
 *	PROGRAM:	JRD Access Method
 *	MODULE:		jrd.cpp
 *	DESCRIPTION:	User visible entrypoints
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
 * 2001.07.06 Sean Leyne - Code Cleanup, removed "#ifdef READONLY_DATABASE"
 *                         conditionals, as the engine now fully supports
 *                         readonly databases.
 * 2001.07.09 Sean Leyne - Restore default setting to Force Write = "On", for
 *                         Windows NT platform, for new database files. This was changed
 *                         with IB 6.0 to OFF and has introduced many reported database
 *                         corruptions.
 *
 * 2002.10.29 Sean Leyne - Removed obsolete "Netware" port
 *
 */

#include "firebird.h"
#include "../jrd/common.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../jrd/common.h"
#include "../jrd/ThreadStart.h"
#include "../jrd/os/thd_priority.h"
#include <stdarg.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_PWD_H
#include <pwd.h>
#endif
#include <errno.h>

#include "../jrd/ibase.h"
#include "../jrd/jrd.h"
#include "../jrd/irq.h"
#include "../jrd/isc.h"
#include "../jrd/drq.h"
#include "../jrd/req.h"
#include "../jrd/tra.h"
#include "../jrd/blb.h"
#include "../jrd/lck.h"
#include "../jrd/nbak.h"
#include "../jrd/scl.h"
#include "../jrd/os/pio.h"
#include "../jrd/ods.h"
#include "../jrd/exe.h"
#include "../jrd/val.h"
#include "../jrd/rse.h"
#include "../jrd/all.h"
#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
#include "../jrd/log.h"
#endif
#include "../jrd/fil.h"
#include "../jrd/intl.h"
#include "../jrd/sbm.h"
#include "../jrd/svc.h"
#include "../jrd/sdw.h"
#include "../jrd/lls.h"
#include "../jrd/cch.h"
#include "../jrd/iberr.h"
#include "../intl/charsets.h"
#include "../jrd/sort.h"

#include "../jrd/blb_proto.h"
#include "../jrd/cch_proto.h"
#include "../jrd/cmp_proto.h"
#include "../jrd/dbg_proto.h"
#include "../jrd/dyn_proto.h"
#include "../jrd/err_proto.h"
#include "../jrd/exe_proto.h"
#include "../jrd/ext_proto.h"
#include "../jrd/fun_proto.h"
#include "../jrd/gds_proto.h"
#include "../jrd/iberr_proto.h"
#include "../jrd/inf_proto.h"
#include "../jrd/ini_proto.h"
#include "../jrd/intl_proto.h"
#include "../jrd/isc_f_proto.h"
#include "../jrd/isc_proto.h"
#include "../jrd/jrd_proto.h"

#include "../jrd/lck_proto.h"
#include "../jrd/log_proto.h"
#include "../jrd/met_proto.h"
#include "../jrd/mov_proto.h"
#include "../jrd/pag_proto.h"
#include "../jrd/par_proto.h"
#include "../jrd/os/pio_proto.h"
#include "../jrd/sch_proto.h"
#include "../jrd/scl_proto.h"
#include "../jrd/sdw_proto.h"
#include "../jrd/shut_proto.h"
#include "../jrd/sort_proto.h"
#include "../jrd/thread_proto.h"
#include "../jrd/tra_proto.h"
#include "../jrd/val_proto.h"
#include "../jrd/vio_proto.h"
#include "../jrd/file_params.h"
#include "../jrd/event_proto.h"
#include "../jrd/why_proto.h"
#include "../jrd/flags.h"

#include "../common/config/config.h"
#include "../common/config/dir_list.h"
#include "../jrd/plugin_manager.h"
#include "../jrd/db_alias.h"
#include "../jrd/IntlManager.h"
#include "../common/classes/fb_tls.h"
#include "../common/classes/ClumpletReader.h"
#include "../jrd/DebugInterface.h"

using namespace Jrd;

const SSHORT WAIT_PERIOD	= -1;

#ifdef SUPPORT_RAW_DEVICES
#define unlink PIO_unlink
#endif

#ifdef DEV_BUILD
int debug;
#endif

namespace
{
	Database* databases = NULL;
	Firebird::GlobalPtr<Firebird::Mutex> databases_rec_mutex;

	inline void dbMutexLock()
	{
		databases_rec_mutex->enter();
	}

	inline void dbMutexUnlock()
	{
		databases_rec_mutex->leave();
	}

	class EngineStartup
	{
	public:
		static void init()
		{
			IntlManager::initialize();
			PluginManager::load_engine_plugins();

#if defined(WIN_NT) && !defined(SERVER_SHUTDOWN)
			setup_NT_handlers();
#endif
		}

		static void cleanup() {}
	};

	Firebird::InitMutex<EngineStartup> engineStartup;

	class DbMutexGuard
	{
	public:
		DbMutexGuard()
		{
			dbMutexLock();
		}

		~DbMutexGuard()
		{
			try {
				dbMutexUnlock();
			}
			catch (const Firebird::Exception&)
			{
				Firebird::MutexLockGuard::onDtorException();
			}
		}

	private:
		// copying is prohibited
		DbMutexGuard(const DbMutexGuard&);
		DbMutexGuard& operator=(const DbMutexGuard&);
	};

	inline void validateHandle(thread_db* tdbb, Attachment* const attachment)
	{
		if (!attachment->checkHandle() ||
			!attachment->att_database->checkHandle())
		{
			Firebird::status_exception::raise(isc_bad_db_handle, 0);
		}

		Database* const dbb = attachment->att_database;

		const Attachment* attach = dbb->dbb_attachments;
		while (attach && attach != attachment)
			attach = attach->att_next;

		if (!attach)
			Firebird::status_exception::raise(isc_bad_db_handle, 0);

		tdbb->setAttachment(attachment);
		tdbb->setDatabase(dbb);
	}

	inline void validateHandle(thread_db* tdbb, jrd_tra* transaction)
	{
		if (!transaction->checkHandle())
			Firebird::status_exception::raise(isc_bad_trans_handle, 0);

		validateHandle(tdbb, transaction->tra_attachment);

		tdbb->setTransaction(transaction);
	}

	inline void validateHandle(thread_db* tdbb, jrd_req* request)
	{
		if (!request->checkHandle())
			Firebird::status_exception::raise(isc_bad_req_handle, 0);

		validateHandle(tdbb, request->req_attachment);
	}

	inline void validateHandle(thread_db* tdbb, blb* blob)
	{
		if (!blob->checkHandle())
			Firebird::status_exception::raise(isc_bad_segstr_handle, 0);

		validateHandle(tdbb, blob->blb_transaction);
		validateHandle(tdbb, blob->blb_attachment);
	}

	inline void validateHandle(Service* service)
	{
		if (!service->checkHandle())
			Firebird::status_exception::raise(isc_bad_svc_handle, 0);
	}


	class DatabaseContextHolder : public Jrd::ContextPoolHolder
	{
	public:
		DatabaseContextHolder(thread_db* arg)
			: Jrd::ContextPoolHolder(arg, arg->getDatabase()->dbb_permanent),
			  tdbb(arg)
		{
			Database* dbb = tdbb->getDatabase();
			dbb->dbb_sync.lock();
			++dbb->dbb_use_count;
		}

		~DatabaseContextHolder()
		{
			Database* dbb = tdbb->getDatabase();
			if (dbb->checkHandle())
			{
				--dbb->dbb_use_count;
				dbb->dbb_sync.unlock();
			}
		}

	private:
		// copying is prohibited
		DatabaseContextHolder(const DatabaseContextHolder&);
		DatabaseContextHolder& operator=(const DatabaseContextHolder&);

		thread_db* tdbb;
	};


	// Shuts SecurityDatabase in case of errors during attach or create.
	// When attachment is created, control is passed to it using clear
	class SecurityInitHolder
	{
	public:
		SecurityInitHolder()
			: shutdown(true)
		{
			SecurityDatabase::initialize();
		}
		
		void clear()
		{
			shutdown = false;
		}
		
		~SecurityInitHolder()
		{
			if (shutdown)
			{
				SecurityDatabase::shutdown();
			}
		}

	private:
		bool shutdown;
	};

} // anonymous

#ifdef  WIN_NT
#include <windows.h>
// these should stop a most annoying warning
#undef TEXT
#define TEXT    SCHAR
#endif	// WIN_NT

void Jrd::Trigger::compile(thread_db* tdbb)
{
	if (!request /*&& !compile_in_progress*/)
	{
		SET_TDBB(tdbb);

		Database* dbb = tdbb->getDatabase();

		Database::CheckoutLockGuard guard(dbb, dbb->dbb_sp_rec_mutex);

		if (request)
		{
			return;
		}

		compile_in_progress = true;
		// Allocate statement memory pool
		MemoryPool* new_pool = dbb->createPool();
		// Trigger request is not compiled yet. Lets do it now
		USHORT par_flags = (USHORT)
			(flags & TRG_ignore_perm) ? csb_ignore_perm : 0;
		if (type & 1)
			par_flags |= csb_pre_trigger;
		else
			par_flags |= csb_post_trigger;

		CompilerScratch* csb = NULL;
		try {
			Jrd::ContextPoolHolder context(tdbb, new_pool);

			csb = CompilerScratch::newCsb(*tdbb->getDefaultPool(), 5);
			csb->csb_g_flags |= par_flags;

			if (!dbg_blob_id.isEmpty())
				DBG_parse_debug_info(tdbb, &dbg_blob_id, csb->csb_dbg_info);

			PAR_blr(tdbb, relation, blr.begin(),  NULL, &csb, &request, (relation ? true : false),
					par_flags);

			delete csb;
		}
		catch (const Firebird::Exception&) {
			compile_in_progress = false;
			if (csb) {
				delete csb;
				csb = NULL;
			}
			if (request) {
				CMP_release(tdbb, request);
				request = NULL;
			}
			else {
				dbb->deletePool(new_pool);
			}

			throw;
		}
		
		request->req_trg_name = name;

		if (sys_trigger)
		{
			request->req_flags |= req_sys_trigger;
		}
		if (flags & TRG_ignore_perm)
		{
			request->req_flags |= req_ignore_perm;
		}

		compile_in_progress = false;
	}
}

void Jrd::Trigger::release(thread_db* tdbb)
{
	if (blr.getCount() == 0 ||	//sys_trigger
		!request || CMP_clone_is_active(request))
	{
		return; // FALSE;
	}
	
	CMP_release(tdbb, request);
	request = NULL;
	return; // TRUE;
}

// Option block for database parameter block

class DatabaseOptions
{
public:
	USHORT	dpb_wal_action;
	SLONG	dpb_sweep_interval;
	ULONG	dpb_page_buffers;
	bool	dpb_set_page_buffers;
	ULONG dpb_buffers;
	USHORT	dpb_debug;
	USHORT	dpb_verify;
	USHORT	dpb_sweep;
	USHORT	dpb_trace;
	USHORT	dpb_disable;
	USHORT	dpb_dbkey_scope;
	USHORT	dpb_page_size;
	bool	dpb_activate_shadow;
	bool	dpb_delete_shadow;
	USHORT	dpb_no_garbage;
#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
	bool	dpb_quit_log;
#endif
	USHORT	dpb_shutdown;
	SSHORT	dpb_shutdown_delay;
	USHORT	dpb_online;
	SSHORT	dpb_force_write;
	UCHAR	dpb_set_force_write;
	UCHAR	dpb_no_reserve;
	UCHAR	dpb_set_no_reserve;
	SSHORT	dpb_interp;
	USHORT	dpb_single_user;
	bool	dpb_overwrite;
	bool	dpb_sec_attach;
	bool	dpb_disable_wal;
	bool	dpb_gsec_attach;
	SLONG	dpb_connect_timeout;
	SLONG	dpb_dummy_packet_interval;
	bool	dpb_db_readonly;
	bool	dpb_set_db_readonly;
	bool	dpb_gfix_attach;
	bool	dpb_gstat_attach;
	USHORT	dpb_sql_dialect;
	USHORT	dpb_set_db_sql_dialect;
	SLONG	dpb_remote_pid;
	bool	dpb_no_db_triggers;
	bool	dpb_gbak_attach;
	bool	dpb_trusted_role;
	ULONG	dpb_flags;			// to OR'd with dbb_flags

// here begin compound objects
// for constructor to work properly dpb_sys_user_name 
// MUST be FIRST
	Firebird::string	dpb_sys_user_name;
	Firebird::string	dpb_user_name;
	Firebird::string	dpb_password;
	Firebird::string	dpb_password_enc;
	Firebird::string	dpb_role_name;
	Firebird::string	dpb_journal;
	Firebird::string	dpb_key;
#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
	Firebird::PathName	dpb_log;
#endif
	Firebird::PathName	dpb_lc_messages;
	Firebird::string	dpb_lc_ctype;
	Firebird::PathName	dpb_working_directory;
	Firebird::string	dpb_set_db_charset;
	Firebird::string	dpb_network_protocol;
	Firebird::string	dpb_remote_address;
	Firebird::string	dpb_trusted_login;
	Firebird::PathName	dpb_remote_process;

public:
	DatabaseOptions()
	{
		memset(this, 0, 
			reinterpret_cast<char*>(&this->dpb_sys_user_name) - 
			reinterpret_cast<char*>(this));
	}
	void get(const UCHAR*, USHORT, bool&);
};

#ifndef SUPERSERVER
static int blocking_ast_dsql_cache(void*);
#endif
static void			check_database(thread_db* tdbb);
static void			check_transaction(thread_db*, jrd_tra*);
static ISC_STATUS	commit(ISC_STATUS*, jrd_tra**, const bool);
static ISC_STATUS	compile_request(ISC_STATUS*, Attachment**, jrd_req**, SSHORT, const SCHAR*, USHORT, const char*, USHORT, const UCHAR*);
static bool			drop_files(const jrd_file*);
static void			find_intl_charset(thread_db*, Attachment*, const DatabaseOptions*);
static jrd_tra*		find_transaction(thread_db*, ISC_STATUS);
static void			init_database_locks(thread_db*, Database*);
static ISC_STATUS	handle_error(ISC_STATUS*, ISC_STATUS);
static void			run_commit_triggers(thread_db* tdbb, jrd_tra* transaction);
static void			verify_request_synchronization(jrd_req*& request, SSHORT level);
static unsigned int purge_transactions(thread_db*, Attachment*, const bool, const ULONG);
namespace {
	enum vdnResult {vdnFail, vdnOk, vdnSecurity};
}
static vdnResult	verify_database_name(const Firebird::PathName&, ISC_STATUS*);
static ISC_STATUS	unwindAttach(const Firebird::Exception& ex, 
								 ISC_STATUS* userStatus, 
								 thread_db* tdbb, 
								 Attachment* attachment, 
								 Database* dbb);
#if defined (WIN_NT)
#ifdef SERVER_SHUTDOWN
static void		ExtractDriveLetter(const TEXT*, ULONG*);
#else // SERVER_SHUTDOWN
static void		setup_NT_handlers(void);
static BOOLEAN	handler_NT(SSHORT);
#endif	// SERVER_SHUTDOWN
#endif	// WIN_NT

static Database*	init(thread_db*, ISC_STATUS*, const Firebird::PathName&, bool);
static void		prepare(thread_db*, jrd_tra*, USHORT, const UCHAR*);
static void		release_attachment(thread_db*, Attachment*);
static void		detachLocksFromAttachment(Attachment*);
static ISC_STATUS	rollback(ISC_STATUS*, jrd_tra**, const bool);
static void		shutdown_database(Database*, const bool);
static void		strip_quotes(Firebird::string&);
static void		purge_attachment(thread_db*, ISC_STATUS*, Attachment*, const bool);
static void		getUserInfo(UserId&, const DatabaseOptions&);

//____________________________________________________________
//
// check whether we need to perform an autocommit;
// do it here to prevent committing every record update
// in a statement
//
static void check_autocommit(jrd_req* request, thread_db* tdbb)
{
	// dimitr: we should ignore autocommit for requests
	// created by EXECUTE STATEMENT
	if (request->req_transaction->tra_callback_count > 0)
		return;

	if (request->req_transaction->tra_flags & TRA_perform_autocommit)
	{
		if (!(tdbb->getAttachment()->att_flags & ATT_no_db_triggers) &&
			!(request->req_transaction->tra_flags & TRA_prepared))
		{
			// run ON TRANSACTION COMMIT triggers
			run_commit_triggers(tdbb, request->req_transaction);
		}

		request->req_transaction->tra_flags &= ~TRA_perform_autocommit;
		TRA_commit(tdbb, request->req_transaction, true);
	}
}

const int SWEEP_INTERVAL		= 20000;

const char DBL_QUOTE			= '\042';
const char SINGLE_QUOTE			= '\'';
const int BUFFER_LENGTH128		= 128;

#define GDS_ATTACH_DATABASE		jrd8_attach_database
#define GDS_BLOB_INFO			jrd8_blob_info
#define GDS_CANCEL_BLOB			jrd8_cancel_blob
#define GDS_CANCEL_EVENTS		jrd8_cancel_events
#define GDS_CANCEL_OPERATION	jrd8_cancel_operation
#define GDS_CLOSE_BLOB			jrd8_close_blob
#define GDS_COMMIT				jrd8_commit_transaction
#define GDS_COMMIT_RETAINING	jrd8_commit_retaining
#define GDS_COMPILE				jrd8_compile_request
#define GDS_CREATE_BLOB2		jrd8_create_blob2
#define GDS_CREATE_DATABASE		jrd8_create_database
#define GDS_DATABASE_INFO		jrd8_database_info
#define GDS_DDL					jrd8_ddl
#define GDS_DETACH				jrd8_detach_database
#define GDS_DROP_DATABASE		jrd8_drop_database
#define GDS_INTL_FUNCTION		jrd8_intl_function
#define GDS_DSQL_CACHE			jrd8_dsql_cache
#define GDS_INTERNAL_COMPILE	jrd8_internal_compile_request
#define GDS_GET_SEGMENT			jrd8_get_segment
#define GDS_GET_SLICE			jrd8_get_slice
#define GDS_OPEN_BLOB2			jrd8_open_blob2
#define GDS_PREPARE				jrd8_prepare_transaction
#define GDS_PUT_SEGMENT			jrd8_put_segment
#define GDS_PUT_SLICE			jrd8_put_slice
#define GDS_QUE_EVENTS			jrd8_que_events
#define GDS_RECONNECT			jrd8_reconnect_transaction
#define GDS_RECEIVE				jrd8_receive
#define GDS_RELEASE_REQUEST		jrd8_release_request
#define GDS_REQUEST_INFO		jrd8_request_info
#define GDS_ROLLBACK			jrd8_rollback_transaction
#define GDS_ROLLBACK_RETAINING	jrd8_rollback_retaining
#define GDS_SEEK_BLOB			jrd8_seek_blob
#define GDS_SEND				jrd8_send
#define GDS_SERVICE_ATTACH		jrd8_service_attach
#define GDS_SERVICE_DETACH		jrd8_service_detach
#define GDS_SERVICE_QUERY		jrd8_service_query
#define GDS_SERVICE_START		jrd8_service_start
#define GDS_START_AND_SEND		jrd8_start_and_send
#define GDS_START				jrd8_start_request
#define GDS_START_MULTIPLE		jrd8_start_multiple
#define GDS_START_TRANSACTION	jrd8_start_transaction
#define GDS_TRANSACT_REQUEST	jrd8_transact_request
#define GDS_TRANSACTION_INFO	jrd8_transaction_info
#define GDS_UNWIND				jrd8_unwind_request


// External hook definitions

/* dimitr: just uncomment the following line to use this feature.
		   Requires support from the PIO modules. Only Win32 is 100% ready
		   for this so far. Note that the database encryption code in the
		   PIO layer seems to be incompatible with the SUPERSERVER_V2 code.
		   2003.02.09 */
//#define ISC_DATABASE_ENCRYPTION

static const char* CRYPT_IMAGE = "fbcrypt";
static const char* ENCRYPT = "encrypt";
static const char* DECRYPT = "decrypt";


void JRD_print_pools(const char* filename)
{
	FILE* out = fopen(filename, "w");
	if (out)
	{
		ALL_print_memory_pool_info(out, databases);
		fclose(out);
	}
}


ISC_STATUS GDS_ATTACH_DATABASE(ISC_STATUS*	user_status,
								SSHORT	_file_length,
								const TEXT*	_file_name,
								Attachment**	handle,
								SSHORT	dpb_length,
								const UCHAR*	dpb,
								const TEXT* _expanded_filename)
{
/**************************************
 *
 *	g d s _ $ a t t a c h _ d a t a b a s e
 *
 **************************************
 *
 * Functional description
 *	Attach a moldy, grungy, old database
 *	sullied by user data.
 *
 **************************************/
	ThreadContextHolder tdbb(user_status);

	if (*handle) 
	{
		return handle_error(user_status, isc_bad_db_handle);
	}

	Firebird::PathName file_name(_file_name, 
		_file_length ? _file_length : strlen(_file_name));
	Firebird::PathName expanded_name(file_name);

	// Resolve given alias name
	const bool is_alias = ResolveDatabaseAlias(expanded_name, expanded_name);
	if (is_alias)
	{
		ISC_expand_filename(expanded_name, false);
	}
	else
	{
		expanded_name = _expanded_filename;
	}

	UserId userId;
	DatabaseOptions options;
	bool invalid_client_SQL_dialect = false;
	SecurityInitHolder siHolder;

	try {
		// Process database parameter block
		options.get(dpb, dpb_length, invalid_client_SQL_dialect);

		// Ccheck for correct credentials supplied
		getUserInfo(userId, options);
	}
	catch (const Firebird::Exception& e)
	{
		e.stuff_exception(user_status);
		return user_status[1];
	}

	// Check database against conf file.
	const vdnResult vdn = verify_database_name(expanded_name, user_status);
	if (!is_alias && vdn == vdnFail)
	{
		return user_status[1];
	}

	// Unless we're already attached, do some initialization
	Database* dbb = init(tdbb, user_status, expanded_name, true);
	if (!dbb) {
		dbMutexUnlock();
		return user_status[1];
	}

	tdbb->setDatabase(dbb);
	DatabaseContextHolder dbbHolder(tdbb);

	dbb->dbb_flags |= DBB_being_opened;

	// Initialize special error handling

	ISC_STATUS* const status = user_status;
	Attachment* attachment = NULL;

	bool initing_security = false;

	try {

#ifndef NO_NFS
	// Don't check nfs if single user
	if (!options.dpb_single_user)
#endif
	{
		// Check to see if the database is truly local or if it just looks
		// that way
      
		if (ISC_check_if_remote(expanded_name, true)) {
			ERR_post(isc_unavailable, 0);
		}
	}

/* If database to be opened is security database, then only 
   gsec or SecurityDatabase may open it. This protects from use
   of old gsec to write wrong password hashes into it. */
	if (vdn == vdnSecurity && !options.dpb_gsec_attach && !options.dpb_sec_attach)
	{
		ERR_post(isc_no_priv,
				 isc_arg_string, "direct",
				 isc_arg_string, "security database",
				 isc_arg_string, 
				 ERR_string(file_name), 
				 0);
	}

	// Worry about encryption key

	if (dbb->dbb_decrypt) {
		if (dbb->dbb_filename.hasData() && 
			(dbb->dbb_encrypt_key.hasData() || options.dpb_key.hasData()))
		{
			if ((dbb->dbb_encrypt_key.hasData() && options.dpb_key.isEmpty()) ||
				(dbb->dbb_encrypt_key.empty() && options.dpb_key.hasData()) ||
				(dbb->dbb_encrypt_key != options.dpb_key))
			{
				ERR_post(isc_no_priv,
						 isc_arg_string, "encryption",
						 isc_arg_string, "database",
						 isc_arg_string, 
                         ERR_string(file_name), 
                         0);
			}
		}
		else if (options.dpb_key.hasData()) 
		{
			dbb->dbb_encrypt_key = options.dpb_key;
		}
	}

	tdbb->setAttachment((attachment = FB_NEW(*dbb->dbb_permanent) Attachment(dbb)));
	attachment->att_filename = is_alias ? file_name : expanded_name;
	attachment->att_network_protocol = options.dpb_network_protocol;
	attachment->att_remote_address = options.dpb_remote_address;
	attachment->att_remote_pid = options.dpb_remote_pid;
	attachment->att_remote_process = options.dpb_remote_process;
	attachment->att_next = dbb->dbb_attachments;

	dbb->dbb_attachments = attachment;
	dbb->dbb_flags &= ~DBB_being_opened;
	dbb->dbb_sys_trans->tra_attachment = attachment;

	attachment->att_charset = options.dpb_interp;

	if (options.dpb_lc_messages.hasData()) {
		attachment->att_lc_messages = options.dpb_lc_messages;
	}

	if (options.dpb_no_garbage)
		attachment->att_flags |= ATT_no_cleanup;

	if (options.dpb_gbak_attach)
		attachment->att_flags |= ATT_gbak_attachment;

	if (options.dpb_gstat_attach)
		attachment->att_flags |= ATT_gstat_attachment;

	if (options.dpb_gfix_attach)
		attachment->att_flags |= ATT_gfix_attachment;

	if (options.dpb_working_directory.hasData()) {
		attachment->att_working_directory = options.dpb_working_directory;
	}

	// If we're a not a secondary attachment, initialize some stuff

	bool first = false;

	LCK_init(tdbb, LCK_OWNER_attachment);	// For the attachment
	attachment->att_flags |= ATT_lck_init_done;

	if (dbb->dbb_filename.empty())
	{
#if defined(DEV_BUILD) && defined(SUPERSERVER)
		// make sure we do not reopen same DB twice
		for (Database* d = databases; d; d = d->dbb_next)
		{
			if (d->dbb_filename == expanded_name)
			{
				Firebird::fatal_exception::raise(("Attempt to reopen " + expanded_name).c_str());
			}
		}
#endif
		first = true;
		dbb->dbb_filename = expanded_name;
		dbb->dbb_flags |= options.dpb_flags;
		
		// NS: Use alias as database ID only if accessing database using file name is not possible.
		//
		// This way we:
		// 1. Ensure uniqueness of ID even in presence of multiple processes
		// 2. Make sure that ID value can be used to connect back to database
		//
		if (is_alias && vdn == vdnFail) 
			dbb->dbb_database_name = file_name;
		else
			dbb->dbb_database_name = expanded_name;
			
		// Extra LCK_init() done to keep the lock table until the
		// database is shutdown() after the last detach.
		LCK_init(tdbb, LCK_OWNER_database);
		dbb->dbb_flags |= DBB_lck_init_done;

		INI_init();

		PageSpace* pageSpace = dbb->dbb_page_manager.findPageSpace(DB_PAGE_SPACE);
		pageSpace->file =
			PIO_open(dbb, expanded_name, options.dpb_trace != 0, file_name, false);
		SHUT_init(tdbb);
		PAG_header_init();
		INI_init2();
		PAG_init();
		if (options.dpb_set_page_buffers) {
			dbb->dbb_page_buffers = options.dpb_page_buffers;
		}
		CCH_init(tdbb, options.dpb_buffers);

		// Initialize locks
		init_database_locks(tdbb, dbb);

		// Initialize backup difference subsystem. This must be done before WAL and shadowing
		// is enabled because nbackup it is a lower level subsystem
		dbb->dbb_backup_manager = FB_NEW(*dbb->dbb_permanent) BackupManager(tdbb, dbb, nbak_state_unknown);

		PAG_init2(0);
		PAG_header(false);

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
		LOG_init(expanded_name, length_expanded);
#endif

		// initialize shadowing as soon as the database is ready for it
		// but before any real work is done
		SDW_init(options.dpb_activate_shadow,
				 options.dpb_delete_shadow);

		/* dimitr: disabled due to unreliable behaviour of minor ODS upgrades
			a) in the case of any failure it's impossible to attach the database
			b) there's no way to handle failures properly, because upgrade is
			   being made in the context of system transaction which doesn't use
			   the backout logic
		INI_update_database();
		*/
	}
	else
	{
		if (dbb->dbb_flags & options.dpb_flags != options.dpb_flags)
		{
			// looks like someone tries to attach incompatibly
			Firebird::status_exception::raise(isc_bad_dpb_content, 0);
		}
	}

    // Attachments to a ReadOnly database need NOT do garbage collection
    if (dbb->dbb_flags & DBB_read_only) {
            attachment->att_flags |= ATT_no_cleanup;
	}

    if (options.dpb_disable_wal) {
		ERR_post(isc_lock_timeout, isc_arg_gds, isc_obj_in_use,
				 isc_arg_string, 
                 ERR_string(file_name), 
                 0);
	}

	if (options.dpb_buffers && !dbb->dbb_page_buffers) {
		CCH_expand(tdbb, options.dpb_buffers);
	}

	if (!options.dpb_verify && CCH_exclusive(tdbb, LCK_PW, LCK_NO_WAIT))
	{
		TRA_cleanup(tdbb);
	}

	initing_security = true;

	if (invalid_client_SQL_dialect)
	{
		ERR_post(isc_inv_client_dialect_specified, isc_arg_number,
				 options.dpb_sql_dialect,
				 isc_arg_gds, isc_valid_client_dialects,
				 isc_arg_string, "1, 2 or 3", 0);
	}

	if (userId.usr_sql_role_name.hasData())
	{
		switch (options.dpb_sql_dialect)
		{
		case 0:
			{
				// V6 Client --> V6 Server, dummy client SQL dialect 0 was passed
				// It means that client SQL dialect was not set by user
				// and takes DB SQL dialect as client SQL dialect
				if (ENCODE_ODS(dbb->dbb_ods_version, dbb->dbb_minor_original)
					>= ODS_10_0)
				{
					if (dbb->dbb_flags & DBB_DB_SQL_dialect_3) {
						// DB created in IB V6.0 by client SQL dialect 3
						options.dpb_sql_dialect = SQL_DIALECT_V6;
					}
					else {
						// old DB was gbaked in IB V6.0
						options.dpb_sql_dialect = SQL_DIALECT_V5;
					}
				}
				else {
					options.dpb_sql_dialect = SQL_DIALECT_V5;
				}
			}
			break;
		case 99:
			// V5 Client --> V6 Server, old client has no concept of dialect
			options.dpb_sql_dialect = SQL_DIALECT_V5;
			break;
		default:
			// V6 Client --> V6 Server, but client SQL dialect was set
			// by user and was passed.
			break;
		}

		switch (options.dpb_sql_dialect)
		{
		case SQL_DIALECT_V5:
			{
				strip_quotes(userId.usr_sql_role_name);
				userId.usr_sql_role_name.upper();
			}
			break;
		case SQL_DIALECT_V6_TRANSITION:
		case SQL_DIALECT_V6:
			{
				if (userId.usr_sql_role_name.hasData() && 
					(userId.usr_sql_role_name[0] == DBL_QUOTE ||
					 userId.usr_sql_role_name[0] == SINGLE_QUOTE))
				{
					const char end_quote = userId.usr_sql_role_name[0];
					// remove the delimited quotes and escape quote
					// from ROLE name
					userId.usr_sql_role_name.erase(0, 1);
					for (Firebird::string::iterator p = 
								userId.usr_sql_role_name.begin(); 
						 p < userId.usr_sql_role_name.end(); ++p)
					{
						if (*p == end_quote)
						{
							if (++p < userId.usr_sql_role_name.end() &&
								*p == end_quote)
							{
								// skip the escape quote here
								userId.usr_sql_role_name.erase(p--);
							}
							else
							{
								// delimited done
								userId.usr_sql_role_name.erase(--p, 
									userId.usr_sql_role_name.end());
							}
						}
					}
				}
				else
				{
					userId.usr_sql_role_name.upper();
				}
			}
			break;
		default:
			break;
		}
	}

	options.dpb_sql_dialect = 0;

	SCL_init(false, userId, tdbb);

	initing_security = false;

	if (options.dpb_shutdown)
	{
		dbMutexUnlock();
		if (!SHUT_database(tdbb, options.dpb_shutdown,
						   options.dpb_shutdown_delay))
		{
			dbMutexLock();
			if (user_status[1] != FB_SUCCESS)
				ERR_punt();
			else
				ERR_post(isc_no_priv,
						 isc_arg_string, "shutdown or online",
						 isc_arg_string, "database",
						 isc_arg_string, 
                         ERR_string(file_name), 
                         0);
		}
		dbMutexLock();
	}

	if (options.dpb_online)
	{
		dbMutexUnlock();
		if (!SHUT_online(tdbb, options.dpb_online)) 
		{
			dbMutexLock();
			if (user_status[1] != FB_SUCCESS)
				ERR_punt();
			else
				ERR_post(isc_no_priv,
						 isc_arg_string, "shutdown or online",
						 isc_arg_string, "database",
						 isc_arg_string, 
                         ERR_string(file_name), 
                         0);
		}
		dbMutexLock();
	}

#ifdef SUPERSERVER
/* Check if another attachment has or is requesting exclusive database access.
   If this is an implicit attachment for the security (password) database, don't
   try to get exclusive attachment to avoid a deadlock condition which happens
   when a client tries to connect to the security database itself. */

	if (!options.dpb_sec_attach) {
		dbMutexUnlock();
		bool attachment_succeeded = true;
		if (dbb->dbb_ast_flags & DBB_shutdown_single)
			attachment_succeeded = CCH_exclusive_attachment(tdbb, LCK_none, -1);
		else
			CCH_exclusive_attachment(tdbb, LCK_none, LCK_WAIT);
		dbMutexLock();
		if (attachment->att_flags & ATT_shutdown) {
			if (dbb->dbb_ast_flags & DBB_shutdown) {
				ERR_post(isc_shutdown, isc_arg_string, 
						 ERR_string(file_name), 0);
			}
			else {
				ERR_post(isc_att_shutdown, 0);
			}
		}
		if (!attachment_succeeded) {
			ERR_post(isc_shutdown, isc_arg_string, 
					 ERR_string(file_name), 0);
		}
	}
#endif

	// If database is shutdown then kick 'em out.

	if (dbb->dbb_ast_flags & (DBB_shut_attach | DBB_shut_tran))
	{
		ERR_post(isc_shutinprog, isc_arg_string, 
				ERR_string(file_name), 0);
	}

	if (dbb->dbb_ast_flags & DBB_shutdown) {
		// Allow only SYSDBA/owner to access database that is shut down
		bool allow_access = attachment->locksmith();
		// Handle special shutdown modes
		if (allow_access) {
			if (dbb->dbb_ast_flags & DBB_shutdown_full) {
				// Full shutdown. Deny access always
				allow_access = false;
			}
			else if (dbb->dbb_ast_flags & DBB_shutdown_single) {
				// Single user maintenance. Allow access only if we were able to take exclusive lock
				// Note that logic below this exclusive lock differs for SS and CS builds:
				//   - CS keeps PW database lock from releasing in AST in single-user maintenance mode
				//   - for SS this code effectively checks that no other attachments are present
				//     at call point, ATT_exclusive bit is released just before this procedure exits
				// Things are done this way to handle return to online mode nicely.
				allow_access = CCH_exclusive(tdbb, LCK_PW, WAIT_PERIOD);
			}
		}
		if (!allow_access) {
			// Note we throw exception here when entering full-shutdown mode
			ERR_post(isc_shutdown, isc_arg_string, 
					 ERR_string(file_name), 0);
		}
	}

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
	if (options.dpb_quit_log) {
		LOG_disable();
	}
#endif

	// Figure out what character set & collation this attachment prefers

	find_intl_charset(tdbb, attachment, &options);

	if (options.dpb_verify)
	{
		if (!CCH_exclusive(tdbb, LCK_PW, WAIT_PERIOD)) {
			ERR_post(isc_bad_dpb_content, isc_arg_gds, isc_cant_validate, 0);
		}

#ifdef GARBAGE_THREAD
		// Can't allow garbage collection during database validation.

		VIO_fini(tdbb);
#endif
		dbMutexUnlock();
		if (!VAL_validate(tdbb, options.dpb_verify)) {
			dbMutexLock();
			ERR_punt();
		}
		dbMutexLock();
	}

	if (options.dpb_journal.hasData()) {
		ERR_post(isc_bad_dpb_content,
				 isc_arg_gds, isc_cant_start_journal,
				 0);
	}

	if (options.dpb_wal_action)
	{
		// No WAL anymore. We deleted it.
		ERR_post(isc_no_wal, 0);
	}

/*
 * if the attachment is through gbak and this attachment is not by owner
 * or sysdba then return error. This has been added here to allow for the
 * GBAK security feature of only allowing the owner or sysdba to backup a
 * database. smistry 10/5/98
 */

	if (((attachment->att_flags & ATT_gbak_attachment) ||
		 (attachment->att_flags & ATT_gfix_attachment) ||
		 (attachment->att_flags & ATT_gstat_attachment)) &&
		!attachment->locksmith())
	{
		ERR_post(isc_adm_task_denied, 0);
	}

	if (((attachment->att_flags & ATT_gfix_attachment) ||
		 (attachment->att_flags & ATT_gstat_attachment)))
	{
		options.dpb_no_db_triggers = true;
	}

	if (options.dpb_no_db_triggers)
	{
		if (attachment->locksmith())
			attachment->att_flags |= ATT_no_db_triggers;
		else
			ERR_post(isc_adm_task_denied, 0);
	}

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
	if (options.dpb_log) {
		if (first) {
			if (!CCH_exclusive(tdbb, LCK_EX, WAIT_PERIOD)) {
				ERR_post(isc_lock_timeout, isc_arg_gds, isc_obj_in_use,
						 isc_arg_string, 
                         ERR_string(file_name),
                         0);
			}
			LOG_enable(options.dpb_log, strlen(options.dpb_log));
		}
		else {
			ERR_post(isc_bad_dpb_content, isc_arg_gds,
					 isc_cant_start_logging, 0);
		}
	}
#endif

	if (options.dpb_set_db_sql_dialect) {
		PAG_set_db_SQL_dialect(dbb, options.dpb_set_db_sql_dialect);
	}

	if (options.dpb_sweep_interval != -1) {
		PAG_sweep_interval(options.dpb_sweep_interval);
		dbb->dbb_sweep_interval = options.dpb_sweep_interval;
	}

	if (options.dpb_set_force_write) {
		PAG_set_force_write(dbb, options.dpb_force_write);
	}

	if (options.dpb_set_no_reserve) {
		PAG_set_no_reserve(dbb, options.dpb_no_reserve);
	}

	if (options.dpb_set_page_buffers) {
		PAG_set_page_buffers(options.dpb_page_buffers);
	}

	if (options.dpb_set_db_readonly) {
		if (!CCH_exclusive(tdbb, LCK_EX, WAIT_PERIOD)) {
			ERR_post(isc_lock_timeout, isc_arg_gds, isc_obj_in_use,
					 isc_arg_string,
					 ERR_string(file_name), 
					 0); 
		}
		PAG_set_db_readonly(dbb, options.dpb_db_readonly);
	}

	PAG_attachment_id(tdbb);

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
	// don't record the attach until now in case the log is added during the attach

	LOG_call(log_attach2, file_name, *handle, dpb_length, dpb,
			 expanded_filename);
#endif

#ifdef GARBAGE_THREAD
	VIO_init(tdbb);
#endif

	CCH_release_exclusive(tdbb);

	// if there was an error, the status vector is all set

	if (options.dpb_sweep & isc_dpb_records)
	{
		dbMutexUnlock();
		if (!(TRA_sweep(tdbb, 0)))
		{
			dbMutexLock();
			ERR_punt();
		}
		dbMutexLock();
	}

	if (options.dpb_dbkey_scope) {
		attachment->att_dbkey_trans = TRA_start(tdbb, 0, 0);
	}

	// Recover database after crash during backup difference file merge
	dbb->dbb_backup_manager->end_backup(tdbb, true); // true = do recovery

	if (!(attachment->att_flags & ATT_no_db_triggers))
	{
		jrd_tra* transaction = NULL;

		try
		{
			// start a transaction to execute ON CONNECT triggers
			transaction = TRA_start(tdbb, 0, NULL);

			// load all database triggers
			MET_load_db_triggers(tdbb, DB_TRIGGER_CONNECT);
			MET_load_db_triggers(tdbb, DB_TRIGGER_DISCONNECT);
			MET_load_db_triggers(tdbb, DB_TRIGGER_TRANS_START);
			MET_load_db_triggers(tdbb, DB_TRIGGER_TRANS_COMMIT);
			MET_load_db_triggers(tdbb, DB_TRIGGER_TRANS_ROLLBACK);

			// run ON CONNECT triggers
			EXE_execute_db_triggers(tdbb, transaction, jrd_req::req_trigger_connect);

			// and commit the transaction
			TRA_commit(tdbb, transaction, false);
		}
		catch (const Firebird::Exception&)
		{
			if (!(dbb->dbb_flags & DBB_bugcheck) && transaction)
				TRA_rollback(tdbb, transaction, false, false);
			throw;
		}
	}

	dbMutexUnlock();
	
	*handle = attachment;	

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
	LOG_call(log_handle_returned, *handle);
#endif	

	}	// try
	catch (const DelayFailedLogin& ex)
	{
		ISC_STATUS s = unwindAttach(ex, user_status, tdbb, attachment, dbb);
		ex.sleep();
		return s;
	}
	catch (const Firebird::Exception& ex)
	{
		return unwindAttach(ex, user_status, tdbb, attachment, dbb);
	}

	siHolder.clear();
	return FB_SUCCESS;
}


ISC_STATUS GDS_BLOB_INFO(ISC_STATUS*	user_status,
						blb**	blob_handle,
						SSHORT	item_length,
						const SCHAR*	items,
						SSHORT	buffer_length,
						SCHAR*	buffer)
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
	ThreadContextHolder tdbb(user_status);

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
	LOG_call(log_blob_info2, *blob_handle, item_length, items, buffer_length);
#endif

	try {
		blb* blob = *blob_handle;
		validateHandle(tdbb, blob);
		DatabaseContextHolder dbbHolder(tdbb);
		check_database(tdbb);

		INF_blob_info(blob, items, item_length, buffer, buffer_length);
	}
	catch (const Firebird::Exception& ex)
	{
		Firebird::stuff_exception(user_status, ex);
	}

	return user_status[1];
}


ISC_STATUS GDS_CANCEL_BLOB(ISC_STATUS * user_status, blb** blob_handle)
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
	ThreadContextHolder tdbb(user_status);

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
	LOG_call(log_cancel_blob, *blob_handle);
#endif

	try {
		blb* blob = *blob_handle;
		validateHandle(tdbb, blob);
		DatabaseContextHolder dbbHolder(tdbb);
		check_database(tdbb);

		BLB_cancel(tdbb, blob);
		*blob_handle = NULL;
	}
	catch (const Firebird::Exception& ex)
	{
		Firebird::stuff_exception(user_status, ex);
	}

	return user_status[1];
}


ISC_STATUS GDS_CANCEL_EVENTS(ISC_STATUS*	user_status,
							Attachment**	handle,
							SLONG*	id)
{
/**************************************
 *
 *	g d s _ $ c a n c e l _ e v e n t s
 *
 **************************************
 *
 * Functional description
 *	Cancel an outstanding event.
 *
 **************************************/
	ThreadContextHolder tdbb(user_status);

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
	LOG_call(log_cancel_events, *handle, *id);
#endif

	try
	{
		validateHandle(tdbb, *handle);
		DatabaseContextHolder dbbHolder(tdbb);
		check_database(tdbb);

		EVENT_cancel(*id);
	}
	catch (const Firebird::Exception& ex)
	{
		Firebird::stuff_exception(user_status, ex);
	}

	return user_status[1];
}


ISC_STATUS GDS_CANCEL_OPERATION(ISC_STATUS* user_status,
								Attachment** handle,
								USHORT option)
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
	ThreadContextHolder tdbb(user_status);

	try
	{
		Attachment* attachment = *handle;
		validateHandle(tdbb, attachment);
		DatabaseContextHolder dbbHolder(tdbb);

		switch (option) {
		case CANCEL_disable:
			attachment->att_flags |= ATT_cancel_disable;
			break;

		case CANCEL_enable:
			attachment->att_flags &= ~ATT_cancel_disable;
			break;

		case CANCEL_raise:
			attachment->att_flags |= ATT_cancel_raise;
			break;

		default:
			fb_assert(false);
		}
	}
	catch (const Firebird::Exception& ex)
	{
		Firebird::stuff_exception(user_status, ex);
	}

	return user_status[1];
}


ISC_STATUS GDS_CLOSE_BLOB(ISC_STATUS * user_status, blb** blob_handle)
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
	ThreadContextHolder tdbb(user_status);

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
	LOG_call(log_close_blob, *blob_handle);
#endif

	try
	{
		blb* blob = *blob_handle;
		validateHandle(tdbb, blob);
		DatabaseContextHolder dbbHolder(tdbb);
		check_database(tdbb);

		BLB_close(tdbb, blob);
		*blob_handle = NULL;
	}
	catch (const Firebird::Exception& ex)
	{
		Firebird::stuff_exception(user_status, ex);
	}

	return user_status[1];
}


ISC_STATUS GDS_COMMIT(ISC_STATUS * user_status, jrd_tra** tra_handle)
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

	if (commit(user_status, tra_handle, false) == FB_SUCCESS)
		*tra_handle = NULL;

	return user_status[1];
}


ISC_STATUS GDS_COMMIT_RETAINING(ISC_STATUS * user_status, jrd_tra** tra_handle)
{
/**************************************
 *
 *	g d s _ $ c o m m i t _ r e t a i n i n g
 *
 **************************************
 *
 * Functional description
 *	Commit a transaction.
 *
 **************************************/

	return commit(user_status, tra_handle, true);
}


ISC_STATUS GDS_COMPILE(ISC_STATUS* user_status,
						Attachment** db_handle,
						jrd_req** req_handle,
						SSHORT blr_length,
						const SCHAR* blr)
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
	return compile_request(user_status, db_handle, req_handle, blr_length, blr, 0, NULL, 0, NULL);
}


ISC_STATUS GDS_CREATE_BLOB2(ISC_STATUS* user_status,
							Attachment** db_handle,
							jrd_tra** tra_handle,
							blb** blob_handle,
							bid* blob_id,
							USHORT bpb_length,
							const UCHAR* bpb)
{
/**************************************
 *
 *	g d s _ $ c r e a t e _ b l o b
 *
 **************************************
 *
 * Functional description
 *	Create a new blob.
 *
 **************************************/
	ThreadContextHolder tdbb(user_status);

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
	LOG_call(log_create_blob2, *db_handle, *tra_handle, *blob_handle, blob_id,
			 bpb_length, bpb);
#endif

	try
	{
		if (*blob_handle)
			Firebird::status_exception::raise(isc_bad_segstr_handle, 0);

		validateHandle(tdbb, *db_handle);
		validateHandle(tdbb, *tra_handle);
		DatabaseContextHolder dbbHolder(tdbb);
		check_database(tdbb);

		jrd_tra* transaction = find_transaction(tdbb, isc_segstr_wrong_db);

		blb* blob = BLB_create2(tdbb, transaction, blob_id, bpb_length, bpb);
		*blob_handle = blob;
	
#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
		LOG_call(log_handle_returned, *blob_handle);
		LOG_call(log_handle_returned, blob_id->bid_stuff.bid_temp_id);
#endif
	}
	catch (const Firebird::Exception& ex)
	{
		Firebird::stuff_exception(user_status, ex);
	}

	return user_status[1];
}


ISC_STATUS GDS_CREATE_DATABASE(ISC_STATUS*	user_status,
								USHORT	_file_length,
								const TEXT*	_file_name,
								Attachment**	handle,
								USHORT	dpb_length,
								const UCHAR*	dpb,
								USHORT	db_type,
								const TEXT*	_expanded_filename)
{
/**************************************
 *
 *	g d s _ $ c r e a t e _ d a t a b a s e
 *
 **************************************
 *
 * Functional description
 *	Create a nice, squeeky clean database, uncorrupted by user data.
 *
 **************************************/
	ThreadContextHolder tdbb(user_status);

	if (*handle)
	{
		return handle_error(user_status, isc_bad_db_handle);
	}

	Firebird::PathName file_name(_file_name, 
		_file_length ? _file_length : strlen(_file_name));
	Firebird::PathName expanded_name(file_name);

	// Resolve given alias name
	const bool is_alias = ResolveDatabaseAlias(expanded_name, expanded_name);
	if (is_alias)
	{
		ISC_expand_filename(expanded_name, false);
	}
	else
	{
		expanded_name = _expanded_filename;
	}

	UserId userId;
	DatabaseOptions options;
	SecurityInitHolder siHolder;

	try {
		// Process database parameter block
		bool invalid_client_SQL_dialect = false;
		options.get(dpb, dpb_length, invalid_client_SQL_dialect);
		if (!invalid_client_SQL_dialect && options.dpb_sql_dialect == 99) {
			options.dpb_sql_dialect = 0;
		}

		// Check for correct credentials supplied
		getUserInfo(userId, options);
	}
	catch (const Firebird::Exception& e)
	{
		e.stuff_exception(user_status);
		return user_status[1];
	}

	// Check database against conf file.
	const vdnResult vdn = verify_database_name(expanded_name, user_status);
	if (!is_alias && vdn == vdnFail)
	{
		return user_status[1];
	} 

	Database* dbb = init(tdbb, user_status, expanded_name, false);
	if (!dbb) {
		dbMutexUnlock();
		return user_status[1];
	}

	tdbb->setDatabase(dbb);
	DatabaseContextHolder dbbHolder(tdbb);

	dbb->dbb_flags |= (DBB_being_opened | options.dpb_flags);

	ISC_STATUS* const status = user_status;
	Attachment* attachment = NULL;

	bool initing_security = false;

	try {

#ifndef NO_NFS
	// Don't check nfs if single user
	if (!options.dpb_single_user)
#endif
	{
		// Check to see if the database is truly local or if it just looks
		// that way

		if (ISC_check_if_remote(expanded_name, true)) 
		{
			ERR_post(isc_unavailable, 0);
		}
	}

	if (options.dpb_key.hasData()) 
	{
		dbb->dbb_encrypt_key = options.dpb_key;
	}

	tdbb->setAttachment((attachment = FB_NEW(*dbb->dbb_permanent) Attachment(dbb)));
	attachment->att_filename = is_alias ? file_name : expanded_name;
	attachment->att_network_protocol = options.dpb_network_protocol;
	attachment->att_remote_address = options.dpb_remote_address;
	attachment->att_remote_pid = options.dpb_remote_pid;
	attachment->att_remote_process = options.dpb_remote_process;
	attachment->att_next = dbb->dbb_attachments;

	dbb->dbb_attachments = attachment;
	dbb->dbb_flags &= ~DBB_being_opened;
	dbb->dbb_sys_trans->tra_attachment = attachment;

	if (options.dpb_working_directory.hasData()) {
		attachment->att_working_directory = options.dpb_working_directory;
	}

	if (options.dpb_gbak_attach) {
		attachment->att_flags |= ATT_gbak_attachment;
	}

	if (options.dpb_no_db_triggers)
		attachment->att_flags |= ATT_no_db_triggers;

	switch (options.dpb_sql_dialect) {
	case 0:
		// This can be issued by QLI, GDEF and old BDE clients.  
		// In this case assume dialect 1
		options.dpb_sql_dialect = SQL_DIALECT_V5;
	case SQL_DIALECT_V5:
		break;
	case SQL_DIALECT_V6:
		dbb->dbb_flags |= DBB_DB_SQL_dialect_3;
		break;
	default:
		ERR_post(isc_database_create_failed, isc_arg_string,
				 expanded_name.c_str(), isc_arg_gds, isc_inv_dialect_specified,
				 isc_arg_number, options.dpb_sql_dialect, isc_arg_gds,
				 isc_valid_db_dialects, isc_arg_string, "1 and 3", 0);
		break;
	}

	attachment->att_charset = options.dpb_interp;

	if (options.dpb_lc_messages.hasData()) {
		attachment->att_lc_messages = options.dpb_lc_messages;
	}

	if (!options.dpb_page_size) {
		options.dpb_page_size = DEFAULT_PAGE_SIZE;
	}

	USHORT page_size = MIN_NEW_PAGE_SIZE;
	for (; page_size < MAX_PAGE_SIZE; page_size <<= 1)
	{
		if (options.dpb_page_size < page_size << 1)
			break;
	}

	dbb->dbb_page_size =
		(page_size > MAX_PAGE_SIZE) ? MAX_PAGE_SIZE : page_size;

	LCK_init(tdbb, LCK_OWNER_attachment);	// For the attachment
	attachment->att_flags |= ATT_lck_init_done;
	// Extra LCK_init() done to keep the lock table until the
	// database is shutdown() after the last detach.
	LCK_init(tdbb, LCK_OWNER_database);
	dbb->dbb_flags |= DBB_lck_init_done;

	INI_init();
	PAG_init();
	initing_security = true;

    SCL_init(true, userId, tdbb);

	initing_security = false;

	PageSpace* pageSpace = dbb->dbb_page_manager.findPageSpace(DB_PAGE_SPACE);
	try
	{
		// try to create with overwrite = false
		pageSpace->file = PIO_create(dbb, expanded_name, false, false, false);
	}
	catch (Firebird::status_exception)
	{
		if (options.dpb_overwrite)
		{
			if (GDS_ATTACH_DATABASE(user_status, _file_length, _file_name, handle,
					dpb_length, dpb, _expanded_filename) == isc_adm_task_denied)
			{
				throw;
			}

			bool allow_overwrite = false;

			if (*handle)
			{
				allow_overwrite = (*handle)->att_user->locksmith();
				GDS_DETACH(user_status, handle);
			}
			else
			{
				// clear status after failed attach
				user_status[0] = 0;
				allow_overwrite = true;
			}

			if (allow_overwrite)
			{
				// file is a database and the user (SYSDBA or owner) has right to overwrite
				pageSpace->file =
					PIO_create(dbb, expanded_name, options.dpb_overwrite, false, false);
			}
			else
			{
				ERR_post(isc_no_priv,
					isc_arg_string, "overwrite",
					isc_arg_string, "database",
					isc_arg_string,
					ERR_cstring(expanded_name.c_str()), 0);
			}
		}
		else
			throw;
	}

	const jrd_file* first_dbb_file = pageSpace->file;

	if (options.dpb_set_page_buffers)
		dbb->dbb_page_buffers = options.dpb_page_buffers;
	CCH_init(tdbb, options.dpb_buffers);

	// Initialize locks
	init_database_locks(tdbb, dbb);

	// Initialize backup difference subsystem. This must be done before WAL and shadowing
	// is enabled because nbackup it is a lower level subsystem
	dbb->dbb_backup_manager = FB_NEW(*dbb->dbb_permanent) BackupManager(tdbb, dbb, nbak_state_normal); 
	
	dbb->dbb_backup_manager->dbCreating = true;
	PAG_format_header();
	INI_init2();
	PAG_format_log();
	PAG_format_pip(tdbb, *pageSpace);

	if (options.dpb_set_page_buffers)
		PAG_set_page_buffers(options.dpb_page_buffers);

	if (options.dpb_set_no_reserve)
		PAG_set_no_reserve(dbb, options.dpb_no_reserve);

	INI_format(attachment->att_user->usr_user_name.c_str(), 
			   options.dpb_set_db_charset.c_str());

	// There is no point to move database online at database creation since it is online by default.
	// We do not allow to create database that is fully shut down.
	if (options.dpb_online || (options.dpb_shutdown & isc_dpb_shut_mode_mask) == isc_dpb_shut_full)
		ERR_post(isc_bad_shutdown_mode, isc_arg_string, ERR_string(file_name), 0);
	
	if (options.dpb_shutdown) {
		if (!SHUT_database(tdbb, options.dpb_shutdown,
						   options.dpb_shutdown_delay))
		{
			ERR_post(isc_no_priv,
					 isc_arg_string, "shutdown or online",
					 isc_arg_string, "database",
					 isc_arg_string,
					 ERR_string(file_name), 0);
		}
	}
	
	if (options.dpb_sweep_interval != -1) {
		PAG_sweep_interval(options.dpb_sweep_interval);
		dbb->dbb_sweep_interval = options.dpb_sweep_interval;
	}

	if (options.dpb_set_force_write)
		PAG_set_force_write(dbb, options.dpb_force_write);

	// initialize shadowing semaphore as soon as the database is ready for it
	// but before any real work is done

	SDW_init(options.dpb_activate_shadow, options.dpb_delete_shadow);

#ifdef GARBAGE_THREAD
	VIO_init(tdbb);
#endif

    if (options.dpb_set_db_readonly) {
        if (!CCH_exclusive (tdbb, LCK_EX, WAIT_PERIOD))
            ERR_post (isc_lock_timeout, isc_arg_gds, isc_obj_in_use,
                      isc_arg_string, 
                      ERR_string (file_name), 
                      0);
        
        PAG_set_db_readonly (dbb, options.dpb_db_readonly);
    }

	PAG_attachment_id(tdbb);

	CCH_release_exclusive(tdbb);

	// Figure out what character set & collation this attachment prefers

	find_intl_charset(tdbb, attachment, &options);

#ifdef WIN_NT
	dbb->dbb_filename.assign(first_dbb_file->fil_string,
									first_dbb_file->fil_length);
#else
	dbb->dbb_filename = expanded_name;
#endif

	// NS: Use alias as database ID only if accessing database using file name is not possible.
	//
	// This way we:
	// 1. Ensure uniqueness of ID even in presence of multiple processes
	// 2. Make sure that ID value can be used to connect back to database
	//
	if (is_alias && vdn == vdnFail)
		dbb->dbb_database_name = file_name;
	else
		dbb->dbb_database_name = dbb->dbb_filename;

	CCH_flush(tdbb, FLUSH_FINI, 0);

	dbb->dbb_backup_manager->dbCreating = false;

	dbMutexUnlock();

	*handle = attachment;

	}	// try
	catch (const DelayFailedLogin& ex)
	{
		ISC_STATUS s = unwindAttach(ex, user_status, tdbb, attachment, dbb);
		ex.sleep();
		return s;
	}
	catch (const Firebird::Exception& ex)
	{
		return unwindAttach(ex, user_status, tdbb, attachment, dbb);
	}

	siHolder.clear();
	return FB_SUCCESS;
}


ISC_STATUS GDS_DATABASE_INFO(ISC_STATUS* user_status,
							Attachment** handle,
							SSHORT item_length,
							const SCHAR* items,
							SSHORT buffer_length,
							SCHAR* buffer)
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
	ThreadContextHolder tdbb(user_status);

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
	LOG_call(log_database_info2, *handle, item_length, items, buffer_length);
#endif

	try
	{
		Attachment* attachment = *handle;
		validateHandle(tdbb, attachment);
		DatabaseContextHolder dbbHolder(tdbb);
		check_database(tdbb);

		INF_database_info(items, item_length, buffer, buffer_length);
	}
	catch (const Firebird::Exception& ex)
	{
		Firebird::stuff_exception(user_status, ex);
	}

	return user_status[1];
}


ISC_STATUS GDS_DDL(ISC_STATUS* user_status,
					Attachment** db_handle,
					jrd_tra** tra_handle,
					USHORT ddl_length,
					const SCHAR* ddl)
{
/**************************************
 *
 *	g d s _ $ d d l
 *
 **************************************
 *
 * Functional description
 *
 **************************************/
	ThreadContextHolder tdbb(user_status);

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
	LOG_call(log_ddl, *db_handle, *tra_handle, ddl_length, ddl);
#endif

	try {
		Attachment* attachment = *db_handle;
		validateHandle(tdbb, attachment);
		validateHandle(tdbb, *tra_handle);
		DatabaseContextHolder dbbHolder(tdbb);
		check_database(tdbb);

		jrd_tra* transaction = find_transaction(tdbb, isc_segstr_wrong_db);

		DYN_ddl(attachment, transaction, ddl_length,
				reinterpret_cast<const UCHAR*>(ddl));

		// Perform an auto commit for autocommit transactions.
		// This is slightly tricky. If the commit retain works,
		// all is well. If TRA_commit() fails, we perform
		// a rollback_retain(). This will backout the
		// effects of the transaction, mark it dead and
		// start a new transaction.

		if (transaction->tra_flags & TRA_perform_autocommit)
		{
			transaction->tra_flags &= ~TRA_perform_autocommit;

			try {
				TRA_commit(tdbb, transaction, true);
			}
			catch (const Firebird::Exception&) {
				ISC_STATUS_ARRAY temp_status;
				tdbb->tdbb_status_vector = temp_status;
				try {
					TRA_rollback(tdbb, transaction, true, false);
				}
				catch (const Firebird::Exception&)
				{} // no-op
				tdbb->tdbb_status_vector = user_status;
				throw;
			}
		}
	}
	catch (const Firebird::Exception& ex) {
		Firebird::stuff_exception(user_status, ex);
	}

	return user_status[1];
}


ISC_STATUS GDS_DETACH(ISC_STATUS* user_status, Attachment** handle)
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
	ThreadContextHolder tdbb(user_status);

	try
	{
		DbMutexGuard guard;

		Attachment* attachment = *handle;
		validateHandle(tdbb, attachment);

		{ // holder scope
			DatabaseContextHolder dbbHolder(tdbb);

			Database* dbb = tdbb->getDatabase();

			// if this is the last attachment, mark dbb as not in use
			if (dbb->dbb_attachments == attachment &&
				!attachment->att_next &&
				!(dbb->dbb_flags & DBB_being_opened))
			{
				dbb->dbb_flags |= DBB_not_in_use;
			}

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
			LOG_call(log_detach, *handle);
			LOG_call(log_statistics, dbb->dbb_reads, dbb->dbb_writes,
					 dbb->dbb_max_memory);
#endif

			// Purge attachment, don't rollback open transactions
			attachment->att_flags |= ATT_cancel_disable;
			purge_attachment(tdbb, user_status, attachment, false);

			*handle = NULL;
		}

		SecurityDatabase::shutdown();
	}
	catch (const Firebird::Exception& ex) {
		Database* dbb = tdbb->getDatabase();
		if (dbb)
		{
			dbb->dbb_flags &= ~DBB_not_in_use;
		}
		Firebird::stuff_exception(user_status, ex);
	}

	return user_status[1];
}


ISC_STATUS GDS_DROP_DATABASE(ISC_STATUS* user_status, Attachment** handle)
{
/**************************************
 *
 *	i s c _ d r o p _ d a t a b a s e
 *
 **************************************
 *
 * Functional description
 *	Close down and purge a database.
 *
 **************************************/
	ThreadContextHolder tdbb(user_status);

	try
	{
		DbMutexGuard guard;

		Attachment* attachment = *handle;
		validateHandle(tdbb, attachment);
		DatabaseContextHolder dbbHolder(tdbb);

		Database* dbb = tdbb->getDatabase();

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
		LOG_call(log_drop_database, *handle);
		LOG_call(log_statistics, dbb->dbb_reads, dbb->dbb_writes,
				 dbb->dbb_max_memory);
#endif
		const Firebird::PathName& file_name = attachment->att_filename;

		if (!attachment->locksmith())
		{
			ERR_post(isc_no_priv,
					 isc_arg_string, "drop",
					 isc_arg_string, "database",
					 isc_arg_string, ERR_cstring(file_name), 0);
		}

		if (attachment->att_flags & ATT_shutdown)
		{
			if (dbb->dbb_ast_flags & DBB_shutdown)
			{
				ERR_post(isc_shutdown, isc_arg_string,
						 ERR_cstring(file_name), 0);
			}
			else {
				ERR_post(isc_att_shutdown, 0);
			}
		}

		if (!CCH_exclusive(tdbb, LCK_PW, WAIT_PERIOD))
		{
			ERR_post(isc_lock_timeout,
					 isc_arg_gds, isc_obj_in_use,
					 isc_arg_string, ERR_cstring(file_name), 0);
		}

		// Check if same process has more attachments

		if (dbb->dbb_attachments && dbb->dbb_attachments->att_next) {
			ERR_post(isc_no_meta_update, isc_arg_gds, isc_obj_in_use,
					isc_arg_string, "DATABASE", 0);
		}

		// Forced release of all transactions
		purge_transactions(tdbb, attachment, true, attachment->att_flags);

		attachment->att_flags |= ATT_cancel_disable;

		// Here we have database locked in exclusive mode.
		// Just mark the header page with an 0 ods version so that no other
		// process can attach to this database once we release our exclusive
		// lock and start dropping files.

   		WIN window(HEADER_PAGE_NUMBER);
		Ods::header_page* header = (Ods::header_page*) CCH_FETCH(tdbb, &window, LCK_write, pag_header);
		CCH_MARK_MUST_WRITE(tdbb, &window);
		header->hdr_ods_version = 0;
		CCH_RELEASE(tdbb, &window);

		// This point on database is useless
		// mark the dbb unusable

		dbb->dbb_flags |= DBB_not_in_use;
		*handle = NULL;

		PageSpace* pageSpace = dbb->dbb_page_manager.findPageSpace(DB_PAGE_SPACE);
		const jrd_file* file = pageSpace->file;
		const Shadow* shadow = dbb->dbb_shadow;

		// Unlink attachment from database
		release_attachment(tdbb, attachment);

		shutdown_database(dbb, false);

		// drop the files here
		bool err = drop_files(file);
		for (; shadow; shadow = shadow->sdw_next) 
		{
			err = err || drop_files(shadow->sdw_file);
		}

		tdbb->setDatabase(NULL);
		Database::deleteDbb(dbb);

		if (err) {
			user_status[0] = isc_arg_gds;
			user_status[1] = isc_drdb_completed_with_errs;
			user_status[2] = isc_arg_end;
		}
	}
	catch (const Firebird::Exception& ex) {
		Firebird::stuff_exception(user_status, ex);
	}

	return user_status[1];
}


ISC_STATUS GDS_INTL_FUNCTION(ISC_STATUS* user_status, Attachment** handle,
	USHORT function, UCHAR charSetNumber, USHORT strLen, const UCHAR* str, void* result)
{
/**************************************
 *
 *	g d s _ i n t l _ f u n c t i o n
 *
 **************************************
 *
 * Functional description
 *	Return INTL informations.
 *  (candidate for removal when engine functions can be called by DSQL)
 *
 **************************************/
	ThreadContextHolder tdbb(user_status);

	try
	{
		Attachment* attachment = *handle;
		validateHandle(tdbb, attachment);
		DatabaseContextHolder dbbHolder(tdbb);
		check_database(tdbb);

		CharSet* charSet = INTL_charset_lookup(tdbb, charSetNumber);

		switch (function)
		{
			case INTL_FUNCTION_CHAR_LENGTH:
			{
				ULONG offendingPos;

				if (!charSet->wellFormed(strLen, str, &offendingPos))
				{
					ERR_post(isc_sqlerr,
							isc_arg_number, (SLONG) - 104,
							isc_arg_gds, isc_malformed_string, 0);
				}
				else
					*static_cast<USHORT*>(result) = charSet->length(strLen, str, true);

				break;
			}

			case INTL_FUNCTION_CONV_TO_METADATA:
			{
				Firebird::UCharBuffer* buffer = static_cast<Firebird::UCharBuffer*>(result);
				buffer->resize(INTL_convert_bytes(tdbb, CS_METADATA, buffer->getBuffer(strLen * 4),
					strLen * 4,	charSetNumber, str, strLen, ERR_post));
				break;
			}

			default:
				fb_assert(false);
		}
	}
	catch (const Firebird::Exception& ex)
	{
		Firebird::stuff_exception(user_status, ex);
	}

	return user_status[1];
}


ISC_STATUS GDS_DSQL_CACHE(ISC_STATUS* user_status, Attachment** handle,
						  USHORT operation, int type, const char* name, bool* obsolete)
{
/**************************************
 *
 *	g d s _ d s q l _ c a c h e
 *
 **************************************
 *
 * Functional description
 *	Manage DSQL cache locks.
 *  (candidate for removal when engine functions can be called by DSQL)
 *
 **************************************/
	ThreadContextHolder tdbb(user_status);

#ifdef SUPERSERVER
	if (obsolete)
		*obsolete = false;
#else
	try
	{
		Attachment* attachment = *handle;
		validateHandle(tdbb, attachment);
		DatabaseContextHolder dbbHolder(tdbb);
		check_database(tdbb);

		Database* dbb = tdbb->getDatabase();

		Firebird::string key((char*)&type, sizeof(type));
		key.append(name);

		DSqlCacheItem* item = attachment->att_dsql_cache.put(key);
		if (item)
		{
			item->obsolete = false;
			item->locked = false;
			item->lock = FB_NEW_RPT(*dbb->dbb_permanent, key.length()) Lock();

			item->lock->lck_type = LCK_dsql_cache;
			item->lock->lck_owner_handle = LCK_get_owner_handle(tdbb, item->lock->lck_type);
			item->lock->lck_parent = dbb->dbb_lock;
			item->lock->lck_dbb = dbb;
			item->lock->lck_object = (blk*)item;
			item->lock->lck_ast = blocking_ast_dsql_cache;
			item->lock->lck_length = key.length();
			memcpy(item->lock->lck_key.lck_string, key.c_str(), key.length());
		}
		else
		{
			item = &attachment->att_dsql_cache.current()->second;
		}

		if (obsolete)
			*obsolete = item->obsolete;

		if (operation == DSQL_CACHE_USE && !item->locked)
		{
			// lock to be notified by others when we should mark as obsolete
			LCK_lock(tdbb, item->lock, LCK_SR, LCK_WAIT);
			item->locked = true;
		}
		else if (operation == DSQL_CACHE_RELEASE)
		{
			// notify others through AST to mark as obsolete
			LCK_lock(tdbb, item->lock, LCK_EX, LCK_WAIT);

			// release lock
			LCK_release(tdbb, item->lock);
			item->locked = false;
		}

		item->obsolete = false;
	}
	catch (const Firebird::Exception& ex)
	{
		Firebird::stuff_exception(user_status, ex);
	}
#endif	// SUPERSERVER

	return user_status[1];
}


ISC_STATUS GDS_INTERNAL_COMPILE(ISC_STATUS* user_status,
								Attachment** db_handle,
								jrd_req** req_handle,
								SSHORT blr_length,
								const SCHAR* blr,
								USHORT string_length, const char* string,
								USHORT dbginfo_length, const UCHAR* dbginfo)
{
/**************************************
 *
 *	g d s _ $ i n t e r n a l _ c o m p i l e
 *
 **************************************
 *
 * Functional description
 *	Compile a request passing the SQL text and debug information.
 *  (candidate for removal when engine functions can be called by DSQL)
 *
 **************************************/
	return compile_request(user_status, db_handle, req_handle, blr_length, blr,
		string_length, string, dbginfo_length, dbginfo);
}


ISC_STATUS GDS_GET_SEGMENT(ISC_STATUS * user_status,
							blb** blob_handle,
							USHORT * length,
							USHORT buffer_length,
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
	ThreadContextHolder tdbb(user_status);

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
	LOG_call(log_get_segment2, *blob_handle, length, buffer_length);
#endif

	try
	{
		blb* blob = *blob_handle;
		validateHandle(tdbb, blob);
		DatabaseContextHolder dbbHolder(tdbb);
		check_database(tdbb);

		*length = BLB_get_segment(tdbb, blob, buffer, buffer_length);
		user_status[0] = isc_arg_gds;
		user_status[2] = isc_arg_end;

		if (blob->blb_flags & BLB_eof) {
			user_status[1] = isc_segstr_eof;
		}
		else if (blob->blb_fragment_size) {
			user_status[1] = isc_segment;
		}
	}
	catch (const Firebird::Exception& ex)
	{
		Firebird::stuff_exception(user_status, ex);
	}

	return user_status[1];
}


ISC_STATUS GDS_GET_SLICE(ISC_STATUS* user_status,
						Attachment** db_handle,
						jrd_tra** tra_handle,
						ISC_QUAD* array_id,
						USHORT sdl_length,
						const UCHAR* sdl,
						USHORT param_length,
						const UCHAR* param,
						SLONG slice_length,
						UCHAR* slice,
						SLONG* return_length)
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
	ThreadContextHolder tdbb(user_status);

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
	LOG_call(log_get_slice2, *db_handle, *tra_handle, *array_id, sdl_length,
			 sdl, param_length, param, slice_length);
#endif

	try
	{
		validateHandle(tdbb, *db_handle);
		validateHandle(tdbb, *tra_handle);
		DatabaseContextHolder dbbHolder(tdbb);
		check_database(tdbb);

		jrd_tra* transaction = find_transaction(tdbb, isc_segstr_wrong_db);
	
		if (!array_id->gds_quad_low && !array_id->gds_quad_high) {
			MOVE_CLEAR(slice, slice_length);
			*return_length = 0;
		}
		else {
			*return_length = BLB_get_slice(tdbb,
									   transaction,
									   reinterpret_cast<bid*>(array_id),
									   sdl,
									   param_length,
									   param,
									   slice_length, slice);
		}
	}
	catch (const Firebird::Exception& ex)
	{
		Firebird::stuff_exception(user_status, ex);
	}

	return user_status[1];
}


ISC_STATUS GDS_OPEN_BLOB2(ISC_STATUS* user_status,
						Attachment** db_handle,
						jrd_tra** tra_handle,
						blb** blob_handle,
						bid* blob_id,
						USHORT bpb_length,
						const UCHAR* bpb)
{
/**************************************
 *
 *	g d s _ $ o p e n _ b l o b 2
 *
 **************************************
 *
 * Functional description
 *	Open an existing blob.
 *
 **************************************/
	ThreadContextHolder tdbb(user_status);

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
	LOG_call(log_open_blob2, *db_handle, *tra_handle, *blob_handle, blob_id,
			 bpb_length, bpb);
#endif

	try
	{
		if (*blob_handle)
			Firebird::status_exception::raise(isc_bad_segstr_handle, 0);

		validateHandle(tdbb, *db_handle);
		validateHandle(tdbb, *tra_handle);
		DatabaseContextHolder dbbHolder(tdbb);
		check_database(tdbb);

		jrd_tra* transaction = find_transaction(tdbb, isc_segstr_wrong_db);

		blb* blob = BLB_open2(tdbb, transaction, blob_id, bpb_length, bpb, true);
		*blob_handle = blob;
	
#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
		LOG_call(log_handle_returned, *blob_handle);
#endif
	}
	catch (const Firebird::Exception& ex)
	{
		Firebird::stuff_exception(user_status, ex);
	}

	return user_status[1];
}


ISC_STATUS GDS_PREPARE(ISC_STATUS * user_status,
						jrd_tra** tra_handle,
						USHORT length,
						const UCHAR* msg)
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
	ThreadContextHolder tdbb(user_status);

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
	LOG_call(log_prepare2, *tra_handle, length, msg);
#endif

	try
	{
		jrd_tra* transaction = *tra_handle;
		validateHandle(tdbb, transaction);
		DatabaseContextHolder dbbHolder(tdbb);
		check_database(tdbb);

		prepare(tdbb, transaction, length, msg);
	}
	catch (const Firebird::Exception& ex)
	{
		Firebird::stuff_exception(user_status, ex);
	}

	return user_status[1];
}


ISC_STATUS GDS_PUT_SEGMENT(ISC_STATUS* user_status,
							blb** blob_handle,
							USHORT buffer_length,
							const UCHAR* buffer)
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
	ThreadContextHolder tdbb(user_status);

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
	LOG_call(log_put_segment, *blob_handle, buffer_length, buffer);
#endif

	try
	{
		blb* blob = *blob_handle;
		validateHandle(tdbb, blob);
		DatabaseContextHolder dbbHolder(tdbb);
		check_database(tdbb);

		BLB_put_segment(tdbb, blob, buffer, buffer_length);
	}
	catch (const Firebird::Exception& ex)
	{
		Firebird::stuff_exception(user_status, ex);
	}

	return user_status[1];
}


ISC_STATUS GDS_PUT_SLICE(ISC_STATUS* user_status,
						Attachment** db_handle,
						jrd_tra** tra_handle,
						ISC_QUAD* array_id,
						USHORT sdl_length,
						const UCHAR* sdl,
						USHORT param_length,
						const UCHAR* param,
						SLONG slice_length,
						UCHAR* slice)
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
	ThreadContextHolder tdbb(user_status);

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
	LOG_call(log_put_slice, *db_handle, *tra_handle, *array_id, sdl_length,
			 sdl, param_length, param, slice_length, slice);
#endif

	try
	{
		validateHandle(tdbb, *db_handle);
		validateHandle(tdbb, *tra_handle);
		DatabaseContextHolder dbbHolder(tdbb);
		check_database(tdbb);

		jrd_tra* transaction = find_transaction(tdbb, isc_segstr_wrong_db);

		BLB_put_slice(tdbb,
				  transaction,
				  reinterpret_cast<bid*>(array_id),
				  sdl,
				  param_length,
				  reinterpret_cast<const SLONG*>(param), slice_length, slice);
	}
	catch (const Firebird::Exception& ex)
	{
		Firebird::stuff_exception(user_status, ex);
	}

	return user_status[1];
}


ISC_STATUS GDS_QUE_EVENTS(ISC_STATUS* user_status,
							Attachment** handle,
							SLONG* id,
							SSHORT length,
							const UCHAR* items,
							FPTR_EVENT_CALLBACK ast,
							void* arg)
{
/**************************************
 *
 *	g d s _ $ q u e _ e v e n t s
 *
 **************************************
 *
 * Functional description
 *	Que a request for event notification.
 *
 **************************************/
	ThreadContextHolder tdbb(user_status);

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
	LOG_call(log_que_events, *handle, length, items);
#endif

	try
	{
		Attachment* attachment = *handle;
		validateHandle(tdbb, attachment);
		DatabaseContextHolder dbbHolder(tdbb);
		check_database(tdbb);

		Database* dbb = tdbb->getDatabase();
		Lock* lock = dbb->dbb_lock;

		if (!attachment->att_event_session &&
			!(attachment->att_event_session = EVENT_create_session(user_status)))
		{
			return user_status[1];
		}
	
		*id = EVENT_que(user_status,
						attachment->att_event_session,
						lock->lck_length,
						(const TEXT*) &lock->lck_key, length, items, ast, arg);
	
#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
		LOG_call(log_handle_returned, *id);
#endif
	}
	catch (const Firebird::Exception& ex)
	{
		Firebird::stuff_exception(user_status, ex);
	}

	return user_status[1];
}


ISC_STATUS GDS_RECEIVE(ISC_STATUS * user_status,
						jrd_req** req_handle,
						USHORT msg_type,
						USHORT msg_length,
						SCHAR * msg,
						SSHORT level
#ifdef SCROLLABLE_CURSORS
						, USHORT direction,
						ULONG offset
#endif
	)
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
	ThreadContextHolder tdbb(user_status);

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
	LOG_call(log_receive2, *req_handle, msg_type, msg_length, level);
#endif

	try
	{
		jrd_req* request = *req_handle;
		validateHandle(tdbb, request);
		DatabaseContextHolder dbbHolder(tdbb);
		check_database(tdbb);
		check_transaction(tdbb, request->req_transaction);

		verify_request_synchronization(request, level);

#ifdef SCROLLABLE_CURSORS
		if (direction)
			EXE_seek(tdbb, request, direction, offset);
#endif
	
		EXE_receive(tdbb, request, msg_type, msg_length,
					reinterpret_cast<UCHAR*>(msg), true);
	
		check_autocommit(request, tdbb);
	
		if (request->req_flags & req_warning)
			request->req_flags &= ~req_warning;
	}
	catch (const Firebird::Exception& ex)
	{
		Firebird::stuff_exception(user_status, ex);
	}

	return user_status[1];
}


ISC_STATUS GDS_RECONNECT(ISC_STATUS* user_status,
						Attachment** db_handle,
						jrd_tra** tra_handle,
						SSHORT length,
						const UCHAR* id)
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
	ThreadContextHolder tdbb(user_status);

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
	LOG_call(log_reconnect, *db_handle, *tra_handle, length, id);
#endif

	try
	{
		if (*tra_handle)
			Firebird::status_exception::raise(isc_bad_trans_handle, 0);

		Attachment* attachment = *db_handle;
		validateHandle(tdbb, attachment);
		DatabaseContextHolder dbbHolder(tdbb);
		check_database(tdbb);

		jrd_tra* transaction = TRA_reconnect(tdbb, id, length);
		*tra_handle = transaction;
	
#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
		LOG_call(log_handle_returned, *tra_handle);
#endif
	}
	catch (const Firebird::Exception& ex)
	{
		Firebird::stuff_exception(user_status, ex);
	}

	return user_status[1];
}


ISC_STATUS GDS_RELEASE_REQUEST(ISC_STATUS * user_status, jrd_req** req_handle)
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
	ThreadContextHolder tdbb(user_status);

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
	LOG_call(log_release_request, *req_handle);
#endif

	try
	{
		jrd_req* request = *req_handle;
		validateHandle(tdbb, request);
		DatabaseContextHolder dbbHolder(tdbb);
		check_database(tdbb);

		CMP_release(tdbb, request);
		*req_handle = NULL;
	}
	catch (const Firebird::Exception& ex)
	{
		Firebird::stuff_exception(user_status, ex);
	}

	return user_status[1];
}


ISC_STATUS GDS_REQUEST_INFO(ISC_STATUS* user_status,
							jrd_req** req_handle,
							SSHORT level,
							SSHORT item_length,
							const SCHAR* items,
							SSHORT buffer_length,
							SCHAR* buffer)
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
	ThreadContextHolder tdbb(user_status);

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
	LOG_call(log_request_info2, *req_handle, level, item_length, items,
			 buffer_length);
#endif

	try
	{
		jrd_req* request = *req_handle;
		validateHandle(tdbb, request);
		DatabaseContextHolder dbbHolder(tdbb);
		check_database(tdbb);

		verify_request_synchronization(request, level);

		INF_request_info(request, items, item_length, buffer, buffer_length);
	}
	catch (const Firebird::Exception& ex)
	{
		Firebird::stuff_exception(user_status, ex);
	}

	return user_status[1];
}


ISC_STATUS GDS_ROLLBACK_RETAINING(ISC_STATUS * user_status,
									jrd_tra** tra_handle)
{
/**************************************
 *
 *	i s c _ r o l l b a c k _ r e t a i n i n g
 *
 **************************************
 *
 * Functional description
 *	Abort a transaction but keep the environment valid
 *
 **************************************/
	return rollback(user_status, tra_handle, true);
}


ISC_STATUS GDS_ROLLBACK(ISC_STATUS * user_status, jrd_tra** tra_handle)
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
	if (rollback(user_status, tra_handle, false) == FB_SUCCESS)
		*tra_handle = NULL;

	return user_status[1];
}


ISC_STATUS GDS_SEEK_BLOB(ISC_STATUS * user_status,
						blb** blob_handle,
						SSHORT mode,
						SLONG offset,
						SLONG * result)
{
/**************************************
 *
 *	g d s _ $ s e e k _ b l o b
 *
 **************************************
 *
 * Functional description
 *	Seek a stream blob.
 *
 **************************************/
	ThreadContextHolder tdbb(user_status);

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
	LOG_call(log_blob_seek, *blob_handle, mode, offset);
#endif

	try
	{
		blb* blob = *blob_handle;
		validateHandle(tdbb, blob);
		DatabaseContextHolder dbbHolder(tdbb);
		check_database(tdbb);

		*result = BLB_lseek(blob, mode, offset);
	}
	catch (const Firebird::Exception& ex)
	{
		Firebird::stuff_exception(user_status, ex);
	}

	return user_status[1];
}


ISC_STATUS GDS_SEND(ISC_STATUS * user_status,
					jrd_req** req_handle,
					USHORT msg_type,
					USHORT msg_length,
					SCHAR * msg,
					SSHORT level)
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
	ThreadContextHolder tdbb(user_status);

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
	LOG_call(log_send, *req_handle, msg_type, msg_length, msg, level);
#endif

	try
	{
		jrd_req* request = *req_handle;
		validateHandle(tdbb, request);
		DatabaseContextHolder dbbHolder(tdbb);
		check_database(tdbb);
		check_transaction(tdbb, request->req_transaction);

		verify_request_synchronization(request, level);
	
		EXE_send(tdbb, request, msg_type, msg_length,
				reinterpret_cast<UCHAR*>(msg));
	
		check_autocommit(request, tdbb);
	
		if (request->req_flags & req_warning)
			request->req_flags &= ~req_warning;
	}
	catch (const Firebird::Exception& ex)
	{
		Firebird::stuff_exception(user_status, ex);
	}

	return user_status[1];
}


ISC_STATUS GDS_SERVICE_ATTACH(ISC_STATUS* user_status,
							USHORT service_length,
							const TEXT* service_name,
							Service** svc_handle,
							USHORT spb_length,
							const SCHAR* spb)
{
/**************************************
 *
 *	g d s _ $ s e r v i c e _ a t t a c h
 *
 **************************************
 *
 * Functional description
 *	Connect to a Firebird service.
 *
 **************************************/
	ThreadContextHolder tdbb(user_status);

	try
	{
		if (*svc_handle)
			Firebird::status_exception::raise(isc_bad_svc_handle, 0);

		*svc_handle = new Service(service_length, service_name, spb_length, spb);
	}
	catch (const DelayFailedLogin& ex)
	{
		ex.sleep();
		Firebird::stuff_exception(user_status, ex);
	}
	catch (const Firebird::Exception& ex)
	{
		Firebird::stuff_exception(user_status, ex);
	}

	return user_status[1];
}


ISC_STATUS GDS_SERVICE_DETACH(ISC_STATUS* user_status, Service** svc_handle)
{
/**************************************
 *
 *	g d s _ $ s e r v i c e _ d e t a c h
 *
 **************************************
 *
 * Functional description
 *	Close down a service.
 *
 **************************************/
	ThreadContextHolder tdbb(user_status);

	try
	{
		Service* service = *svc_handle;
		validateHandle(service);

		service->detach();
		*svc_handle = NULL;
	}
	catch (const Firebird::Exception& ex)
	{
		Firebird::stuff_exception(user_status, ex);
	}

	return user_status[1];
}


ISC_STATUS GDS_SERVICE_QUERY(ISC_STATUS*	user_status,
							Service**	svc_handle,
							ULONG*	reserved,
							USHORT	send_item_length,
							const SCHAR*	send_items,
							USHORT	recv_item_length,
							const SCHAR*	recv_items,
							USHORT	buffer_length,
							SCHAR*	buffer)
{
/**************************************
 *
 *	g d s _ $ s e r v i c e _ q u e r y
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
 *
 **************************************/
	ThreadContextHolder tdbb(user_status);

	try
	{
		Service* service = *svc_handle;
		validateHandle(service);

		if (service->getVersion() == isc_spb_version1) {
			service->query(send_item_length, send_items, recv_item_length,
					recv_items, buffer_length, buffer);
		}
		else {
			// For SVC_query2, we are going to completly dismantle user_status (since at this point it is
			// meaningless anyway).  The status vector returned by this function can hold information about
			// the call to query the service manager and/or a service thread that may have been running.

			service->query2(tdbb, send_item_length, send_items,
					recv_item_length, recv_items, buffer_length, buffer);
	
			// If there is a status vector from a service thread, copy it into the thread status
			int len, warning;
			PARSE_STATUS(service->getStatus(), len, warning);
			if (len) {
				memcpy(tdbb->tdbb_status_vector, service->getStatus(), sizeof(ISC_STATUS) * len);
				// Empty out the service status vector
				memset(service->getStatus(), 0, sizeof(ISC_STATUS_ARRAY));
			}
		}
	}
	catch (const Firebird::Exception& ex)
	{
		Firebird::stuff_exception(user_status, ex);
	}

	return user_status[1];
}


ISC_STATUS GDS_SERVICE_START(ISC_STATUS*	user_status,
							Service**	svc_handle,
							ULONG*	reserved,
							USHORT	spb_length,
							const SCHAR*	spb)
{
/**************************************
 *
 *	g d s _ s e r v i c e _ s t a r t
 *
 **************************************
 *
 * Functional description
 *	Start the specified service
 *
 *	NOTE: The parameter RESERVED must not be used
 *	for any purpose as there are networking issues
 *  	involved (as with any handle that goes over the
 *   	network).  This parameter will be implemented at
 * 	a later date.
 **************************************/
	ThreadContextHolder tdbb(user_status);

	try
	{
		Service* service = *svc_handle;
		validateHandle(service);

		service->start(spb_length, spb);
	
		if (service->getStatus()[1]) {
			ISC_STATUS* svc_status = service->getStatus();
			ISC_STATUS* tdbb_status = tdbb->tdbb_status_vector;

			while (*svc_status) {
				*tdbb_status++ = *svc_status++;
			}
			*tdbb_status = isc_arg_end;
		}
	}
	catch (const Firebird::Exception& ex)
	{
		Firebird::stuff_exception(user_status, ex);
	}

	return user_status[1];
}


ISC_STATUS GDS_START_AND_SEND(ISC_STATUS* user_status,
							jrd_req** req_handle,
							jrd_tra** tra_handle,
							USHORT msg_type,
							USHORT msg_length,
							SCHAR* msg,
							SSHORT level)
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
	ThreadContextHolder tdbb(user_status);

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
	LOG_call(log_start_and_send, *req_handle, *tra_handle, msg_type,
			 msg_length, msg, level);
#endif

	try
	{
		jrd_req* request = *req_handle;
		validateHandle(tdbb, request);
		validateHandle(tdbb, *tra_handle);
		DatabaseContextHolder dbbHolder(tdbb);
		check_database(tdbb);
		check_transaction(tdbb, request->req_transaction);

		jrd_tra* transaction = find_transaction(tdbb, isc_req_wrong_db);
	
		if (level)
			request = CMP_clone_request(tdbb, request, level, false);
	
		EXE_unwind(tdbb, request);
		EXE_start(tdbb, request, transaction);
		EXE_send(tdbb, request, msg_type, msg_length,
				reinterpret_cast<UCHAR*>(msg));
	
		check_autocommit(request, tdbb);
	
		if (request->req_flags & req_warning)
			request->req_flags &= ~req_warning;
	}
	catch (const Firebird::Exception& ex)
	{
		Firebird::stuff_exception(user_status, ex);
	}

	return user_status[1];
}


ISC_STATUS GDS_START(ISC_STATUS * user_status,
					jrd_req** req_handle,
					jrd_tra** tra_handle,
					SSHORT level)
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
	ThreadContextHolder tdbb(user_status);

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
	LOG_call(log_start, *req_handle, *tra_handle, level);
#endif

	try
	{
		jrd_req* request = *req_handle;
		validateHandle(tdbb, request);
		validateHandle(tdbb, *tra_handle);
		DatabaseContextHolder dbbHolder(tdbb);
		check_database(tdbb);
		check_transaction(tdbb, request->req_transaction);

		jrd_tra* transaction = find_transaction(tdbb, isc_req_wrong_db);

		if (level)
			request = CMP_clone_request(tdbb, request, level, false);

		EXE_unwind(tdbb, request);
		EXE_start(tdbb, request, transaction);

		check_autocommit(request, tdbb);

		if (request->req_flags & req_warning)
			request->req_flags &= ~req_warning;
	}
	catch (const Firebird::Exception& ex)
	{
		Firebird::stuff_exception(user_status, ex);
	}

	return user_status[1];
}


ISC_STATUS GDS_START_MULTIPLE(ISC_STATUS * user_status,
							jrd_tra** tra_handle,
							USHORT count,
							TEB * vector)
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
	ThreadContextHolder tdbb(user_status);

	jrd_tra* prior = NULL;
	jrd_tra* transaction = NULL;

	try
	{
		if (*tra_handle)
			Firebird::status_exception::raise(isc_bad_trans_handle, 0);

		if (count < 1 || count > MAX_DB_PER_TRANS)
		{
			Firebird::status_exception::raise(isc_max_db_per_trans_allowed,
											  isc_arg_number, MAX_DB_PER_TRANS,
											  0);
		}

		for (TEB* v = vector; v < vector + count; v++)
		{
			Attachment* attachment = *v->teb_database;
			validateHandle(tdbb, attachment);
			DatabaseContextHolder dbbHolder(tdbb);
			check_database(tdbb);

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
			LOG_call(log_start_multiple, *tra_handle, count, vector);
#endif

			transaction = TRA_start(tdbb, v->teb_tpb_length, v->teb_tpb);

			transaction->tra_sibling = prior;
			prior = transaction;

			// run ON TRANSACTION START triggers
			EXE_execute_db_triggers(tdbb, transaction,
									jrd_req::req_trigger_trans_start);
		}

		*tra_handle = transaction;

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
		LOG_call(log_handle_returned, *tra_handle);
#endif
	}
	catch (const Firebird::Exception& ex)
	{
		if (prior)
		{
			ISC_STATUS_ARRAY temp_status;
			rollback(temp_status, &prior, false);
		}

		Firebird::stuff_exception(user_status, ex);
	}

	return user_status[1];
}


ISC_STATUS GDS_START_TRANSACTION(ISC_STATUS * user_status,
								jrd_tra** tra_handle,
								SSHORT count,
								...)
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
	try
	{
		if (count < 1 || USHORT(count) > MAX_DB_PER_TRANS)
		{
			Firebird::status_exception::raise(isc_max_db_per_trans_allowed,
											  isc_arg_number, MAX_DB_PER_TRANS,
											  0);
		}

		Firebird::HalfStaticArray<TEB, 16> tebs;
		tebs.grow(count);

		va_list ptr;
		va_start(ptr, count);

		for (TEB* teb_iter = tebs.begin(); teb_iter < tebs.end(); teb_iter++) {
			teb_iter->teb_database = va_arg(ptr, Attachment**);
			teb_iter->teb_tpb_length = va_arg(ptr, int);
			teb_iter->teb_tpb = va_arg(ptr, UCHAR*);
		}

		va_end(ptr);

		GDS_START_MULTIPLE(user_status, tra_handle, count, tebs.begin());
	}
	catch (const Firebird::Exception& ex)
	{
		Firebird::stuff_exception(user_status, ex);
	}

	return user_status[1];
}


ISC_STATUS GDS_TRANSACT_REQUEST(ISC_STATUS*	user_status,
								Attachment**		db_handle,
								jrd_tra**		tra_handle,
								USHORT	blr_length,
								const SCHAR*	blr,
								USHORT	in_msg_length,
								SCHAR*	in_msg,
								USHORT	out_msg_length,
								SCHAR*	out_msg)
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
	ThreadContextHolder tdbb(user_status);

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
	LOG_call(log_transact_request, *db_handle, *tra_handle,
			 blr_length, blr, in_msg_length, in_msg, out_msg_length);
#endif

	try 
	{
		Attachment* attachment = *db_handle;
		validateHandle(tdbb, attachment);
		validateHandle(tdbb, *tra_handle);
		DatabaseContextHolder dbbHolder(tdbb);
		check_database(tdbb);

		Database* dbb = tdbb->getDatabase();

		jrd_tra* transaction = find_transaction(tdbb, isc_req_wrong_db);

		jrd_nod* in_message = NULL;
		jrd_nod* out_message = NULL;

		jrd_req* request = NULL;
		MemoryPool* new_pool = dbb->createPool();

		try
		{
			Jrd::ContextPoolHolder context(tdbb, new_pool);

			CompilerScratch* csb =
				PAR_parse(tdbb, reinterpret_cast<const UCHAR*>(blr), FALSE);
			request = CMP_make_request(tdbb, csb);
			CMP_verify_access(tdbb, request);

			jrd_nod* node;
			for (size_t i = 0; i < csb->csb_rpt.getCount(); i++)
			{
				if ( (node = csb->csb_rpt[i].csb_message) )
				{
					if ((int) (IPTR) node->nod_arg[e_msg_number] == 0) 
					{
						in_message = node;
					}
					else if ((int) (IPTR) node->nod_arg[e_msg_number] == 1) 
					{
						out_message = node;
					}
				}
			}
		}
		catch (const Firebird::Exception&)
		{
			if (request)
				CMP_release(tdbb, request);
			else
				dbb->deletePool(new_pool);

			throw;
		}

		request->req_attachment = attachment;

		USHORT len;
		if (in_msg_length)
		{
			if (in_message) {
				const Format* format = (Format*) in_message->nod_arg[e_msg_format];
				len = format->fmt_length;
			}
			else {
				len = 0;
			}

			if (in_msg_length != len)
			{
				ERR_post(isc_port_len,
						 isc_arg_number, (SLONG) in_msg_length,
						 isc_arg_number, (SLONG) len,
						 0);
			}

			memcpy((SCHAR*) request + in_message->nod_impure, in_msg, in_msg_length);
		}

		EXE_start(tdbb, request, transaction);

		if (out_message) {
			const Format* format = (Format*) out_message->nod_arg[e_msg_format];
			len = format->fmt_length;
		}
		else {
			len = 0;
		}

		if (out_msg_length != len) {
			ERR_post(isc_port_len,
					 isc_arg_number, (SLONG) out_msg_length,
					 isc_arg_number, (SLONG) len, 0);
		}

		if (out_msg_length) {
			memcpy(out_msg, (SCHAR*) request + out_message->nod_impure,
				   out_msg_length);
		}

		check_autocommit(request, tdbb);

		CMP_release(tdbb, request);
	}
	catch (const Firebird::Exception& ex)
	{
		Firebird::stuff_exception(user_status, ex);
	}

	return user_status[1];
}


ISC_STATUS GDS_TRANSACTION_INFO(ISC_STATUS* user_status,
								jrd_tra** tra_handle,
								SSHORT item_length,
								const SCHAR* items,
								SSHORT buffer_length,
								SCHAR* buffer)
{
/**************************************
 *
 *	g d s _ $ t r a n s a c t i o n _ i n f o
 *
 **************************************
 *
 * Functional description
 *	Provide information on blob object.
 *
 **************************************/
	ThreadContextHolder tdbb(user_status);

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
	LOG_call(log_transaction_info2, *tra_handle, item_length, items,
			 buffer_length);
#endif

	try
	{
		jrd_tra* transaction = *tra_handle;
		validateHandle(tdbb, transaction);
		DatabaseContextHolder dbbHolder(tdbb);
		check_database(tdbb);

		INF_transaction_info(transaction, items, item_length, buffer,
						 buffer_length);
	}
	catch (const Firebird::Exception& ex)
	{
		Firebird::stuff_exception(user_status, ex);
	}

	return user_status[1];
}


ISC_STATUS GDS_UNWIND(ISC_STATUS * user_status,
						jrd_req** req_handle,
						SSHORT level)
{
/**************************************
 *
 *	g d s _ $ u n w i n d
 *
 **************************************
 *
 * Functional description
 *	Unwind a running request.  This is potentially nasty since it can
 *	be called asynchronously.
 *
 **************************************/
	ThreadContextHolder tdbb(user_status);

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
	LOG_call(log_unwind, *req_handle, level);
#endif

	try
	{
		jrd_req* request = *req_handle;
		validateHandle(tdbb, request);
		DatabaseContextHolder dbbHolder(tdbb);
		check_database(tdbb);

		// Pick up and validate request level
		verify_request_synchronization(request, level);

		// Unwind request. This just tweaks some bits.
		EXE_unwind(tdbb, request);
	}
	catch (const Firebird::Exception& ex)
	{
		Firebird::stuff_exception(user_status, ex);
	}

	return user_status[1];
}


#ifdef SUPERSERVER
bool JRD_getdir(Firebird::PathName& buf)
{
/**************************************
 *
 *	J R D _ g e t d i r
 *
 **************************************
 *
 * Functional description
 *	Current working directory is cached in the attachment
 *	block.  Get it out. This function could be called before
 *	an attachment is created. In such a case thread specific
 *	data (t_data) will hold the user name which will be used
 *	to get the users home directory.
 *
 **************************************/
	char* t_data = NULL;
	char b[MAXPATHLEN];

	ThreadData::getSpecificData((void**) &t_data);

	if (t_data) {
#ifdef WIN_NT
		GetCurrentDirectory(MAXPATHLEN, b);
		buf = b;
#else
		const struct passwd* pwd;
		strcpy(b, t_data);
		pwd = getpwnam(b);
		if (pwd)
			buf = pwd->pw_dir;
		else	// No home dir for this users here. Default to server dir
			return fb_getcwd(buf);
#endif
	}
	else
	{
		thread_db* tdbb = JRD_get_thread_data();

   /** If the server has not done a JRD_set_thread_data prior to this call
       (which will be the case when connecting via IPC), thread_db will
       be NULL so do not attempt to get the attachment handle from
       thread_db. Just return false as described below.  
	   NOTE:  The only time
       this code is entered via IPC is if the database name = "".
   **/

   /** In case of backup/restore APIs, JRD_set_thread_data has been done but
       the thread's context is a 'gbak' specific, so don't try extract
       attachment from there.
   **/

		Attachment* attachment;
		if (tdbb && (tdbb->getType() == ThreadData::tddDBB))
			attachment = tdbb->getAttachment();
		else
			return false;

   /**
    An older version of client will not be sending isc_dpb_working directory
    so in all probabilities attachment->att_working_directory will be null.
    return false so that ISC_expand_filename will create the file in fbserver's dir
   **/
		if (!attachment || attachment->att_working_directory.empty())
		{
			return false;
		}
		buf = attachment->att_working_directory;
	}

	return true;
}
#endif // SUPERSERVER


#ifdef DEBUG_PROCS
void JRD_print_procedure_info(thread_db* tdbb, const char* mesg)
{
/*****************************************************
 *
 *	J R D _ p r i n t _ p r o c e d u r e _ i n f o
 *
 *****************************************************
 *
 * Functional description
 *	print name , use_count of all procedures in
 *      cache
 *
 ******************************************************/
	TEXT fname[MAXPATHLEN];

	gds__prefix(fname, "proc_info.log");
	FILE* fptr = fopen(fname, "a+");
	if (!fptr) {
		char buff[MAXPATHLEN + 25];
		sprintf(buff, "Failed to open %s\n", fname);
		gds__log(buff, 0);
		return;
	}

	if (mesg)
		fputs(mesg, fptr);
	fprintf(fptr,
			   "Prc Name      , prc id , flags  ,  Use Count , Alter Count\n");

	vec<jrd_prc*>* procedures = tdbb->getDatabase()->dbb_procedures;
	if (procedures) {
		vec<jrd_prc*>::iterator ptr, end;
		for (ptr = procedures->begin(), end = procedures->end();
					ptr < end; ++ptr)
		{
			const jrd_prc* procedure = *ptr;
			if (procedure)
				fprintf(fptr, "%s  ,  %d,  %X,  %d, %d\n",
							(procedure->prc_name->hasData()) ?
								procedure->prc_name->c_str() : "NULL",
							procedure->prc_id,
							procedure->prc_flags, procedure->prc_use_count,
							0); // procedure->prc_alter_count
		}
	}
	else
		fprintf(fptr, "No Cached Procedures\n");

	fclose(fptr);

}
#endif // DEBUG_PROCS


bool JRD_reschedule(thread_db* tdbb, SLONG quantum, bool punt)
{
/**************************************
 *
 *	J R D _ r e s c h e d u l e
 *
 **************************************
 *
 * Functional description
 *	Somebody has kindly offered to relinquish
 *	control so that somebody else may run.
 *
 **************************************/
	Database* dbb = tdbb->getDatabase();

	if (dbb->dbb_sync.hasContention())
	{
		Database::Checkout dcoHolder(dbb);
		THREAD_YIELD();
	}

	// Test various flags and unwind/throw if required.
	// But do that only if we're not in the verb cleanup state,
	// which should never be interrupted.

	if (!(tdbb->tdbb_flags & TDBB_verb_cleanup))
	{
		// If database has been shutdown then get out

		Attachment* attachment = tdbb->getAttachment();
		jrd_tra* transaction = tdbb->getTransaction();
		jrd_req* request = tdbb->getRequest();

		if (attachment)
		{
			if (dbb->dbb_ast_flags & DBB_shutdown &&
				attachment->att_flags & ATT_shutdown)
			{
				const Firebird::PathName& file_name = attachment->att_filename;
				if (punt) {
					CCH_unwind(tdbb, false);
					ERR_post(isc_shutdown, isc_arg_string,
							 ERR_cstring(file_name), 0);
				}
				else {
					ISC_STATUS* status = tdbb->tdbb_status_vector;
					*status++ = isc_arg_gds;
					*status++ = isc_shutdown;
					*status++ = isc_arg_string;
					*status++ = (ISC_STATUS) ERR_cstring(file_name);
					*status++ = isc_arg_end;
					return true;
				}
			}
			else if (attachment->att_flags & ATT_shutdown &&
				!(tdbb->tdbb_flags & TDBB_shutdown_manager))
			{
				if (punt) {
					CCH_unwind(tdbb, false);
					ERR_post(isc_att_shutdown, 0);
				}
				else {
					ISC_STATUS* status = tdbb->tdbb_status_vector;
					*status++ = isc_arg_gds;
					*status++ = isc_att_shutdown;
					*status++ = isc_arg_end;
					return true;
				}
			}

			// If a cancel has been raised, defer its acknowledgement
			// when executing in the context of an internal request or
			// the system transaction.

			if ((attachment->att_flags & ATT_cancel_raise) &&
				!(attachment->att_flags & ATT_cancel_disable))
			{
				if ((!request ||
					 !(request->req_flags & (req_internal | req_sys_trigger))) &&
					(!transaction || !(transaction->tra_flags & TRA_system)))
				{
					attachment->att_flags &= ~ATT_cancel_raise;
					if (punt) {
						CCH_unwind(tdbb, false);
						ERR_post(isc_cancelled, 0);
					}
					else {
						ISC_STATUS* status = tdbb->tdbb_status_vector;
						*status++ = isc_arg_gds;
						*status++ = isc_cancelled;
						*status++ = isc_arg_end;
						return true;
					}
				}
			}
		}

		// Handle request cancellation

		if (transaction && (transaction->tra_flags & TRA_cancel_request))
		{
			transaction->tra_flags &= ~TRA_cancel_request;
			tdbb->tdbb_flags |= TDBB_sys_error;

			if (punt) {
				CCH_unwind(tdbb, false);
				ERR_post(isc_cancelled, 0);
			}
			else {
				ISC_STATUS* status = tdbb->tdbb_status_vector;
				*status++ = isc_arg_gds;
				*status++ = isc_cancelled;
				*status++ = isc_arg_end;
				return true;
			}
		}
	}

	// Enable signal handler for the monitoring stuff

	if (dbb->dbb_ast_flags & DBB_monitor_off) {
		dbb->dbb_ast_flags &= ~DBB_monitor_off;
		LCK_lock(tdbb, dbb->dbb_monitor_lock, LCK_SR, LCK_WAIT);
	}

	tdbb->tdbb_quantum = (tdbb->tdbb_quantum <= 0) ?
		(quantum ? quantum : QUANTUM) : tdbb->tdbb_quantum;

	return false;
}


void jrd_vtof(const char* string, char* field, SSHORT length)
{
/**************************************
 *
 *	j r d _ v t o f
 *
 **************************************
 *
 * Functional description
 *	Move a null terminated string to a fixed length
 *	field.
 *	If the length of the string pointed to by 'field'
 *	is less than 'length', this function pads the
 *	destination string with space upto 'length' bytes.
 *
 *	The call is primarily generated  by the preprocessor.
 *
 *	This is the same code as gds__vtof but is used internally.
 *
 **************************************/

	while (*string) {
		*field++ = *string++;
		if (--length <= 0) {
			return;
		}
	}

	if (length) {
		memset(field, ' ', length);
	}
}

#ifndef SUPERSERVER
static int blocking_ast_dsql_cache(void* ast_object)
{
/**************************************
 *
 *	b l o c k i n g _ a s t _ d s q l _ c a c h e
 *
 **************************************
 *
 * Functional description
 *	Someone is trying to drop a item from the DSQL cache.
 *	Mark the symbol as obsolete and release the lock.
 *
 **************************************/
	DSqlCacheItem* item = static_cast<DSqlCacheItem*>(ast_object);

	Database* dbb = item->lock->lck_dbb;
	Database::SyncGuard dsGuard(dbb, true);

	ThreadContextHolder tdbb;

	tdbb->setDatabase(dbb);
	tdbb->setAttachment(item->lock->lck_attachment);
	Jrd::ContextPoolHolder context(tdbb, 0);

	item->obsolete = true;
	item->locked = false;
	LCK_release(tdbb, item->lock);

	return 0;
}
#endif	// SUPERSERVER


static void check_database(thread_db* tdbb)
{
/**************************************
 *
 *	c h e c k _ d a t a b a s e
 *
 **************************************
 *
 * Functional description
 *	Check an attachment for validity.
 *
 **************************************/
	SET_TDBB(tdbb);

	Database* dbb = tdbb->getDatabase();
	Attachment* attachment = tdbb->getAttachment();

	if (dbb->dbb_flags & DBB_bugcheck)
	{
		static const char string[] = "can't continue after bugcheck";
		Firebird::status_exception::raise(isc_bug_check,
										  isc_arg_string, string,
										  0);
	}

	if (attachment->att_flags & ATT_shutdown ||
		((dbb->dbb_ast_flags & DBB_shutdown) &&
		 ((dbb->dbb_ast_flags & DBB_shutdown_full) ||
		 !attachment->locksmith())))
	{
		if (dbb->dbb_ast_flags & DBB_shutdown)
		{
			const Firebird::PathName& filename = attachment->att_filename;
			Firebird::status_exception::raise(isc_shutdown,
											  isc_arg_string, ERR_cstring(filename),
											  0);
		}
		else
		{
			Firebird::status_exception::raise(isc_att_shutdown, 0);
		}
	}

	if ((attachment->att_flags & ATT_cancel_raise) &&
		!(attachment->att_flags & ATT_cancel_disable))
	{
		attachment->att_flags &= ~ATT_cancel_raise;
		Firebird::status_exception::raise(isc_cancelled, 0);
	}

	// Enable signal handler for the monitoring stuff

	if (dbb->dbb_ast_flags & DBB_monitor_off)
	{
		dbb->dbb_ast_flags &= ~DBB_monitor_off;
		LCK_lock(tdbb, dbb->dbb_monitor_lock, LCK_SR, LCK_WAIT);
	}
}


static void check_transaction(thread_db* tdbb, jrd_tra* transaction)
{
/**************************************
 *
 *	c h e c k _ t r a n s a c t i o n
 *
 **************************************
 *
 * Functional description
 *	Check transaction for not being interrupted
 *  in the meantime.
 *
 **************************************/
	SET_TDBB(tdbb);

	if (transaction && (transaction->tra_flags & TRA_cancel_request))
	{
		transaction->tra_flags &= ~TRA_cancel_request;
		tdbb->tdbb_flags |= TDBB_sys_error;

		Firebird::status_exception::raise(isc_cancelled, 0);
	}
}


static ISC_STATUS commit(ISC_STATUS* user_status,
						 jrd_tra** tra_handle,
						 const bool retaining_flag)
{
/**************************************
 *
 *	c o m m i t
 *
 **************************************
 *
 * Functional description
 *	Commit a transaction.
 *
 **************************************/
	ThreadContextHolder tdbb(user_status);

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
	LOG_call((retaining_flag) ? log_commit_retaining : log_commit,
			 *tra_handle);
#endif

	try
	{
		jrd_tra* transaction = *tra_handle;
		validateHandle(tdbb, transaction);
		DatabaseContextHolder dbbHolder(tdbb);

		if (transaction->tra_sibling && !(transaction->tra_flags & TRA_prepared))
		{
			prepare(tdbb, transaction, 0, NULL);
		}

		const Attachment* const attachment = tdbb->getAttachment();

		if (!(attachment->att_flags & ATT_no_db_triggers) &&
			!(transaction->tra_flags & TRA_prepared))
		{
			// run ON TRANSACTION COMMIT triggers
			run_commit_triggers(tdbb, transaction);
		}

		jrd_tra* next = transaction;

		while ( (transaction = next) )
		{
			next = transaction->tra_sibling;
			validateHandle(tdbb, transaction->tra_attachment);
			tdbb->setTransaction(transaction);
			check_database(tdbb);
			TRA_commit(tdbb, transaction, retaining_flag);
		}
	}
	catch (const Firebird::Exception& ex) {
		Firebird::stuff_exception(user_status, ex);
	}

	return user_status[1];
}


ISC_STATUS compile_request(ISC_STATUS* user_status,
						   Attachment** db_handle,
						   jrd_req** req_handle,
						   SSHORT blr_length,
						   const SCHAR* blr,
						   USHORT string_length, const char* string,
						   USHORT dbginfo_length, const UCHAR* dbginfo)
{
/**************************************
 *
 *	compile_request
 *
 **************************************
 *
 * Functional description
 *
 **************************************/
	ThreadContextHolder tdbb(user_status);

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
	LOG_call(log_compile, *db_handle, *req_handle, blr_length, blr);
#endif

	try
	{
		if (*req_handle)
			Firebird::status_exception::raise(isc_bad_req_handle, 0);

		Attachment* attachment = *db_handle;
		validateHandle(tdbb, attachment);
		DatabaseContextHolder dbbHolder(tdbb);
		check_database(tdbb);

		jrd_req* request =
			CMP_compile2(tdbb, reinterpret_cast<const UCHAR*>(blr), FALSE,
						 dbginfo_length, dbginfo);

		request->req_attachment = attachment;
		request->req_request = attachment->att_requests;
		attachment->att_requests = request;

		request->req_sql_text.assign(string, string_length);

		*req_handle = request;

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
		LOG_call(log_handle_returned, *req_handle);
#endif
	}
	catch (const Firebird::Exception& ex)
	{
		Firebird::stuff_exception(user_status, ex);
	}

	return user_status[1];
}


static bool drop_files(const jrd_file* file)
{
/**************************************
 *
 *	d r o p _ f i l e s
 *
 **************************************
 *
 * Functional description
 *	drop a linked list of files
 *
 **************************************/
	ISC_STATUS_ARRAY status;

	status[1] = FB_SUCCESS;

	for (; file; file = file->fil_next)
	{
		if (unlink(file->fil_string))
		{
			IBERR_build_status(status, isc_io_error,
							   isc_arg_string, "unlink",
							   isc_arg_string,
							   ERR_cstring(file->fil_string),
							   isc_arg_gds, isc_io_delete_err,
							   SYS_ERR, errno,
							   0);
			Database* dbb = GET_DBB();
			PageSpace* pageSpace = dbb->dbb_page_manager.findPageSpace(DB_PAGE_SPACE);
			gds__log_status(pageSpace->file->fil_string, status);
		}
	}

	return status[1] ? true : false;
}


static jrd_tra* find_transaction(thread_db* tdbb, ISC_STATUS error_code)
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
	SET_TDBB(tdbb);

	const Attachment* const attachment = tdbb->getAttachment();

	for (jrd_tra* transaction = tdbb->getTransaction(); transaction;
		transaction = transaction->tra_sibling)
	{
		if (transaction->tra_attachment == attachment)
		{
			return transaction;
		}
	}

	Firebird::status_exception::raise(error_code, 0);
	return NULL;	// Added to remove compiler warnings
}


static void find_intl_charset(thread_db* tdbb, Attachment* attachment, const DatabaseOptions* options)
{
/**************************************
 *
 *	f i n d _ i n t l _ c h a r s e t
 *
 **************************************
 *
 * Functional description
 *	Attachment has declared it's prefered character set
 *	as part of LC_CTYPE, passed over with the attachment
 *	block.  Now let's resolve that to an internal subtype id.
 *
 **************************************/
	SET_TDBB(tdbb);

	if (options->dpb_lc_ctype.isEmpty()) {
		// No declaration of character set, act like 3.x Interbase
		attachment->att_charset = DEFAULT_ATTACHMENT_CHARSET;
		return;
	}

	USHORT id;

	const UCHAR* lc_ctype =
		reinterpret_cast<const UCHAR*>(options->dpb_lc_ctype.c_str());
		
	if (MET_get_char_coll_subtype(tdbb, &id, lc_ctype, options->dpb_lc_ctype.length()) &&
		INTL_defined_type(tdbb, id & 0xFF) &&
		((id & 0xFF) != CS_BINARY))
	{
		attachment->att_charset = id & 0xFF;
	}
	else
	{
		// Report an error - we can't do what user has requested
		ERR_post(isc_bad_dpb_content,
				isc_arg_gds, isc_charset_not_found,
				isc_arg_string, ERR_cstring(options->dpb_lc_ctype),
				0);
	}
}


void DatabaseOptions::get(const UCHAR* dpb, USHORT dpb_length, bool& invalid_client_SQL_dialect)
{
/**************************************
 *
 *	D a t a b a s e O p t i o n s : : g e t
 *
 **************************************
 *
 * Functional description
 *	Parse database parameter block picking up options and things.
 *
 **************************************/
	SSHORT num_old_files = 0;

	ULONG page_cache_size = Config::getDefaultDbCachePages();
	if (page_cache_size < MIN_PAGE_BUFFERS)
		page_cache_size = MIN_PAGE_BUFFERS;
	if (page_cache_size > MAX_PAGE_BUFFERS)
		page_cache_size = MAX_PAGE_BUFFERS;

	dpb_buffers = page_cache_size;
	dpb_sweep_interval = -1;
	dpb_overwrite = false;
	dpb_sql_dialect = 99;
	invalid_client_SQL_dialect = false;

	if (dpb_length == 0)
	{
		return;
	}
	if (dpb == NULL)
	{
		ERR_post(isc_bad_dpb_form, 0);
	}

	Firebird::ClumpletReader rdr(Firebird::ClumpletReader::Tagged, dpb, dpb_length);

	if (rdr.getBufferTag() != isc_dpb_version1)
	{
		ERR_post(isc_bad_dpb_form, isc_arg_gds, isc_wrodpbver, 0);
	}

	for (; !(rdr.isEof()); rdr.moveNext())
	{
		switch (rdr.getClumpTag())
		{
		case isc_dpb_working_directory:
			{
				rdr.getPath(dpb_working_directory);

				// CLASSIC have no thread data. Init to zero.
				char* t_data = 0;
				ThreadData::getSpecificData((void **) &t_data);

				// Null value for working_directory implies remote database. So get
				// the users HOME directory
#ifndef WIN_NT
				if (dpb_working_directory.isEmpty()) {
					struct passwd *passwd = NULL;

					if (t_data)
						passwd = getpwnam(t_data);
					if (passwd) 
					{
						dpb_working_directory = passwd->pw_dir;
					}
					else {		// No home dir for this users here. Default to server dir
						fb_getcwd(dpb_working_directory);
					}
				}
#endif
				if (t_data)
				{
					free(t_data);
					t_data = NULL;
				}
				// Null out the thread local data so that further references will fail
				ThreadData::putSpecificData(0);
			}
			break;

		case isc_dpb_set_page_buffers:
			dpb_page_buffers = rdr.getInt();
			if (dpb_page_buffers &&
				(dpb_page_buffers < MIN_PAGE_BUFFERS ||
				 dpb_page_buffers > MAX_PAGE_BUFFERS))
			{
				ERR_post(isc_bad_dpb_content, 0);
			}
			dpb_set_page_buffers = true;
			break;

		case isc_dpb_num_buffers:
			dpb_buffers = rdr.getInt();
			if (dpb_buffers < 10)
			{
				ERR_post(isc_bad_dpb_content, 0);
			}
			break;

		case isc_dpb_page_size:
			dpb_page_size = (USHORT) rdr.getInt();
			break;

		case isc_dpb_debug:
			dpb_debug = (USHORT) rdr.getInt();
			break;

		case isc_dpb_sweep:
			dpb_sweep = (USHORT) rdr.getInt();
			break;

		case isc_dpb_sweep_interval:
			dpb_sweep_interval = rdr.getInt();
			break;

		case isc_dpb_verify:
			dpb_verify = (USHORT) rdr.getInt();
			if (dpb_verify & isc_dpb_ignore)
				dpb_flags |= DBB_damaged;
			break;

		case isc_dpb_trace:
			dpb_trace = (USHORT) rdr.getInt();
			break;

		case isc_dpb_damaged:
			if (rdr.getInt() & 1)
				dpb_flags |= DBB_damaged;
			break;

		case isc_dpb_enable_journal:
			rdr.getString(dpb_journal);
			break;

		case isc_dpb_wal_backup_dir:
			// ignore, skip
			break;

		case isc_dpb_drop_walfile:
			dpb_wal_action = (USHORT) rdr.getInt();
			break;

		case isc_dpb_old_dump_id:
		case isc_dpb_online_dump:
		case isc_dpb_old_file_size:
		case isc_dpb_old_num_files:
		case isc_dpb_old_start_page:
		case isc_dpb_old_start_seqno:
		case isc_dpb_old_start_file:
			// ignore, skip
			break;

		case isc_dpb_old_file:
			//if (num_old_files >= MAX_OLD_FILES) complain here, for now.
				ERR_post(isc_num_old_files, 0);
			// following code is never executed now !
			num_old_files++;
			break;

		case isc_dpb_wal_chkptlen:
		case isc_dpb_wal_numbufs:
		case isc_dpb_wal_bufsize:
		case isc_dpb_wal_grp_cmt_wait:
			// ignore, skip
			break;

		case isc_dpb_dbkey_scope:
			dpb_dbkey_scope = (USHORT) rdr.getInt();
			break;

		case isc_dpb_sys_user_name:
			rdr.getString(dpb_sys_user_name);
			break;

		case isc_dpb_sql_role_name:
			if (! dpb_trusted_role)
			{
			    rdr.getString(dpb_role_name);
			}
			break;

		case isc_dpb_user_name:
			rdr.getString(dpb_user_name);
			break;

		case isc_dpb_password:
			rdr.getString(dpb_password);
			break;

		case isc_dpb_password_enc:
			rdr.getString(dpb_password_enc);
			break;

		case isc_dpb_trusted_auth:
			rdr.getString(dpb_trusted_login);
			break;

		case isc_dpb_trusted_role:
			dpb_trusted_role = true;
			rdr.getString(dpb_role_name);
			break;

		case isc_dpb_encrypt_key:
#ifdef ISC_DATABASE_ENCRYPTION
			rdr.getString(dpb_key);
#else
			// Just in case there WAS a customer using this unsupported
			// feature - post an error when they try to access it in 4.0
			ERR_post(isc_uns_ext, isc_arg_gds, isc_random,
					 isc_arg_string, "Encryption not supported", 0);
#endif
			break;

		case isc_dpb_no_garbage_collect:
			dpb_no_garbage = TRUE;
			break;

		case isc_dpb_disable_journal:
			dpb_disable = TRUE;
			break;

		case isc_dpb_activate_shadow:
			dpb_activate_shadow = true;
			break;

		case isc_dpb_delete_shadow:
			dpb_delete_shadow = true;
			break;

		case isc_dpb_force_write:
			dpb_set_force_write = TRUE;
			dpb_force_write = (SSHORT) rdr.getInt();
			break;

		case isc_dpb_begin_log:
#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
			rdr.getPath(dpb_log);
#endif
			break;

		case isc_dpb_quit_log:
#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
			dpb_quit_log = true;
#endif
			break;

		case isc_dpb_no_reserve:
			dpb_set_no_reserve = TRUE;
			dpb_no_reserve = (UCHAR) rdr.getInt();
			break;

		case isc_dpb_interp:
			dpb_interp = (SSHORT) rdr.getInt();
			break;

		case isc_dpb_lc_messages:
			rdr.getPath(dpb_lc_messages);
			break;

		case isc_dpb_lc_ctype:
			rdr.getString(dpb_lc_ctype);
			break;

		case isc_dpb_shutdown:
			dpb_shutdown = (USHORT) rdr.getInt();
			// Enforce default
			if ((dpb_shutdown & isc_dpb_shut_mode_mask) == isc_dpb_shut_default)
				dpb_shutdown |= isc_dpb_shut_multi;
			break;

		case isc_dpb_shutdown_delay:
			dpb_shutdown_delay = (SSHORT) rdr.getInt();
			break;

		case isc_dpb_online:
			dpb_online = (USHORT) rdr.getInt();
			// Enforce default
			if ((dpb_online & isc_dpb_shut_mode_mask) == isc_dpb_shut_default)
			{
				dpb_online |= isc_dpb_shut_normal;
			}
			break;

		case isc_dpb_reserved:
			{
				Firebird::string single;
				rdr.getString(single);
				if (single == "YES")
				{
					dpb_single_user = TRUE;
				}
			}
			break;

		case isc_dpb_overwrite:
			dpb_overwrite = rdr.getInt() != 0;
			break;

		case isc_dpb_sec_attach:
			dpb_sec_attach = rdr.getInt() != 0;
			dpb_buffers = 50;
			dpb_flags |= DBB_security_db;
			break;

		case isc_dpb_gbak_attach:
			{
				Firebird::string gbakStr;
				rdr.getString(gbakStr);
				dpb_gbak_attach = gbakStr.hasData();
			}
			break;

		case isc_dpb_gstat_attach:
			dpb_gstat_attach = true;
			break;

		case isc_dpb_gfix_attach:
			dpb_gfix_attach = true;
			break;

		case isc_dpb_gsec_attach:
			dpb_gsec_attach = rdr.getBoolean();
			break;

		case isc_dpb_disable_wal:
			dpb_disable_wal = true;
			break;

		case isc_dpb_connect_timeout:
			dpb_connect_timeout = rdr.getInt();
			break;

		case isc_dpb_dummy_packet_interval:
			dpb_dummy_packet_interval = rdr.getInt();
			break;

		case isc_dpb_sql_dialect:
			dpb_sql_dialect = (USHORT) rdr.getInt();
			if (dpb_sql_dialect > SQL_DIALECT_V6)
				invalid_client_SQL_dialect = true;
			break;

		case isc_dpb_set_db_sql_dialect:
			dpb_set_db_sql_dialect = (USHORT) rdr.getInt();
			break;

		case isc_dpb_set_db_readonly:
			dpb_set_db_readonly = true;
			dpb_db_readonly = rdr.getInt() != 0;
			break;

		case isc_dpb_set_db_charset:
			rdr.getString(dpb_set_db_charset);
			break;

		case isc_dpb_address_path:
			{
				Firebird::ClumpletReader address_stack(Firebird::ClumpletReader::UnTagged,
					rdr.getBytes(), rdr.getClumpLength());
				while (!address_stack.isEof()) {
					if (address_stack.getClumpTag() != isc_dpb_address) {
						address_stack.moveNext();
						continue;
					}
					Firebird::ClumpletReader address(Firebird::ClumpletReader::UnTagged,
						address_stack.getBytes(), address_stack.getClumpLength());
					while (!address.isEof()) {
						switch (address.getClumpTag()) {
							case isc_dpb_addr_protocol:
								address.getString(dpb_network_protocol);
								break;
							case isc_dpb_addr_endpoint:
								address.getString(dpb_remote_address);
								break;
							default:
								break;
						}
						address.moveNext();
					}
					break;
				}
			}
			break;

		case isc_dpb_process_id:
			dpb_remote_pid = rdr.getInt();
			break;

		case isc_dpb_process_name:
			rdr.getPath(dpb_remote_process);
			break;

		case isc_dpb_no_db_triggers:
			dpb_no_db_triggers = rdr.getInt() != 0;
			break;

		default:
			break;
		}
	}

	if (! rdr.isEof())
	{
		ERR_post(isc_bad_dpb_form, 0);
	}
}


static ISC_STATUS handle_error(ISC_STATUS* user_status, ISC_STATUS code)
{
/**************************************
 *
 *	h a n d l e _ e r r o r
 *
 **************************************
 *
 * Functional description
 *	An invalid handle has been passed in.  If there is a user status
 *	vector, make it reflect the error.  If not, emulate the routine
 *	"error" and abort.
 *
 **************************************/
	ISC_STATUS* vector = user_status;
	*vector++ = isc_arg_gds;
	*vector++ = code;
	*vector = isc_arg_end;

	return code;
}


#if defined (WIN_NT) && !defined(SERVER_SHUTDOWN)
static BOOLEAN handler_NT(SSHORT controlAction)
{
/**************************************
 *
 *      h a n d l e r _ N T
 *
 **************************************
 *
 * Functional description
 *	For some actions, get NT to issue a popup asking
 *	the user to delay.
 *
 **************************************/

	switch (controlAction) {
	case CTRL_CLOSE_EVENT:
	case CTRL_LOGOFF_EVENT:
	case CTRL_SHUTDOWN_EVENT:
		return TRUE;			// NT will issue popup

	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
		return FALSE;			// let it go
	}
	// So, what are we to return here?!
	return FALSE;				// let it go
}
#endif


static Database* init(thread_db*	tdbb,
				ISC_STATUS*	user_status,
				const Firebird::PathName& expanded_filename,
				bool attach_flag)
{
/**************************************
 *
 *	i n i t
 *
 **************************************
 *
 * Functional description
 *	Initialize for database access.  First call from both CREATE and
 *	OPEN.
 *
 **************************************/
	Firebird::Mutex temp_mutx[DBB_MUTX_max];

	SET_TDBB(tdbb);

	// Initialize standard random generator.
	// MSVC (at least since version 7) have per-thread random seed.
	// As we don't know who uses per-thread seed, this should work for both cases.
	static bool first_rand = true;
	static int first_rand_value = rand();

	if (first_rand || (rand() == first_rand_value))
		srand(time(NULL));

	first_rand = false;

	engineStartup.init();

	try {

	dbMutexLock();

	// Check to see if the database is already actively attached

	Database* dbb;
#ifdef SUPERSERVER
	for (dbb = databases; dbb; dbb = dbb->dbb_next)
	{
		if (!(dbb->dbb_flags & (DBB_bugcheck | DBB_not_in_use)) &&
			 (dbb->dbb_filename == expanded_filename))
		{
			return (attach_flag) ? dbb : NULL;
		}
	}
#endif

#ifdef SUPERSERVER
	Firebird::MemoryStats temp_stats;
	MemoryPool* perm = MemoryPool::createPool(NULL, temp_stats);
	dbb = Database::newDbb(perm);
	perm->setStatsGroup(dbb->dbb_memory_stats);
#else
	MemoryPool* perm = MemoryPool::createPool(NULL);
	dbb = Database::newDbb(perm);
#endif
	dbb->dbb_mutexes = temp_mutx;

	tdbb->setDatabase(dbb);

	dbb->dbb_bufferpool = dbb->createPool();

	// provide context pool for the rest stuff
	Jrd::ContextPoolHolder context(tdbb, perm);

#ifdef SUPERSERVER
	dbb->dbb_next = databases;
	databases = dbb;
#endif

	dbb->dbb_mutexes = FB_NEW(*dbb->dbb_permanent) Firebird::Mutex[DBB_MUTX_max];
	dbb->dbb_internal = vec<jrd_req*>::newVector(*dbb->dbb_permanent, irq_MAX);
	dbb->dbb_dyn_req = vec<jrd_req*>::newVector(*dbb->dbb_permanent, drq_MAX);
	dbb->dbb_flags |= DBB_exclusive;
	dbb->dbb_sweep_interval = SWEEP_INTERVAL;

	GenerateGuid(&dbb->dbb_guid);

	// set a garbage collection policy

	if ((dbb->dbb_flags & (DBB_gc_cooperative | DBB_gc_background)) == 0)
	{
		Firebird::string gc_policy = Config::getGCPolicy();
		gc_policy.lower();
		if (gc_policy == GCPolicyCooperative) {
			dbb->dbb_flags |= DBB_gc_cooperative;
		}
		else if (gc_policy == GCPolicyBackground) {
			dbb->dbb_flags |= DBB_gc_background;
		}
		else if (gc_policy == GCPolicyCombined) {
			dbb->dbb_flags |= DBB_gc_cooperative | DBB_gc_background;
		}
		else // config value is invalid, use default
		{
			if (GCPolicyDefault == GCPolicyCooperative) {
				dbb->dbb_flags |= DBB_gc_cooperative;
			}
			else if (GCPolicyDefault == GCPolicyBackground) {
				dbb->dbb_flags |= DBB_gc_background;
			}
			else if (GCPolicyDefault == GCPolicyCombined) {
				dbb->dbb_flags |= DBB_gc_cooperative | DBB_gc_background;
			}
			else 
				fb_assert(false);
		}
	}

	// Initialize a number of subsystems

	TRA_init(tdbb);

	// Lookup some external "hooks"

	PluginManager::Plugin crypt_lib =
		PluginManager::enginePluginManager().findPlugin(CRYPT_IMAGE);
	if (crypt_lib) {
		Firebird::string encrypt_entrypoint(ENCRYPT);
		Firebird::string decrypt_entrypoint(DECRYPT);
		dbb->dbb_encrypt =
			(Database::crypt_routine) crypt_lib.lookupSymbol(encrypt_entrypoint);
		dbb->dbb_decrypt =
			(Database::crypt_routine) crypt_lib.lookupSymbol(decrypt_entrypoint);
	}

	INTL_init(tdbb);

	return dbb;

	}	// try
	catch (const Firebird::Exception& ex) {
		Firebird::stuff_exception(user_status, ex);
		return 0;
	}
}


static void init_database_locks(thread_db* tdbb, Database* dbb)
{
/**************************************
 *
 *	i n i t _ d a t a b a s e _ l o c k s
 *
 **************************************
 *
 * Functional description
 *	Initialize secondary database locks.
 *
 **************************************/
	SET_TDBB(tdbb);

	fb_assert(dbb);

	// Lock shared by all dbb owners, used to signal other processes
	// to dump their monitoring data and synchronize operations
	Lock* lock = FB_NEW_RPT(*dbb->dbb_permanent, sizeof(SLONG)) Lock();
	dbb->dbb_monitor_lock = lock;
	lock->lck_type = LCK_monitor;
	lock->lck_owner_handle = LCK_get_owner_handle(tdbb, lock->lck_type);
	lock->lck_parent = dbb->dbb_lock;
	lock->lck_length = sizeof(SLONG);
	lock->lck_dbb = dbb;
	lock->lck_object = reinterpret_cast<blk*>(dbb);
	lock->lck_ast = DatabaseSnapshot::blockingAst;
	LCK_lock(tdbb, lock, LCK_SR, LCK_WAIT);

	// Lock that identifies a dbb instance
	const size_t key_length = sizeof(FB_GUID);
	lock = FB_NEW_RPT(*dbb->dbb_permanent, key_length) Lock();
	dbb->dbb_instance_lock = lock;
	lock->lck_type = LCK_instance;
	lock->lck_owner_handle = LCK_get_owner_handle(tdbb, lock->lck_type);
	lock->lck_length = key_length;
	memcpy(lock->lck_key.lck_string, &dbb->dbb_guid, key_length);
	lock->lck_dbb = dbb;
	LCK_lock(tdbb, lock, LCK_SR, LCK_WAIT);
}


static void prepare(thread_db* tdbb,
					jrd_tra* transaction,
					USHORT length,
					const UCHAR* msg)
{
/**************************************
 *
 *	p r e p a r e
 *
 **************************************
 *
 * Functional description
 *	Attempt to prepare a transaction.
 *
 **************************************/
	SET_TDBB(tdbb);

	if (!(transaction->tra_flags & TRA_prepared))
	{
		// run ON TRANSACTION COMMIT triggers
		run_commit_triggers(tdbb, transaction);
	}

	for (; transaction; transaction = transaction->tra_sibling) {
		DatabaseContextHolder dbbHolder(tdbb);
		check_database(tdbb);
		TRA_prepare(tdbb, transaction, length, msg);
	}
}


static void release_attachment(thread_db* tdbb, Attachment* attachment)
{
/**************************************
 *
 *	r e l e a s e _ a t t a c h m e n t
 *
 **************************************
 *
 * Functional description
 *	Disconnect attachment block from database block.
 *
 **************************************/
	SET_TDBB(tdbb);
	Database* dbb = tdbb->getDatabase();
	CHECK_DBB(dbb);

	if (!attachment)
		return;

#ifdef SUPERSERVER
	if (dbb->dbb_relations)
	{
		vec<jrd_rel*>& rels = *dbb->dbb_relations;
		for (size_t i = 1; i < rels.count(); i++)
		{
			jrd_rel* relation = rels[i];
			if (relation && (relation->rel_flags & REL_temp_conn) &&
				!(relation->rel_flags & (REL_deleted | REL_deleting)) )
			{
				relation->delPages(tdbb);
			}
		}
	}
#endif

	if (attachment->att_event_session)
		EVENT_delete_session(attachment->att_event_session);

	if (attachment->att_id_lock)
		LCK_release(tdbb, attachment->att_id_lock);

#ifndef SUPERSERVER
	if (attachment->att_temp_pg_lock)
		LCK_release(tdbb, attachment->att_temp_pg_lock);

	for (bool getResult = attachment->att_dsql_cache.getFirst(); getResult; 
			  getResult = attachment->att_dsql_cache.getNext())
	{
		LCK_release(tdbb, attachment->att_dsql_cache.current()->second.lock);
	}
#endif

	for (vcl** vector = attachment->att_counts;
		 vector < attachment->att_counts + DBB_max_count; ++vector)
	{
		if (*vector)
		{
			delete *vector;
			*vector = 0;
		}
	}

	// Release any validation error vector allocated

	if (attachment->att_val_errors) {
		delete attachment->att_val_errors;
		attachment->att_val_errors = NULL;
	}

	detachLocksFromAttachment(attachment);

	if (attachment->att_flags & ATT_lck_init_done) {
		LCK_fini(tdbb, LCK_OWNER_attachment);	// For the attachment
		attachment->att_flags &= ~ATT_lck_init_done;
	}

	if (attachment->att_compatibility_table)
		delete attachment->att_compatibility_table;

	// remove the attachment block from the dbb linked list

	for (Attachment** ptr = &dbb->dbb_attachments; *ptr; ptr = &(*ptr)->att_next) {
		if (*ptr == attachment) {
			*ptr = attachment->att_next;
			break;
		}
	}
}


static void detachLocksFromAttachment(Attachment* attachment)
{
/**************************************
 *
 *	d e t a c h L o c k s F r o m A t t a c h m e n t
 *
 **************************************
 *
 * Functional description
 * Bug #7781, need to null out the attachment pointer of all locks which
 * were hung off this attachment block, to ensure that the attachment
 * block doesn't get dereferenced after it is released
 *
 **************************************/
	Lock* long_lock = attachment->att_long_locks;
	while (long_lock) {
		Lock* next = long_lock->lck_next;
		long_lock->lck_attachment = NULL;
		long_lock->lck_next = NULL;
		long_lock->lck_prior = NULL;
		long_lock = next;
	}
	attachment->att_long_locks = NULL;
}


Attachment::~Attachment()
{
// For normal attachments that happens release_attachment(),
// but for special ones like GC should be done also in dtor - 
// they do not (and should not) call release_attachment().
// It's no danger calling detachLocksFromAttachment() 
// once more here because it nulls att_long_locks.
//		AP 2007
	detachLocksFromAttachment(this);
}


Attachment::Attachment(Database* dbb) :
		att_database(dbb), 
		att_lock_owner_id(Database::getLockOwnerId()),
		att_lc_messages(*dbb->dbb_permanent),
		att_working_directory(*dbb->dbb_permanent), 
		att_filename(*dbb->dbb_permanent),
		att_context_vars(*dbb->dbb_permanent),
		att_network_protocol(*dbb->dbb_permanent),
		att_remote_address(*dbb->dbb_permanent),
		att_remote_process(*dbb->dbb_permanent)
#ifndef SUPERSERVER
		, att_dsql_cache(*dbb->dbb_permanent)
#endif
		{
		}


static ISC_STATUS rollback(ISC_STATUS* user_status,
						   jrd_tra** tra_handle,
						   const bool retaining_flag)
{
/**************************************
 *
 *	r o l l b a c k
 *
 **************************************
 *
 * Functional description
 *	Abort a transaction.
 *
 **************************************/
	ThreadContextHolder tdbb(user_status);
	ISC_STATUS_ARRAY local_status;

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
	LOG_call(log_rollback, *tra_handle);
#endif

	try
	{
		jrd_tra* transaction = *tra_handle;
		validateHandle(tdbb, transaction);
		DatabaseContextHolder dbbHolder(tdbb);

		jrd_tra* next = transaction;

		while ( (transaction = next) )
		{
			next = transaction->tra_sibling;

			try
			{
				validateHandle(tdbb, transaction->tra_attachment);
				check_database(tdbb);

				const Database* const dbb = tdbb->getDatabase();
				const Attachment* const attachment = tdbb->getAttachment();

				if (!(attachment->att_flags & ATT_no_db_triggers))
				{
					try
					{
						ISC_STATUS_ARRAY temp_status = {0};
						tdbb->tdbb_status_vector = temp_status;

						// run ON TRANSACTION ROLLBACK triggers
						EXE_execute_db_triggers(tdbb, transaction, jrd_req::req_trigger_trans_rollback);
					}
					catch (const Firebird::Exception&)
					{
						if (dbb->dbb_flags & DBB_bugcheck)
							throw;
					}
				}

				tdbb->tdbb_status_vector = user_status;
				tdbb->setTransaction(transaction);
				TRA_rollback(tdbb, transaction, retaining_flag, false);
			}
			catch (const Firebird::Exception& ex) {
				Firebird::stuff_exception(user_status, ex);
				user_status = local_status;
			}
		}
	}
	catch (const Firebird::Exception& ex)
	{
		Firebird::stuff_exception(user_status, ex);
	}

	return user_status[1];
}


#if defined (WIN_NT) && !defined(SERVER_SHUTDOWN)
static void setup_NT_handlers()
{
/**************************************
 *
 *      s e t u p _ N T _ h a n d l e r s
 *
 **************************************
 *
 * Functional description
 *      Set up Windows NT console control handlers for
 *      things that can happen.   The handler used for
 *      all cases, handler_NT(), will flush and close
 *      any open database files.
 *
 **************************************/

	SetConsoleCtrlHandler((PHANDLER_ROUTINE) handler_NT, TRUE);
}
#endif


bool Database::onRawDevice()
{
#ifdef SUPPORT_RAW_DEVICES
	return PIO_on_raw_device(dbb_filename);
#else
	return false;
#endif
}


static void shutdown_database(Database* dbb, const bool release_pools)
{
/**************************************
 *
 *	s h u t d o w n _ d a t a b a s e
 *
 **************************************
 *
 * Functional description
 *	Shutdown physical database environment.
 *	NOTE:  This routine assumes that upon entry,
 *	mutex databases_mutex will be locked.
 *
 **************************************/
	thread_db* tdbb = JRD_get_thread_data();

	// Shutdown file and/or remote connection

#ifdef SUPERSERVER_V2
	TRA_header_write(tdbb, dbb, 0L);	// Update transaction info on header page.
#endif

#ifdef GARBAGE_THREAD
	VIO_fini(tdbb);
#endif
	CMP_fini(tdbb);
	CCH_fini(tdbb);
	if (dbb->dbb_backup_manager)
		dbb->dbb_backup_manager->shutdown(tdbb);
	// FUN_fini(tdbb);

	if (dbb->dbb_instance_lock)
		LCK_release(tdbb, dbb->dbb_instance_lock);

	if (dbb->dbb_monitor_lock)
		LCK_release(tdbb, dbb->dbb_monitor_lock);

	if (dbb->dbb_shadow_lock)
		LCK_release(tdbb, dbb->dbb_shadow_lock);

	if (dbb->dbb_retaining_lock)
		LCK_release(tdbb, dbb->dbb_retaining_lock);

	// temporal measure to avoid unstable state of lock file -
	// this is anyway called in ~Database()
	dbb->destroyIntlObjects();

#ifdef REPLAY_OSRI_API_CALLS_SUBSYSTEM
	if (dbb->dbb_log)
		LOG_fini();
#endif

	// Shut down any extern relations

	if (dbb->dbb_relations)
	{
		vec<jrd_rel*>* vector = dbb->dbb_relations;
		vec<jrd_rel*>::iterator ptr = vector->begin(), end = vector->end();

		for (; ptr < end; ++ptr)
		{
			jrd_rel* relation = *ptr;
			if (relation)
			{
				if (relation->rel_file)
				{
					EXT_fini(*ptr, false);
				}

				for (IndexLock* index_lock = relation->rel_index_locks; index_lock;
					index_lock = index_lock->idl_next)
				{
					if (index_lock->idl_lock)
						LCK_release(tdbb, index_lock->idl_lock);
				}

				for (IndexBlock* index_block = relation->rel_index_blocks; index_block;
					index_block = index_block->idb_next)
				{
					if (index_block->idb_lock)
						LCK_release(tdbb, index_block->idb_lock);
				}
			}
		}
	}

	if (dbb->dbb_lock)
		LCK_release(tdbb, dbb->dbb_lock);

	Database** d_ptr;	// Intentionally left outside loop (HP/UX compiler)
	for (d_ptr = &databases; *(d_ptr); d_ptr = &(*d_ptr)->dbb_next) {
		if (*d_ptr == dbb) {
			*d_ptr = dbb->dbb_next;
			break;
		}
	}

	if (dbb->dbb_flags & DBB_lck_init_done) {
		dbb->dbb_page_manager.releaseLocks();

		LCK_fini(tdbb, LCK_OWNER_database);	// For the database
		dbb->dbb_flags &= ~DBB_lck_init_done;
	}

	delete[] dbb->dbb_mutexes;

	if (release_pools) {
		tdbb->setDatabase(NULL);
		Database::deleteDbb(dbb);
	}
}


static void strip_quotes(Firebird::string& out)
{
/**************************************
 *
 *	s t r i p _ q u o t e s
 *
 **************************************
 *
 * Functional description
 *	Get rid of quotes around strings
 *
 **************************************/
	if (out.isEmpty()) 
	{
		return;
	}

	if (out[0] == DBL_QUOTE || out[0] == SINGLE_QUOTE)
	{
// Skip any initial quote
		const char quote = out[0];
		out.erase(0, 1);
// Search for same quote
		size_t pos = out.find(quote);
		if (pos != Firebird::string::npos)
		{
			out.erase(pos);
		}
	}
}


static bool shutdown_dbb(thread_db* tdbb, Database* dbb, Attachment** released)
{
/**************************************
 *
 *	s h u t d o w n _ d b b
 *
 **************************************
 *
 * Functional description
 *	rollback every transaction,
 *	release every attachment,
 *	and shutdown database.
 *
 **************************************/
	tdbb->setDatabase(dbb);
	tdbb->tdbb_flags |= TDBB_shutdown_manager;
	DatabaseContextHolder dbbHolder(tdbb);

	if (!(dbb->dbb_flags & (DBB_bugcheck | DBB_not_in_use | DBB_security_db)) &&
		!(dbb->dbb_ast_flags & DBB_shutdown &&
		  dbb->dbb_ast_flags & DBB_shutdown_locks))
	{
		Attachment* att_next;

		for (Attachment* attach = dbb->dbb_attachments; attach; attach = att_next)
		{
			att_next = attach->att_next;
			tdbb->setAttachment(attach);

			// purge_attachment() below can do an ERR_post
			ISC_STATUS_ARRAY temp_status;
			tdbb->tdbb_status_vector = temp_status;

			try
			{
				// purge attachment, rollback any open transactions
				purge_attachment(tdbb, temp_status, attach, true);
			}
			catch (const Firebird::Exception&)
			{
				if (released)
				{
					*released = NULL;
				}
				return false;
			}
			
			// attach became invalid pointer
			// if we have someone interested in that fact, inform him
			if (released)
			{
				*released++ = attach;
			}
		}
	}

	if (released)
	{
		*released = NULL;
	}
	return true;
}


static bool shutdown_all()
{
/**************************************
 *
 *	s h u t d o w n _ a l l
 *
 **************************************
 *
 * Functional description
 *	rollback every transaction,
 *	release every attachment,
 *	and shutdown every database.
 *
 **************************************/
	ThreadContextHolder tdbb;

	try {
		DbMutexGuard guard;

		Database* dbb_next;
		for (Database* dbb = databases; dbb; dbb = dbb_next)
		{
			dbb_next = dbb->dbb_next;

			if (!shutdown_dbb(tdbb, dbb, NULL))
			{
				return false;
			}
		}
	}
	catch (const Firebird::Exception&)
	{
		return false;
	}

	return true;
}


#ifdef SERVER_SHUTDOWN
TEXT* JRD_num_attachments(TEXT* const buf, USHORT buf_len, USHORT flag,
						  ULONG* atts, ULONG* dbs)
{
/**************************************
 *
 *	J R D _ n u m _ a t t a c h m e n t s
 *
 **************************************
 *
 * Functional description
 *	Count the number of active databases and
 *	attachments.  If flag is set then put
 *	what it says into buf, if it fits. If it does not fit
 *	then allocate local buffer, put info into there, and
 *	return pointer to caller (in this case a caller must
 *	release memory allocated for local buffer).
 *
 **************************************/

	// protect against NULL value for buf

	TEXT* lbuf = buf;
	if (!lbuf)
		buf_len = 0;

#ifdef WIN_NT
	// Check that the buffer is big enough for the requested
	// information.  If not, unset the flag

	if (flag == JRD_info_drivemask) {
		if (buf_len < sizeof(ULONG)) {
		    lbuf = (TEXT*) gds__alloc((SLONG) (sizeof(ULONG)));
			if (!lbuf)
				flag = 0;
		}
	}
#endif

	ULONG num_dbs = 0;
	ULONG num_att = 0;
	ULONG drive_mask = 0L;
	USHORT total = 0;
	Firebird::HalfStaticArray<Firebird::PathName, 8> dbFiles;

	try {
	dbMutexLock();

	// Zip through the list of databases and count the number of local
	// connections.  If buf is not NULL then copy all the database names
	// that will fit into it.

	for (Database* dbb = databases; dbb; dbb = dbb->dbb_next)
	{
		Database::SyncGuard dsGuard(dbb);

#ifdef WIN_NT
		// Get drive letters for db files

		if (flag == JRD_info_drivemask)
		{
			PageSpace* pageSpace = dbb->dbb_page_manager.findPageSpace(DB_PAGE_SPACE);
			for (jrd_file* files = pageSpace->file; files; files = files->fil_next)
				ExtractDriveLetter(files->fil_string, &drive_mask);
		}
#endif

		if (!(dbb->dbb_flags & (DBB_bugcheck | DBB_not_in_use | DBB_security_db)) &&
			!(dbb->dbb_ast_flags & DBB_shutdown
			  && dbb->dbb_ast_flags & DBB_shutdown_locks))
		{
			num_dbs++;
			if (flag == JRD_info_dbnames) {
				dbFiles.push(dbb->dbb_database_name);
				total += sizeof(USHORT) + dbb->dbb_database_name.length();
			}

			for (const Attachment* attach = dbb->dbb_attachments; attach;
				attach = attach->att_next)
			{
				num_att++;

#ifdef WIN_NT
				// Get drive letters for temp directories

				if (flag == JRD_info_drivemask) {
					const Firebird::TempDirectoryList dirList;
					for (size_t i = 0; i < dirList.getCount(); i++) {
						const Firebird::PathName& path = dirList[i];
						ExtractDriveLetter(path.c_str(), &drive_mask);
					}
				}
#endif
			}
		}
	}

	dbMutexUnlock();
	}
	catch (const Firebird::Exception&)
	{
		// Here we ignore possible errors from databases_rec_mutex.
		// They were always silently ignored, and for this function 
		// we really have no way to notify world about mutex problem.
		//		AP. 2008.
	}

	*atts = num_att;
	*dbs = num_dbs;

	if (dbFiles.getCount() > 0)
	{
		if (flag == JRD_info_dbnames)
		{
			if (buf_len < (sizeof(USHORT) + total))
			{
				lbuf = (TEXT *) gds__alloc((SLONG) (sizeof(USHORT) + total));
			}
			TEXT* lbufp = lbuf;
			if (lbufp)
			{
				/*  Put db info into buffer. Format is as follows:

				   number of dbases sizeof (USHORT)
				   1st db name length   sizeof (USHORT)
				   1st db name      sizeof (TEXT) * length
				   2nd db name length
				   2nd db name
				   ...
				   last db name length
				   last db name
				 */

				lbufp += sizeof(USHORT);
				total = 0;
				for (size_t n = 0; n < dbFiles.getCount(); ++n) {
					*lbufp++ = (TEXT) dbFiles[n].length();
					*lbufp++ = (TEXT) (dbFiles[n].length() >> 8);
					memcpy(lbufp, dbFiles[n].c_str(), dbFiles[n].length());
					lbufp += dbFiles[n].length();
					total++;
				}
				fb_assert(total == num_dbs);
				lbufp = lbuf;
				*lbufp++ = (TEXT) total;
				*lbufp++ = (TEXT) (total >> 8);
			}
		}
	}

#ifdef WIN_NT
	if (flag == JRD_info_drivemask)
		*(ULONG *) lbuf = drive_mask;
#endif

// CVC: Apparently, the original condition will leak memory, because flag
// may be JRD_info_drivemask and memory could be allocated for that purpose,
// as few as sizeof(ULONG), but a leak is a leak! I added the ifdef below.
	if (num_dbs == 0)
	{
#ifdef WIN_NT
		if (flag == JRD_info_drivemask && lbuf != buf)
		    gds__free(lbuf);
#endif
		lbuf = NULL;
	}

	return lbuf;
}


#ifdef WIN_NT
static void ExtractDriveLetter(const TEXT* file_name, ULONG* drive_mask)
{
/**************************************
 *
 *	E x t r a c t D r i v e L e t t e r
 *
 **************************************
 *
 * Functional description
 *	Determine the drive letter of file_name
 *	and set the proper bit in the bit mask.
 *		bit 0 = drive A
 *		bit 1 = drive B and so on...
 *	This function is used to determine drive
 *	usage for use with Plug and Play for
 *	MS Windows 4.0.
 *
 **************************************/
	ULONG mask = 1;

	const SHORT shift = (*file_name - 'A');
	mask <<= shift;
	*drive_mask |= mask;
}
#endif


static THREAD_ENTRY_DECLARE shutdown_thread(THREAD_ENTRY_PARAM arg) 
{
/**************************************
 *
 *	s h u t d o w n _ t h r e a d
 *
 **************************************
 *
 * Functional description
 *	Shutdown SuperServer. If hangs, server 
 *  is forcely & dirty closed after timeout.
 *
 **************************************/

	*static_cast<int*>(arg) = shutdown_all();
	return 0;
}


void JRD_shutdown_all(bool asyncMode)
{
/**************************************
 *
 *	J R D _ s h u t d o w n _ a l l
 *
 **************************************
 *
 * Functional description
 *	Rollback every transaction, release
 *	every attachment, and shutdown every
 *	database. Can be called in either a
 *  blocking mode or as a request for
 *  asynchronous shutdown.
 *
 **************************************/
	int flShutdownComplete = 0;

	if (asyncMode)
	{
		gds__thread_start(shutdown_thread, &flShutdownComplete, 
			THREAD_medium, 0, 0);
		int timeout = 10;	// seconds
		while (timeout--) 
		{
			if (flShutdownComplete)
				break;
			THREAD_SLEEP(1 * 1000);
		}
	}
	else // sync mode
	{
		flShutdownComplete = shutdown_all();
	}

	if (!flShutdownComplete)
	{
		gds__log("Forced server shutdown - not all databases closed");
	}
}

#else // SERVER_SHUTDOWN

// This conditional compilation, though sitting under not
// defined SERVER_SHUTDOWN, performs full or partial shutdown 
// of database. SERVER_SHUTDOWN defined controls some other
// aspects of operation, therefore was left "as is". 
// Who wants to invent better name for it, please do it.

void JRD_process_close()
{
	shutdown_all();
}

void JRD_database_close(Attachment** handle, Attachment** released)
{
	ThreadContextHolder tdbb;

	try {
		DbMutexGuard guard;

		Database* dbb = databases;
		for (; dbb; dbb = dbb->dbb_next)
		{
			for (Attachment* attach = dbb->dbb_attachments; attach; attach = attach->att_next)
			{
				if (attach == *handle)
				{
					// got dbb to be closed
					shutdown_dbb(tdbb, dbb, released);
					return;
				}
			}
		}
	}
	catch (const Firebird::Exception&)
	{
		// This function is called from yValve as a last attempt to help 
		// when terminate signal is sent to firebird server.
		// Ignore possible mutex problems.
	}
}

#endif // SERVER_SHUTDOWN


static unsigned int purge_transactions(thread_db*	tdbb,
									   Attachment*	attachment,
									   const bool	force_flag,
									   const ULONG	att_flags)
{
/**************************************
 *
 *	p u r g e _ t r a n s a c t i o n s
 *
 **************************************
 *
 * Functional description
 *	commit or rollback all transactions 
 *	from an attachment
 *
 **************************************/
	Database* dbb = attachment->att_database;
	jrd_tra* trans_dbk = attachment->att_dbkey_trans;

	unsigned int count = 0;
	jrd_tra* next;

	for (jrd_tra* transaction = attachment->att_transactions;
		 transaction;
		 transaction = next)
	{
		next = transaction->tra_next;
		if (transaction != trans_dbk)
		{
			if ((transaction->tra_flags & TRA_prepared) ||
				(dbb->dbb_ast_flags & DBB_shutdown) ||
				(att_flags & ATT_shutdown))
			{
				TRA_release_transaction(tdbb, transaction);
			}
			else if (force_flag)
				TRA_rollback(tdbb, transaction, false, true);
			else
				++count;
		}
	}

	if (count)
	{
		return count;
	}

	// If there's a side transaction for db-key scope, get rid of it
	if (trans_dbk)
	{
		attachment->att_dbkey_trans = NULL;
		if ((dbb->dbb_ast_flags & DBB_shutdown) ||
			(att_flags & ATT_shutdown))
		{
			TRA_release_transaction(tdbb, trans_dbk);
		}
		else
		{
			TRA_commit(tdbb, trans_dbk, false);
		}
	}

	return 0;
}


static void purge_attachment(thread_db*		tdbb,
							 ISC_STATUS*	user_status,
							 Attachment*	attachment,
							 const bool		force_flag)
{
/**************************************
 *
 *	p u r g e _ a t t a c h m e n t
 *
 **************************************
 *
 * Functional description
 *	Zap an attachment, shutting down the database
 *	if it is the last one.
 *	NOTE:  This routine assumes that upon entry,
 *	mutex databases_mutex will be locked.
 *
 **************************************/
	SET_TDBB(tdbb);
	Database* dbb = attachment->att_database;
	
	if (!(dbb->dbb_flags & DBB_bugcheck))
	{
		ISC_STATUS* const original_status = tdbb->tdbb_status_vector;

		try
		{
			if (!(attachment->att_flags & ATT_no_db_triggers) &&
				!(attachment->att_flags & ATT_shutdown))
			{
				ISC_STATUS_ARRAY temp_status = {0};
				tdbb->tdbb_status_vector = temp_status;

				jrd_tra* transaction = NULL;

				try
				{
					// start a transaction to execute ON DISCONNECT triggers
					transaction = TRA_start(tdbb, 0, NULL);

					// run ON DISCONNECT triggers
					EXE_execute_db_triggers(tdbb, transaction,
											jrd_req::req_trigger_disconnect);

					// and commit the transaction
					TRA_commit(tdbb, transaction, false);
				}
				catch (const Firebird::Exception&)
				{
					if (dbb->dbb_flags & DBB_bugcheck)
						throw;

					try
					{
						if (transaction)
							TRA_rollback(tdbb, transaction, false, false);
					}
					catch (const Firebird::Exception&)
					{
						if (dbb->dbb_flags & DBB_bugcheck)
							throw;
					}
				}
			}
		}
		catch (const Firebird::Exception&)
		{
			tdbb->tdbb_status_vector = original_status;
			attachment->att_flags |= ATT_shutdown;
			throw;
		}

		tdbb->tdbb_status_vector = original_status;
	}

	const ULONG att_flags = attachment->att_flags;
	attachment->att_flags |= ATT_shutdown;

	if (!(dbb->dbb_flags & DBB_bugcheck))
	{
		// Check for any pending transactions
		unsigned int count = purge_transactions(tdbb, attachment, force_flag, att_flags);
		if (count)
		{
			ERR_post(isc_open_trans, isc_arg_number, (SLONG) count, 0);
		}

		SORT_shutdown(attachment);
	}

	// Unlink attachment from database

	release_attachment(tdbb, attachment);

	// If there are still attachments, do a partial shutdown

	if (dbb->checkHandle())
	{
		if (dbb->dbb_attachments || (dbb->dbb_flags & DBB_being_opened))
		{
			// There are still attachments so do a partial shutdown

            // Both CMP_release() and SCL_release() do advance the pointer
			// before the deallocation.
			jrd_req* request;
			while ( (request = attachment->att_requests) ) {
				CMP_release(tdbb, request);
			}

			SecurityClass* sec_class;
			while ( (sec_class = attachment->att_security_classes) ) {
				SCL_release(sec_class);
			}

			delete attachment->att_user;
			delete attachment;
		}
		else
		{
			shutdown_database(dbb, true);
		}
	}
}


static void run_commit_triggers(thread_db* tdbb, jrd_tra* transaction)
{
/**************************************
 *
 *	r u n _ c o m m i t _ t r i g g e r s
 *
 **************************************
 *
 * Functional description
 *	Run ON TRANSACTION COMMIT triggers of a transaction.
 *
 **************************************/
	SET_TDBB(tdbb);

	if (transaction == tdbb->getDatabase()->dbb_sys_trans)
		return;

	// start a savepoint to rollback changes of all triggers
	VIO_start_save_point(tdbb, transaction);

	try
	{
		// run ON TRANSACTION COMMIT triggers
		EXE_execute_db_triggers(tdbb, transaction,
								jrd_req::req_trigger_trans_commit);
		VIO_verb_cleanup(tdbb, transaction);
	}
	catch (const Firebird::Exception&)
	{
		if (!(tdbb->getDatabase()->dbb_flags & DBB_bugcheck))
		{
			// rollbacks the created savepoint
			++transaction->tra_save_point->sav_verb_count;
			VIO_verb_cleanup(tdbb, transaction);
		}
		throw;
	}
}


// verify_request_synchronization
//
// @brief Finds the sub-requests at the given level and replaces it with the
// original passed request (note the pointer by reference). If that specific
// sub-request is not found, throw the dreaded "request synchronization error".
// Notice that at this time, the calling function's "request" pointer has been
// set to null, so remember that if you write a debugging routine.
// This function replaced a chunk of code repeated four times.
//
// @param request The incoming, parent request to be replaced.
// @param level The level of the sub-request we need to find.
static void verify_request_synchronization(jrd_req*& request, SSHORT level)
{
	const USHORT lev = level;
	if (lev) {
		const vec<jrd_req*>* vector = request->req_sub_requests;
		if (!vector || lev >= vector->count() ||
			!(request = (*vector)[lev]))
		{
			ERR_post(isc_req_sync, 0);
		}
	}
}


/**
  
 	verify_database_name
  
    @brief	Verify database name for open/create 
	against given in conf file list of available directories
	and security database name

    @param name
    @param status

 **/
static vdnResult verify_database_name(const Firebird::PathName& name, ISC_STATUS* status)
{
	// Check for security2.fdb
	static TEXT SecurityNameBuffer[MAXPATHLEN] = "";
	static Firebird::PathName ExpandedSecurityNameBuffer(*getDefaultMemoryPool());
	if (! SecurityNameBuffer[0]) {
		SecurityDatabase::getPath(SecurityNameBuffer);
		ExpandedSecurityNameBuffer = SecurityNameBuffer;
		ISC_expand_filename(ExpandedSecurityNameBuffer, false);
	}
	if (name == SecurityNameBuffer || name == ExpandedSecurityNameBuffer)
		return vdnSecurity;

	// Check for .conf
	if (!ISC_verify_database_access(name)) {
		status[0] = isc_arg_gds;
		status[1] = isc_conf_access_denied;
		status[2] = isc_arg_string;
		// CVC: Using STATUS to hold pointer to literal string!
		status[3] = reinterpret_cast<ISC_STATUS>("database");
		status[4] = isc_arg_string;
		status[5] = (ISC_STATUS)(U_IPTR) ERR_cstring(name.c_str());
		status[6] = isc_arg_end;
		return vdnFail;
	}
	return vdnOk;
}


/**
  
 	Attachment::locksmith
  
    @brief	Validate - is attached user locksmith?

 **/
bool Attachment::locksmith() const
{
	return att_user->locksmith();
}


/**
  
	getUserInfo
  
    @brief	Checks the userinfo database to validate
    password to that passed in.
    Takes into account possible trusted authentication.
	Fills UserId structure with resulting values.

    @param user
    @param options

 **/
static void getUserInfo(UserId& user, const DatabaseOptions& options)
{
	int id = -1, group = -1;	// CVC: This var contained trash
	int node_id = 0;
	Firebird::string name;

#ifdef BOOT_BUILD
	bool wheel = true;
#else
	bool wheel = false;
	if (options.dpb_trusted_login.hasData())
	{
		name = options.dpb_trusted_login;
	}
	else
	{
		if (options.dpb_user_name.isEmpty()) 
		{
	       wheel = ISC_get_user(&name, &id, &group,
                        	    options.dpb_sys_user_name.nullStr());
		}

		if (options.dpb_user_name.hasData() || (id == -1))
		{
			Firebird::string remote = options.dpb_network_protocol + 
				(options.dpb_network_protocol.isEmpty() || options.dpb_remote_address.isEmpty() ? "" : "/") +
				options.dpb_remote_address;

			SecurityDatabase::verifyUser(name, 
										 options.dpb_user_name.nullStr(), 
										 options.dpb_password.nullStr(), 
										 options.dpb_password_enc.nullStr(),
										 &id, &group, &node_id, remote);
		}
	}

	// if the name from the user database is defined as SYSDBA,
	// we define that user id as having system privileges

	if (name == SYSDBA_USER_NAME)
	{
		wheel = true;
	}
#endif // BOOT_BUILD

	// In case we became WHEEL on an OS that didn't require name SYSDBA,
	// (Like Unix) force the effective Database User name to be SYSDBA

	if (wheel)
	{
		name = SYSDBA_USER_NAME;
	}

	if (name.length() > USERNAME_LENGTH)
	{
		Firebird::status_exception::raise(isc_long_login, 
										  isc_arg_number, name.length(), 
										  isc_arg_number, USERNAME_LENGTH,
										  0);
	}

	user.usr_user_name = name;
	user.usr_project_name = "";
	user.usr_org_name = "";
	user.usr_sql_role_name = options.dpb_role_name;
	user.usr_user_id = id;
	user.usr_group_id = group;
	user.usr_node_id = node_id;
	if (wheel) 
	{
		user.usr_flags |= USR_locksmith;
	}

	if (options.dpb_trusted_role)
	{
		user.usr_flags |= USR_trole;
	}
}

static ISC_STATUS unwindAttach(const Firebird::Exception& ex, 
							   ISC_STATUS* userStatus, 
							   thread_db* tdbb, 
							   Attachment* attachment, 
							   Database* dbb)
{
	ISC_STATUS_ARRAY temp_status;
	ISC_STATUS* const save_status = tdbb->tdbb_status_vector;
	tdbb->tdbb_status_vector = temp_status;

	try
	{
		dbb->dbb_flags &= ~DBB_being_opened;
		release_attachment(tdbb, attachment);

		if (dbb->checkHandle())
		{
			if (!dbb->dbb_attachments)
			{
				shutdown_database(dbb, true);
			}
			else if (attachment)
			{
				delete attachment;
			}
		}
	}
	catch (const Firebird::Exception&)
	{
	}

	tdbb->tdbb_status_vector = save_status;
	dbMutexUnlock();

	Firebird::stuff_exception(userStatus, ex);
	return userStatus[1];
}

void thread_db::setTransaction(jrd_tra* val)
{
	transaction = val;
	traStat = val ? &val->tra_stats : RuntimeStatistics::getDummy();
}

void thread_db::setRequest(jrd_req* val)
{
	request = val;
	reqStat = val ? &val->req_stats : RuntimeStatistics::getDummy();
}

Database::~Database()
{
	delete dbb_sys_trans;

	destroyIntlObjects();

	fb_assert(dbb_pools[0] == dbb_permanent);
	for (size_t i = 1; i < dbb_pools.getCount(); ++i)
	{
		MemoryPool::deletePool(dbb_pools[i]);
	}
}
