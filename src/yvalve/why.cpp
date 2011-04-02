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
 * Dmitry Yemanov
 * Sean Leyne
 * Alex Peshkoff
 * Adriano dos Santos Fernandes
 *
 */

#include "firebird.h"
#include "ProviderInterface.h"
#include "memory_routines.h"
#include "gen/iberror.h"
#include "gen/msg_facs.h"
#include "../dsql/sqlda.h"
#include "../dsql/sqlda_pub.h"
#include "../common/common.h"
#include "../common/gdsassert.h"
#include "../common/db_alias.h"
#include "../common/StatusHolder.h"
#include "../common/ThreadStart.h"
#include "../common/isc_proto.h"
#include "../common/isc_f_proto.h"
#include "../common/utils_proto.h"
#include "../common/os/isc_i_proto.h"
#include "../common/os/path_utils.h"
#include "../common/classes/alloc.h"
#include "../common/classes/array.h"
#include "../common/classes/fb_string.h"
#include "../common/classes/fb_atomic.h"
#include "../common/classes/init.h"
#include "../common/classes/rwlock.h"
#include "../common/classes/semaphore.h"
#include "../common/classes/ClumpletWriter.h"
#include "../common/classes/FpeControl.h"
#include "../common/classes/GenericMap.h"
#include "../common/classes/GetPlugins.h"
#include "../yvalve/prepa_proto.h"
#include "../yvalve/utl_proto.h"
#include "../yvalve/utly_proto.h"
#include "../yvalve/why_proto.h"
#include "../yvalve/MasterImplementation.h"
#include "../yvalve/PluginManager.h"
#include "../remote/client/interface.h"
#include "../jrd/acl.h"
#include "../jrd/msg_encode.h"
#include "../jrd/inf_pub.h"

#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif

using namespace Firebird;
using namespace Why;

namespace
{
	class YService;
	class YAttachment;
	class YEvents;
	class YTransaction;
	class YStatement;
	class YRequest;
	class YBlob;
};


static void badHandle(ISC_STATUS code);
static bool isNetworkError(const Status* status);
static void nullCheck(const FB_API_HANDLE* ptr, ISC_STATUS code);
static void saveErrorString(ISC_STATUS* status);
static bool setPath(const PathName& filename, PathName& expandedName);
static void releaseDsqlSupport(sqlda_sup& dasup);
static USHORT sqldaBufferSize(USHORT min_buffer_size, const XSQLDA* sqlda, USHORT dialect);
static void iterativeSqlInfo(ISC_STATUS* userStatus, FB_API_HANDLE* stmtHandle, USHORT itemLength,
	const SCHAR* items, SSHORT bufferLength, SCHAR* buffer, USHORT dialect, XSQLDA* sqlda);
static ISC_STATUS openOrCreateBlob(ISC_STATUS* userStatus, FB_API_HANDLE* dbHandle,
	FB_API_HANDLE* traHandle, FB_API_HANDLE* blobHandle, ISC_QUAD* blobId,
	USHORT bpbLength, const UCHAR* bpb, bool createFlag);


//-------------------------------------


static const USHORT PREPARE_BUFFER_SIZE = 32768;	// size of buffer used in isc_dsql_prepare call
static const USHORT DESCRIBE_BUFFER_SIZE = 1024;	// size of buffer used in isc_dsql_describe_xxx call

// Information items for two phase commit.

static const UCHAR PREPARE_TR_INFO[] =
{
	isc_info_tra_id,
	isc_info_end
};

// Information items for DSQL prepare.

static const SCHAR SQL_PREPARE_INFO[] =
{
	isc_info_sql_select,
	isc_info_sql_describe_vars,
	isc_info_sql_sqlda_seq,
	isc_info_sql_type,
	isc_info_sql_sub_type,
	isc_info_sql_scale,
	isc_info_sql_length,
	isc_info_sql_field,
	isc_info_sql_relation,
	isc_info_sql_owner,
	isc_info_sql_alias,
	isc_info_sql_describe_end
};

// Information items for SQL info.

static const SCHAR DESCRIBE_SELECT_INFO[] =
{
	isc_info_sql_select,
	isc_info_sql_describe_vars,
	isc_info_sql_sqlda_seq,
	isc_info_sql_type,
	isc_info_sql_sub_type,
	isc_info_sql_scale,
	isc_info_sql_length,
	isc_info_sql_field,
	isc_info_sql_relation,
	isc_info_sql_owner,
	isc_info_sql_alias,
	isc_info_sql_describe_end
};

static const SCHAR DESCRIBE_BIND_INFO[] =
{
	isc_info_sql_bind,
	isc_info_sql_describe_vars,
	isc_info_sql_sqlda_seq,
	isc_info_sql_type,
	isc_info_sql_sub_type,
	isc_info_sql_scale,
	isc_info_sql_length,
	isc_info_sql_field,
	isc_info_sql_relation,
	isc_info_sql_owner,
	isc_info_sql_alias,
	isc_info_sql_describe_end
};

static const SCHAR SQL_PREPARE_INFO2[] =
{
	isc_info_sql_stmt_type,

	// DESCRIBE_SELECT_INFO
	isc_info_sql_select,
	isc_info_sql_describe_vars,
	isc_info_sql_sqlda_seq,
	isc_info_sql_type,
	isc_info_sql_sub_type,
	isc_info_sql_scale,
	isc_info_sql_length,
	isc_info_sql_field,
	isc_info_sql_relation,
	isc_info_sql_owner,
	isc_info_sql_alias,
	isc_info_sql_describe_end,

	// DESCRIBE_BIND_INFO
	isc_info_sql_bind,
	isc_info_sql_describe_vars,
	isc_info_sql_sqlda_seq,
	isc_info_sql_type,
	isc_info_sql_sub_type,
	isc_info_sql_scale,
	isc_info_sql_length,
	isc_info_sql_field,
	isc_info_sql_relation,
	isc_info_sql_owner,
	isc_info_sql_alias,
	isc_info_sql_describe_end
};

static GlobalPtr<RWLock> handleMappingLock;
static GlobalPtr<GenericMap<Pair<NonPooled<FB_API_HANDLE, YService*> > > > services;
static GlobalPtr<GenericMap<Pair<NonPooled<FB_API_HANDLE, YAttachment*> > > > attachments;
static GlobalPtr<GenericMap<Pair<NonPooled<FB_API_HANDLE, YEvents*> > > > events;
static GlobalPtr<GenericMap<Pair<NonPooled<FB_API_HANDLE, YTransaction*> > > > transactions;
static GlobalPtr<GenericMap<Pair<NonPooled<FB_API_HANDLE, YStatement*> > > > statements;
static GlobalPtr<GenericMap<Pair<NonPooled<FB_API_HANDLE, YRequest*> > > > requests;
static GlobalPtr<GenericMap<Pair<NonPooled<FB_API_HANDLE, YBlob*> > > > blobs;
static bool shutdownStarted = false;


//-------------------------------------


template <typename T>
static FB_API_HANDLE makeHandle(GenericMap<Pair<NonPooled<FB_API_HANDLE, T*> > >* map, T* obj)
{
	static ULONG sequenceNumber = 0;

	WriteLockGuard sync(handleMappingLock);
	ULONG handle = 0;

	// Loop until we find an empty handle slot. This is to care of case when counter rolls over.
	do
	{
		// Generate handle number using rolling counter.
		// This way we tend to give out unique handle numbers and closed
		// handles do not appear as valid to our clients.
		handle = ++sequenceNumber;

		// Avoid generating NULL handle when sequence number wraps
		if (!handle)
			handle = ++sequenceNumber;
	} while (map->put(static_cast<FB_API_HANDLE>(handle), obj));

	return static_cast<FB_API_HANDLE>(handle);
}

template <typename T>
static void removeHandle(GenericMap<Pair<NonPooled<FB_API_HANDLE, T*> > >* map, FB_API_HANDLE handle)
{
	WriteLockGuard sync(handleMappingLock);
	bool removed = map->remove(handle);

	fb_assert(removed);
	(void) removed;	// avoid warning in prod build
}

template <typename T>
static RefPtr<T> translateHandle(GlobalPtr<GenericMap<Pair<NonPooled<FB_API_HANDLE, T*> > > >& map,
	FB_API_HANDLE* handle)
{
	ReadLockGuard sync(handleMappingLock);

	T** obj = map->get(*handle);

	if (!obj)
		status_exception::raise(Arg::Gds(T::ERROR_CODE));

	return RefPtr<T>(*obj);
}


//-------------------------------------


namespace
{
	// Status:	Provides correct status vector for operation and init() it.
	class StatusVector : public StackIface<Status>
	{
	public:
		explicit StatusVector(ISC_STATUS* v) throw()
			: localVector(v ? v : localStatus)
		{
			init();
		}

		operator ISC_STATUS*()
		{
			return localVector;
		}

		~StatusVector()
		{
#ifdef DEV_BUILD
			checkStatusVector(localVector);
#endif
		}

		void FB_CARG set(unsigned int length, const ISC_STATUS* value)
		{
			fb_utils::copyStatus(localVector, FB_NELEM(localStatus), value, length);
		}

		void FB_CARG set(const ISC_STATUS* value)
		{
			set(fb_utils::statusLength(value), value);
		}

		void FB_CARG init()
		{
			fb_utils::init_status(localVector);
		}

		const ISC_STATUS* FB_CARG get() const
		{
			return localVector;
		}

		int FB_CARG isSuccess() const
		{
			switch (localVector[1])
			{
				case 0:
				case 100:
				case 101:
					return true;

				default:
					break;
			}

			return false;
		}

#ifdef DEV_BUILD
		// Validate that a status vector looks valid.
		static void checkStatusVector(const ISC_STATUS* status)
		{
#define SV_MSG(x)	\
	do {	\
		gds__log ("%s %d check_status_vector: %s", __FILE__, __LINE__, (x)); BREAKPOINT (__LINE__);	\
	} while (false)

			const ISC_STATUS* s = status;
			if (!s)
			{
				SV_MSG("Invalid status vector");
				return;
			}

			if (*s != isc_arg_gds)
			{
				SV_MSG("Must start with isc_arg_gds");
				return;
			}

			// Vector [2] could either end the vector, or start a warning
			// in either case the status vector is a success.
			if (s[1] == FB_SUCCESS && s[2] != isc_arg_end && s[2] != isc_arg_gds &&
				s[2] != isc_arg_warning)
			{
				SV_MSG("Bad success vector format");
			}

			ULONG length;

			while (*s != isc_arg_end)
			{
				const ISC_STATUS code = *s++;

				switch (code)
				{
					case isc_arg_warning:
					case isc_arg_gds:
						// The next element must either be 0 (indicating no error) or a
						// valid isc error message, let's check.
						if (*s && (*s & ISC_MASK) != ISC_MASK)
						{
							if (code == isc_arg_warning)
								SV_MSG("warning code not a valid ISC message");
							else
								SV_MSG("error code not a valid ISC message");
						}

						// If the error code is valid, then I better be able to retrieve a
						// proper facility code from it ... let's find out.
						if (*s && (*s & ISC_MASK) == ISC_MASK)
						{
							bool found = false;

							const _facilities* facs = facilities;
							const int fac_code = GET_FACILITY(*s);

							while (facs->facility)
							{
								if (facs->fac_code == fac_code)
								{
									found = true;
									break;
								}

								++facs;
							}

							if (!found)
							{
								if (code == isc_arg_warning)
									SV_MSG("warning code does not contain a valid facility");
								else
									SV_MSG("error code does not contain a valid facility");
							}
						}

						++s;
						break;

					case isc_arg_interpreted:
					case isc_arg_string:
					case isc_arg_sql_state:
						length = strlen((const char*) *s);
						// This check is heuristic, not deterministic.
						if (length > 1024 - 1)
							SV_MSG("suspect length value");
						if (*((const UCHAR*) *s) == 0xCB)
							SV_MSG("string in freed memory");
						++s;
						break;

					case isc_arg_cstring:
						length = (ULONG) *s;
						++s;
						// This check is heuristic, not deterministic.
						// Note: This can never happen anyway, as the length is coming from a byte value.
						if (length > 1024 - 1)
							SV_MSG("suspect length value");
						if (*((const UCHAR*) *s) == 0xCB)
							SV_MSG("string in freed memory");
						++s;
						break;

					case isc_arg_number:
					case isc_arg_vms:
					case isc_arg_unix:
					case isc_arg_win32:
						++s;
						break;

					default:
						SV_MSG("invalid status code");
						return;
				}

				if (s - status >= ISC_STATUS_LENGTH)
					SV_MSG("vector too long");
			}

#undef SV_MSG
		}
#endif

	private:
		ISC_STATUS_ARRAY localStatus;
		ISC_STATUS* localVector;
	};

	class ShutChain : public GlobalStorage
	{
	private:
		ShutChain(ShutChain* link, FB_SHUTDOWN_CALLBACK cb, const int m, void* a)
			: next(link),
			  callBack(cb),
			  mask(m),
			  arg(a)
		{
		}

	private:
		static ShutChain* list;
		static GlobalPtr<Mutex> shutdownCallbackMutex;

		ShutChain* next;
		FB_SHUTDOWN_CALLBACK callBack;
		int mask;
		void* arg;

	public:
		static void add(FB_SHUTDOWN_CALLBACK cb, const int m, void* a)
		{
			MutexLockGuard guard(shutdownCallbackMutex);

			for (ShutChain* chain = list; chain; chain = chain->next)
			{
				if (chain->callBack == cb && chain->arg == a)
				{
					if (m)
						chain->mask |= m;
					else
						chain->mask = 0;

					return;
				}
			}

			list = new ShutChain(list, cb, m, a);
		}

		static int run(const int m, const int reason)
		{
			int rc = FB_SUCCESS;
			MutexLockGuard guard(shutdownCallbackMutex);

			for (ShutChain* chain = list; chain; chain = chain->next)
			{
				if ((chain->mask & m) && (chain->callBack(reason, m, chain->arg) != FB_SUCCESS))
					rc = FB_FAILURE;
			}

			return rc;
		}
	};

	ShutChain* ShutChain::list = NULL;
	GlobalPtr<Mutex> ShutChain::shutdownCallbackMutex;

#ifdef UNIX
	int killed;
	bool procInt, procTerm;
	SignalSafeSemaphore* shutdownSemaphore = NULL;

	const int SHUTDOWN_TIMEOUT = 5000;	// 5 sec

	void atExitShutdown()
	{
		if (!shutdownStarted)	// static not protected by mutex flag is OK here - works in dtors
			fb_shutdown(SHUTDOWN_TIMEOUT, fb_shutrsn_exit_called);
	}

	THREAD_ENTRY_DECLARE shutdownThread(THREAD_ENTRY_PARAM)
	{
		for (;;)
		{
			killed = 0;
			try
			{
				if (shutdownSemaphore)
					shutdownSemaphore->enter();
			}
			catch (status_exception& e)
			{
				TEXT buffer[BUFFER_LARGE];
				const ISC_STATUS* vector = e.value();

				if (! (vector && fb_interpret(buffer, sizeof(buffer), &vector)))
					strcpy(buffer, "Unknown failure in shutdown thread in shutSem:enter()");

				gds__log("%s", buffer);
				exit(0);
			}

			if (!killed)
				break;

			// perform shutdown
			if (fb_shutdown(SHUTDOWN_TIMEOUT, fb_shutrsn_signal) == FB_SUCCESS)
			{
				InstanceControl::registerShutdown(0);
				break;	//exit(0);
			}
		}

		return 0;
	}

	void handler(int sig)
	{
		if (!killed && shutdownSemaphore)
		{
			killed = sig;
			shutdownSemaphore->release();
		}
	}

	void handlerInt(void*)
	{
		handler(SIGINT);
	}

	void handlerTerm(void*)
	{
		handler(SIGTERM);
	}

	class CtrlCHandler
	{
	public:
		SignalSafeSemaphore semaphore;

		explicit CtrlCHandler(MemoryPool&)
		{
			InstanceControl::registerShutdown(atExitShutdown);

			Thread::start(shutdownThread, 0, 0, &handle);

			procInt = ISC_signal(SIGINT, handlerInt, 0);
			procTerm = ISC_signal(SIGTERM, handlerTerm, 0);
			shutdownSemaphore = &semaphore;
		}

		~CtrlCHandler()
		{
			ISC_signal_cancel(SIGINT, handlerInt, 0);
			ISC_signal_cancel(SIGTERM, handlerTerm, 0);

			if (!killed)
			{
				// Must be done to let shutdownThread close.
				shutdownSemaphore->release();
				shutdownSemaphore = NULL;
				Thread::waitForCompletion(handle);
			}
		}
	private:
		Thread::Handle handle;
	};
#endif // UNIX

	class BuiltinRegister
	{
	public:
		static void init()
		{
			PluginInterface pi;
			Remote::registerRedirector(pi);
		}

