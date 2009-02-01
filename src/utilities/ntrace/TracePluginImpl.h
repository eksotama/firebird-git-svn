/*
 *	PROGRAM:	SQL Trace plugin
 *	MODULE:		TracePluginImpl.h
 *	DESCRIPTION:	Plugin implementation
 *
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
 *  The Original Code was created by Nickolay Samofatov
 *  for the Firebird Open Source RDBMS project.
 *
 *  Copyright (c) 2004 Nickolay Samofatov <nickolay@broadviewsoftware.com>
 *  and all contributors signed below.
 *
 *  All Rights Reserved.
 *  Contributor(s): ______________________________________.
 *
 *  2008 Khorsun Vladyslav
 */

#ifndef TRACEPLUGINIMPL_H
#define TRACEPLUGINIMPL_H

#include "../../jrd/ntrace.h"

#include "TracePluginConfig.h"
#include "os/FileObject.h"
#include "../../common/classes/rwlock.h"
#include "../../common/classes/GenericMap.h"
#include "../../common/classes/locks.h"


// Bring in off_t
#include <sys/types.h>
#include <regex.h>

class TracePluginImpl {
public:
	// Create skeletal plugin (to report initialization error)
	static TracePlugin* createSkeletalPlugin();

	// Create trace plugin for particular database
	static TracePlugin* createFullPlugin(const TracePluginConfig &configuration, TraceInitInfo* initInfo);

	// Serialize exception to TLS buffer to return it to user
	static void marshal_exception(const Firebird::Exception& ex);

	// Data for tracked (active) connections
	struct ConnectionData {
        int id;
		Firebird::string* description;

		// Deallocate memory used by objects hanging off this structure
		void deallocate_references() {
			delete description;
			description = NULL;
		}

		static const int& generate(const void* sender, const ConnectionData& item) {
			return item.id;
		}
	};

	typedef Firebird::BePlusTree<ConnectionData, int, Firebird::MemoryPool, ConnectionData>
		ConnectionsTree;

	// Data for tracked (active) transactions
	struct TransactionData {
		int id;
		Firebird::string* description;

		// Deallocate memory used by objects hanging off this structure
		void deallocate_references() {
			delete description;
			description = NULL;
		}

		static const int& generate(const void* sender, const TransactionData& item) {
			return item.id;
		}
	};

	typedef Firebird::BePlusTree<TransactionData, int, Firebird::MemoryPool, TransactionData>
		TransactionsTree;

	// Data for tracked (active) statements
	struct StatementData {
		unsigned int id;
		Firebird::string* description; // NULL in this field indicates that tracing of this statement is not desired

		static const unsigned int& generate(const void* sender, const StatementData& item) {
			return item.id;
		}
	};

	typedef Firebird::BePlusTree<StatementData, unsigned int, Firebird::MemoryPool, StatementData>
		StatementsTree;

	struct ServiceData {
		ntrace_service_t id;
		Firebird::string* description;

		// Deallocate memory used by objects hanging off this structure
		void deallocate_references() {
			delete description;
			description = NULL;
		}

		static const ntrace_service_t& generate(const void* sender, const ServiceData& item) {
			return item.id;
		}
	};

	typedef Firebird::BePlusTree<ServiceData, ntrace_service_t, Firebird::MemoryPool, ServiceData>
		ServicesTree;

private:
	TracePluginImpl(const TracePluginConfig &configuration, TraceInitInfo* initInfo);
	~TracePluginImpl();

	bool operational; // Set if plugin is fully initialized and is ready for logging
					  // Keep this member field first to ensure its correctness
					  // when destructor is called
	int session_id;				// trace session ID, set by Firebird
	Firebird::string session_name;		// trace session name, set by Firebird
	FileObject *logFile;		// Thread-safe
	TraceLogWriter* logWriter;
	TracePluginConfig config;	// Immutable, thus thread-safe

	// Data for currently active connections, transactions, statements
	Firebird::RWLock connectionsLock;
	ConnectionsTree connections;

	Firebird::RWLock transactionsLock;
	TransactionsTree transactions;

	Firebird::RWLock statementsLock;
	StatementsTree statements;

	Firebird::RWLock servicesLock;
	ServicesTree services;

	// Lock for log rotation
	Firebird::RWLock renameLock;

	regex_t include_matcher;
	regex_t exclude_matcher;

	bool need_rotate(size_t added_bytes_length);
	void rotateLog(size_t added_bytes_length);
	void writePacket(const UCHAR* packet_data, const ULONG packet_size);