		static void cleanup()
		{
		}
	};

	class NoEntrypoint
	{
	public:
		virtual void FB_CARG noEntrypoint(Status* s)
		{
			s->set(Arg::Gds(isc_unavailable).value());
		}
	};

	class YObject
	{
	public:
		YObject()
			: handle(NULL)
		{
		}

	public:
		FB_API_HANDLE handle;
	};

	template <typename T, typename CleanupRoutine>	// T = YAttachment or YTransaction
	class CleanupCallbackImpl : public CleanupCallback
	{
	public:
		CleanupCallbackImpl(T* aObject, CleanupRoutine* aRoutine, void* aArg)
			: object(aObject),
			  routine(aRoutine),
			  arg(aArg)
		{
		}

		virtual void FB_CARG cleanupCallbackFunction()
		{
			call(routine);
			delete this;
		}

	private:
		void call(AttachmentCleanupRoutine* routine)
		{
			routine(&object->handle, arg);
		}

		void call(TransactionCleanupRoutine* routine)
		{
			routine(object->handle, arg);
		}

	private:
		T* object;
		CleanupRoutine* routine;
		void* arg;
	};

	template <typename T>
	class HandleArray
	{
	public:
		HandleArray(MemoryPool& pool)
			: array(pool)
		{
		}

		void add(T* obj)
		{
			MutexLockGuard guard(mtx);

			array.add(obj);
		}

		void remove(T* obj)
		{
			MutexLockGuard guard(mtx);
			size_t pos;

			if (array.find(obj, pos))
				array.remove(pos);
		}

		void destroy()
		{
			MutexLockGuard guard(mtx);
			size_t i;

			while ((i = array.getCount()) > 0)
				array[i - 1]->destroy();
		}

	private:
		Mutex mtx;
		SortedArray<T*> array;
	};

	template <typename C, int V>
	class YHelper : public StdPlugin<C, V>, public YObject
	{
	public:
		YHelper()
		{
			this->addRef();
		}
	};

	class YEvents : public YHelper<IEvents, FB_I_EVENTS_VERSION>
	{
	public:
		static const ISC_STATUS ERROR_CODE = isc_bad_events_handle;

		YEvents(YAttachment* aAttachment, IEvents* aNext, EventCallback* aCallback);

		virtual ~YEvents()
		{
			if (deleteCallback)
				delete callback;
		}

		void destroy();

		virtual int FB_CARG release()
		{
			if (--refCounter == 0)
			{
				if (next)
				{
					next->release();
					destroy();
				}

				delete this;
				return 0;
			}

			return 1;
		}

		virtual void FB_CARG cancel(Status* status);

	public:
		YAttachment* attachment;
		IEvents* next;
		EventCallback* callback;
		bool deleteCallback;
	};

	class YRequest : public YHelper<IRequest, FB_I_STATEMENT_VERSION>
	{
	public:
		static const ISC_STATUS ERROR_CODE = isc_bad_req_handle;

		YRequest(YAttachment* aAttachment, IRequest* aNext);

		void destroy();

		virtual int FB_CARG release()
		{
			if (--refCounter == 0)
			{
				if (next)
				{
					next->release();
					destroy();
				}

				delete this;
				return 0;
			}

			return 1;
		}

		virtual void FB_CARG receive(Status* status, int level, unsigned int msgType,
			unsigned int length, unsigned char* message);
		virtual void FB_CARG send(Status* status, int level, unsigned int msgType,
			unsigned int length, const unsigned char* message);
		virtual void FB_CARG getInfo(Status* status, int level, unsigned int itemsLength,
			const unsigned char* items, unsigned int bufferLength, unsigned char* buffer);
		virtual void FB_CARG start(Status* status, ITransaction* transaction, int level);
		virtual void FB_CARG startAndSend(Status* status, ITransaction* transaction, int level,
			unsigned int msgType, unsigned int length, const unsigned char* message);
		virtual void FB_CARG unwind(Status* status, int level);
		virtual void FB_CARG free(Status* status);

	public:
		YAttachment* attachment;
		IRequest* next;
		FB_API_HANDLE* userHandle;
	};

	class YTransaction : public YHelper<ITransaction, FB_I_TRANSACTION_VERSION>
	{
	public:
		static const ISC_STATUS ERROR_CODE = isc_bad_trans_handle;

		YTransaction(YAttachment* aAttachment, ITransaction* aNext);
		explicit YTransaction(const Array<YTransaction*>& subTransactions);

		static ITransaction* getNext(ITransaction* transaction, YAttachment* attachment)
		{
			if (!transaction)
				badHandle(isc_bad_trans_handle);

			YTransaction* yTrans = static_cast<YTransaction*>(transaction);
			ITransaction* nextTrans = yTrans->next;

			for (YTransaction* i = yTrans->sub; !nextTrans && i; i = i->sub)
			{
				if (i->attachment == attachment)
					nextTrans = i->next;
			}

			if (!nextTrans)
				badHandle(isc_bad_trans_handle);

			return nextTrans;
		}

		void destroy();

		virtual int FB_CARG release()
		{
			if (--refCounter == 0)
			{
				if (next)
				{
					next->release();
					destroy();
				}

				delete this;
				return 0;
			}

			return 1;
		}

		virtual void FB_CARG getInfo(Status* status, unsigned int itemsLength,
			const unsigned char* items, unsigned int bufferLength, unsigned char* buffer);
		virtual void FB_CARG prepare(Status* status, unsigned int msgLength,
			const unsigned char* message);
		virtual void FB_CARG commit(Status* status);
		virtual void FB_CARG commitRetaining(Status* status);
		virtual void FB_CARG rollback(Status* status);
		virtual void FB_CARG rollbackRetaining(Status* status);
		virtual void FB_CARG disconnect(Status* status);

		void addCleanupHandler(Status* status, CleanupCallback* callback);

	private:
		bool prepareCommit(Status* status);
		void buildPrepareInfo(Status* status, UCHAR** ptr);

	public:
		YAttachment* attachment;
		ITransaction* next;
		YTransaction* sub;
		bool limbo;
		HandleArray<YBlob> childBlobs;
		Array<CleanupCallback*> cleanupHandlers;
	};

	class YBlob : public YHelper<IBlob, FB_I_BLOB_VERSION>
	{
	public:
		static const ISC_STATUS ERROR_CODE = isc_bad_segstr_handle;

		YBlob(YAttachment* aAttachment, YTransaction* aTransaction, IBlob* aNext);

		void destroy();

		virtual int FB_CARG release()
		{
			if (--refCounter == 0)
			{
				if (next)
				{
					next->release();
					destroy();
				}

				delete this;
				return 0;
			}

			return 1;
		}

		virtual void FB_CARG getInfo(Status* status, unsigned int itemsLength,
			const unsigned char* items, unsigned int bufferLength, unsigned char* buffer);
		virtual unsigned int FB_CARG getSegment(Status* status, unsigned int length,
			unsigned char* buffer);
		virtual void FB_CARG putSegment(Status* status, unsigned int length,
			const unsigned char* buffer);
		virtual void FB_CARG cancel(Status* status);
		virtual void FB_CARG close(Status* status);
		virtual int FB_CARG seek(Status* status, int mode, int offset);

	public:
		YAttachment* attachment;
		YTransaction* transaction;
		IBlob* next;
	};

	class YStatement : public YHelper<IStatement, FB_I_STATEMENT_VERSION>
	{
	public:
		static const ISC_STATUS ERROR_CODE = isc_bad_stmt_handle;

		YStatement(YAttachment* aAttachment, IStatement* aNext);

		void destroy();

		virtual int FB_CARG release()
		{
			if (--refCounter == 0)
			{
				if (next)
				{
					next->release();
					destroy();
				}

				delete this;
				return 0;
			}

			return 1;
		}

		void checkPrepared() const
		{
			if (!prepared)
				status_exception::raise(Arg::Gds(isc_unprepared_stmt));
		}

		virtual YStatement* FB_CARG prepare(Status* status, ITransaction* transaction,
			unsigned int stmtLength, const char* sqlStmt, unsigned int dialect,
			unsigned int itemLength, const unsigned char* items,
			unsigned int bufferLength, unsigned char* buffer);
		virtual void FB_CARG getInfo(Status* status, unsigned int itemsLength,
			const unsigned char* items, unsigned int bufferLength, unsigned char* buffer);
		virtual void FB_CARG setCursor(Status* status, const char* name, unsigned int type);
		virtual YTransaction* FB_CARG execute(Status* status, ITransaction* transaction,
			unsigned int inMsgType, const MessageBuffer* inMsgBuffer,
			const MessageBuffer* outMsgBuffer);
		virtual int FB_CARG fetch(Status* status, const MessageBuffer* msgBuffer);
		virtual void FB_CARG insert(Status* status, const MessageBuffer* msgBuffer);
		virtual void FB_CARG free(Status* status, unsigned int option);

	public:
		YAttachment* attachment;
		IStatement* next;
		FB_API_HANDLE* userHandle;
		bool prepared;
		sqlda_sup das;
	};

	class YAttachment : public YHelper<IAttachment, FB_I_ATTACHMENT_VERSION>
	{
	public:
		static const ISC_STATUS ERROR_CODE = isc_bad_db_handle;

		explicit YAttachment(PProvider* aProvider, IAttachment* aNext, const PathName& aDbPath)
			: provider(aProvider),
			  next(aNext),
			  dbPath(getPool(), aDbPath),
			  childBlobs(getPool()),
			  childEvents(getPool()),
			  childRequests(getPool()),
			  childStatements(getPool()),
			  childTransactions(getPool()),
			  cleanupHandlers(getPool()),
			  enterCount(0)
		{
			provider->addRef();
			handle = makeHandle(&attachments, this);
		}

		virtual ~YAttachment()
		{
			PluginInterface()->releasePlugin(provider);
		}

		void destroy();
		void buildPrepareInfo(UCHAR** ptr);

		virtual int FB_CARG release()
		{
			if (--refCounter == 0)
			{
				if (next)
				{
					next->release();
					destroy();
				}

				delete this;
				return 0;
			}

			return 1;
		}

		virtual void FB_CARG getInfo(Status* status, unsigned int itemsLength,
			const unsigned char* items, unsigned int bufferLength, unsigned char* buffer);
		virtual YTransaction* FB_CARG startTransaction(Status* status, unsigned int tpbLength,
			const unsigned char* tpb, FB_API_HANDLE api);
		virtual YTransaction* FB_CARG reconnectTransaction(Status* status, unsigned int length,
			const unsigned char* id);
		virtual YStatement* FB_CARG allocateStatement(Status* status);
		virtual YRequest* FB_CARG compileRequest(Status* status, unsigned int blrLength,
			const unsigned char* blr);
		virtual void FB_CARG transactRequest(Status* status, ITransaction* transaction,
			unsigned int blrLength, const unsigned char* blr, unsigned int inMsgLength,
			const unsigned char* inMsg, unsigned int outMsgLength, unsigned char* outMsg);
		virtual YBlob* FB_CARG createBlob(Status* status, ITransaction* transaction, ISC_QUAD* id,
			unsigned int bpbLength, const unsigned char* bpb);
		virtual YBlob* FB_CARG openBlob(Status* status, ITransaction* transaction, ISC_QUAD* id,
			unsigned int bpbLength, const unsigned char* bpb);
		virtual int FB_CARG getSlice(Status* status, ITransaction* transaction, ISC_QUAD* id,
			unsigned int sdlLength, const unsigned char* sdl, unsigned int paramLength,
			const unsigned char* param, int sliceLength, unsigned char* slice);
		virtual void FB_CARG putSlice(Status* status, ITransaction* transaction, ISC_QUAD* id,
			unsigned int sdlLength, const unsigned char* sdl, unsigned int paramLength,
			const unsigned char* param, int sliceLength, unsigned char* slice);
		virtual void FB_CARG ddl(Status* status, ITransaction* transaction, unsigned int length,
			const unsigned char* dyn);
		virtual YTransaction* FB_CARG execute(Status* status, ITransaction* transaction,
			unsigned int length, const char* string, unsigned int dialect, unsigned int inMsgType,
			const MessageBuffer* inMsgBuffer, const MessageBuffer* outMsgBuffer);
		virtual YEvents* FB_CARG queEvents(Status* status, EventCallback* callback,
			unsigned int length, const unsigned char* eventsData);
		virtual void FB_CARG cancelOperation(Status* status, int option);
		virtual void FB_CARG ping(Status* status);
		virtual void FB_CARG detach(Status* status);
		virtual void FB_CARG drop(Status* status);

		void addCleanupHandler(Status* status, CleanupCallback* callback);

	public:
		PProvider* provider;
		IAttachment* next;
		PathName dbPath;
		HandleArray<YBlob> childBlobs;
		HandleArray<YEvents> childEvents;
		HandleArray<YRequest> childRequests;
		HandleArray<YStatement> childStatements;
		HandleArray<YTransaction> childTransactions;
		Array<CleanupCallback*> cleanupHandlers;
		int enterCount;
		Mutex enterMutex;
		StatusHolder savedStatus;	// Do not use raise() method of this class in yValve.
	};

	class YService : public YHelper<IService, FB_I_SERVICE_VERSION>
	{
	public:
		static const ISC_STATUS ERROR_CODE = isc_bad_svc_handle;

		explicit YService(PProvider* aProvider, IService* aNext)
			: provider(aProvider),
			  next(aNext)
		{
			provider->addRef();
			handle = makeHandle(&services, this);
		}

		virtual ~YService()
		{
			PluginInterface()->releasePlugin(provider);
		}

		void destroy();

		virtual int FB_CARG release()
		{
			if (--refCounter == 0)
			{
				if (next)
				{
					next->release();
					destroy();
				}

				delete this;
				return 0;
			}

			return 1;
		}

		virtual void FB_CARG detach(Status* status);
		virtual void FB_CARG query(Status* status,
			unsigned int sendLength, const unsigned char* sendItems,
			unsigned int receiveLength, const unsigned char* receiveItems,
			unsigned int bufferLength, unsigned char* buffer);
		virtual void FB_CARG start(Status* status,
			unsigned int spbLength, const unsigned char* spb);

	public:
		PProvider* provider;
		IService* next;
	};

	class Dispatcher : public StdPlugin<PProvider, FB_P_PROVIDER_VERSION>
	{
	public:
		virtual void FB_CARG attachDatabase(Status* status, IAttachment** attachment,
			FB_API_HANDLE api, const char* filename, unsigned int dpbLength, const unsigned char* dpb);
		virtual void FB_CARG createDatabase(Status* status, IAttachment** ptr,
			FB_API_HANDLE api, const char* filename, unsigned int dpbLength, const unsigned char* dpb);
		virtual YService* FB_CARG attachServiceManager(Status* status, const char* serviceName,
			unsigned int spbLength, const unsigned char* spb);
		virtual void FB_CARG shutdown(Status* status, unsigned int timeout, const int reason);

		virtual int FB_CARG release()
		{
			if (--refCounter == 0)
			{
				delete this;
				return 0;
			}

			return 1;
		}
	};

	Dispatcher dispatcher;

	class YEntry : public FpeControl	//// TODO: move FpeControl to the engine
	{
	public:
		YEntry(Status* aStatus, YAttachment* aAttachment, bool checkAttachment = true)
			: attachment(aAttachment)
		{
			aStatus->init();

			if (attachment && checkAttachment && attachment->savedStatus.getError())
				status_exception::raise(attachment->savedStatus.value());

			init();
		}

		YEntry(Status* aStatus, YService* aService)
			: attachment(NULL)
		{
			aStatus->init();
			init();
		}

		explicit YEntry(Status* aStatus)
			: attachment(NULL)
		{
			aStatus->init();
			init();
		}

		~YEntry()
		{
			if (attachment)
			{
				MutexLockGuard guard(attachment->enterMutex);
				--attachment->enterCount;
			}
		}

		void init()
		{
#ifdef UNIX
			static GlobalPtr<CtrlCHandler> ctrlCHandler;
#endif // UNIX

			static InitMutex<BuiltinRegister> registerBuiltinPlugins;
			registerBuiltinPlugins.init();

			if (attachment)
			{
				MutexLockGuard guard(attachment->enterMutex);
				++attachment->enterCount;
			}
		}

	private:
		YEntry(const YEntry&);	// prohibit copy constructor

	private:
		RefPtr<YAttachment> attachment;
	};
}	// namespace

PProvider* Why::dispatcherPtr = &dispatcher;


struct TEB
{
	FB_API_HANDLE* teb_database;
	int teb_tpb_length;
	const UCHAR* teb_tpb;
};


//-------------------------------------


static void badHandle(ISC_STATUS code)
{
	status_exception::raise(Arg::Gds(code));
}

static bool isNetworkError(const Status* status)
{
	ISC_STATUS code = status->get()[1];
	return code == isc_network_error || code == isc_net_write_err || code == isc_net_read_err;
}

static void nullCheck(const FB_API_HANDLE* ptr, ISC_STATUS code)
{
	// This function is called for incoming API handles, proposed to be created by the call.
	if (!ptr || *ptr)
		badHandle(code);
}

// This is need because there are cases where the memory allocated for strings in the status vector
// is freed prior to surfacing them to the user. This is an attempt to save off 1 string to surface
// to the user. Any other strings will be set to a standard <Unknown> string.
static void saveErrorString(ISC_STATUS* status)
{
	const int MAXERRORSTRINGLENGTH = 250;
	static TEXT glbstr1[MAXERRORSTRINGLENGTH];
	static const TEXT glbunknown[10] = "<unknown>";

	fb_assert(status != NULL);

	char* p = glbstr1;
	ULONG len = sizeof(glbstr1) - 1;

	while (*status != isc_arg_end)
	{
		ULONG l;

		switch (*status++)
		{
			case isc_arg_cstring:
				l = (ULONG) *status;
				if (l < len)
				{
					++status;		// Length is unchanged.

					// This strncpy should really be a memcpy.
					strncpy(p, reinterpret_cast<char*>(*status), l);
					*status++ = (ISC_STATUS) p;	// string in static memory
					p += l;
					len -= l;
				}
				else
				{
					*status++ = (ISC_STATUS) strlen(glbunknown);
					*status++ = (ISC_STATUS) glbunknown;
				}
				break;

			case isc_arg_interpreted:
			case isc_arg_string:
			case isc_arg_sql_state:
				l = (ULONG) strlen(reinterpret_cast<char*>(*status)) + 1;
				if (l < len)
				{
					strncpy(p, reinterpret_cast<char*>(*status), l);
					*status++ = (ISC_STATUS) p;	// string in static memory
					p += l;
					len -= l;
				}
				else
					*status++ = (ISC_STATUS) glbunknown;
				break;

			default:
				fb_assert(false);
				// fall into

			case isc_arg_gds:
			case isc_arg_number:
			case isc_arg_vms:
			case isc_arg_unix:
			case isc_arg_win32:
				status++;			// Skip parameter
				break;
		}
	}
}

// Set a prefix to a filename based on the ISC_PATH user variable.
static bool setPath(const PathName& filename, PathName& expandedName)
{
	// Look for the environment variables to tack onto the beginning of the database path.
	PathName pathname;
	if (!fb_utils::readenv("ISC_PATH", pathname))
		return false;

	// If the file already contains a remote node or any path at all forget it.
	for (const char* p = filename.c_str(); *p; p++)
	{
		if (*p == ':' || *p == '/' || *p == '\\')
			return false;
	}

	// concatenate the strings

	expandedName = pathname;

	// CVC: Make the concatenation work if no slash is present.
	char lastChar = expandedName[expandedName.length() - 1];
	if (lastChar != ':' && lastChar != '/' && lastChar != '\\')
		expandedName.append(1, PathUtils::dir_sep);

	expandedName.append(filename);

	return true;
}

// Release some memory.
static void releaseDsqlSupport(sqlda_sup& dasup)
{
	sqlda_sup::dasup_clause* pClauses = dasup.dasup_clauses;

	gds__free(pClauses[DASUP_CLAUSE_bind].dasup_blr);
	gds__free(pClauses[DASUP_CLAUSE_select].dasup_blr);
	gds__free(pClauses[DASUP_CLAUSE_bind].dasup_msg);
	gds__free(pClauses[DASUP_CLAUSE_select].dasup_msg);
	gds__free(pClauses[DASUP_CLAUSE_bind].dasup_info_buf);
	gds__free(pClauses[DASUP_CLAUSE_select].dasup_info_buf);
}

// Calculate size of a buffer that is large enough to store the info items relating to an SQLDA.
static USHORT sqldaBufferSize(USHORT minBufferSize, const XSQLDA* sqlda, USHORT dialect)
{
	USHORT variables;

	// If dialect / 10 == 0, then it has not been combined with the
	// parser version for a prepare statement.  If it has been combined, then
	// the dialect needs to be pulled out to compare to DIALECT_xsqlda

	USHORT sqlDialect = dialect / 10;
	if (sqlDialect == 0)
		sqlDialect = dialect;

	if (!sqlda)
		variables = 0;
	else if (sqlDialect >= DIALECT_xsqlda)
		variables = sqlda->sqln;
	else
		variables = ((SQLDA *) sqlda)->sqln;

	ULONG length = 32 + ULONG(variables) * 172;
	if (length < minBufferSize)
		length = minBufferSize;

	return (USHORT) (length > 65500L ? 65500L : length);
}

// Turn an sql info buffer into an SQLDA. If the info buffer was incomplete, make another request,
// beginning where the previous info call left off.
static void iterativeSqlInfo(ISC_STATUS* userStatus, FB_API_HANDLE* stmtHandle, USHORT itemLength,
	const SCHAR* items, SSHORT bufferLength, SCHAR* buffer, USHORT dialect, XSQLDA* sqlda)
{
	USHORT lastIndex;
	SCHAR newItems[32];

	while (UTLD_parse_sql_info(userStatus, dialect, buffer, sqlda, &lastIndex) && lastIndex)
	{
		char* p = newItems;
		*p++ = isc_info_sql_sqlda_start;
		*p++ = 2;
		*p++ = lastIndex;
		*p++ = lastIndex >> 8;
		fb_assert(p + itemLength <= newItems + sizeof(newItems));
		memcpy(p, items, itemLength);
		p += itemLength;

		if (isc_dsql_sql_info(userStatus, stmtHandle, (SSHORT) (p - newItems), newItems,
				bufferLength, buffer))
		{
			break;
		}
	}
}