	void appendGlobalCounts(PerformanceInfo *info, Firebird::string& line);
	void appendTableCounts(PerformanceInfo *info, Firebird::string& line);
	void appendParams(TraceParams *params, Firebird::string& line);
	void appendServiceQueryParams(size_t send_item_length,
		const ntrace_byte_t* send_items, size_t recv_item_length, 
		const ntrace_byte_t* recv_items, Firebird::string& line);
	void formatStringArgument(Firebird::string& result, const UCHAR* str, size_t len);


	// register various objects
	void register_connection(TraceConnection* connection);
	void register_transaction(TraceTransaction* transaction);
	void register_sql_statement(TraceSQLStatement* statement);
	void register_blr_statement(TraceBLRStatement* statement);
	void register_service(TraceService* service);

	// Write message to text log file
	void logRecord(const char* action, Firebird::string& line);
	void logRecordConn(const char* action, TraceConnection* connection, Firebird::string& line);
	void logRecordTrans(const char* action, TraceConnection* connection, 
		TraceTransaction* transaction, Firebird::string& line);
	void logRecordProc(const char* action, TraceConnection* connection, 
		TraceTransaction* transaction, const char* proc_name, Firebird::string& line);
	void logRecordStmt(const char* action, TraceConnection* connection, 
		TraceTransaction* transaction, TraceStatement* statement, 
		bool isSQL, Firebird::string& line);
	void logRecordServ(const char* action, TraceService* service, Firebird::string& line);

	/* Methods which do logging of events to file */
	void log_init();
	void log_finalize();

	void log_event_attach(
		TraceConnection* connection, ntrace_boolean_t create_db, 
		ntrace_result_t att_result);
	void log_event_detach(
		TraceConnection* connection, ntrace_boolean_t drop_db);
	
	void log_event_transaction_start(
		TraceConnection* connection, TraceTransaction* transaction, 
		size_t tpb_length, const ntrace_byte_t* tpb, ntrace_result_t tra_result);
	void log_event_transaction_end(
		TraceConnection* connection, TraceTransaction* transaction, 
		ntrace_boolean_t commit, ntrace_boolean_t retain_context, ntrace_result_t tra_result);
	
	void log_event_set_context(
		TraceConnection* connection, TraceTransaction* transaction, 
		TraceContextVariable* variable);
	
	void log_event_proc_execute(
		TraceConnection* connection, TraceTransaction* transaction, TraceProcedure* procedure, 
		bool started, ntrace_result_t proc_result);
	
	void log_event_trigger_execute(
		TraceConnection* connection, TraceTransaction* transaction, TraceTrigger* trigger, 
		bool started, ntrace_result_t trig_result);
	
	void log_event_dsql_prepare(
		TraceConnection* connection, TraceTransaction* transaction,
		TraceSQLStatement* statement, ntrace_counter_t time_millis, ntrace_result_t req_result);
	void log_event_dsql_free(
		TraceConnection* connection, TraceSQLStatement* statement, unsigned short option);
	void log_event_dsql_execute(
		TraceConnection* connection, TraceTransaction* transaction, TraceSQLStatement* statement, 
		bool started, ntrace_result_t req_result);

	void log_event_blr_compile(
		TraceConnection* connection,	TraceTransaction* transaction, 
		TraceBLRStatement* statement, ntrace_counter_t time_millis, ntrace_result_t req_result);
	void log_event_blr_execute(
		TraceConnection* connection, TraceTransaction* transaction,
		TraceBLRStatement* statement, ntrace_result_t req_result);
	
	void log_event_dyn_execute(
		TraceConnection* connection, TraceTransaction* transaction, 
		TraceDYNRequest* request, ntrace_counter_t time_millis,
		ntrace_result_t req_result);
	
	void log_event_service_attach(
		TraceService* service, ntrace_result_t att_result);
	void log_event_service_start(
		TraceService* service, size_t switches_length, const char* switches, 
		ntrace_result_t start_result);
	void log_event_service_query(
		TraceService* service, size_t send_item_length, 
		const ntrace_byte_t* send_items, size_t recv_item_length, 
		const ntrace_byte_t* recv_items, ntrace_result_t query_result);
	void log_event_service_detach(
		TraceService* service, ntrace_result_t detach_result);

	/* Finalize plugin. Called when database is closed by the engine */
	static ntrace_boolean_t ntrace_shutdown(const TracePlugin* tpl_plugin);