// Open or create an existing blob (extended edition).
static ISC_STATUS openOrCreateBlob(ISC_STATUS* userStatus, FB_API_HANDLE* dbHandle,
	FB_API_HANDLE* traHandle, FB_API_HANDLE* blobHandle, ISC_QUAD* blobId,
	USHORT bpbLength, const UCHAR* bpb, bool createFlag)
{
	StatusVector status(userStatus);

	try
	{
		nullCheck(blobHandle, isc_bad_segstr_handle);

		RefPtr<YAttachment> attachment(translateHandle(attachments, dbHandle));
		RefPtr<YTransaction> transaction(translateHandle(transactions, traHandle));

		YBlob* blob = createFlag ?
			attachment->createBlob(&status, transaction, blobId, bpbLength, bpb) :
			attachment->openBlob(&status, transaction, blobId, bpbLength, bpb);

		if (!status.isSuccess())
			return status[1];

		*blobHandle = blob->handle;
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


//-------------------------------------


// Attach a database through the first subsystem that recognizes it.
ISC_STATUS API_ROUTINE isc_attach_database(ISC_STATUS* userStatus, SSHORT fileLength,
	const TEXT* filename, FB_API_HANDLE* publicHandle, SSHORT dpbLength, const SCHAR* dpb)
{
	StatusVector status(userStatus);

	try
	{
		nullCheck(publicHandle, isc_bad_db_handle);

		if (!filename)
			status_exception::raise(Arg::Gds(isc_bad_db_format) << Arg::Str(""));

		PathName pathName(filename, fileLength ? fileLength : strlen(filename));

		IAttachment* attachment = NULL;
		dispatcher.attachDatabase(&status, &attachment, 0, pathName.c_str(), dpbLength,
			reinterpret_cast<const UCHAR*>(dpb));

		if (!status.isSuccess())
			return status[1];

		YAttachment* yAttach = static_cast<YAttachment*>(attachment);
		*publicHandle = yAttach->handle;
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Provide information on blob object.
ISC_STATUS API_ROUTINE isc_blob_info(ISC_STATUS* userStatus, FB_API_HANDLE* blobHandle,
	SSHORT itemLength, const SCHAR* items, SSHORT bufferLength, SCHAR* buffer)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YBlob> blob(translateHandle(blobs, blobHandle));

		blob->getInfo(&status, itemLength, reinterpret_cast<const UCHAR*>(items),
			bufferLength, reinterpret_cast<UCHAR*>(buffer));
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Abort a partially completed blob.
ISC_STATUS API_ROUTINE isc_cancel_blob(ISC_STATUS* userStatus, FB_API_HANDLE* blobHandle)
{
	if (!*blobHandle)
	{
		if (userStatus)
			fb_utils::init_status(userStatus);

		return FB_SUCCESS;
	}

	StatusVector status(userStatus);

	try
	{
		RefPtr<YBlob> blob(translateHandle(blobs, blobHandle));

		blob->cancel(&status);

		if (status.isSuccess())
			*blobHandle = 0;
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Try to cancel an event.
ISC_STATUS API_ROUTINE isc_cancel_events(ISC_STATUS* userStatus, FB_API_HANDLE* handle, SLONG* id)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YAttachment> attachment(translateHandle(attachments, handle));
		// This cast appears awful, but as long as handles are 32-bit entities it's OK.
		RefPtr<YEvents> event(translateHandle(events, (FB_API_HANDLE*) id));

		if (event->attachment != attachment)
			Arg::Gds(isc_bad_events_handle).raise();

		event->cancel(&status);

		if (status.isSuccess())
			*id = 0;
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Try to cancel an operation.
ISC_STATUS API_ROUTINE fb_cancel_operation(ISC_STATUS* userStatus, FB_API_HANDLE* handle,
	USHORT option)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YAttachment> attachment(translateHandle(attachments, handle));
		attachment->cancelOperation(&status, option);
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Close a blob opened either for reading or writing (creation).
ISC_STATUS API_ROUTINE isc_close_blob(ISC_STATUS* userStatus, FB_API_HANDLE* blobHandle)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YBlob> blob(translateHandle(blobs, blobHandle));

		blob->close(&status);

		if (status.isSuccess())
			*blobHandle = 0;
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Commit a transaction.
ISC_STATUS API_ROUTINE isc_commit_transaction(ISC_STATUS* userStatus, FB_API_HANDLE* traHandle)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YTransaction> transaction(translateHandle(transactions, traHandle));
		transaction->commit(&status);

		if (status.isSuccess())
			*traHandle = 0;
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Do a commit retaining.
// N.B., the transaction cleanup handlers are NOT called here since, conceptually, the transaction
// is still running.
ISC_STATUS API_ROUTINE isc_commit_retaining(ISC_STATUS* userStatus, FB_API_HANDLE* traHandle)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YTransaction> transaction(translateHandle(transactions, traHandle));
		transaction->commitRetaining(&status);

		if (status.isSuccess())
			*traHandle = 0;
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


ISC_STATUS API_ROUTINE isc_compile_request(ISC_STATUS* userStatus, FB_API_HANDLE* dbHandle,
	FB_API_HANDLE* reqHandle, USHORT blrLength, const SCHAR* blr)
{
	StatusVector status(userStatus);
	YRequest* request = NULL;

	try
	{
		RefPtr<YAttachment> attachment(translateHandle(attachments, dbHandle));

		nullCheck(reqHandle, isc_bad_stmt_handle);

		request = attachment->compileRequest(&status, blrLength,
			reinterpret_cast<const UCHAR*>(blr));

		if (!status.isSuccess())
			return status[1];

		*reqHandle = request->handle;
	}
	catch (const Exception& e)
	{
		if (request)
		{
			*reqHandle = 0;
			StatusVector temp(NULL);
			request->free(&temp);
		}

		e.stuffException(&status);
	}

	return status[1];
}


ISC_STATUS API_ROUTINE isc_compile_request2(ISC_STATUS* userStatus, FB_API_HANDLE* dbHandle,
	FB_API_HANDLE* reqHandle, USHORT blrLength, const SCHAR* blr)
{
	StatusVector status(userStatus);

	if (isc_compile_request(status, dbHandle, reqHandle, blrLength, blr))
		return status[1];

	try
	{
		RefPtr<YRequest> request(translateHandle(requests, reqHandle));
		request->userHandle = reqHandle;
	}
	catch (const Exception& e)
	{
		StatusVector temp(NULL);
		isc_release_request(temp, reqHandle);

		e.stuffException(&status);
	}

	return status[1];
}


// Open an existing blob.
ISC_STATUS API_ROUTINE isc_create_blob(ISC_STATUS* userStatus, FB_API_HANDLE* dbHandle,
	FB_API_HANDLE* traHandle, FB_API_HANDLE* blobHandle, ISC_QUAD* blobId)
{
	return openOrCreateBlob(userStatus, dbHandle, traHandle, blobHandle, blobId, 0, NULL, true);
}


// Create a new blob.
ISC_STATUS API_ROUTINE isc_create_blob2(ISC_STATUS* userStatus, FB_API_HANDLE* dbHandle,
	FB_API_HANDLE* traHandle, FB_API_HANDLE* blobHandle, ISC_QUAD* blobId,
	SSHORT bpbLength, const UCHAR* bpb)
{
	return openOrCreateBlob(userStatus, dbHandle, traHandle, blobHandle, blobId,
		bpbLength, bpb, true);
}


// Create a nice, squeaky clean database, uncorrupted by user data.
ISC_STATUS API_ROUTINE isc_create_database(ISC_STATUS* userStatus, USHORT fileLength,
	const TEXT* filename, FB_API_HANDLE* publicHandle, SSHORT dpbLength, const UCHAR* dpb,
	USHORT /*db_type*/)
{
	StatusVector status(userStatus);

	try
	{
		nullCheck(publicHandle, isc_bad_db_handle);

		if (!filename)
			status_exception::raise(Arg::Gds(isc_bad_db_format) << Arg::Str(""));

		PathName pathName(filename, fileLength ? fileLength : strlen(filename));

		IAttachment* attachment = NULL;
		dispatcher.createDatabase(&status, &attachment, 0, pathName.c_str(), dpbLength,
			reinterpret_cast<const UCHAR*>(dpb));

		if (!status.isSuccess())
			return status[1];

		YAttachment* yAttach = static_cast<YAttachment*>(attachment);
		*publicHandle = yAttach->handle;
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Register an attachment specific cleanup handler.
ISC_STATUS API_ROUTINE isc_database_cleanup(ISC_STATUS* userStatus, FB_API_HANDLE* handle,
	AttachmentCleanupRoutine* routine, void* arg)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YAttachment> attachment(translateHandle(attachments, handle));

		AutoPtr<CleanupCallbackImpl<YAttachment, AttachmentCleanupRoutine> > callback(
			new CleanupCallbackImpl<YAttachment, AttachmentCleanupRoutine>(attachment, routine, arg));

		attachment->addCleanupHandler(&status, callback);

		if (status.isSuccess())
			callback.release();
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Provide information on database object.
ISC_STATUS API_ROUTINE isc_database_info(ISC_STATUS* userStatus, FB_API_HANDLE* handle,
	SSHORT itemLength, const SCHAR* items, SSHORT bufferLength, SCHAR* buffer)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YAttachment> attachment(translateHandle(attachments, handle));
		attachment->getInfo(&status, itemLength, reinterpret_cast<const UCHAR*>(items),
			bufferLength, reinterpret_cast<UCHAR*>(buffer));
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Do meta-data update.
ISC_STATUS API_ROUTINE isc_ddl(ISC_STATUS* userStatus, FB_API_HANDLE* dbHandle,
	FB_API_HANDLE* traHandle, SSHORT length, const UCHAR* dyn)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YAttachment> attachment(translateHandle(attachments, dbHandle));
		RefPtr<YTransaction> transaction(translateHandle(transactions, traHandle));

		attachment->ddl(&status, transaction, length, dyn);
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Close down an attachment.
ISC_STATUS API_ROUTINE isc_detach_database(ISC_STATUS* userStatus, FB_API_HANDLE* handle)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YAttachment> attachment(translateHandle(attachments, handle));
		attachment->detach(&status);

		if (status.isSuccess())
			*handle = 0;
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Disable access to a specific subsystem. If no subsystem has been explicitly disabled, all are
// available.
int API_ROUTINE gds__disable_subsystem(TEXT* subsystem)
{
	return FALSE;
}


// Close down a database and then purge it.
ISC_STATUS API_ROUTINE isc_drop_database(ISC_STATUS* userStatus, FB_API_HANDLE* handle)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YAttachment> attachment(translateHandle(attachments, handle));
		attachment->drop(&status);

		if (status.isSuccess() || status[1] == isc_drdb_completed_with_errs)
			*handle = 0;
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


ISC_STATUS API_ROUTINE isc_dsql_alloc_statement(ISC_STATUS* userStatus, FB_API_HANDLE* dbHandle,
	FB_API_HANDLE* stmtHandle)
{
	return isc_dsql_allocate_statement(userStatus, dbHandle, stmtHandle);
}


// Allocate a statement handle.
ISC_STATUS API_ROUTINE isc_dsql_alloc_statement2(ISC_STATUS* userStatus, FB_API_HANDLE* dbHandle,
	FB_API_HANDLE* stmtHandle)
{
	StatusVector status(userStatus);

	if (isc_dsql_alloc_statement(status, dbHandle, stmtHandle))
		return status[1];

	try
	{
		RefPtr<YStatement> statement(translateHandle(statements, stmtHandle));
		statement->userHandle = stmtHandle;
	}
	catch (const Exception& e)
	{
		StatusVector temp(NULL);
		isc_dsql_free_statement(temp, stmtHandle, DSQL_drop);

		e.stuffException(&status);
	}

	return status[1];
}


// Allocate a statement handle.
ISC_STATUS API_ROUTINE isc_dsql_allocate_statement(ISC_STATUS* userStatus, FB_API_HANDLE* dbHandle,
	FB_API_HANDLE* stmtHandle)
{
	StatusVector status(userStatus);
	YStatement* statement = NULL;

	try
	{
		RefPtr<YAttachment> attachment(translateHandle(attachments, dbHandle));

		nullCheck(stmtHandle, isc_bad_stmt_handle);

		statement = attachment->allocateStatement(&status);
		if (!status.isSuccess())
			return status[1];

		*stmtHandle = statement->handle;
	}
	catch (const Exception& e)
	{
		if (statement)
		{
			*stmtHandle = 0;
			StatusVector temp(NULL);
			statement->free(&temp, DSQL_drop);
		}

		e.stuffException(&status);
	}

	return status[1];
}


// Describe output parameters for a prepared statement.
ISC_STATUS API_ROUTINE isc_dsql_describe(ISC_STATUS* userStatus, FB_API_HANDLE* stmtHandle,
	USHORT dialect, XSQLDA* sqlda)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YStatement> statement(translateHandle(statements, stmtHandle));

		statement->checkPrepared();
		sqlda_sup::dasup_clause& clause = statement->das.dasup_clauses[DASUP_CLAUSE_select];

		if (clause.dasup_info_len && clause.dasup_info_buf)
		{
			iterativeSqlInfo(status, stmtHandle, sizeof(DESCRIBE_SELECT_INFO), DESCRIBE_SELECT_INFO,
				clause.dasup_info_len, clause.dasup_info_buf, dialect, sqlda);
		}
		else
		{
			HalfStaticArray<SCHAR, DESCRIBE_BUFFER_SIZE> localBuffer;
			const USHORT bufferLen = sqldaBufferSize(DESCRIBE_BUFFER_SIZE, sqlda, dialect);
			SCHAR* buffer = localBuffer.getBuffer(bufferLen);

			if (!isc_dsql_sql_info(status, stmtHandle, sizeof(DESCRIBE_SELECT_INFO),
					DESCRIBE_SELECT_INFO, bufferLen, buffer))
			{
				iterativeSqlInfo(status, stmtHandle, sizeof(DESCRIBE_SELECT_INFO),
					DESCRIBE_SELECT_INFO, bufferLen, buffer, dialect, sqlda);
			}
		}
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Describe input parameters for a prepared statement.
ISC_STATUS API_ROUTINE isc_dsql_describe_bind(ISC_STATUS* userStatus, FB_API_HANDLE* stmtHandle,
	USHORT dialect, XSQLDA* sqlda)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YStatement> statement(translateHandle(statements, stmtHandle));

		sqlda_sup::dasup_clause& clause = statement->das.dasup_clauses[DASUP_CLAUSE_bind];

		if (clause.dasup_info_len && clause.dasup_info_buf)
		{
			iterativeSqlInfo(status, stmtHandle, sizeof(DESCRIBE_BIND_INFO), DESCRIBE_BIND_INFO,
				clause.dasup_info_len, clause.dasup_info_buf, dialect, sqlda);
		}
		else
		{
			HalfStaticArray<SCHAR, DESCRIBE_BUFFER_SIZE> localBuffer;
			const USHORT bufferLen = sqldaBufferSize(DESCRIBE_BUFFER_SIZE, sqlda, dialect);
			SCHAR* buffer = localBuffer.getBuffer(bufferLen);

			if (!isc_dsql_sql_info(status, stmtHandle, sizeof(DESCRIBE_BIND_INFO), DESCRIBE_BIND_INFO,
					bufferLen, buffer))
			{
				iterativeSqlInfo(status, stmtHandle, sizeof(DESCRIBE_BIND_INFO), DESCRIBE_BIND_INFO,
					bufferLen, buffer, dialect, sqlda);
			}
		}
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Execute a non-SELECT dynamic SQL statement.
ISC_STATUS API_ROUTINE isc_dsql_execute(ISC_STATUS* userStatus, FB_API_HANDLE* traHandle,
	FB_API_HANDLE* stmtHandle, USHORT dialect, const XSQLDA* sqlda)
{
	return isc_dsql_execute2(userStatus, traHandle, stmtHandle, dialect, sqlda, NULL);
}


// Execute a non-SELECT dynamic SQL statement.
ISC_STATUS API_ROUTINE isc_dsql_execute2(ISC_STATUS* userStatus, FB_API_HANDLE* traHandle,
	FB_API_HANDLE* stmtHandle, USHORT dialect, const XSQLDA* inSqlda, const XSQLDA* outSqlda)
{
	StatusVector status(userStatus);

	try
	{
		USHORT inBlrLength, inMsgType, inMsgLength, outBlrLength, outMsgType, outMsgLength;

		RefPtr<YStatement> statement(translateHandle(statements, stmtHandle));

		sqlda_sup* dasup = &statement->das;
		statement->checkPrepared();

		if (UTLD_parse_sqlda(status, dasup, &inBlrLength, &inMsgType, &inMsgLength,
				dialect, inSqlda, DASUP_CLAUSE_bind))
		{
			return status[1];
		}

		if (UTLD_parse_sqlda(status, dasup, &outBlrLength, &outMsgType, &outMsgLength,
				dialect, outSqlda, DASUP_CLAUSE_select))
		{
			return status[1];
		}

		if (isc_dsql_execute2_m(status, traHandle, stmtHandle,
				inBlrLength, dasup->dasup_clauses[DASUP_CLAUSE_bind].dasup_blr,
				inMsgType, inMsgLength, dasup->dasup_clauses[DASUP_CLAUSE_bind].dasup_msg,
				outBlrLength, dasup->dasup_clauses[DASUP_CLAUSE_select].dasup_blr,
				outMsgType, outMsgLength, dasup->dasup_clauses[DASUP_CLAUSE_select].dasup_msg))
		{
			return status[1];
		}

		if (UTLD_parse_sqlda(status, dasup, NULL, NULL, NULL, dialect, outSqlda, DASUP_CLAUSE_select))
			return status[1];
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Execute a non-SELECT dynamic SQL statement.
ISC_STATUS API_ROUTINE isc_dsql_execute_m(ISC_STATUS* userStatus, FB_API_HANDLE* traHandle,
	FB_API_HANDLE* stmtHandle, USHORT blrLength, const SCHAR* blr,
	USHORT msgType, USHORT msgLength, SCHAR* msg)
{
	return isc_dsql_execute2_m(userStatus, traHandle, stmtHandle, blrLength, blr,
		msgType, msgLength, msg, 0, NULL, 0, 0, NULL);
}


// Execute a non-SELECT dynamic SQL statement.
ISC_STATUS API_ROUTINE isc_dsql_execute2_m(ISC_STATUS* userStatus, FB_API_HANDLE* traHandle,
	FB_API_HANDLE* stmtHandle, USHORT inBlrLength, const SCHAR* inBlr, USHORT inMsgType,
	USHORT inMsgLength, const SCHAR* inMsg, USHORT outBlrLength,
	SCHAR* outBlr, USHORT /*outMsgType*/, USHORT outMsgLength, SCHAR* outMsg)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YStatement> statement(translateHandle(statements, stmtHandle));
		RefPtr<YTransaction> transaction;

		if (traHandle && *traHandle)
			transaction = translateHandle(transactions, traHandle);

		Message inMessage(inBlrLength, reinterpret_cast<const unsigned char*>(inBlr), inMsgLength);
		MessageBuffer inMsgBuffer(&inMessage, reinterpret_cast<UCHAR*>(const_cast<SCHAR*>(inMsg)));
		Message outMessage(outBlrLength, reinterpret_cast<unsigned char*>(outBlr), outMsgLength);
		MessageBuffer outMsgBuffer(&outMessage, reinterpret_cast<unsigned char*>(outMsg));

		YTransaction* newTrans = statement->execute(&status, transaction, inMsgType,
			&inMsgBuffer, &outMsgBuffer);

		if (status.isSuccess())
		{
			if (transaction && !newTrans)
			{
				transaction->destroy();
				*traHandle = 0;
			}
			else if (!transaction && newTrans)
			{
				if (traHandle)
					*traHandle = newTrans->handle;
			}
		}
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Is this really API function? Where is it declared?
ISC_STATUS API_ROUTINE isc_dsql_exec_immediate(ISC_STATUS* userStatus, FB_API_HANDLE* dbHandle,
	FB_API_HANDLE* traHandle, USHORT stmtLength, const SCHAR* sqlStmt, USHORT dialect,
	const XSQLDA* sqlda)
{
	return isc_dsql_execute_immediate(userStatus, dbHandle, traHandle, stmtLength, sqlStmt,
		dialect, sqlda);
}


// Prepare a statement for execution.
ISC_STATUS API_ROUTINE isc_dsql_execute_immediate(ISC_STATUS* userStatus, FB_API_HANDLE* dbHandle,
	FB_API_HANDLE* traHandle, USHORT stmtLength, const SCHAR* sqlStmt, USHORT dialect,
	const XSQLDA* sqlda)
{
	return isc_dsql_exec_immed2(userStatus, dbHandle, traHandle, stmtLength, sqlStmt,
		dialect, sqlda, NULL);
}


// Prepare a statement for execution.
ISC_STATUS API_ROUTINE isc_dsql_exec_immed2(ISC_STATUS* userStatus, FB_API_HANDLE* dbHandle,
	FB_API_HANDLE* traHandle, USHORT stmtLength, const SCHAR* sqlStmt, USHORT dialect,
	const XSQLDA* inSqlda, const XSQLDA* outSqlda)
{
	StatusVector status(userStatus);
	ISC_STATUS s = 0;
	sqlda_sup dasup;
	memset(&dasup, 0, sizeof(sqlda_sup));

	try
	{
		if (!sqlStmt)
			Arg::Gds(isc_command_end_err).raise();

		USHORT inBlrLength, inMsgType, inMsgLength, outBlrLength, outMsgType, outMsgLength;

		if (UTLD_parse_sqlda(status, &dasup, &inBlrLength, &inMsgType, &inMsgLength,
				dialect, inSqlda, DASUP_CLAUSE_bind))
		{
			return status[1];
		}

		if (UTLD_parse_sqlda(status, &dasup, &outBlrLength, &outMsgType, &outMsgLength,
				dialect, outSqlda, DASUP_CLAUSE_select))
		{
			return status[1];
		}

		s = isc_dsql_exec_immed2_m(status, dbHandle, traHandle, stmtLength, sqlStmt, dialect,
				inBlrLength, dasup.dasup_clauses[DASUP_CLAUSE_bind].dasup_blr,
				inMsgType, inMsgLength, dasup.dasup_clauses[DASUP_CLAUSE_bind].dasup_msg,
				outBlrLength, dasup.dasup_clauses[DASUP_CLAUSE_select].dasup_blr,
				outMsgType, outMsgLength, dasup.dasup_clauses[DASUP_CLAUSE_select].dasup_msg);

		if (!s)
		{
			s =	UTLD_parse_sqlda(status, &dasup, NULL, NULL, NULL, dialect,
					outSqlda, DASUP_CLAUSE_select);
		}
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
		s = status[1];
	}

	releaseDsqlSupport(dasup);
	return s;
}


// Is this really API function? Where is it declared?
ISC_STATUS API_ROUTINE isc_dsql_exec_immediate_m(ISC_STATUS* userStatus, FB_API_HANDLE* dbHandle,
	FB_API_HANDLE* traHandle, USHORT stmtLength, const SCHAR* sqlStmt, USHORT dialect,
	USHORT blrLength, USHORT msgType, USHORT msgLength, SCHAR* blr, SCHAR* msg)
{
	return isc_dsql_execute_immediate_m(userStatus, dbHandle, traHandle, stmtLength, sqlStmt,
		dialect, blrLength, blr, msgType, msgLength, msg);
}


// Prepare a statement for execution.
ISC_STATUS API_ROUTINE isc_dsql_execute_immediate_m(ISC_STATUS* userStatus, FB_API_HANDLE* dbHandle,
	FB_API_HANDLE* traHandle, USHORT stmtLength, const SCHAR* sqlStmt, USHORT dialect,
	USHORT blrLength, SCHAR* blr, USHORT msgType, USHORT msgLength, SCHAR* msg)
{
	return isc_dsql_exec_immed2_m(userStatus, dbHandle, traHandle, stmtLength, sqlStmt, dialect,
		blrLength, blr, msgType, msgLength, msg, 0, NULL, 0, 0, NULL);
}


// Prepare a statement for execution.
ISC_STATUS API_ROUTINE isc_dsql_exec_immed2_m(ISC_STATUS* userStatus, FB_API_HANDLE* dbHandle,
	FB_API_HANDLE* traHandle, USHORT stmtLength, const SCHAR* sqlStmt, USHORT dialect,
	USHORT inBlrLength, SCHAR* inBlr, USHORT inMsgType, USHORT inMsgLength, const SCHAR* inMsg,
	USHORT outBlrLength, SCHAR* outBlr, USHORT outMsgType, USHORT outMsgLength, SCHAR* outMsg)
{
	StatusVector status(userStatus);

	bool stmtEaten;

	if (PREPARSE_execute(status, dbHandle, traHandle, stmtLength, sqlStmt, &stmtEaten, dialect))
	{
		if (status[1])
			return status[1];

		ISC_STATUS_ARRAY tempStatus;
		FB_API_HANDLE crdbTransHandle = 0;

		if (isc_start_transaction(status, &crdbTransHandle, 1, dbHandle, 0, 0))
		{
			saveErrorString(status);
			isc_drop_database(tempStatus, dbHandle);
			*dbHandle = 0;
			return status[1];
		}

		bool v3Error = false;

		if (!stmtEaten)
		{
			// Check if against < 4.0 database.

			const SCHAR ch = isc_info_base_level;
			SCHAR buffer[16];
			if (!isc_database_info(status, dbHandle, 1, &ch, sizeof(buffer), buffer))
			{
				if (buffer[0] != isc_info_base_level || buffer[4] > 3)
				{
					isc_dsql_exec_immed3_m(status, dbHandle, &crdbTransHandle,
						stmtLength, sqlStmt, dialect,
						inBlrLength, inBlr, inMsgType, inMsgLength, inMsg,
						outBlrLength, outBlr, outMsgType, outMsgLength, outMsg);
				}
				else
					v3Error = true;
			}
		}

		if (status[1])
		{
			isc_rollback_transaction(tempStatus, &crdbTransHandle);
			saveErrorString(status);
			isc_drop_database(tempStatus, dbHandle);
			*dbHandle = 0;
			return status[1];
		}

		if (isc_commit_transaction(status, &crdbTransHandle))
		{
			isc_rollback_transaction(tempStatus, &crdbTransHandle);
			saveErrorString(status);
			isc_drop_database(tempStatus, dbHandle);
			*dbHandle = 0;
			return status[1];
		}

		if (v3Error)
		{
			Arg::Gds(isc_srvr_version_too_old).copyTo(status);
			return status[1];
		}

		return status[1];
	}

	return isc_dsql_exec_immed3_m(userStatus, dbHandle, traHandle, stmtLength, sqlStmt, dialect,
		inBlrLength, inBlr, inMsgType, inMsgLength, inMsg,
		outBlrLength, outBlr, outMsgType, outMsgLength, outMsg);
}


// Prepare a statement for execution.
ISC_STATUS API_ROUTINE isc_dsql_exec_immed3_m(ISC_STATUS* userStatus, FB_API_HANDLE* dbHandle,
	FB_API_HANDLE* traHandle, USHORT stmtLength, const SCHAR* sqlStmt, USHORT dialect,
	USHORT inBlrLength, const SCHAR* inBlr, USHORT inMsgType, USHORT inMsgLength, const SCHAR* inMsg,
	USHORT outBlrLength, SCHAR* outBlr, USHORT /*outMsgType*/, USHORT outMsgLength, SCHAR* outMsg)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YAttachment> attachment(translateHandle(attachments, dbHandle));
		RefPtr<YTransaction> transaction;

		if (traHandle && *traHandle)
			transaction = translateHandle(transactions, traHandle);

		Message inMessage(inBlrLength, reinterpret_cast<const unsigned char*>(inBlr), inMsgLength);
		MessageBuffer inMsgBuffer(&inMessage, reinterpret_cast<UCHAR*>(const_cast<SCHAR*>(inMsg)));
		Message outMessage(outBlrLength, reinterpret_cast<unsigned char*>(outBlr), outMsgLength);
		MessageBuffer outMsgBuffer(&outMessage, reinterpret_cast<unsigned char*>(outMsg));

		YTransaction* newTrans = attachment->execute(&status, transaction, stmtLength, sqlStmt,
			dialect, inMsgType, &inMsgBuffer, &outMsgBuffer);

		if (status.isSuccess())
		{
			if (transaction && !newTrans)
			{
				transaction->destroy();
				*traHandle = 0;
			}
			else if (!transaction && newTrans)
			{
				if (traHandle)
					*traHandle = newTrans->handle;
			}
		}
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Fetch next record from a dynamic SQL cursor
ISC_STATUS API_ROUTINE isc_dsql_fetch(ISC_STATUS* userStatus, FB_API_HANDLE* stmtHandle,
	USHORT dialect, const XSQLDA* sqlda)
{
	StatusVector status(userStatus);

	try
	{
		if (!sqlda)
			status_exception::raise(Arg::Gds(isc_dsql_sqlda_err));

		RefPtr<YStatement> statement(translateHandle(statements, stmtHandle));

		statement->checkPrepared();
		sqlda_sup& dasup = statement->das;

		USHORT blrLength, msgType, msgLength;

		if (UTLD_parse_sqlda(status, &dasup, &blrLength, &msgType, &msgLength,
				dialect, sqlda, DASUP_CLAUSE_select))
		{
			return status[1];
		}

		ISC_STATUS s = isc_dsql_fetch_m(status, stmtHandle,
			blrLength, dasup.dasup_clauses[DASUP_CLAUSE_select].dasup_blr,
			0, msgLength, dasup.dasup_clauses[DASUP_CLAUSE_select].dasup_msg);

		if (s && s != 101)
			return s;

		if (UTLD_parse_sqlda(status, &dasup, NULL, NULL, NULL, dialect, sqlda, DASUP_CLAUSE_select))
			return status[1];
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Fetch next record from a dynamic SQL cursor
ISC_STATUS API_ROUTINE isc_dsql_fetch_m(ISC_STATUS* userStatus, FB_API_HANDLE* stmtHandle,
	USHORT blrLength, SCHAR* blr, USHORT /*msgType*/, USHORT msgLength, SCHAR* msg)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YStatement> statement(translateHandle(statements, stmtHandle));

		Message message(blrLength, reinterpret_cast<UCHAR*>(blr), msgLength);
		MessageBuffer msgBuffer(&message, reinterpret_cast<UCHAR*>(msg));

		int s = statement->fetch(&status, &msgBuffer);

		if (s == 100 || s == 101)
			return s;
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Release request for an sql statement
ISC_STATUS API_ROUTINE isc_dsql_free_statement(ISC_STATUS* userStatus, FB_API_HANDLE* stmtHandle,
	USHORT option)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YStatement> statement(translateHandle(statements, stmtHandle));
		statement->free(&status, option);

		if (status.isSuccess() && (option & DSQL_drop))
			*stmtHandle = 0;
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Insert next record into a dynamic SQL cursor
ISC_STATUS API_ROUTINE isc_dsql_insert(ISC_STATUS* userStatus, FB_API_HANDLE* stmtHandle,
	USHORT dialect, XSQLDA* sqlda)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YStatement> statement(translateHandle(statements, stmtHandle));

		statement->checkPrepared();

		sqlda_sup& dasup = statement->das;
		USHORT blrLength, msgType, msgLength;

		if (UTLD_parse_sqlda(status, &dasup, &blrLength, &msgType, &msgLength,
				dialect, sqlda, DASUP_CLAUSE_bind))
		{
			return status[1];
		}

		return isc_dsql_insert_m(status, stmtHandle, blrLength,
			dasup.dasup_clauses[DASUP_CLAUSE_bind].dasup_blr, 0, msgLength,
			dasup.dasup_clauses[DASUP_CLAUSE_bind]. dasup_msg);
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Insert next record into a dynamic SQL cursor
ISC_STATUS API_ROUTINE isc_dsql_insert_m(ISC_STATUS* userStatus, FB_API_HANDLE* stmtHandle,
	USHORT blrLength, const SCHAR* blr, USHORT /*msgType*/, USHORT msgLength, const SCHAR* msg)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YStatement> statement(translateHandle(statements, stmtHandle));

		Message message(blrLength, reinterpret_cast<const unsigned char*>(blr), msgLength);
		MessageBuffer msgBuffer(&message, reinterpret_cast<UCHAR*>(const_cast<SCHAR*>(msg)));

		statement->insert(&status, &msgBuffer);
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Prepare a statement for execution.
ISC_STATUS API_ROUTINE isc_dsql_prepare(ISC_STATUS* userStatus, FB_API_HANDLE* traHandle,
	FB_API_HANDLE* stmtHandle, USHORT stmtLength, const SCHAR* sqlStmt, USHORT dialect,
	XSQLDA* sqlda)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YStatement> statement(translateHandle(statements, stmtHandle));
		sqlda_sup& dasup = statement->das;

		const USHORT bufferLen = sqldaBufferSize(PREPARE_BUFFER_SIZE, sqlda, dialect);
		Array<SCHAR> dbPrepareBuffer;
		SCHAR* const buffer = dbPrepareBuffer.getBuffer(bufferLen);

		if (!isc_dsql_prepare_m(status, traHandle, stmtHandle, stmtLength, sqlStmt, dialect,
				sizeof(SQL_PREPARE_INFO2), SQL_PREPARE_INFO2, bufferLen, buffer))
		{
			statement->prepared = false;
			releaseDsqlSupport(dasup);
			memset(&dasup, 0, sizeof(dasup));

			dasup.dasup_dialect = dialect;

			SCHAR* p = buffer;

			dasup.dasup_stmt_type = 0;

			if (*p == isc_info_sql_stmt_type)
			{
				const USHORT len = gds__vax_integer((UCHAR*) p + 1, 2);
				dasup.dasup_stmt_type = gds__vax_integer((UCHAR*) p + 3, len);
				p += 3 + len;
			}

			sqlda_sup::dasup_clause &das_select = dasup.dasup_clauses[DASUP_CLAUSE_select];
			sqlda_sup::dasup_clause &das_bind = dasup.dasup_clauses[DASUP_CLAUSE_bind];
			das_select.dasup_info_buf = das_bind.dasup_info_buf = NULL;
			das_select.dasup_info_len = das_bind.dasup_info_len = NULL;

			SCHAR* bufSelect = NULL;	// pointer in the buffer where isc_info_sql_select starts
			USHORT lenSelect = 0;	// length of isc_info_sql_select part

			if (*p == isc_info_sql_select)
			{
				das_select.dasup_info_buf = p;
				bufSelect = p;
				lenSelect = bufferLen - (bufSelect - buffer);
			}

			das_bind.dasup_info_buf = UTLD_skip_sql_info(p);

			p = das_select.dasup_info_buf;

			if (p)
			{
				SCHAR* p2 = das_bind.dasup_info_buf;
				if (p2)
				{
					const SLONG len = p2 - p;

					p2 = new SCHAR[len + 1];
					memmove(p2, p, len);
					p2[len] = isc_info_end;
					das_select.dasup_info_buf = p2;
					das_select.dasup_info_len = len + 1;

					bufSelect = das_select.dasup_info_buf;
					lenSelect = das_select.dasup_info_len;
				}
				else
				{
					das_select.dasup_info_buf = NULL;
					das_select.dasup_info_len = 0;
				}
			}

			p = das_bind.dasup_info_buf;

			if (p)
			{
				SCHAR* p2 = UTLD_skip_sql_info(p);
				if (p2)
				{
					const SLONG len = p2 - p;

					p2 = new SCHAR[len + 1];
					memmove(p2, p, len);
					p2[len] = isc_info_end;
					das_bind.dasup_info_buf = p2;
					das_bind.dasup_info_len = len + 1;
				}
				else
				{
					das_bind.dasup_info_buf = NULL;
					das_bind.dasup_info_len = 0;
				}
			}

			iterativeSqlInfo(status, stmtHandle, sizeof(SQL_PREPARE_INFO),
				SQL_PREPARE_INFO, lenSelect, bufSelect, dialect, sqlda);

			// Statement prepared OK.
			statement->prepared = true;
		}
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Prepare a statement for execution.
ISC_STATUS API_ROUTINE isc_dsql_prepare_m(ISC_STATUS* userStatus, FB_API_HANDLE* traHandle,
	FB_API_HANDLE* stmtHandle, USHORT stmtLength, const SCHAR* sqlStmt, USHORT dialect,
	USHORT itemLength, const SCHAR* items, USHORT bufferLength, SCHAR* buffer)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YTransaction> transaction(translateHandle(transactions, traHandle));
		RefPtr<YStatement> statement(translateHandle(statements, stmtHandle));

		statement->prepare(&status, transaction, stmtLength, sqlStmt, dialect,
			itemLength, reinterpret_cast<const UCHAR*>(items),
			bufferLength, reinterpret_cast<UCHAR*>(buffer));
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Set a cursor name for a dynamic request.
ISC_STATUS API_ROUTINE isc_dsql_set_cursor_name(ISC_STATUS* userStatus, FB_API_HANDLE* stmtHandle,
	const SCHAR* cursorName, USHORT cursorType)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YStatement> statement(translateHandle(statements, stmtHandle));
		statement->setCursor(&status, cursorName, cursorType);
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Provide information on sql statement.
ISC_STATUS API_ROUTINE isc_dsql_sql_info(ISC_STATUS* userStatus, FB_API_HANDLE* stmtHandle,
	SSHORT itemLength, const SCHAR* items, SSHORT bufferLength, SCHAR* buffer)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YStatement> statement(translateHandle(statements, stmtHandle));

		statement->getInfo(&status, itemLength, reinterpret_cast<const UCHAR*>(items),
			bufferLength, reinterpret_cast<UCHAR*>(buffer));
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Enable access to a specific subsystem. If no subsystem has been explicitly enabled, all are
// available.
int API_ROUTINE gds__enable_subsystem(TEXT* subsystem)
{
	return FALSE;
}


// Que request for event notification.
ISC_STATUS API_ROUTINE isc_wait_for_event(ISC_STATUS* userStatus, FB_API_HANDLE* dbHandle,
	USHORT length, const UCHAR* eventsData, UCHAR* buffer)
{
	StatusVector status(userStatus);
	YEvents* events = NULL;

	class Callback : public EventCallback
	{
	public:
		Callback(UCHAR* aBuffer)
			: buffer(aBuffer)
		{
		}

		virtual void FB_CARG eventCallbackFunction(unsigned int length, const UCHAR* events)
		{
			memcpy(buffer, events, length);
			sem.release();
		}

		UCHAR* buffer;
		Semaphore sem;
	} callback(buffer);

	try
	{
		RefPtr<YAttachment> attachment(translateHandle(attachments, dbHandle));

		///nullCheck(id, isc_bad_events_handle);

		events = attachment->queEvents(&status, &callback, length, eventsData);

		if (!status.isSuccess())
			return status[1];

		callback.sem.enter();
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	if (events)
	{
		StatusVector temp(NULL);
		events->cancel(&temp);
	}

	return status[1];
}


// Get a segment from a blob opened for reading.
ISC_STATUS API_ROUTINE isc_get_segment(ISC_STATUS* userStatus, FB_API_HANDLE* blobHandle,
	USHORT* returnLength, USHORT bufferLength, UCHAR* buffer)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YBlob> blob(translateHandle(blobs, blobHandle));
		unsigned int length = blob->getSegment(&status, bufferLength, buffer);

		if ((status.isSuccess() || status[1] == isc_segment || status[1] == isc_segstr_eof) &&
			returnLength)
		{
			*returnLength = length;
		}
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Snatch a slice of an array.
ISC_STATUS API_ROUTINE isc_get_slice(ISC_STATUS* userStatus, FB_API_HANDLE* dbHandle,
	FB_API_HANDLE* traHandle, ISC_QUAD* arrayId, USHORT sdlLength, const UCHAR* sdl,
	USHORT paramLength, const UCHAR* param, SLONG sliceLength, UCHAR* slice, SLONG* returnLength)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YAttachment> attachment(translateHandle(attachments, dbHandle));
		RefPtr<YTransaction> transaction(translateHandle(transactions, traHandle));

		int length = attachment->getSlice(&status, transaction, arrayId, sdlLength, sdl,
			paramLength, param, sliceLength, slice);

		if (status.isSuccess() && returnLength)
			*returnLength = length;
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Clean up a dangling transaction handle.
ISC_STATUS API_ROUTINE fb_disconnect_transaction(ISC_STATUS* userStatus, FB_API_HANDLE* traHandle)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YTransaction> transaction(translateHandle(transactions, traHandle));
		transaction->disconnect(&status);

		if (status.isSuccess())
			*traHandle = 0;	// ASF: Previous versions wasn't cleaning the handle.
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Open an existing blob.
ISC_STATUS API_ROUTINE isc_open_blob(ISC_STATUS* userStatus, FB_API_HANDLE* dbHandle,
	FB_API_HANDLE* traHandle, FB_API_HANDLE* blobHandle, ISC_QUAD* blobId)
{
	return openOrCreateBlob(userStatus, dbHandle, traHandle, blobHandle, blobId, 0, 0, false);
}


// Open an existing blob (extended edition).
ISC_STATUS API_ROUTINE isc_open_blob2(ISC_STATUS* userStatus, FB_API_HANDLE* dbHandle,
	FB_API_HANDLE* traHandle, FB_API_HANDLE* blobHandle, ISC_QUAD* blobId,
	SSHORT bpbLength, const UCHAR* bpb)
{
	return openOrCreateBlob(userStatus, dbHandle, traHandle, blobHandle, blobId,
		bpbLength, bpb, false);
}


// Prepare a transaction for commit. First phase of a two phase commit.
ISC_STATUS API_ROUTINE isc_prepare_transaction(ISC_STATUS* userStatus, FB_API_HANDLE* traHandle)
{
	return isc_prepare_transaction2(userStatus, traHandle, 0, NULL);
}


// Prepare a transaction for commit. First phase of a two phase commit.
ISC_STATUS API_ROUTINE isc_prepare_transaction2(ISC_STATUS* userStatus, FB_API_HANDLE* traHandle,
	USHORT msgLength, const UCHAR* msg)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YTransaction> transaction(translateHandle(transactions, traHandle));
		transaction->prepare(&status, msgLength, msg);
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Put a segment in a blob opened for writing (creation).
ISC_STATUS API_ROUTINE isc_put_segment(ISC_STATUS* userStatus, FB_API_HANDLE* blobHandle,
	USHORT bufferLength, const UCHAR* buffer)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YBlob> blob(translateHandle(blobs, blobHandle));
		blob->putSegment(&status, bufferLength, buffer);
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Put a slice in an array.
ISC_STATUS API_ROUTINE isc_put_slice(ISC_STATUS* userStatus, FB_API_HANDLE* dbHandle,
	FB_API_HANDLE* traHandle, ISC_QUAD* arrayId, USHORT sdlLength, const UCHAR* sdl,
	USHORT paramLength, const SLONG* param, SLONG sliceLength, UCHAR* slice)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YAttachment> attachment(translateHandle(attachments, dbHandle));
		RefPtr<YTransaction> transaction(translateHandle(transactions, traHandle));

		attachment->putSlice(&status, transaction, arrayId, sdlLength, sdl,
			paramLength, reinterpret_cast<const UCHAR*>(param), sliceLength, slice);
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Que request for event notification.
ISC_STATUS API_ROUTINE isc_que_events(ISC_STATUS* userStatus, FB_API_HANDLE* dbHandle, SLONG* id,
	USHORT length, const UCHAR* eventsData, FPTR_EVENT_CALLBACK ast, void* arg)
{
	StatusVector status(userStatus);
	YEvents* events = NULL;

	try
	{
		RefPtr<YAttachment> attachment(translateHandle(attachments, dbHandle));

		///nullCheck(id, isc_bad_events_handle);

		class Callback : public EventCallback
		{
		public:
			Callback(FPTR_EVENT_CALLBACK aAst, void* aArg)
				: ast(aAst),
				  arg(aArg)
			{
			}

			virtual void FB_CARG eventCallbackFunction(unsigned int length, const UCHAR* events)
			{
				ast(arg, length, events);
			}

			FPTR_EVENT_CALLBACK ast;
			void* arg;
		};

		Callback* callback = new Callback(ast, arg);

		events = attachment->queEvents(&status, callback, length, eventsData);
		events->deleteCallback = true;

		if (!status.isSuccess())
		{
			delete callback;
			return status[1];
		}

		*id = events->handle;
	}
	catch (const Exception& e)
	{
		if (events)
		{
			*id = 0;
			StatusVector temp(NULL);
			events->cancel(&temp);
		}

		e.stuffException(&status);
	}

	return status[1];
}


// Send a record to the host program.
ISC_STATUS API_ROUTINE isc_receive(ISC_STATUS* userStatus, FB_API_HANDLE* reqHandle, USHORT msgType,
	USHORT msgLength, SCHAR* msg, SSHORT level)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YRequest> request(translateHandle(requests, reqHandle));
		request->receive(&status, level, msgType, msgLength, reinterpret_cast<UCHAR*>(msg));
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Connect to a transaction in limbo.
ISC_STATUS API_ROUTINE isc_reconnect_transaction(ISC_STATUS* userStatus, FB_API_HANDLE* dbHandle,
	FB_API_HANDLE* traHandle, SSHORT length, const UCHAR* id)
{
	StatusVector status(userStatus);

	try
	{
		nullCheck(traHandle, isc_bad_trans_handle);

		RefPtr<YAttachment> attachment(translateHandle(attachments, dbHandle));

		YTransaction* transaction = attachment->reconnectTransaction(&status, length, id);

		if (!status.isSuccess())
			status_exception::raise(status);

		*traHandle = transaction->handle;
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Release a request.
ISC_STATUS API_ROUTINE isc_release_request(ISC_STATUS* userStatus, FB_API_HANDLE* reqHandle)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YRequest> request(translateHandle(requests, reqHandle));
		request->free(&status);

		if (status.isSuccess())
			*reqHandle = 0;
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Provide information on blob object.
ISC_STATUS API_ROUTINE isc_request_info(ISC_STATUS* userStatus, FB_API_HANDLE* reqHandle,
	SSHORT level, SSHORT itemLength, const SCHAR* items, SSHORT bufferLength, SCHAR* buffer)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YRequest> request(translateHandle(requests, reqHandle));

		request->getInfo(&status, level, itemLength, reinterpret_cast<const UCHAR*>(items),
			bufferLength, reinterpret_cast<UCHAR*>(buffer));
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}

// API to be used to tell Firebird to reset it's FPE handler - eg: client has an FPE of it's own
// and just changed it. Returns prior setting of the FPE reset flag.
#if defined (SOLARIS) || defined (WIN_NT)
extern "C"
#endif
SLONG API_ROUTINE isc_reset_fpe(USHORT /*fpe_status*/)
{
	// A client-only API call, isc_reset_fpe() is deprecated - we do not use
	// the FPE handler anymore, it can't be used in multithreaded library.
	// Parameter is ignored, it always returns FPE_RESET_ALL_API_CALL,
	// this is the most close code to what we are doing now.

	//static const USHORT FPE_RESET_INIT_ONLY		= 0x0;	// Don't reset FPE after init
	//static const USHORT FPE_RESET_NEXT_API_CALL	= 0x1;	// Reset FPE on next gds call
	static const USHORT FPE_RESET_ALL_API_CALL		= 0x2;	// Reset FPE on all gds call

	return FPE_RESET_ALL_API_CALL;
}


// Abort a transaction, but keep all cursors open.
ISC_STATUS API_ROUTINE isc_rollback_retaining(ISC_STATUS* userStatus, FB_API_HANDLE* traHandle)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YTransaction> transaction(translateHandle(transactions, traHandle));
		transaction->rollbackRetaining(&status);

		if (status.isSuccess())
			*traHandle = 0;
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Abort a transaction.
ISC_STATUS API_ROUTINE isc_rollback_transaction(ISC_STATUS* userStatus, FB_API_HANDLE* traHandle)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YTransaction> transaction(translateHandle(transactions, traHandle));
		transaction->rollback(&status);

		if (status.isSuccess())
			*traHandle = 0;
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Seek a blob.
ISC_STATUS API_ROUTINE isc_seek_blob(ISC_STATUS* userStatus, FB_API_HANDLE* blobHandle,
	SSHORT mode, SLONG offset, SLONG* result)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YBlob> blob(translateHandle(blobs, blobHandle));
		unsigned int pos = blob->seek(&status, mode, offset);

		if (status.isSuccess() && result)
			*result = pos;
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Get a record from the host program.
ISC_STATUS API_ROUTINE isc_send(ISC_STATUS* userStatus, FB_API_HANDLE* reqHandle, USHORT msgType,
	USHORT msgLength, const SCHAR* msg, SSHORT level)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YRequest> request(translateHandle(requests, reqHandle));
		request->send(&status, level, msgType, msgLength, reinterpret_cast<const UCHAR*>(msg));
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Attach a service through the first subsystem that recognizes it.
ISC_STATUS API_ROUTINE isc_service_attach(ISC_STATUS* userStatus, USHORT serviceLength,
	const TEXT* serviceName, FB_API_HANDLE* publicHandle, USHORT spbLength, const SCHAR* spb)
{
	StatusVector status(userStatus);
	YService* service = NULL;

	try
	{
		nullCheck(publicHandle, isc_bad_svc_handle);

		if (!serviceName)
			status_exception::raise(Arg::Gds(isc_service_att_err) << Arg::Gds(isc_svc_name_missing));

		string svcName(serviceName, serviceLength ? serviceLength : strlen(serviceName));

		service = dispatcher.attachServiceManager(&status, svcName.c_str(), spbLength,
			reinterpret_cast<const UCHAR*>(spb));

		if (!status.isSuccess())
			return status[1];

		*publicHandle = service->handle;
	}
	catch (const Exception& e)
	{
		if (service)
		{
			StatusVector temp(NULL);
			service->detach(&temp);
		}

		e.stuffException(&status);
	}

	return status[1];
}


// Close down a service.
ISC_STATUS API_ROUTINE isc_service_detach(ISC_STATUS* userStatus, FB_API_HANDLE* handle)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YService> service(translateHandle(services, handle));
		service->detach(&status);

		if (status.isSuccess())
			*handle = 0;
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Provide information on service object.
// NOTE: The parameter RESERVED must not be used for any purpose as there are networking issues
// involved (as with any handle that goes over the network). This parameter will be implemented at
// a later date.
ISC_STATUS API_ROUTINE isc_service_query(ISC_STATUS* userStatus, FB_API_HANDLE* handle,
	ULONG* /*reserved*/, USHORT sendItemLength, const SCHAR* sendItems, USHORT recvItemLength,
	const SCHAR* recvItems, USHORT bufferLength, SCHAR* buffer)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YService> service(translateHandle(services, handle));

		service->query(&status, sendItemLength, reinterpret_cast<const unsigned char*>(sendItems),
			recvItemLength, reinterpret_cast<const unsigned char*>(recvItems),
			bufferLength, reinterpret_cast<unsigned char*>(buffer));
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Starts a service thread.
// NOTE: The parameter RESERVED must not be used for any purpose as there are networking issues
// involved (as with any handle that goes over the network). This parameter will be implemented at
// a later date.
ISC_STATUS API_ROUTINE isc_service_start(ISC_STATUS* userStatus, FB_API_HANDLE* handle,
	ULONG* /*reserved*/, USHORT spbLength, const SCHAR* spb)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YService> service(translateHandle(services, handle));
		service->start(&status, spbLength, reinterpret_cast<const unsigned char*>(spb));
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Get a record from the host program.
ISC_STATUS API_ROUTINE isc_start_and_send(ISC_STATUS* userStatus, FB_API_HANDLE* reqHandle,
	FB_API_HANDLE* traHandle, USHORT msgType, USHORT msgLength, const SCHAR* msg, SSHORT level)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YRequest> request(translateHandle(requests, reqHandle));
		RefPtr<YTransaction> transaction(translateHandle(transactions, traHandle));

		request->startAndSend(&status, transaction, level, msgType,
			msgLength, reinterpret_cast<const UCHAR*>(msg));
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Get a record from the host program.
ISC_STATUS API_ROUTINE isc_start_request(ISC_STATUS* userStatus, FB_API_HANDLE* reqHandle,
	FB_API_HANDLE* traHandle, SSHORT level)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YRequest> request(translateHandle(requests, reqHandle));
		RefPtr<YTransaction> transaction(translateHandle(transactions, traHandle));

		request->start(&status, transaction, level);
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Start a transaction.
ISC_STATUS API_ROUTINE isc_start_multiple(ISC_STATUS* userStatus, FB_API_HANDLE* traHandle,
	SSHORT count, void* vec)
{
	StatusVector status(userStatus);
	Array<YTransaction*> subTransactions;
	YTransaction* multiTrans = NULL;
	TEB* vector = (TEB*) vec;

	try
	{
		nullCheck(traHandle, isc_bad_trans_handle);

		if (count <= 0 || !vector)
			status_exception::raise(Arg::Gds(isc_bad_teb_form));

		for (USHORT n = 0; n < count; ++n, ++vector)
		{
			if (vector->teb_tpb_length < 0 || (vector->teb_tpb_length > 0 && !vector->teb_tpb))
				status_exception::raise(Arg::Gds(isc_bad_tpb_form));

			RefPtr<YAttachment> attachment(translateHandle(attachments, vector->teb_database));
			YTransaction* transaction = attachment->startTransaction(&status,
				vector->teb_tpb_length, vector->teb_tpb, 0);

			if (!status.isSuccess())
				status_exception::raise(status);

			subTransactions.add(transaction);

			if (count == 1)
			{
				*traHandle = transaction->handle;
				return status[1];
			}
		}

		multiTrans = new YTransaction(subTransactions);
		*traHandle = multiTrans->handle;
	}
	catch (const Exception& e)
	{
		StatusVector temp(NULL);

		if (multiTrans)
			multiTrans->rollback(&temp);
		else
		{
			for (Array<YTransaction*>::iterator i(subTransactions.begin());
				 i != subTransactions.end();
				 ++i)
			{
				(*i)->rollback(&temp);
			}
		}

		e.stuffException(&status);
	}

	return status[1];
}


// Start a transaction..
ISC_STATUS API_ROUTINE_VARARG isc_start_transaction(ISC_STATUS* userStatus,
	FB_API_HANDLE* traHandle, SSHORT count, ...)
{
	StatusVector status(userStatus);

	try
	{
		HalfStaticArray<TEB, 16> tebs;
		TEB* teb = tebs.getBuffer(count);

		const TEB* const end = teb + count;
		va_list ptr;
		va_start(ptr, count);

		for (TEB* teb_iter = teb; teb_iter < end; teb_iter++)
		{
			teb_iter->teb_database = va_arg(ptr, FB_API_HANDLE*);
			teb_iter->teb_tpb_length = va_arg(ptr, int);
			teb_iter->teb_tpb = va_arg(ptr, UCHAR *);
		}

		va_end(ptr);

		isc_start_multiple(status, traHandle, count, teb);
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Execute a procedure.
ISC_STATUS API_ROUTINE isc_transact_request(ISC_STATUS* userStatus, FB_API_HANDLE* dbHandle,
	FB_API_HANDLE* traHandle, USHORT blrLength, SCHAR* blr, USHORT inMsgLength, SCHAR* inMsg,
	USHORT outMsgLength, SCHAR* outMsg)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YAttachment> attachment(translateHandle(attachments, dbHandle));
		RefPtr<YTransaction> transaction(translateHandle(transactions, traHandle));

		attachment->transactRequest(&status, transaction,
			blrLength, reinterpret_cast<unsigned char*>(blr),
			inMsgLength, reinterpret_cast<unsigned char*>(inMsg),
			outMsgLength, reinterpret_cast<unsigned char*>(outMsg));
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Register a transaction specific cleanup handler.
ISC_STATUS API_ROUTINE gds__transaction_cleanup(ISC_STATUS* userStatus, FB_API_HANDLE* traHandle,
	TransactionCleanupRoutine* routine, void* arg)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YTransaction> transaction(translateHandle(transactions, traHandle));

		AutoPtr<CleanupCallbackImpl<YTransaction, TransactionCleanupRoutine> > callback(
			new CleanupCallbackImpl<YTransaction, TransactionCleanupRoutine>(transaction, routine, arg));

		transaction->addCleanupHandler(&status, callback);

		if (status.isSuccess())
			callback.release();
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Provide information on transaction object.
ISC_STATUS API_ROUTINE isc_transaction_info(ISC_STATUS* userStatus, FB_API_HANDLE* traHandle,
	SSHORT itemLength, const SCHAR* items, SSHORT bufferLength, UCHAR* buffer)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YTransaction> transaction(translateHandle(transactions, traHandle));

		transaction->getInfo(&status, itemLength, reinterpret_cast<const UCHAR*>(items),
			bufferLength, reinterpret_cast<UCHAR*>(buffer));
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Unwind a running request. This is potentially nasty since it can be called asynchronously.
ISC_STATUS API_ROUTINE isc_unwind_request(ISC_STATUS* userStatus, FB_API_HANDLE* reqHandle,
	SSHORT level)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YRequest> request(translateHandle(requests, reqHandle));
		request->unwind(&status, level);
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Shutdown firebird.
int API_ROUTINE fb_shutdown(unsigned int timeout, const int reason)
{
	StatusVector status(NULL);
	dispatcher.shutdown(&status, timeout, reason);
	return status.isSuccess() ? FB_SUCCESS : FB_FAILURE;
}


// Register client callback to be called when FB is going down.
ISC_STATUS API_ROUTINE fb_shutdown_callback(ISC_STATUS* userStatus, FB_SHUTDOWN_CALLBACK callBack,
	const int mask, void* arg)
{
	StatusVector status(userStatus);

	try
	{
		ShutChain::add(callBack, mask, arg);
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Check the attachment handle for persistent errors.
ISC_STATUS API_ROUTINE fb_ping(ISC_STATUS* userStatus, FB_API_HANDLE* dbHandle)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YAttachment> attachment(translateHandle(attachments, dbHandle));
		attachment->ping(&status);
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


//-------------------------------------


YEvents::YEvents(YAttachment* aAttachment, IEvents* aNext, EventCallback* aCallback)
	: attachment(aAttachment),
	  next(aNext),
	  callback(aCallback),
	  deleteCallback(false)
{
	attachment->childEvents.add(this);
	handle = makeHandle(&events, this);
}

void YEvents::destroy()
{
	attachment->childEvents.remove(this);

	removeHandle(&events, handle);

	next = NULL;
	release();
}

void YEvents::cancel(Status* status)
{
	try
	{
		YEntry entry(status, attachment);

		next->cancel(status);

		if (status->isSuccess())
			destroy();
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}


//-------------------------------------


YRequest::YRequest(YAttachment* aAttachment, IRequest* aNext)
	: attachment(aAttachment),
	  next(aNext),
	  userHandle(NULL)
{
	attachment->childRequests.add(this);
	handle = makeHandle(&requests, this);
}

void YRequest::destroy()
{
	if (userHandle)
	{
		*userHandle = 0;
		userHandle = NULL;
	}

	attachment->childRequests.remove(this);

	removeHandle(&requests, handle);

	next = NULL;
	release();
}

void YRequest::receive(Status* status, int level, unsigned int msgType,
	unsigned int length, unsigned char* message)
{
	try
	{
		YEntry entry(status, attachment);
		next->receive(status, level, msgType, length, message);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YRequest::send(Status* status, int level, unsigned int msgType,
	unsigned int length, const unsigned char* message)
{
	try
	{
		YEntry entry(status, attachment);
		next->send(status, level, msgType, length, message);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YRequest::getInfo(Status* status, int level, unsigned int itemsLength,
	const unsigned char* items, unsigned int bufferLength, unsigned char* buffer)
{
	try
	{
		YEntry entry(status, attachment);
		next->getInfo(status, level, itemsLength, items, bufferLength, buffer);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YRequest::start(Status* status, ITransaction* transaction, int level)
{
	try
	{
		YEntry entry(status, attachment);

		ITransaction* trans = YTransaction::getNext(transaction, attachment);
		next->start(status, trans, level);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YRequest::startAndSend(Status* status, ITransaction* transaction, int level,
	unsigned int msgType, unsigned int length, const unsigned char* message)
{
	try
	{
		YEntry entry(status, attachment);

		ITransaction* trans = YTransaction::getNext(transaction, attachment);
		next->startAndSend(status, trans, level, msgType, length, message);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YRequest::unwind(Status* status, int level)
{
	try
	{
		YEntry entry(status, attachment);
		next->unwind(status, level);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YRequest::free(Status* status)
{
	try
	{
		YEntry entry(status, attachment);

		next->free(status);

		if (status->isSuccess())
			destroy();
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}


//-------------------------------------


YBlob::YBlob(YAttachment* aAttachment, YTransaction* aTransaction, IBlob* aNext)
	: attachment(aAttachment),
	  transaction(aTransaction),
	  next(aNext)
{
	attachment->childBlobs.add(this);
	transaction->childBlobs.add(this);
	handle = makeHandle(&blobs, this);
}

void YBlob::destroy()
{
	attachment->childBlobs.remove(this);
	transaction->childBlobs.remove(this);

	removeHandle(&blobs, handle);

	next = NULL;
	release();
}

void YBlob::getInfo(Status* status, unsigned int itemsLength,
	const unsigned char* items, unsigned int bufferLength, unsigned char* buffer)
{
	try
	{
		YEntry entry(status, attachment);
		next->getInfo(status, itemsLength, items, bufferLength, buffer);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

unsigned int YBlob::getSegment(Status* status, unsigned int length, unsigned char* buffer)
{
	try
	{
		YEntry entry(status, attachment);
		return next->getSegment(status, length, buffer);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}

	return 0;
}

void YBlob::putSegment(Status* status, unsigned int length, const unsigned char* buffer)
{
	try
	{
		YEntry entry(status, attachment);
		next->putSegment(status, length, buffer);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YBlob::cancel(Status* status)
{
	try
	{
		YEntry entry(status, attachment);

		next->cancel(status);

		if (status->isSuccess())
			destroy();
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YBlob::close(Status* status)
{
	try
	{
		YEntry entry(status, attachment);

		next->close(status);

		if (status->isSuccess())
			destroy();
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

int YBlob::seek(Status* status, int mode, int offset)
{
	try
	{
		YEntry entry(status, attachment);
		return next->seek(status, mode, offset);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}

	return 0;
}


//-------------------------------------


YStatement::YStatement(YAttachment* aAttachment, IStatement* aNext)
	: attachment(aAttachment),
	  next(aNext),
	  userHandle(NULL),
	  prepared(false)
{
	attachment->childStatements.add(this);
	handle = makeHandle(&statements, this);

	memset(&das, 0, sizeof das);
}

void YStatement::destroy()
{
	if (userHandle)
	{
		*userHandle = 0;
		userHandle = NULL;
	}

	attachment->childStatements.remove(this);

	removeHandle(&statements, handle);

	releaseDsqlSupport(das);

	next = NULL;
	release();
}

YStatement* YStatement::prepare(Status* status, ITransaction* transaction,
	unsigned int stmtLength, const char* sqlStmt, unsigned int dialect,
	unsigned int itemLength, const unsigned char* items,
	unsigned int bufferLength, unsigned char* buffer)
{
	try
	{
		YEntry entry(status, attachment);

		if (!sqlStmt)
			Arg::Gds(isc_command_end_err).raise();

		ITransaction* trans = YTransaction::getNext(transaction, attachment);

		IStatement* newStmt = next->prepare(status, trans, stmtLength, sqlStmt, dialect,
			itemLength, items, bufferLength, buffer);

		if (status->isSuccess())
			next = newStmt;
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}

	return this;
}

void YStatement::getInfo(Status* status, unsigned int itemsLength,
	const unsigned char* items, unsigned int bufferLength, unsigned char* buffer)
{
	try
	{
		YEntry entry(status, attachment);

		if (((itemsLength == 1 && items[0] == isc_info_sql_stmt_type) ||
				(itemsLength == 2 && items[0] == isc_info_sql_stmt_type &&
					(items[1] == isc_info_end || items[1] == 0))) &&
			prepared && das.dasup_stmt_type)
		{
			if (bufferLength >= 8)
			{
				*buffer++ = isc_info_sql_stmt_type;
				put_vax_short((UCHAR*) buffer, 4);
				buffer += 2;
				put_vax_long((UCHAR*) buffer, das.dasup_stmt_type);
				buffer += 4;
				*buffer = isc_info_end;
			}
			else
				*buffer = isc_info_truncated;
		}
		else
			next->getInfo(status, itemsLength, items, bufferLength, buffer);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YStatement::setCursor(Status* status, const char* name, unsigned int type)
{
	try
	{
		YEntry entry(status, attachment);

		next->setCursor(status, name, type);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

YTransaction* YStatement::execute(Status* status, ITransaction* transaction,
	unsigned int inMsgType, const MessageBuffer* inMsgBuffer,
	const MessageBuffer* outMsgBuffer)
{
	try
	{
		YEntry entry(status, attachment);

		ITransaction* trans = transaction ? YTransaction::getNext(transaction, attachment) : NULL;
		ITransaction* newTrans = next->execute(status, trans, inMsgType, inMsgBuffer, outMsgBuffer);

		if (newTrans)
		{
			if (newTrans == transaction)
				newTrans = transaction;
			else
				newTrans = new YTransaction(attachment, newTrans);
		}

		return static_cast<YTransaction*>(newTrans);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}

	return NULL;
}

int YStatement::fetch(Status* status, const MessageBuffer* msgBuffer)
{
	try
	{
		YEntry entry(status, attachment);
		return next->fetch(status, msgBuffer);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}

	return status->get()[1];
}

void YStatement::insert(Status* status, const MessageBuffer* msgBuffer)
{
	try
	{
		YEntry entry(status, attachment);

		checkPrepared();
		next->insert(status, msgBuffer);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YStatement::free(Status* status, unsigned int option)
{
	try
	{
		YEntry entry(status, attachment);

		next->free(status, option);

		if (status->isSuccess())
		{
			if (option & DSQL_unprepare)
				prepared = false;

			if (option & DSQL_drop)
				destroy();
		}
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}


//-------------------------------------


YTransaction::YTransaction(YAttachment* aAttachment, ITransaction* aNext)
	: attachment(aAttachment),
	  next(aNext),
	  sub(NULL),
	  limbo(false),
	  childBlobs(getPool()),
	  cleanupHandlers(getPool())
{
	attachment->childTransactions.add(this);
	handle = makeHandle(&transactions, this);
}

YTransaction::YTransaction(const Array<YTransaction*>& subTransactions)
	: attachment(NULL),
	  next(NULL),
	  sub(NULL),
	  limbo(false),
	  childBlobs(getPool()),
	  cleanupHandlers(getPool())
{
	YTransaction* prev = this;

	for (YTransaction* const* i = subTransactions.begin(); i != subTransactions.end(); ++i)
	{
		prev->sub = *i;
		prev = *i;
	}

	handle = makeHandle(&transactions, this);
}

void YTransaction::destroy()
{
	if (sub)
	{
		sub->destroy();
		sub = NULL;
	}

	for (CleanupCallback** handler = cleanupHandlers.begin();
		 handler != cleanupHandlers.end();
		 ++handler)
	{
		(*handler)->cleanupCallbackFunction();
	}

	cleanupHandlers.clear();

	childBlobs.destroy();

	if (attachment)
		attachment->childTransactions.remove(this);

	removeHandle(&transactions, handle);

	next = NULL;
	release();
}

void YTransaction::getInfo(Status* status, unsigned int itemsLength,
	const unsigned char* items, unsigned int bufferLength, unsigned char* buffer)
{
	try
	{
		YEntry entry(status, attachment);

		if (next)
		{
			next->getInfo(status, itemsLength, items, bufferLength, buffer);
			return;
		}

		for (YTransaction* i = sub; i; i = i->sub)
		{
			ITransaction* next = i->next;

			next->getInfo(status, itemsLength, items, bufferLength, buffer);
			if (!status->isSuccess())
				return;

			UCHAR* ptr = buffer;
			const UCHAR* const end = buffer + bufferLength;

			while (ptr < end && *ptr == isc_info_tra_id)
				ptr += 3 + gds__vax_integer(ptr + 1, 2);

			if (ptr >= end || *ptr != isc_info_end)
				return;

			bufferLength = end - ptr;
			buffer = ptr;
		}
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YTransaction::prepare(Status* status, unsigned int msgLength, const unsigned char* message)
{
	try
	{
		YEntry entry(status, attachment);

		for (YTransaction* i = this; i; i = i->sub)
		{
			if (i->next)
			{
				i->next->prepare(status, msgLength, message);

				if (!status->isSuccess())
					return;
			}
		}

		limbo = true;
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YTransaction::commit(Status* status)
{
	try
	{
		YEntry entry(status, attachment);

		// Handle two phase commit. Start by putting everybody into limbo. If anybody fails, punt.

		if (!next && !limbo)
		{
			if (!prepareCommit(status))
				return;
		}

		// Everybody is in limbo, now commit. In theory, this can't fail.

		for (YTransaction* i = this; i; i = i->sub)
		{
			if (i->next)
			{
				i->next->commit(status);

				if (!status->isSuccess())
					return;

				i->next = NULL;
			}
		}

		destroy();
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YTransaction::commitRetaining(Status* status)
{
	try
	{
		YEntry entry(status, attachment);

		for (YTransaction* i = this; i; i = i->sub)
		{
			if (i->next)
			{
				i->next->commitRetaining(status);

				if (!status->isSuccess())
					return;
			}
		}

		limbo = true;	// ASF: why do retaining marks limbo?
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YTransaction::rollback(Status* status)
{
	try
	{
		YEntry entry(status, attachment);

		for (YTransaction* i = this; i; i = i->sub)
		{
			if (i->next)
			{
				i->next->rollback(status);

				if (!status->isSuccess() && (!isNetworkError(status) || limbo))
					return;

				i->next = NULL;
			}
		}

		if (isNetworkError(status))
			status->init();

		destroy();
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YTransaction::rollbackRetaining(Status* status)
{
	try
	{
		YEntry entry(status, attachment);

		for (YTransaction* i = this; i; i = i->sub)
		{
			if (i->next)
			{
				i->next->commitRetaining(status);

				if (!status->isSuccess())
					return;
			}
		}

		limbo = true;	// ASF: why do retaining marks limbo?
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YTransaction::disconnect(Status* status)
{
	try
	{
		YEntry entry(status, attachment);

		if (!limbo)
			status_exception::raise(Arg::Gds(isc_no_recon));

		/*** ASF: We must call the provider, but this makes the shutdown to crash currently.
		for (YTransaction* i = this; i; i = i->sub)
		{
			if (i->next)
			{
				i->next->disconnect(status);

				if (!status->isSuccess())
					return;

				i->next = NULL;
			}
		}
		***/

		destroy();
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YTransaction::addCleanupHandler(Status* status, CleanupCallback* callback)
{
	try
	{
		YEntry entry(status, attachment);

		cleanupHandlers.add(callback);
	}
	catch (const Exception& ex)
	{
		ex.stuffException(status);
	}
}

// Perform the first phase of a two-phase commit for a multi-database transaction.
bool YTransaction::prepareCommit(Status* status)
{
	YTransaction* i;
	HalfStaticArray<UCHAR, 1024> tdrBuffer;
	size_t length = 0;
	///int transCount = 0;

	for (i = sub; i; i = i->sub)
	{
		length += 256;
		///++transCount;
	}

	// To do: use transCount to check the maximum allowed dbs in a two phase commit.

	TEXT host[64];
	ISC_get_host(host, sizeof(host));
	const size_t hostlen = strlen(host);
	length += hostlen + 3; // TDR_version + TDR_host_site + UCHAR(strlen(host))

	UCHAR* const description = tdrBuffer.getBuffer(length);

	// Build a transaction description record containing the host site and database/transaction
	// information for the target databases.

	unsigned char* p = description;
	*p++ = TDR_VERSION;
	*p++ = TDR_HOST_SITE;
	*p++ = UCHAR(hostlen);
	memcpy(p, host, hostlen);
	p += hostlen;

	// Get database and transaction stuff for each sub-transaction.

	for (i = sub; i; i = i->sub)
	{
		i->attachment->buildPrepareInfo(&p);
		i->buildPrepareInfo(status, &p);
	}

	// So far so good -- prepare each sub-transaction.

	length = p - description;

	for (i = sub; i; i = i->sub)
	{
		i->next->prepare(status, length, description);

		if (!status->isSuccess())
			return false;
	}

	return true;
}

// Put a transaction's id into the transaction description record.
void YTransaction::buildPrepareInfo(Status* status, UCHAR** ptr)
{
	try
	{
		UCHAR buffer[16];
		UCHAR* p = *ptr;

		next->getInfo(status, sizeof(PREPARE_TR_INFO), PREPARE_TR_INFO, sizeof(buffer), buffer);
		if (!status->isSuccess())
			return;

		UCHAR* q = buffer + 3;
		*p++ = TDR_TRANSACTION_ID;

		USHORT length = (USHORT) gds__vax_integer(buffer + 1, 2);

		// Prevent information out of sync.
		if (length > MAX_UCHAR)
			length = MAX_UCHAR;

		*p++ = length;
		memcpy(p, q, length);
		*ptr = p + length;
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}


//-------------------------------------


void YAttachment::destroy()
{
	for (CleanupCallback** handler = cleanupHandlers.begin();
		 handler != cleanupHandlers.end();
		 ++handler)
	{
		(*handler)->cleanupCallbackFunction();
	}

	cleanupHandlers.clear();

	childRequests.destroy();
	childStatements.destroy();
	childBlobs.destroy();
	childEvents.destroy();
	childTransactions.destroy();

	removeHandle(&attachments, handle);

	next = NULL;
	release();
}

// Get the full database pathname and put it in the transaction description record.
void YAttachment::buildPrepareInfo(UCHAR** ptr)
{
	// Look at the changed code: we don't support here more than 254 bytes in the path
	// so it's better to truncate or we'll have corrupt data in the trans desc record:
	// the length in one byte would wrap and we would copy more bytes that expected.
	// Our caller (prepare) assumed each call consumes at most 256 bytes (item, len, data)
	// hence if we don't check here, we have a B.O.

	UCHAR* p = *ptr;
	*p++ = TDR_DATABASE_PATH;
	const char* q = dbPath.c_str();
	size_t len = strlen(q);

	if (len > 254)
		len = 254;

	*p++ = (UCHAR) len;
	memcpy(p, q, len);
	*ptr = p + len;
}

void YAttachment::getInfo(Status* status, unsigned int itemsLength,
	const unsigned char* items, unsigned int bufferLength, unsigned char* buffer)
{
	try
	{
		YEntry entry(status, this);
		next->getInfo(status, itemsLength, items, bufferLength, buffer);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

YTransaction* YAttachment::startTransaction(Status* status, unsigned int tpbLength,
	const unsigned char* tpb, FB_API_HANDLE api)
{
	try
	{
		YEntry entry(status, this);

		ITransaction* transaction = next->startTransaction(status, tpbLength, tpb, api);
		if (transaction)
			transaction = new YTransaction(this, transaction);

		return static_cast<YTransaction*>(transaction);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}

	return NULL;
}

YTransaction* YAttachment::reconnectTransaction(Status* status, unsigned int length,
	const unsigned char* id)
{
	try
	{
		YEntry entry(status, this);

		ITransaction* transaction = next->reconnectTransaction(status, length, id);

		if (transaction)
		{
			transaction = new YTransaction(this, transaction);
			static_cast<YTransaction*>(transaction)->limbo = true;
		}

		return static_cast<YTransaction*>(transaction);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}

	return NULL;
}

YStatement* YAttachment::allocateStatement(Status* status)
{
	try
	{
		YEntry entry(status, this);

		IStatement* statement = next->allocateStatement(status);
		YStatement* yStatement = statement ? new YStatement(this, statement) : NULL;
		return yStatement;
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}

	return NULL;
}

YRequest* YAttachment::compileRequest(Status* status, unsigned int blrLength,
	const unsigned char* blr)
{
	try
	{
		YEntry entry(status, this);

		IRequest* request = next->compileRequest(status, blrLength, blr);
		YRequest* yRequest = request ? new YRequest(this, request) : NULL;
		return yRequest;
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}

	return NULL;
}

void YAttachment::transactRequest(Status* status, ITransaction* transaction,
	unsigned int blrLength, const unsigned char* blr, unsigned int inMsgLength,
	const unsigned char* inMsg, unsigned int outMsgLength, unsigned char* outMsg)
{
	try
	{
		YEntry entry(status, this);

		ITransaction* trans = YTransaction::getNext(transaction, this);

		next->transactRequest(status, trans, blrLength, blr, inMsgLength, inMsg,
			outMsgLength, outMsg);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

YBlob* YAttachment::createBlob(Status* status, ITransaction* transaction, ISC_QUAD* id,
	unsigned int bpbLength, const unsigned char* bpb)
{
	try
	{
		YEntry entry(status, this);

		ITransaction* trans = YTransaction::getNext(transaction, this);

		IBlob* blob = next->createBlob(status, trans, id, bpbLength, bpb);
		YBlob* yBlob = blob ? new YBlob(this, static_cast<YTransaction*>(transaction), blob) : NULL;
		return yBlob;
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}

	return NULL;
}

YBlob* YAttachment::openBlob(Status* status, ITransaction* transaction, ISC_QUAD* id,
	unsigned int bpbLength, const unsigned char* bpb)
{
	try
	{
		YEntry entry(status, this);

		ITransaction* trans = YTransaction::getNext(transaction, this);

		IBlob* blob = next->openBlob(status, trans, id, bpbLength, bpb);
		YBlob* yBlob = blob ? new YBlob(this, static_cast<YTransaction*>(transaction), blob) : NULL;
		return yBlob;
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}

	return NULL;
}

int YAttachment::getSlice(Status* status, ITransaction* transaction, ISC_QUAD* id,
	unsigned int sdlLength, const unsigned char* sdl, unsigned int paramLength,
	const unsigned char* param, int sliceLength, unsigned char* slice)
{
	try
	{
		YEntry entry(status, this);

		ITransaction* trans = YTransaction::getNext(transaction, this);

		return next->getSlice(status, trans, id, sdlLength, sdl, paramLength, param,
			sliceLength, slice);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}

	return 0;
}

void YAttachment::putSlice(Status* status, ITransaction* transaction, ISC_QUAD* id,
	unsigned int sdlLength, const unsigned char* sdl, unsigned int paramLength,
	const unsigned char* param, int sliceLength, unsigned char* slice)
{
	try
	{
		YEntry entry(status, this);

		ITransaction* trans = YTransaction::getNext(transaction, this);
		next->putSlice(status, trans, id, sdlLength, sdl, paramLength, param, sliceLength, slice);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YAttachment::ddl(Status* status, ITransaction* transaction, unsigned int length,
	const unsigned char* dyn)
{
	try
	{
		YEntry entry(status, this);

		ITransaction* trans = YTransaction::getNext(transaction, this);
		return next->ddl(status, trans, length, dyn);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

YTransaction* YAttachment::execute(Status* status, ITransaction* transaction,
	unsigned int length, const char* string, unsigned int dialect, unsigned int inMsgType,
	const MessageBuffer* inMsgBuffer, const MessageBuffer* outMsgBuffer)
{
	try
	{
		YEntry entry(status, this);

		ITransaction* trans = transaction ? YTransaction::getNext(transaction, this) : NULL;
		ITransaction* newTrans = next->execute(status, trans, length, string, dialect,
			inMsgType, inMsgBuffer, outMsgBuffer);

		if (newTrans)
		{
			if (newTrans == trans)
				newTrans = transaction;
			else
				newTrans = new YTransaction(this, newTrans);
		}

		return static_cast<YTransaction*>(newTrans);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}

	return NULL;
}

YEvents* YAttachment::queEvents(Status* status, EventCallback* callback,
	unsigned int length, const unsigned char* eventsData)
{
	try
	{
		YEntry entry(status, this);

		IEvents* events = next->queEvents(status, callback, length, eventsData);
		YEvents* yEvents = events ? new YEvents(this, events, callback) : NULL;
		return yEvents;
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}

	return NULL;
}

void YAttachment::cancelOperation(Status* status, int option)
{
	try
	{
		YEntry entry(status);

		// Mutex will be locked here for a really long time.
		MutexLockGuard guard(enterMutex);

		if (enterCount != 0 || option != fb_cancel_raise)
			next->cancelOperation(status, option);
		else
			status_exception::raise(Arg::Gds(isc_nothing_to_cancel));
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YAttachment::ping(Status* status)
{
	try
	{
		YEntry entry(status, this);

		next->ping(status);

		if (!status->isSuccess())
		{
			if (!savedStatus.getError())
				savedStatus.save(status->get());

			StatusVector temp(NULL);
			next->detach(&temp);
			next = NULL;

			status_exception::raise(savedStatus.value());
		}
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YAttachment::detach(Status* status)
{
	try
	{
		YEntry entry(status, this, false);

		if (next)
			next->detach(status);

		if (status->isSuccess())
			destroy();
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YAttachment::drop(Status* status)
{
	try
	{
		YEntry entry(status, this);

		next->drop(status);

		if (status->isSuccess())
			destroy();
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YAttachment::addCleanupHandler(Status* status, CleanupCallback* callback)
{
	try
	{
		YEntry entry(status, this);

		cleanupHandlers.add(callback);
	}
	catch (const Exception& ex)
	{
		ex.stuffException(status);
	}
}


//-------------------------------------


void YService::destroy()
{
	removeHandle(&services, handle);

	next = NULL;
	release();
}

void YService::detach(Status* status)
{
	try
	{
		YEntry entry(status, this);

		next->detach(status);

		if (status->isSuccess())
			destroy();
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YService::query(Status* status, unsigned int sendLength, const unsigned char* sendItems,
	unsigned int receiveLength, const unsigned char* receiveItems,
	unsigned int bufferLength, unsigned char* buffer)
{
	try
	{
		YEntry entry(status, this);
		next->query(status, sendLength, sendItems, receiveLength, receiveItems, bufferLength, buffer);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YService::start(Status* status, unsigned int spbLength, const unsigned char* spb)
{
	try
	{
		YEntry entry(status, this);
		next->start(status, spbLength, spb);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}


//-------------------------------------


// Attach a database through the first subsystem that recognizes it.
void Dispatcher::attachDatabase(Status* status, IAttachment** attachment, FB_API_HANDLE /*api*/,
	const char* filename, unsigned int dpbLength, const unsigned char* dpb)
{
	try
	{
		YEntry entry(status);

		*attachment = NULL;

		if (shutdownStarted)
			status_exception::raise(Arg::Gds(isc_att_shutdown));

		if (!filename)
			status_exception::raise(Arg::Gds(isc_bad_db_format) << Arg::Str(""));

		if (dpbLength > 0 && !dpb)
			status_exception::raise(Arg::Gds(isc_bad_dpb_form));

		// Copy the file name to a temp buffer, since some of the following utilities can modify it.

		PathName orgFilename(filename);
		ClumpletWriter newDpb(ClumpletReader::dpbList, MAX_DPB_SIZE, dpb, dpbLength);
		bool utfFilename = newDpb.find(isc_dpb_utf8_filename);

		if (utfFilename)
			ISC_utf8ToSystem(orgFilename);
		else
		{
			newDpb.insertTag(isc_dpb_utf8_filename);

			for (newDpb.rewind(); !newDpb.isEof(); newDpb.moveNext())
			{
				UCHAR tag = newDpb.getClumpTag();

				switch (tag)
				{
					case isc_dpb_user_name:
					case isc_dpb_password:
					case isc_dpb_sql_role_name:
					case isc_dpb_trusted_auth:
					case isc_dpb_trusted_role:
					case isc_dpb_working_directory:
					case isc_dpb_set_db_charset:
					case isc_dpb_process_name:
					{
						string s;
						newDpb.getString(s);
						ISC_systemToUtf8(s);
						newDpb.deleteClumplet();
						newDpb.insertString(tag, s);
						break;
					}
				}
			}
		}

		setLogin(newDpb);
		orgFilename.rtrim();

		PathName expandedFilename;
		bool unescaped = false;

		if (!setPath(orgFilename, expandedFilename))
		{
			expandedFilename = orgFilename;
			ISC_systemToUtf8(expandedFilename);
			ISC_unescape(expandedFilename);
			unescaped = true;
			ISC_utf8ToSystem(expandedFilename);
			ISC_expand_filename(expandedFilename, true);
		}

		ISC_systemToUtf8(orgFilename);
		ISC_systemToUtf8(expandedFilename);

		if (unescaped)
			ISC_escape(expandedFilename);

		if (orgFilename != expandedFilename && !newDpb.find(isc_dpb_org_filename))
			newDpb.insertPath(isc_dpb_org_filename, orgFilename);

		StatusVector temp(NULL);
		Status* currentStatus = status;

		PathName dummy;
		RefPtr<Config> config;
		ResolveDatabaseAlias(expandedFilename, dummy, &config);

		for (GetPlugins<PProvider, NoEntrypoint> providerIterator(PluginType::Provider,
				FB_P_PROVIDER_VERSION, config);
			 providerIterator.hasData();
			 providerIterator.next())
		{
			PProvider* provider = providerIterator.plugin();

			provider->attachDatabase(currentStatus, attachment, 0, expandedFilename.c_str(),
				newDpb.getBufferLength(), newDpb.getBuffer());

			if (currentStatus->isSuccess())
			{
				*attachment = new YAttachment(provider, *attachment, expandedFilename);
				status->set(currentStatus->get());
				return;
			}

			*attachment = NULL;

			if (currentStatus->get()[1] != isc_unavailable)
				currentStatus = &temp;

			currentStatus->init();
		}

		if (status->get()[1] == 0)
			Arg::Gds(isc_unavailable).raise();
	}
	catch (const Exception& e)
	{
		if (*attachment)
			(*attachment)->release();

		e.stuffException(status);
	}
}

void Dispatcher::createDatabase(Status* status, IAttachment** attachment, FB_API_HANDLE /*api*/,
	const char* filename, unsigned int dpbLength, const unsigned char* dpb)
{
	try
	{
		YEntry entry(status);

		*attachment = NULL;

		if (shutdownStarted)
			status_exception::raise(Arg::Gds(isc_att_shutdown));

		if (!filename)
			status_exception::raise(Arg::Gds(isc_bad_db_format) << Arg::Str(""));

		if (dpbLength > 0 && !dpb)
			status_exception::raise(Arg::Gds(isc_bad_dpb_form));

		// Copy the file name to a temp buffer, since some of the following utilities can modify it.

		PathName orgFilename(filename);
		ClumpletWriter newDpb(ClumpletReader::dpbList, MAX_DPB_SIZE, dpb, dpbLength);
		bool utfFilename = newDpb.find(isc_dpb_utf8_filename);

		if (utfFilename)
			ISC_utf8ToSystem(orgFilename);
		else
		{
			newDpb.insertTag(isc_dpb_utf8_filename);

			/***
			for (newDpb.rewind(); !newDpb.isEof(); newDpb.moveNext())
			{
				UCHAR tag = newDpb.getClumpTag();

				switch (tag)
				{
					case isc_dpb_user_name:
					case isc_dpb_password:
					case isc_dpb_sql_role_name:
					case isc_dpb_trusted_auth:
					case isc_dpb_trusted_role:
					case isc_dpb_working_directory:
					case isc_dpb_set_db_charset:
					case isc_dpb_process_name:
					{
						string s;
						newDpb.getString(s);
						ISC_systemToUtf8(s);
						newDpb.deleteClumplet();
						newDpb.insertString(tag, s);
						break;
					}
				}
			}
			***/
		}

		setLogin(newDpb);
		orgFilename.rtrim();

		PathName expandedFilename;
		bool unescaped = false;

		if (!setPath(orgFilename, expandedFilename))
		{
			expandedFilename = orgFilename;
			ISC_systemToUtf8(expandedFilename);
			ISC_unescape(expandedFilename);
			unescaped = true;
			ISC_utf8ToSystem(expandedFilename);
			ISC_expand_filename(expandedFilename, true);
		}

		ISC_systemToUtf8(orgFilename);
		ISC_systemToUtf8(expandedFilename);

		if (unescaped)
			ISC_escape(expandedFilename);

		if (orgFilename != expandedFilename && !newDpb.find(isc_dpb_org_filename))
			newDpb.insertPath(isc_dpb_org_filename, orgFilename);

		StatusVector temp(NULL);
		Status* currentStatus = status;

		/***
		PathName dummy;
		RefPtr<Config> config;
		ResolveDatabaseAlias(expandedFilename, dummy, &config);
		***/

		for (GetPlugins<PProvider, NoEntrypoint> providerIterator(PluginType::Provider,
				FB_P_PROVIDER_VERSION/***, config***/);
			 providerIterator.hasData();
			 providerIterator.next())
		{
			PProvider* provider = providerIterator.plugin();

			provider->createDatabase(currentStatus, attachment, 0, expandedFilename.c_str(),
				newDpb.getBufferLength(), newDpb.getBuffer());

			if (currentStatus->isSuccess())
			{
				// ASF: Why this is different than in attachDatabase, which always uses expandedFilename?
#ifdef WIN_NT
				PathName path(expandedFilename);
#else
				PathName path(orgFilename);
#endif

				*attachment = new YAttachment(provider, *attachment, path);
				status->set(currentStatus->get());
				return;
			}

			*attachment = NULL;

			if (currentStatus->get()[1] != isc_unavailable)
				currentStatus = &temp;

			currentStatus->init();
		}

		if (status->get()[1] == 0)
			Arg::Gds(isc_unavailable).raise();
	}
	catch (const Exception& e)
	{
		if (*attachment)
		{
			StatusVector temp(NULL);
			(*attachment)->drop(&temp);
		}

		e.stuffException(status);
	}
}

// Attach a service through the first subsystem that recognizes it.
YService* Dispatcher::attachServiceManager(Status* status, const char* serviceName,
	unsigned int spbLength, const unsigned char* spb)
{
	try
	{
		YEntry entry(status);

		IService* service = NULL;

		if (shutdownStarted)
			status_exception::raise(Arg::Gds(isc_att_shutdown));

		if (!serviceName)
			status_exception::raise(Arg::Gds(isc_service_att_err) << Arg::Gds(isc_svc_name_missing));

		if (spbLength > 0 && !spb)
			status_exception::raise(Arg::Gds(isc_bad_spb_form));

		string svcName(serviceName);
		svcName.rtrim();

		try
		{
			for (GetPlugins<PProvider, NoEntrypoint> providerIterator(PluginType::Provider,
					FB_P_PROVIDER_VERSION);
				 providerIterator.hasData();
				 providerIterator.next())
			{
				PProvider* provider = providerIterator.plugin();

				service = provider->attachServiceManager(status, svcName.c_str(), spbLength, spb);

				if (status->isSuccess())
				{
					service = new YService(provider, service);
					return static_cast<YService*>(service);
				}

				service = NULL;
			}

			if (status->get()[1] == 0)
				Arg::Gds(isc_service_att_err).raise();
		}
		catch (const Exception& e)
		{
			if (service)
			{
				StatusVector temp(NULL);
				service->detach(&temp);
			}

			throw;
		}
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}

	return NULL;
}

void Dispatcher::shutdown(Status* userStatus, unsigned int timeout, const int reason)
{
	try
	{
		YEntry entry(userStatus);

		static GlobalPtr<Mutex> singleShutdown;
		MutexLockGuard guard(singleShutdown);

		if (shutdownStarted)
			return;

		StatusVector status(NULL);
		int rc = FB_SUCCESS;

#ifdef DEV_BUILD
		// Make timeout huge in debug build: hard to debug something during 5-10 sec
		if (timeout)
			timeout = 10 * 3600 * 1000;		// 10 hours
#endif

		// In some cases it's OK to stop plugin manager even now.
		if (reason == fb_shutrsn_exit_called)
			PluginManager::shutdown();

		const Arg::Gds error(isc_att_shutdown);	//// TODO: review

		// Ask clients about shutdown confirmation.
		if (ShutChain::run(fb_shut_confirmation, reason) != FB_SUCCESS)
		{
			userStatus->set(error.value());
			return;	// Do not perform former shutdown
		}

		// Shut known components, internal in yValve
		PluginManager::shutdown();
		shutdownTimers();

		// Shutdown clients before providers
		if (ShutChain::run(fb_shut_preproviders, reason) != FB_SUCCESS)
			userStatus->set(error.value());

		// shutdown yValve

		// Since this moment no new thread will be able to enter yValve.
		// Unfortunately existing threads continue to run inside it.
		shutdownStarted = true;

		// Shutdown providers (if any present).
		for (GetPlugins<PProvider, NoEntrypoint> providerIterator(
				PluginType::Provider, FB_P_PROVIDER_VERSION);
			 providerIterator.hasData();
			 providerIterator.next())
		{
			PProvider* provider = providerIterator.plugin();

			StatusVector status2(NULL);
			provider->shutdown(&status2, timeout, reason);

			if (status2[1])
				userStatus->set(error.value());
		}

		// Close all known interfaces from providers...
		{	// guard scope
			WriteLockGuard sync(handleMappingLock);

			{
				GenericMap<Pair<NonPooled<FB_API_HANDLE, YService*> > >::Accessor accessor(&services);

				if (accessor.getFirst())
				{
					do
					{
						PluginInterface()->releasePlugin(accessor.current()->second->provider);
					} while (accessor.getNext());
				}
			}

			{
				GenericMap<Pair<NonPooled<FB_API_HANDLE, YAttachment*> > >::Accessor accessor(&attachments);

				if (accessor.getFirst())
				{
					do
					{
						PluginInterface()->releasePlugin(accessor.current()->second->provider);
					} while (accessor.getNext());
				}
			}
		}

		// ... and wait for all providers to go away
		PluginManager::waitForType(PluginType::Provider);

		// Shutdown clients after providers.
		if (ShutChain::run(fb_shut_postproviders, reason) != FB_SUCCESS)
			userStatus->set(error.value());

		// Finish shutdown.
		if (ShutChain::run(fb_shut_finish, reason) != FB_SUCCESS)
			userStatus->set(error.value());

		// At this step callbacks are welcome to exit (or take actions to make main thread do it).
		if (ShutChain::run(fb_shut_exit, reason) != FB_SUCCESS)
			userStatus->set(error.value());
	}
	catch (const Exception& e)
	{
		e.stuffException(userStatus);
		gds__log_status(0, userStatus->get());
	}
}