	/* Function to return error string for hook failure */
	static const char* ntrace_get_error(const TracePlugin* tpl_plugin);

	/* Create/close attachment */
	static ntrace_boolean_t ntrace_event_attach(const TracePlugin* tpl_plugin, 
		TraceConnection* connection, ntrace_boolean_t create_db, 
		ntrace_result_t att_result);
	static ntrace_boolean_t ntrace_event_detach(const TracePlugin* tpl_plugin, 
		TraceConnection* connection, ntrace_boolean_t drop_db);

	/* Start/end transaction */
	static ntrace_boolean_t ntrace_event_transaction_start(const TracePlugin* tpl_plugin, 
		TraceConnection* connection, TraceTransaction* transaction, 
		size_t tpb_length, const ntrace_byte_t* tpb, ntrace_result_t tra_result);
	static ntrace_boolean_t ntrace_event_transaction_end(const TracePlugin* tpl_plugin,
		TraceConnection* connection, TraceTransaction* transaction, 
		ntrace_boolean_t commit, ntrace_boolean_t retain_context, ntrace_result_t tra_result);

	/* Assignment to context variables */
	static ntrace_boolean_t ntrace_event_set_context(const struct TracePlugin* tpl_plugin,
		TraceConnection* connection, TraceTransaction* transaction, 
		TraceContextVariable* variable);

	/* Stored procedure executing */
	static ntrace_boolean_t ntrace_event_proc_execute(const struct TracePlugin* tpl_plugin,
		TraceConnection* connection, TraceTransaction* transaction, TraceProcedure* procedure, 
		bool started, ntrace_result_t proc_result);

	static ntrace_boolean_t ntrace_event_trigger_execute(const TracePlugin* tpl_plugin, 
		TraceConnection* connection, TraceTransaction* transaction, TraceTrigger* trigger, 
		bool started, ntrace_result_t trig_result);

	/* DSQL statement lifecycle */
	static ntrace_boolean_t ntrace_event_dsql_prepare(const TracePlugin* tpl_plugin, 
		TraceConnection* connection, TraceTransaction* transaction,
		TraceSQLStatement* statement, ntrace_counter_t time_millis, ntrace_result_t req_result);
	static ntrace_boolean_t ntrace_event_dsql_free(const TracePlugin* tpl_plugin, 
		TraceConnection* connection, TraceSQLStatement* statement, unsigned short option);
	static ntrace_boolean_t ntrace_event_dsql_execute(const TracePlugin* tpl_plugin, 
		TraceConnection* connection, TraceTransaction* transaction, TraceSQLStatement* statement, 
		bool started, ntrace_result_t req_result);

	/* BLR requests */
	static ntrace_boolean_t ntrace_event_blr_compile(const TracePlugin* tpl_plugin,
		TraceConnection* connection,	TraceTransaction* transaction, 
		TraceBLRStatement* statement, ntrace_counter_t time_millis, ntrace_result_t req_result);
	static ntrace_boolean_t ntrace_event_blr_execute(const TracePlugin* tpl_plugin, 
		TraceConnection* connection, TraceTransaction* transaction,
		TraceBLRStatement* statement, ntrace_result_t req_result);

	/* DYN requests */
	static ntrace_boolean_t ntrace_event_dyn_execute(const TracePlugin* tpl_plugin, 
		TraceConnection* connection, TraceTransaction* transaction, 
		TraceDYNRequest* request,	ntrace_counter_t time_millis, 
		ntrace_result_t req_result);

	/* Using the services */
	static ntrace_boolean_t ntrace_event_service_attach(const TracePlugin* tpl_plugin, 
		TraceService* service, ntrace_result_t att_result);
	static ntrace_boolean_t ntrace_event_service_start(const TracePlugin* tpl_plugin, 
		TraceService* service, size_t switches_length, const char* switches,
		ntrace_result_t start_result);
	static ntrace_boolean_t ntrace_event_service_query(const TracePlugin* tpl_plugin, 
		TraceService* service, size_t send_item_length, 
		const ntrace_byte_t* send_items, size_t recv_item_length, 
		const ntrace_byte_t* recv_items, ntrace_result_t query_result);
	static ntrace_boolean_t ntrace_event_service_detach(const TracePlugin* tpl_plugin, 
		TraceService* service, ntrace_result_t detach_result);
};

#endif // TRACEPLUGINIMPL_H
