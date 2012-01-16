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

#define FB_COMPILING_WHY_CPP

#include "firebird.h"
#include "firebird/Provider.h"
#include "memory_routines.h"
#include "gen/iberror.h"
#include "gen/msg_facs.h"
#include "../yvalve/YObjects.h"
#include "../dsql/sqlda.h"
#include "../dsql/sqlda_pub.h"
#include "../common/common.h"
#include "../common/gdsassert.h"
#include "../common/db_alias.h"
#include "../common/StatementMetadata.h"
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
#include "../yvalve/why_proto.h"
#include "../yvalve/MasterImplementation.h"
#include "../yvalve/PluginManager.h"
#include "../remote/client/interface.h"
#include "../jrd/acl.h"
#include "../jrd/align.h"
#include "../jrd/blr.h"
#include "../jrd/msg_encode.h"
#include "../jrd/inf_pub.h"

#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif

using namespace Firebird;
using namespace Why;


static void badHandle(ISC_STATUS code);
static bool isNetworkError(const IStatus* status);
static void nullCheck(const FB_API_HANDLE* ptr, ISC_STATUS code);
static void saveErrorString(ISC_STATUS* status);
static bool setPath(const PathName& filename, PathName& expandedName);
static void error804(ISC_STATUS err);
static void sqldaDescribeParameters(XSQLDA* sqlda, const IParametersMetadata* parameters);
static void sqldaMove(const XSQLDA* sqlda, Array<UCHAR>& message, bool binding);
static void sqldaParse(const XSQLDA* sqlda, Array<UCHAR>& blr, Array<UCHAR>& message, USHORT dialect);
static ISC_STATUS openOrCreateBlob(ISC_STATUS* userStatus, FB_API_HANDLE* dbHandle,
	FB_API_HANDLE* traHandle, FB_API_HANDLE* blobHandle, ISC_QUAD* blobId,
	USHORT bpbLength, const UCHAR* bpb, bool createFlag);


//-------------------------------------


static const USHORT PREPARE_BUFFER_SIZE = 32768;	// size of buffer used in isc_dsql_prepare call
static const USHORT DESCRIBE_BUFFER_SIZE = 1024;	// size of buffer used in isc_dsql_describe_xxx call

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


// CVC: I'm following types_pub.h here. If there's a better solution, commit it, please.
#if defined(_LP64) || defined(__LP64__) || defined(__arch64__) || defined(_WIN64)
inline FB_API_HANDLE ULONG_TO_FB_API_HANDLE(ULONG h) { return static_cast<FB_API_HANDLE>(h); }
inline ULONG FB_API_HANDLE_TO_ULONG(FB_API_HANDLE h) { return h; }
#else
inline FB_API_HANDLE ULONG_TO_FB_API_HANDLE(ULONG h) { return reinterpret_cast<FB_API_HANDLE>(h); }
inline ULONG FB_API_HANDLE_TO_ULONG(FB_API_HANDLE h) { return reinterpret_cast<ULONG>(h); }
#endif


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
	} while (map->put(ULONG_TO_FB_API_HANDLE(handle), obj));

	return ULONG_TO_FB_API_HANDLE(handle);
}

template <typename T>
static void removeHandle(GenericMap<Pair<NonPooled<FB_API_HANDLE, T*> > >* map, FB_API_HANDLE& handle)
{
	if (handle)
	{
		WriteLockGuard sync(handleMappingLock);
		bool removed = map->remove(handle);

		fb_assert(removed);
		(void) removed;	// avoid warning in prod build
		handle = 0;
	}
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

namespace {

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

void signalInit()
{
#ifdef UNIX
	static GlobalPtr<CtrlCHandler> ctrlCHandler;
#endif // UNIX
}

} // anonymous namespace


//-------------------------------------

namespace Why
{
	// StatusVector:	Provides correct status vector for operation and init() it.
	class StatusVector : public AutoIface<IStatus, FB_STATUS_VERSION>
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

		// IStatus implementation
		void FB_CARG dispose()
		{ }

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

		void check()
		{
			if (!isSuccess())
				status_exception::raise(get());
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

	class BuiltinRegister
	{
	public:
		static void init()
		{
			PluginManagerInterfacePtr pi;
			Remote::registerRedirector(pi);
		}

		static void cleanup()
		{
		}
	};

	class NoEntrypoint
	{
	public:
		virtual void FB_CARG noEntrypoint(IStatus* s)
		{
			s->set(Arg::Gds(isc_wish_list).value());
		}
	};

	MakeUpgradeInfo<NoEntrypoint> upInfo;

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

	template <typename Y>
	class YEntry : public FpeControl	//// TODO: move FpeControl to the engine
	{
	public:
		YEntry(IStatus* aStatus, Y* object, int checkAttachment = 1)
			: ref(object->attachment), nextRef(object->next), counter(object->attachment)
		{
			aStatus->init();

			if (checkAttachment && !(nextRef.hasData()))
				Arg::Gds(Y::ERROR_CODE).raise();

			if (object->attachment && checkAttachment && object->attachment->savedStatus.getError())
				status_exception::raise(object->attachment->savedStatus.value());

			init();
		}

		~YEntry()
		{
			if (counter)
			{
				MutexLockGuard guard(counter->enterMutex);
				--counter->enterCount;
			}
		}

		void init()
		{
			signalInit();

			if (shutdownStarted)
			{
				Arg::Gds(isc_att_shutdown).raise();
			}

			if (counter)
			{
				MutexLockGuard guard(counter->enterMutex);
				++counter->enterCount;
			}
		}

		typename Y::NextInterface* next()
		{
			return nextRef;
		}

	private:
		YEntry(const YEntry&);	// prohibit copy constructor

	private:
		RefPtr<IRefCounted> ref;
		RefPtr<typename Y::NextInterface> nextRef;
		EnterCount* counter;
	};

	template <>
	YEntry<YAttachment>::YEntry(IStatus* aStatus, YAttachment* aAttachment, int checkAttachment)
		: ref(aAttachment), nextRef(aAttachment->next), counter(aAttachment)
	{
		aStatus->init();

		if (checkAttachment && !(nextRef.hasData()))
			Arg::Gds(YAttachment::ERROR_CODE).raise();

		if (aAttachment && checkAttachment && aAttachment->savedStatus.getError())
			status_exception::raise(aAttachment->savedStatus.value());

		init();
	}

	template <>
	YEntry<YService>::YEntry(IStatus* aStatus, YService* aService, int mode)
		: ref(aService), nextRef(NULL), counter(aService)
	{
		aStatus->init();

		nextRef = aService->getNextService(mode, aStatus);
		if ((mode != YService::SERV_DETACH) && !(nextRef.hasData()))
		{
			Arg::Gds(YService::ERROR_CODE).raise();
		}

		init();
	}

	class DispatcherEntry : public FpeControl	//// TODO: move FpeControl to the engine
	{
	public:
		explicit DispatcherEntry(IStatus* aStatus, bool ignoreShutdownError = false)
		{
			signalInit();

			static InitMutex<BuiltinRegister> registerBuiltinPlugins;
			registerBuiltinPlugins.init();

			if (shutdownStarted && !ignoreShutdownError)
			{
				Arg::Gds(isc_att_shutdown).raise();
			}
		}

	private:
		DispatcherEntry(const DispatcherEntry&);	// prohibit copy constructor
	};

}	// namespace Why


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

static bool isNetworkError(const IStatus* status)
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

// Raises a DSQL -804 error message.
static void error804(ISC_STATUS err)
{
	status_exception::raise(
		Arg::Gds(isc_dsql_error) << Arg::Gds(isc_sqlerr) << Arg::Num(-804) <<
		Arg::Gds(err));
}

// Describe parameters metadata in an sqlda.
static void sqldaDescribeParameters(XSQLDA* sqlda, const IParametersMetadata* parameters)
{
	if (!sqlda)
		return;

	if (sqlda->version != SQLDA_VERSION1)
		error804(isc_dsql_sqlda_err);

	StatusVector status(NULL);

	unsigned parametersCount = parameters->getCount(&status);
	status.check();
	sqlda->sqld = (USHORT) parametersCount;

	// If necessary, inform the application that more sqlda items are needed.
	if (sqlda->sqld > sqlda->sqln)
		return;

	for (unsigned i = 0; i < parametersCount; ++i)
	{
		XSQLVAR* var = &sqlda->sqlvar[i];
		const char* s;

		var->sqltype = parameters->getType(&status, i);
		status.check();

		var->sqltype |= (parameters->isNullable(&status, i) ? 1 : 0);
		status.check();

		var->sqlsubtype = parameters->getSubType(&status, i);
		status.check();

		var->sqllen = parameters->getLength(&status, i);
		status.check();

		var->sqlscale = parameters->getScale(&status, i);
		status.check();

		s = parameters->getField(&status, i);
		status.check();
		var->sqlname_length = fb_utils::snprintf(var->sqlname, sizeof(var->sqlname), "%s", s);

		s = parameters->getRelation(&status, i);
		status.check();
		var->relname_length = fb_utils::snprintf(var->relname, sizeof(var->relname), "%s", s);

		s = parameters->getOwner(&status, i);
		status.check();
		var->ownname_length = fb_utils::snprintf(var->ownname, sizeof(var->ownname), "%s", s);

		s = parameters->getAlias(&status, i);
		status.check();
		var->aliasname_length = fb_utils::snprintf(var->aliasname, sizeof(var->aliasname), "%s", s);
	}
}

// This routine moves data from the SQLDA into a message buffer or vice-versa.
static void sqldaMove(const XSQLDA* sqlda, Array<UCHAR>& message, bool binding)
{
	if (!sqlda)
		return;

	unsigned offset = 0;
	UCHAR* msgBuf = message.begin();

	const XSQLVAR* var = sqlda->sqlvar;

	for (USHORT i = 0; i < sqlda->sqld; ++i, ++var)
	{
		USHORT dtype = var->sqltype & ~1;
		USHORT len = var->sqllen;

		switch (dtype)
		{
			case SQL_VARYING:
				dtype = dtype_varying;
				len += sizeof(USHORT);
				break;

			case SQL_TEXT:
				dtype = dtype_text;
				break;

			case SQL_DOUBLE:
				dtype = dtype_double;
				break;

			case SQL_FLOAT:
				dtype = dtype_real;
				break;

			case SQL_D_FLOAT:
				dtype = dtype_d_float;
				break;

			case SQL_TYPE_DATE:
				dtype = dtype_sql_date;
				break;

			case SQL_TYPE_TIME:
				dtype = dtype_sql_time;
				break;

			case SQL_TIMESTAMP:
				dtype = dtype_timestamp;
				break;

			case SQL_BLOB:
				dtype = dtype_blob;
				break;

			case SQL_ARRAY:
				dtype = dtype_array;
				break;

			case SQL_LONG:
				dtype = dtype_long;
				break;

			case SQL_SHORT:
				dtype = dtype_short;
				break;

			case SQL_INT64:
				dtype = dtype_int64;
				break;

			case SQL_QUAD:
				dtype = dtype_quad;
				break;

			case SQL_BOOLEAN:
				dtype = dtype_boolean;
				break;

			case SQL_NULL:
				dtype = dtype_text;
				break;
		}

		unsigned align = type_alignments[dtype];
		if (align)
			offset = FB_ALIGN(offset, align);

		unsigned nullOffset = offset + len;

		align = type_alignments[dtype_short];
		if (align)
			nullOffset = FB_ALIGN(nullOffset, align);

		SSHORT* nullInd = (SSHORT*) (msgBuf + nullOffset);

		if (!binding)
		{
			// Move data from the message into the SQLDA.

			if ((var->sqltype & ~1) != SQL_NULL)
			{
				// Make sure user has specified a data location.
				if (!var->sqldata)
					error804(isc_dsql_sqlda_value_err);

				memcpy(var->sqldata, msgBuf + offset, len);
			}

			if (var->sqltype & 1)
			{
				// Make sure user has specified a location for null indicator.
				if (!var->sqlind)
					error804(isc_dsql_sqlda_value_err);

				*var->sqlind = *nullInd;
			}
		}
		else
		{
			// Move data from the SQLDA into the message. If the indicator variable identifies a
			// null value, permit the data value to be missing.

			if (var->sqltype & 1)
			{
				// Make sure user has specified a location for null indicator.
				if (!var->sqlind)
					error804(isc_dsql_sqlda_value_err);

				*nullInd = *var->sqlind;
			}
			else
				*nullInd = 0;

			// Make sure user has specified a data location (unless NULL).
			if (!var->sqldata && !*nullInd && (var->sqltype & ~1) != SQL_NULL)
				error804(isc_dsql_sqlda_value_err);

			// Copy data - unless known to be NULL.
			if (size_t(offset) + len > message.getCount())
				error804(isc_dsql_sqlda_value_err);

			if (!*nullInd)
				memcpy(msgBuf + offset, var->sqldata, len);
		}

		offset = nullOffset + sizeof(SSHORT);
	}
}

// This routine creates a blr message that describes a SQLDA.
static void sqldaParse(const XSQLDA* sqlda, Array<UCHAR>& blr, Array<UCHAR>& message, USHORT dialect)
{
	blr.clear();
	message.clear();

	if (!sqlda)
		return;

	if (sqlda->version != SQLDA_VERSION1)
		error804(isc_dsql_sqlda_err);

	const USHORT count = sqlda->sqld;
	fb_assert(count < MAX_USHORT / 2); // Nobody does this, but...

	if (count == 0)
		return;	// If there isn't an SQLDA, don't bother with anything else.

	// This is a call from execute or open, or the first call from fetch.
	// Determine the size of the blr, allocate a blr buffer, create the blr, and finally allocate
	// a message buffer.

	// The message buffer we are describing could be for a message to either IB 4.0 or IB 3.3 - thus
	// we need to describe the buffer with the right set of blr. As the BLR is only used to describe
	// space allocation and alignment, we can safely use blr_text for both 4.0 and 3.3.

	// Generate the blr for the message and at the same time, determine the size of the message
	// buffer. Allow for a null indicator with each variable in the SQLDA.

	if (dialect > SQL_DIALECT_V5)
		blr.add(blr_version5);
	else
		blr.add(blr_version4);

	blr.add(blr_begin);
	blr.add(blr_message);
	blr.add(0);
	blr.add((count * 2) & 0xFF);
	blr.add(((count * 2) >> 8) & 0xFF);

	unsigned msgLen = 0;
	const XSQLVAR* var = sqlda->sqlvar;

	for (USHORT i = 0; i < count; ++i, ++var)
	{
		USHORT dtype = var->sqltype & ~1;
		USHORT len = var->sqllen;

		switch (dtype)
		{
			case SQL_VARYING:
				blr.add(blr_varying);
				blr.add(len & 0xFF);
				blr.add((len >> 8) & 0xFF);
				dtype = dtype_varying;
				len += sizeof(USHORT);
				break;

			case SQL_TEXT:
				blr.add(blr_text);
				blr.add(len & 0xFF);
				blr.add((len >> 8) & 0xFF);
				dtype = dtype_text;
				break;

			case SQL_DOUBLE:
				blr.add(blr_double);
				dtype = dtype_double;
				break;

			case SQL_FLOAT:
				blr.add(blr_float);
				dtype = dtype_real;
				break;

			case SQL_D_FLOAT:
				blr.add(blr_d_float);
				dtype = dtype_d_float;
				break;

			case SQL_TYPE_DATE:
				blr.add(blr_sql_date);
				dtype = dtype_sql_date;
				break;

			case SQL_TYPE_TIME:
				blr.add(blr_sql_time);
				dtype = dtype_sql_time;
				break;

			case SQL_TIMESTAMP:
				blr.add(blr_timestamp);
				dtype = dtype_timestamp;
				break;

			case SQL_BLOB:
				blr.add(blr_quad);
				blr.add(0);
				dtype = dtype_blob;
				break;

			case SQL_ARRAY:
				blr.add(blr_quad);
				blr.add(0);
				dtype = dtype_array;
				break;

			case SQL_LONG:
				blr.add(blr_long);
				blr.add(var->sqlscale);
				dtype = dtype_long;
				break;

			case SQL_SHORT:
				blr.add(blr_short);
				blr.add(var->sqlscale);
				dtype = dtype_short;
				break;

			case SQL_INT64:
				blr.add(blr_int64);
				blr.add(var->sqlscale);
				dtype = dtype_int64;
				break;

			case SQL_QUAD:
				blr.add(blr_quad);
				blr.add(var->sqlscale);
				dtype = dtype_quad;
				break;

			case SQL_BOOLEAN:
				blr.add(blr_bool);
				dtype = dtype_boolean;
				break;

			case SQL_NULL:
				blr.add(blr_text);
				blr.add(len & 0xFF);
				blr.add((len >> 8) & 0xFF);
				dtype = dtype_text;
				break;

			default:
				error804(isc_dsql_sqlda_value_err);
		}

		blr.add(blr_short);
		blr.add(0);

		unsigned align = type_alignments[dtype];
		if (align)
			msgLen = FB_ALIGN(msgLen, align);

		msgLen += len;

		align = type_alignments[dtype_short];
		if (align)
			msgLen = FB_ALIGN(msgLen, align);

		msgLen += sizeof(SSHORT);
	}

	blr.add(blr_end);
	blr.add(blr_eoc);

	///memset(message.getBuffer(msgLen), 0, msgLen);
	message.getBuffer(msgLen);
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

		YAttachment* attachment = MasterImplementation::dispatcher->attachDatabase(
			&status, pathName.c_str(), dpbLength, reinterpret_cast<const UCHAR*>(dpb));

		if (!status.isSuccess())
			return status[1];

		*publicHandle = attachment->handle;
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

		YAttachment* attachment = MasterImplementation::dispatcher->createDatabase(
			&status, pathName.c_str(), dpbLength, reinterpret_cast<const UCHAR*>(dpb));

		if (!status.isSuccess())
			return status[1];

		*publicHandle = attachment->handle;
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
	USHORT /*dialect*/, XSQLDA* sqlda)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YStatement> statement(translateHandle(statements, stmtHandle));

		statement->checkPrepared();

		const IParametersMetadata* parameters = statement->next->getOutputParameters(&status);
		sqldaDescribeParameters(sqlda, parameters);
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Describe input parameters for a prepared statement.
ISC_STATUS API_ROUTINE isc_dsql_describe_bind(ISC_STATUS* userStatus, FB_API_HANDLE* stmtHandle,
	USHORT /*dialect*/, XSQLDA* sqlda)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YStatement> statement(translateHandle(statements, stmtHandle));

		const IParametersMetadata* parameters = statement->next->getInputParameters(&status);
		sqldaDescribeParameters(sqlda, parameters);
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
		RefPtr<YStatement> statement(translateHandle(statements, stmtHandle));

		statement->checkPrepared();

		Array<UCHAR> inBlr, inMessage, outBlr, outMessage;

		sqldaParse(inSqlda, inBlr, inMessage, dialect);
		sqldaMove(inSqlda, inMessage, true);

		sqldaParse(outSqlda, outBlr, outMessage, dialect);

		if (isc_dsql_execute2_m(status, traHandle, stmtHandle,
				inBlr.getCount(), reinterpret_cast<SCHAR*>(inBlr.begin()), 0,
				inMessage.getCount(), reinterpret_cast<SCHAR*>(inMessage.begin()),
				outBlr.getCount(), reinterpret_cast<SCHAR*>(outBlr.begin()), 0,
				outMessage.getCount(), reinterpret_cast<SCHAR*>(outMessage.begin())))
		{
			return status[1];
		}

		sqldaMove(outSqlda, outMessage, false);
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

		InternalMessageBuffer inMsgBuffer(inBlrLength, reinterpret_cast<const unsigned char*>(inBlr),
										  inMsgLength, reinterpret_cast<UCHAR*>(const_cast<SCHAR*>(inMsg)));
		InternalMessageBuffer outMsgBuffer(outBlrLength, reinterpret_cast<unsigned char*>(outBlr),
										   outMsgLength, reinterpret_cast<unsigned char*>(outMsg));

		ITransaction* newTrans = statement->execute(&status, transaction, inMsgType,
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
				// in this case we know for sure that newTrans points to YTransaction
				if (traHandle)
					*traHandle = static_cast<YTransaction*>(newTrans)->handle;
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

	try
	{
		if (!sqlStmt)
			Arg::Gds(isc_command_end_err).raise();

		Array<UCHAR> inBlr, inMessage, outBlr, outMessage;

		sqldaParse(inSqlda, inBlr, inMessage, dialect);
		sqldaMove(inSqlda, inMessage, true);

		sqldaParse(outSqlda, outBlr, outMessage, dialect);

		s = isc_dsql_exec_immed2_m(status, dbHandle, traHandle, stmtLength, sqlStmt, dialect,
				inBlr.getCount(), reinterpret_cast<SCHAR*>(inBlr.begin()), 0,
				inMessage.getCount(), reinterpret_cast<SCHAR*>(inMessage.begin()),
				outBlr.getCount(), reinterpret_cast<SCHAR*>(outBlr.begin()), 0,
				outMessage.getCount(), reinterpret_cast<SCHAR*>(outMessage.begin()));

		if (s == 0)
			sqldaMove(outSqlda, outMessage, false);
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
		s = status[1];
	}

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

		InternalMessageBuffer inMsgBuffer(inBlrLength, reinterpret_cast<const unsigned char*>(inBlr),
										  inMsgLength, reinterpret_cast<UCHAR*>(const_cast<SCHAR*>(inMsg)));
		InternalMessageBuffer outMsgBuffer(outBlrLength, reinterpret_cast<unsigned char*>(outBlr),
										   outMsgLength, reinterpret_cast<unsigned char*>(outMsg));

		ITransaction* newTrans = attachment->execute(&status, transaction, stmtLength, sqlStmt,
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
				// in this case we know for sure that newTrans points to YTransaction
				if (traHandle)
					*traHandle = static_cast<YTransaction*>(newTrans)->handle;
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

		Array<UCHAR> outBlr, outMessage;
		sqldaParse(sqlda, outBlr, outMessage, dialect);

		ISC_STATUS s = isc_dsql_fetch_m(status, stmtHandle,
			outBlr.getCount(), reinterpret_cast<SCHAR*>(outBlr.begin()), 0,
			outMessage.getCount(), reinterpret_cast<SCHAR*>(outMessage.begin()));

		if (s && s != 101)
			return s;

		sqldaMove(sqlda, outMessage, false);
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

		InternalMessageBuffer msgBuffer(blrLength, reinterpret_cast<UCHAR*>(blr),
										msgLength, reinterpret_cast<UCHAR*>(msg));

		return statement->fetch(&status, &msgBuffer);
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

		Array<UCHAR> inBlr, inMessage;
		sqldaParse(sqlda, inBlr, inMessage, dialect);
		sqldaMove(sqlda, inMessage, true);

		return isc_dsql_insert_m(status, stmtHandle,
			inBlr.getCount(), reinterpret_cast<SCHAR*>(inBlr.begin()), 0,
			inMessage.getCount(), reinterpret_cast<SCHAR*>(inMessage.begin()));
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

		InternalMessageBuffer msgBuffer(blrLength, reinterpret_cast<UCHAR*>(const_cast<SCHAR*>(blr)),
										msgLength, reinterpret_cast<UCHAR*>(const_cast<SCHAR*>(msg)));

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
		RefPtr<YTransaction> transaction;

		if (traHandle && *traHandle)
			transaction = translateHandle(transactions, traHandle);

		statement->prepare(&status, transaction, stmtLength, sqlStmt, dialect,
			IStatement::PREPARE_PREFETCH_METADATA);

		if (status.isSuccess())
		{
			statement->prepared = false;

			StatusVector tempStatus(NULL);
			const IParametersMetadata* parameters = statement->next->getOutputParameters(&tempStatus);
			tempStatus.check();

			sqldaDescribeParameters(sqlda, parameters);

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
		RefPtr<YStatement> statement(translateHandle(statements, stmtHandle));
		RefPtr<YTransaction> transaction;

		if (traHandle && *traHandle)
			transaction = translateHandle(transactions, traHandle);

		unsigned flags = StatementMetadata::buildInfoFlags(
			itemLength, reinterpret_cast<const UCHAR*>(items));

		statement->prepare(&status, transaction, stmtLength, sqlStmt, dialect, flags);

		if (status.isSuccess())
		{
			StatusVector tempStatus(NULL);
			statement->getInfo(&tempStatus, itemLength, reinterpret_cast<const UCHAR*>(items),
				bufferLength, reinterpret_cast<UCHAR*>(buffer));
			tempStatus.check();
		}
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Set a cursor name for a dynamic request.
ISC_STATUS API_ROUTINE isc_dsql_set_cursor_name(ISC_STATUS* userStatus, FB_API_HANDLE* stmtHandle,
	const SCHAR* cursorName, USHORT /*cursorType*/)
{
	StatusVector status(userStatus);

	try
	{
		RefPtr<YStatement> statement(translateHandle(statements, stmtHandle));
		statement->setCursorName(&status, cursorName);
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

		statement->getInfo(&status, USHORT(itemLength), reinterpret_cast<const UCHAR*>(items),
			USHORT(bufferLength), reinterpret_cast<UCHAR*>(buffer));
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

	class Callback : public AutoIface<IEventCallback, FB_EVENT_CALLBACK_VERSION>
	{
	public:
		explicit Callback(UCHAR* aBuffer)
			: buffer(aBuffer)
		{
		}

		// IEventCallback implementation
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

		class Callback : public VersionedIface<IEventCallback, FB_EVENT_CALLBACK_VERSION>,
						 public GlobalStorage
		{
		public:
			Callback(FPTR_EVENT_CALLBACK aAst, void* aArg)
				: ast(aAst),
				  arg(aArg)
			{
			}

			// IEventCallback implementation
			virtual void FB_CARG eventCallbackFunction(unsigned int length, const UCHAR* events)
			{
				ast(arg, length, events);
			}

			FPTR_EVENT_CALLBACK ast;
			void* arg;
		};

		Callback* callback = new Callback(ast, arg);

		events = attachment->queEvents(&status, callback, length, eventsData);

		if (!status.isSuccess())
		{
			delete callback;
			return status[1];
		}

		events->deleteCallback = true;
		*id = FB_API_HANDLE_TO_ULONG(events->handle);
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

		service = MasterImplementation::dispatcher->attachServiceManager(&status, svcName.c_str(),
			spbLength, reinterpret_cast<const UCHAR*>(spb));

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
	TEB* vector = (TEB*) vec;
	YTransaction* multiTrans = NULL;
	DtcStart* ds = NULL;

	try
	{
		nullCheck(traHandle, isc_bad_trans_handle);

		if (count <= 0 || !vector)
			status_exception::raise(Arg::Gds(isc_bad_teb_form));

		if (count == 1)
		{
			RefPtr<YAttachment> attachment(translateHandle(attachments, vector->teb_database));

			YTransaction* transaction = attachment->startTransaction(&status,
				vector->teb_tpb_length, vector->teb_tpb);

			if (transaction)
				*traHandle = transaction->handle;

			return status[1];
		}

		HalfStaticArray<DtcStart, 16> dtcStartBuffer;
		ds = dtcStartBuffer.getBuffer(count);
		memset(ds, 0, sizeof(DtcStart) * count);
		DtcStart* const end = ds + count;

		for (DtcStart* p = ds; p < end; ++p, ++vector)
		{
			RefPtr<YAttachment> attachment(translateHandle(attachments, vector->teb_database));
			p->attachment = attachment;
			attachment->addRef();
			p->tpbLength = vector->teb_tpb_length;
			p->tpb = reinterpret_cast<const unsigned char*>(vector->teb_tpb);
		}

		multiTrans = MasterImplementation::dtc->start(&status, count, ds);

		if (multiTrans)
			*traHandle = multiTrans->handle;
	}
	catch (const Exception& e)
	{
		StatusVector temp(NULL);

		if (multiTrans)
			multiTrans->rollback(&temp);

		e.stuffException(&status);
	}

	if (ds)
	{
		DtcStart* const end = ds + count;

		for (DtcStart* p = ds; p < end; ++p)
		{
			if (p->attachment)
				p->attachment->release();
		}
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
	MasterImplementation::dispatcher->shutdown(&status, timeout, reason);
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


// Get the legacy handle of a database.
ISC_STATUS API_ROUTINE fb_get_database_handle(ISC_STATUS* userStatus, FB_API_HANDLE* handle, void* obj)
{
	StatusVector status(userStatus);

	try
	{
		YAttachment* yObject = static_cast<YAttachment*>(obj);
		*handle = yObject->handle;
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


// Get the legacy handle of a transaction.
ISC_STATUS API_ROUTINE fb_get_transaction_handle(ISC_STATUS* userStatus, FB_API_HANDLE* handle, void* obj)
{
	StatusVector status(userStatus);

	try
	{
		YTransaction* yObject = static_cast<YTransaction*>(obj);
		*handle = yObject->handle;
	}
	catch (const Exception& e)
	{
		e.stuffException(&status);
	}

	return status[1];
}


//-------------------------------------

namespace Why {

IAttachment* MasterImplementation::registerAttachment(IProvider* provider, IAttachment* attachment)
{
	return new YAttachment(provider, attachment, "");
}

ITransaction* MasterImplementation::registerTransaction(IAttachment* attachment,
	ITransaction* transaction)
{
	return new YTransaction(static_cast<YAttachment*>(attachment), transaction);
}

template <typename Impl, typename Intf, int Vers>
YHelper<Impl, Intf, Vers>::YHelper(Intf* aNext)
{
	MasterInterfacePtr()->upgradeInterface(aNext, Vers, upInfo);
	next = aNext;
	this->addRef();
}

//-------------------------------------


YEvents::YEvents(YAttachment* aAttachment, IEvents* aNext, IEventCallback* aCallback)
	: YHelper<YEvents, IEvents, FB_EVENTS_VERSION>(aNext),
	  attachment(aAttachment),
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

void YEvents::cancel(IStatus* status)
{
	try
	{
		YEntry<YEvents> entry(status, this);

		entry.next()->cancel(status);

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
	: YHelper<YRequest, IRequest, FB_REQUEST_VERSION>(aNext),
	  attachment(aAttachment),
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

void YRequest::receive(IStatus* status, int level, unsigned int msgType,
	unsigned int length, unsigned char* message)
{
	try
	{
		YEntry<YRequest> entry(status, this);
		entry.next()->receive(status, level, msgType, length, message);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YRequest::send(IStatus* status, int level, unsigned int msgType,
	unsigned int length, const unsigned char* message)
{
	try
	{
		YEntry<YRequest> entry(status, this);
		entry.next()->send(status, level, msgType, length, message);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YRequest::getInfo(IStatus* status, int level, unsigned int itemsLength,
	const unsigned char* items, unsigned int bufferLength, unsigned char* buffer)
{
	try
	{
		YEntry<YRequest> entry(status, this);
		entry.next()->getInfo(status, level, itemsLength, items, bufferLength, buffer);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YRequest::start(IStatus* status, ITransaction* transaction, int level)
{
	try
	{
		YEntry<YRequest> entry(status, this);

		NextTransaction trans;
		attachment->getNextTransaction(status, transaction, trans);
		entry.next()->start(status, trans, level);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YRequest::startAndSend(IStatus* status, ITransaction* transaction, int level,
	unsigned int msgType, unsigned int length, const unsigned char* message)
{
	try
	{
		YEntry<YRequest> entry(status, this);

		NextTransaction trans;
		attachment->getNextTransaction(status, transaction, trans);
		entry.next()->startAndSend(status, trans, level, msgType, length, message);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YRequest::unwind(IStatus* status, int level)
{
	try
	{
		YEntry<YRequest> entry(status, this);
		entry.next()->unwind(status, level);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YRequest::free(IStatus* status)
{
	try
	{
		YEntry<YRequest> entry(status, this);

		entry.next()->free(status);

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
	: YHelper<YBlob, IBlob, FB_BLOB_VERSION>(aNext),
	  attachment(aAttachment),
	  transaction(aTransaction)
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

void YBlob::getInfo(IStatus* status, unsigned int itemsLength,
	const unsigned char* items, unsigned int bufferLength, unsigned char* buffer)
{
	try
	{
		YEntry<YBlob> entry(status, this);
		entry.next()->getInfo(status, itemsLength, items, bufferLength, buffer);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

unsigned int YBlob::getSegment(IStatus* status, unsigned int length, void* buffer)
{
	try
	{
		YEntry<YBlob> entry(status, this);
		return entry.next()->getSegment(status, length, buffer);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}

	return 0;
}

void YBlob::putSegment(IStatus* status, unsigned int length, const void* buffer)
{
	try
	{
		YEntry<YBlob> entry(status, this);
		entry.next()->putSegment(status, length, buffer);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YBlob::cancel(IStatus* status)
{
	try
	{
		YEntry<YBlob> entry(status, this);

		entry.next()->cancel(status);

		if (status->isSuccess())
			destroy();
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YBlob::close(IStatus* status)
{
	try
	{
		YEntry<YBlob> entry(status, this);

		entry.next()->close(status);

		if (status->isSuccess())
			destroy();
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

int YBlob::seek(IStatus* status, int mode, int offset)
{
	try
	{
		YEntry<YBlob> entry(status, this);
		return entry.next()->seek(status, mode, offset);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}

	return 0;
}


//-------------------------------------


YStatement::YStatement(YAttachment* aAttachment, IStatement* aNext)
	: YHelper<YStatement, IStatement, FB_STATEMENT_VERSION>(aNext),
	  attachment(aAttachment),
	  userHandle(NULL),
	  prepared(false)
{
	attachment->childStatements.add(this);
	handle = makeHandle(&statements, this);
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

	next = NULL;
	release();
}

void YStatement::prepare(IStatus* status, ITransaction* transaction,
	unsigned int stmtLength, const char* sqlStmt, unsigned int dialect, unsigned int flags)
{
	try
	{
		YEntry<YStatement> entry(status, this);

		if (!sqlStmt)
			Arg::Gds(isc_command_end_err).raise();

		NextTransaction trans;
		if (transaction)
			attachment->getNextTransaction(status, transaction, trans);

		entry.next()->prepare(status, trans, stmtLength, sqlStmt, dialect, flags);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YStatement::getInfo(IStatus* status, unsigned int itemsLength,
	const unsigned char* items, unsigned int bufferLength, unsigned char* buffer)
{
	try
	{
		YEntry<YStatement> entry(status, this);

		entry.next()->getInfo(status, itemsLength, items, bufferLength, buffer);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

unsigned YStatement::getType(IStatus* status)
{
	try
	{
		YEntry<YStatement> entry(status, this);

		return entry.next()->getType(status);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}

	return 0;
}

const char* YStatement::getPlan(IStatus* status, bool detailed)
{
	try
	{
		YEntry<YStatement> entry(status, this);

		return entry.next()->getPlan(status, detailed);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}

	return NULL;
}

const IParametersMetadata* YStatement::getInputParameters(IStatus* status)
{
	try
	{
		YEntry<YStatement> entry(status, this);

		return entry.next()->getInputParameters(status);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}

	return NULL;
}

const IParametersMetadata* YStatement::getOutputParameters(IStatus* status)
{
	try
	{
		YEntry<YStatement> entry(status, this);

		return entry.next()->getOutputParameters(status);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}

	return NULL;
}

FB_UINT64 YStatement::getAffectedRecords(IStatus* status)
{
	try
	{
		YEntry<YStatement> entry(status, this);

		return entry.next()->getAffectedRecords(status);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}

	return 0;
}

void YStatement::setCursorName(IStatus* status, const char* name)
{
	try
	{
		YEntry<YStatement> entry(status, this);

		entry.next()->setCursorName(status, name);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

ITransaction* YStatement::execute(IStatus* status, ITransaction* transaction,
	unsigned int inMsgType, const FbMessage* inMsgBuffer, const FbMessage* outMsgBuffer)
{
	try
	{
		YEntry<YStatement> entry(status, this);

		NextTransaction trans;
		if (transaction)
			attachment->getNextTransaction(status, transaction, trans);
		ITransaction* newTrans = entry.next()->execute(status, trans, inMsgType, inMsgBuffer, outMsgBuffer);

		if (newTrans)
		{
			if (newTrans == trans)
				newTrans = transaction;
			else
				newTrans = new YTransaction(attachment, newTrans);
		}

		return newTrans;
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}

	return NULL;
}

int YStatement::fetch(IStatus* status, const FbMessage* msgBuffer)
{
	try
	{
		YEntry<YStatement> entry(status, this);

		int s = entry.next()->fetch(status, msgBuffer);
		if (s == 100 || s == 101)
			return s;
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}

	return status->get()[1];
}

void YStatement::insert(IStatus* status, const FbMessage* msgBuffer)
{
	try
	{
		YEntry<YStatement> entry(status, this);

		checkPrepared();
		entry.next()->insert(status, msgBuffer);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YStatement::free(IStatus* status, unsigned int option)
{
	try
	{
		YEntry<YStatement> entry(status, this);

		entry.next()->free(status, option);

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
	: YHelper<YTransaction, ITransaction, FB_TRANSACTION_VERSION>(aNext),
	  attachment(aAttachment),
	  childBlobs(getPool()),
	  cleanupHandlers(getPool())
{
	if (attachment)
		attachment->childTransactions.add(this);
	handle = makeHandle(&transactions, this);
}

void YTransaction::destroy()
{
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

void YTransaction::getInfo(IStatus* status, unsigned int itemsLength,
	const unsigned char* items, unsigned int bufferLength, unsigned char* buffer)
{
	Array<unsigned char> newItemsBuffer;

	try
	{
		YEntry<YTransaction> entry(status, this);

		fb_utils::getDbPathInfo(itemsLength, items, bufferLength, buffer,
								newItemsBuffer, attachment->dbPath);

		entry.next()->getInfo(status, itemsLength, items, bufferLength, buffer);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YTransaction::prepare(IStatus* status, unsigned int msgLength, const unsigned char* message)
{
	try
	{
		YEntry<YTransaction> entry(status, this);

		entry.next()->prepare(status, msgLength, message);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YTransaction::commit(IStatus* status)
{
	try
	{
		YEntry<YTransaction> entry(status, this);

		entry.next()->commit(status);

		if (status->isSuccess())
			destroy();
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YTransaction::commitRetaining(IStatus* status)
{
	try
	{
		YEntry<YTransaction> entry(status, this);

		entry.next()->commitRetaining(status);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YTransaction::rollback(IStatus* status)
{
	try
	{
		YEntry<YTransaction> entry(status, this);

		entry.next()->rollback(status);
		if (isNetworkError(status))
			status->init();

		if (status->isSuccess())
			destroy();
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YTransaction::rollbackRetaining(IStatus* status)
{
	try
	{
		YEntry<YTransaction> entry(status, this);

		entry.next()->rollbackRetaining(status);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YTransaction::disconnect(IStatus* status)
{
	try
	{
		YEntry<YTransaction> entry(status, this);

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

void YTransaction::addCleanupHandler(IStatus* status, CleanupCallback* callback)
{
	try
	{
		YEntry<YTransaction> entry(status, this);

		cleanupHandlers.add(callback);
	}
	catch (const Exception& ex)
	{
		ex.stuffException(status);
	}
}

void YTransaction::selfCheck()
{
	if (!next)
		Arg::Gds(isc_bad_trans_handle).raise();
}

ITransaction* FB_CARG YTransaction::join(IStatus* status, ITransaction* transaction)
{
	try
	{
		YEntry<YTransaction> entry(status, this);

		return DtcInterfacePtr()->join(status, this, transaction);
	}
	catch (const Exception& ex)
	{
		ex.stuffException(status);
	}

	return NULL;
}

ITransaction* FB_CARG YTransaction::validate(IStatus* status, IAttachment* testAtt)
{
	try
	{
		YEntry<YTransaction> entry(status, this);

		// Do not raise error in status - just return NULL if attachment does not match
		if (attachment == testAtt)
			return this;

		return entry.next()->validate(status, testAtt);
	}
	catch (const Exception& ex)
	{
		ex.stuffException(status);
	}

	return NULL;
}

YTransaction* FB_CARG YTransaction::enterDtc(IStatus* status)
{
	try
	{
		YEntry<YTransaction> entry(status, this);

		YTransaction* copy = new YTransaction(this);
		// copy is created with zero handle
		copy->addRef();

		if (attachment)
			attachment->childTransactions.remove(this);

		removeHandle(&transactions, handle);
		next = NULL;
		release();

		return copy;
	}
	catch (const Exception& ex)
	{
		ex.stuffException(status);
	}

	return NULL;
}


//-------------------------------------


YAttachment::YAttachment(IProvider* aProvider, IAttachment* aNext, const PathName& aDbPath)
	: YHelper<YAttachment, IAttachment, FB_ATTACHMENT_VERSION>(aNext),
	  provider(aProvider),
	  dbPath(getPool(), aDbPath),
	  childBlobs(getPool()),
	  childEvents(getPool()),
	  childRequests(getPool()),
	  childStatements(getPool()),
	  childTransactions(getPool()),
	  cleanupHandlers(getPool())
{
	provider->addRef();
	handle = makeHandle(&attachments, this);
}

YAttachment::~YAttachment()
{
	if (provider)
		PluginManagerInterfacePtr()->releasePlugin(provider);
}

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

void YAttachment::getInfo(IStatus* status, unsigned int itemsLength,
	const unsigned char* items, unsigned int bufferLength, unsigned char* buffer)
{
	try
	{
		YEntry<YAttachment> entry(status, this);
		entry.next()->getInfo(status, itemsLength, items, bufferLength, buffer);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

YTransaction* YAttachment::startTransaction(IStatus* status, unsigned int tpbLength,
	const unsigned char* tpb)
{
	try
	{
		YEntry<YAttachment> entry(status, this);

		ITransaction* transaction = entry.next()->startTransaction(status, tpbLength, tpb);
		if (transaction)
			return new YTransaction(this, transaction);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}

	return NULL;
}

YTransaction* YAttachment::reconnectTransaction(IStatus* status, unsigned int length,
	const unsigned char* id)
{
	try
	{
		YEntry<YAttachment> entry(status, this);

		ITransaction* transaction = entry.next()->reconnectTransaction(status, length, id);

		if (transaction)
			return new YTransaction(this, transaction);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}

	return NULL;
}

YStatement* YAttachment::allocateStatement(IStatus* status)
{
	try
	{
		YEntry<YAttachment> entry(status, this);

		IStatement* statement = entry.next()->allocateStatement(status);
		return statement ? new YStatement(this, statement) : NULL;
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}

	return NULL;
}

YRequest* YAttachment::compileRequest(IStatus* status, unsigned int blrLength,
	const unsigned char* blr)
{
	try
	{
		YEntry<YAttachment> entry(status, this);

		IRequest* request = entry.next()->compileRequest(status, blrLength, blr);
		return request ? new YRequest(this, request) : NULL;
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}

	return NULL;
}

void YAttachment::transactRequest(IStatus* status, ITransaction* transaction,
	unsigned int blrLength, const unsigned char* blr,
	unsigned int inMsgLength, const unsigned char* inMsg,
	unsigned int outMsgLength, unsigned char* outMsg)
{
	try
	{
		YEntry<YAttachment> entry(status, this);

		NextTransaction trans;
		getNextTransaction(status, transaction, trans);

		entry.next()->transactRequest(status, trans, blrLength, blr, inMsgLength, inMsg,
			outMsgLength, outMsg);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

YBlob* YAttachment::createBlob(IStatus* status, ITransaction* transaction, ISC_QUAD* id,
	unsigned int bpbLength, const unsigned char* bpb)
{
	try
	{
		YEntry<YAttachment> entry(status, this);

		YTransaction* yTra = getTransaction(status, transaction);
		NextTransaction nextTra(yTra->next);

		IBlob* blob = entry.next()->createBlob(status, nextTra, id, bpbLength, bpb);
		return blob ? new YBlob(this, yTra, blob) : NULL;
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}

	return NULL;
}

YBlob* YAttachment::openBlob(IStatus* status, ITransaction* transaction, ISC_QUAD* id,
	unsigned int bpbLength, const unsigned char* bpb)
{
	try
	{
		YEntry<YAttachment> entry(status, this);

		YTransaction* yTra = getTransaction(status, transaction);
		NextTransaction nextTra(yTra->next);

		IBlob* blob = entry.next()->openBlob(status, nextTra, id, bpbLength, bpb);
		return blob ? new YBlob(this, yTra, blob) : NULL;
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}

	return NULL;
}

int YAttachment::getSlice(IStatus* status, ITransaction* transaction, ISC_QUAD* id,
	unsigned int sdlLength, const unsigned char* sdl, unsigned int paramLength,
	const unsigned char* param, int sliceLength, unsigned char* slice)
{
	try
	{
		YEntry<YAttachment> entry(status, this);

		NextTransaction trans;
		getNextTransaction(status, transaction, trans);

		return entry.next()->getSlice(status, trans, id, sdlLength, sdl, paramLength, param,
			sliceLength, slice);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}

	return 0;
}

void YAttachment::putSlice(IStatus* status, ITransaction* transaction, ISC_QUAD* id,
	unsigned int sdlLength, const unsigned char* sdl, unsigned int paramLength,
	const unsigned char* param, int sliceLength, unsigned char* slice)
{
	try
	{
		YEntry<YAttachment> entry(status, this);

		NextTransaction trans;
		getNextTransaction(status, transaction, trans);

		entry.next()->putSlice(status, trans, id, sdlLength, sdl, paramLength, param, sliceLength, slice);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YAttachment::ddl(IStatus* status, ITransaction* transaction, unsigned int length,
	const unsigned char* dyn)
{
	try
	{
		YEntry<YAttachment> entry(status, this);

		NextTransaction trans;
		getNextTransaction(status, transaction, trans);

		return entry.next()->ddl(status, trans, length, dyn);
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

ITransaction* YAttachment::execute(IStatus* status, ITransaction* transaction,
	unsigned int length, const char* string, unsigned int dialect, unsigned int inMsgType,
	const FbMessage* inMsgBuffer, const FbMessage* outMsgBuffer)
{
	try
	{
		YEntry<YAttachment> entry(status, this);

		NextTransaction trans;
		if (transaction)
			getNextTransaction(status, transaction, trans);

		ITransaction* newTrans = entry.next()->execute(status, trans, length, string, dialect,
			inMsgType, inMsgBuffer, outMsgBuffer);

		if (newTrans)
		{
			if (newTrans == trans)
				newTrans = transaction;
			else
				newTrans = new YTransaction(this, newTrans);
		}

		return newTrans;
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}

	return NULL;
}

YEvents* YAttachment::queEvents(IStatus* status, IEventCallback* callback,
	unsigned int length, const unsigned char* eventsData)
{
	try
	{
		YEntry<YAttachment> entry(status, this);

		IEvents* events = entry.next()->queEvents(status, callback, length, eventsData);
		return events ? new YEvents(this, events, callback) : NULL;
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}

	return NULL;
}

void YAttachment::cancelOperation(IStatus* status, int option)
{
	try
	{
		YEntry<YAttachment> entry(status, this);

		// Mutex will be locked here for a really long time.
		MutexLockGuard guard(enterMutex);

		if (enterCount > 1 || option != fb_cancel_raise)
			entry.next()->cancelOperation(status, option);
		else
			status_exception::raise(Arg::Gds(isc_nothing_to_cancel));
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YAttachment::ping(IStatus* status)
{
	try
	{
		YEntry<YAttachment> entry(status, this);

		entry.next()->ping(status);

		if (!status->isSuccess())
		{
			if (!savedStatus.getError())
				savedStatus.save(status->get());

			StatusVector temp(NULL);
			entry.next()->detach(&temp);
			next = NULL;

			status_exception::raise(savedStatus.value());
		}
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YAttachment::detach(IStatus* status)
{
	try
	{
		YEntry<YAttachment> entry(status, this, 0);

		if (entry.next())
			entry.next()->detach(status);

		if (status->isSuccess())
			destroy();
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YAttachment::drop(IStatus* status)
{
	try
	{
		YEntry<YAttachment> entry(status, this);

		entry.next()->drop(status);

		if (status->isSuccess())
			destroy();
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YAttachment::addCleanupHandler(IStatus* status, CleanupCallback* callback)
{
	try
	{
		YEntry<YAttachment> entry(status, this);

		cleanupHandlers.add(callback);
	}
	catch (const Exception& ex)
	{
		ex.stuffException(status);
	}
}


YTransaction* YAttachment::getTransaction(IStatus* status, ITransaction* tra)
{
	if (!tra)
		Arg::Gds(isc_bad_trans_handle).raise();

	status->init();

	// If validation is successfull, this means that this attachment and valid transaction
	// use same provider. I.e. the following cast is safe.
	YTransaction* yt = static_cast<YTransaction*>(tra->validate(status, this));
	if (!status->isSuccess())
		status_exception::raise(status->get());
	if (!yt)
		Arg::Gds(isc_bad_trans_handle).raise();

	yt->selfCheck();
	return yt;
}


void YAttachment::getNextTransaction(IStatus* status, ITransaction* tra, NextTransaction& next)
{
	next = getTransaction(status, tra)->next;
	if (!next.hasData())
		Arg::Gds(isc_bad_trans_handle).raise();
}


//-------------------------------------


static IService* getServiceManagerByName(IProvider** provider, IStatus* status,
	const char* serviceName, unsigned int spbLength, const unsigned char* spb)
{
	for (GetPlugins<IProvider> providerIterator(PluginType::Provider,
			FB_PROVIDER_VERSION, upInfo);
		 providerIterator.hasData();
		 providerIterator.next())
	{
		IProvider* p = providerIterator.plugin();

		IService* service = p->attachServiceManager(status, serviceName, spbLength, spb);

		if (status->isSuccess())
		{
			if (provider)
			{
				p->addRef();
				*provider = p;
			}

			return service;
		}

	}

	if (status->isSuccess())
	{
		(Arg::Gds(isc_service_att_err) <<
		 Arg::Gds(isc_random) << "No providers loaded").copyTo(status);
	}

	return NULL;
}

YService::ServiceType::~ServiceType()
{
	if (provider)
	{
		PluginManagerInterfacePtr()->releasePlugin(provider);
		provider = NULL;
	}
}

void YService::ServiceType::shutdown()
{
	if (provider)
	{
		next = NULL;
		PluginManagerInterfacePtr()->releasePlugin(provider);
		provider = NULL;
	}
}

void YService::ServiceType::detach(IStatus* status)
{
	if (next.hasData())
	{
		next->detach(status);
		if (!status->isSuccess())
		{
			status_exception::raise(status->get());
		}
	}
}

YService::YService(IProvider* aProvider, IService* aNext)
	: regular(aNext, aProvider),
	  attachName(getPool()),
	  checkSpbLen(0),
	  checkSpbPresent(NULL),
	  authBlock(getPool())
{
	handle = makeHandle(&services, this);
	this->addRef();		// from YHelper
}

YService::YService(const char* svcName, unsigned int spbLength, const unsigned char* spb)
	: attachName(getPool()),
	  attachSpb(FB_NEW(getPool()) ClumpletWriter(getPool(), ClumpletReader::spbList,
												 MAX_DPB_SIZE, spb, spbLength)),
	  checkSpbLen(0),
	  checkSpbPresent(NULL),
	  authBlock(getPool())
{
	attachName.assign(svcName);
	handle = makeHandle(&services, this);
	this->addRef();		// from YHelper

	if (attachSpb->find(isc_spb_auth_block))
		authBlock.add(attachSpb->getBytes(), attachSpb->getClumpLength());
}

IService* YService::getNextService(int mode, IStatus* status)
{
	if (regular.next)
		return regular.next;

	switch (mode)
	{
		case SERV_START:
			if (started.provider)
				started.shutdown();

			started.next = getServiceManagerByName(&started.provider, status, attachName.c_str(),
				(attachSpb ? attachSpb->getBufferLength() : 0),
				(attachSpb ? attachSpb->getBuffer() : NULL));

			if (!status->isSuccess())
				status_exception::raise(status->get());

			return started.next;

		case SERV_QUERY:
			if (fb_utils::isRunningCheck(checkSpbPresent, checkSpbLen))
				return started.next;

			if (queryCache.next)
				return queryCache.next;

			queryCache.next = getServiceManagerByName(&queryCache.provider, status, attachName.c_str(),
				(attachSpb ? attachSpb->getBufferLength() : 0),
				(attachSpb ? attachSpb->getBuffer() : NULL));

			if (!status->isSuccess())
				status_exception::raise(status->get());

			return queryCache.next;

		case SERV_DETACH:
			break;

		default:
			fb_assert(false);
	}

	return NULL;
}

YService::~YService()
{
}

void YService::destroy()
{
	removeHandle(&services, handle);

	regular.next = NULL;
	started.next = NULL;
	queryCache.next = NULL;
	release();
}

void YService::detach(IStatus* status)
{
	try
	{
		YEntry<YService> entry(status, this, SERV_DETACH);

		regular.detach(status);
		started.detach(status);
		queryCache.detach(status);

		destroy();
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

void YService::populateSpb(ClumpletWriter& spb, UCHAR tag)
{
	if (attachSpb)
	{
		if (attachSpb->find(isc_spb_auth_block))
			attachSpb->deleteClumplet();
	}
	else
		attachSpb = FB_NEW(getPool()) ClumpletWriter(getPool(), ClumpletReader::spbList, MAX_DPB_SIZE);

	if (spb.find(tag))
	{
		attachSpb->insertBytes(isc_spb_auth_block, spb.getBytes(), spb.getClumpLength());
		spb.deleteClumplet();
	}
	else
		attachSpb->insertTag(isc_spb_auth_block);
}

void YService::query(IStatus* status, unsigned int sendLength, const unsigned char* sendItems,
	unsigned int receiveLength, const unsigned char* receiveItems,
	unsigned int bufferLength, unsigned char* buffer)
{
	try
	{
		ClumpletWriter spb(ClumpletReader::SpbSendItems, MAX_DPB_SIZE, sendItems, sendLength);
		if (!regular.next)
			populateSpb(spb, isc_info_svc_auth_block);

		checkSpbLen = receiveLength;
		checkSpbPresent = receiveItems;
		YEntry<YService> entry(status, this, SERV_QUERY);
		entry.next()->query(status, spb.getBufferLength(), spb.getBuffer(),
			receiveLength, receiveItems, bufferLength, buffer);
		checkSpbLen = 0;
		checkSpbPresent = NULL;
	}
	catch (const Exception& e)
	{
		checkSpbLen = 0;
		checkSpbPresent = NULL;
		e.stuffException(status);
	}
}

void YService::start(IStatus* status, unsigned int spbLength, const unsigned char* spbItems)
{
	try
	{
		ClumpletWriter spb(ClumpletReader::SpbStart, MAX_DPB_SIZE, spbItems, spbLength);
		if (!regular.next)
			populateSpb(spb, isc_spb_auth_block);

		YEntry<YService> entry(status, this, SERV_START);
		entry.next()->start(status, spb.getBufferLength(), spb.getBuffer());
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}
}

int YService::release()
{
	if (--this->refCounter == 0)
	{
		if (regular.next || started.next || queryCache.next)
		{
			++this->refCounter; // to be decremented in destroy()
			++this->refCounter; // to avoid recursion
			this->destroy(); // destroy() must call release()
			--this->refCounter;
		}

		delete this; // call correct destructor !
		return 0;
	}

	return 1;
}


//-------------------------------------


// Attach a database through the first subsystem that recognizes it.
YAttachment* Dispatcher::attachDatabase(IStatus* status, const char* filename,
	unsigned int dpbLength, const unsigned char* dpb)
{
	try
	{
		DispatcherEntry entry(status);

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

		setLogin(newDpb, false);
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
		IStatus* currentStatus = status;

		PathName dummy;
		RefPtr<Config> config;
		ResolveDatabaseAlias(expandedFilename, dummy, &config);

		for (GetPlugins<IProvider> providerIterator(PluginType::Provider,
				FB_PROVIDER_VERSION, upInfo, config);
			 providerIterator.hasData();
			 providerIterator.next())
		{
			IProvider* provider = providerIterator.plugin();

			IAttachment* attachment = provider->attachDatabase(currentStatus,
				expandedFilename.c_str(), newDpb.getBufferLength(), newDpb.getBuffer());

			if (currentStatus->isSuccess())
			{
				status->set(currentStatus->get());
				return new YAttachment(provider, attachment, filename);
			}

			if (currentStatus->get()[1] != isc_unavailable)
				currentStatus = &temp;

			currentStatus->init();
		}

		if (status->get()[1] == 0)
			Arg::Gds(isc_unavailable).raise();
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}

	return NULL;
}

YAttachment* Dispatcher::createDatabase(IStatus* status, const char* filename,
	unsigned int dpbLength, const unsigned char* dpb)
{
	try
	{
		DispatcherEntry entry(status);

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

		setLogin(newDpb, false);
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
		IStatus* currentStatus = status;

		/***
		PathName dummy;
		RefPtr<Config> config;
		ResolveDatabaseAlias(expandedFilename, dummy, &config);
		***/

		for (GetPlugins<IProvider> providerIterator(PluginType::Provider,
				FB_PROVIDER_VERSION, upInfo/***, config***/);
			 providerIterator.hasData();
			 providerIterator.next())
		{
			IProvider* provider = providerIterator.plugin();

			IAttachment* attachment = provider->createDatabase(currentStatus,
				expandedFilename.c_str(), newDpb.getBufferLength(), newDpb.getBuffer());

			if (currentStatus->isSuccess())
			{
				// ASF: Why this is different than in attachDatabase, which always uses expandedFilename?
#ifdef WIN_NT
				PathName path(expandedFilename);
#else
				PathName path(orgFilename);
#endif

				status->set(currentStatus->get());
				return new YAttachment(provider, attachment, filename);
			}

			if (currentStatus->get()[1] != isc_unavailable)
				currentStatus = &temp;

			currentStatus->init();
		}

		if (status->get()[1] == 0)
			Arg::Gds(isc_unavailable).raise();
	}
	catch (const Exception& e)
	{
		e.stuffException(status);
	}

	return NULL;
}

// Attach a service through the first subsystem that recognizes it.
YService* Dispatcher::attachServiceManager(IStatus* status, const char* serviceName,
	unsigned int spbLength, const unsigned char* spb)
{
	IService* service = NULL;

	try
	{
		DispatcherEntry entry(status);

		if (!serviceName)
			status_exception::raise(Arg::Gds(isc_service_att_err) << Arg::Gds(isc_svc_name_missing));

		if (spbLength > 0 && !spb)
			status_exception::raise(Arg::Gds(isc_bad_spb_form) <<
									Arg::Gds(isc_random) << "NULL data with non-zero SPB length");

		PathName svcName(serviceName);
		svcName.trim();

		ClumpletWriter spbWriter(ClumpletReader::spbList, MAX_DPB_SIZE, spb, spbLength);
		setLogin(spbWriter, true);

		if ((spbWriter.find(isc_spb_auth_block) && spbWriter.getClumpLength() > 0) ||
			ISC_check_if_remote(svcName, false))
		{
			IProvider* provider = NULL;
			service = getServiceManagerByName(&provider, status, svcName.c_str(),
											  spbWriter.getBufferLength(), spbWriter.getBuffer());

			if (service)
				return new YService(provider, service);
		}
		else
			return new YService(svcName.c_str(), spbWriter.getBufferLength(), spbWriter.getBuffer());
	}
	catch (const Exception& e)
	{
		if (service)
		{
			StatusVector temp(NULL);
			service->detach(&temp);
		}

		e.stuffException(status);
	}

	return NULL;
}

void Dispatcher::shutdown(IStatus* userStatus, unsigned int timeout, const int reason)
{
	try
	{
		DispatcherEntry entry(userStatus, true);

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

		// Shut known components, internal for libfbclient
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
		for (GetPlugins<IProvider> providerIterator(PluginType::Provider,
				FB_PROVIDER_VERSION, upInfo);
			 providerIterator.hasData();
			 providerIterator.next())
		{
			IProvider* provider = providerIterator.plugin();

			StatusVector status2(NULL);
			provider->shutdown(&status2, timeout, reason);

			if (status2[1])
				userStatus->set(error.value());
		}

		// Close all known interfaces from providers...
		bool hasThreads;

		do
		{
			THD_yield();
			WriteLockGuard sync(handleMappingLock);
			hasThreads = false;

			{
				GenericMap<Pair<NonPooled<FB_API_HANDLE, YService*> > >::Accessor accessor(&services);

				if (accessor.getFirst())
				{
					do
					{
						YService* service = accessor.current()->second;
						if (service->enterCount)
						{
							hasThreads = true;
							break;
						}

						service->shutdown();
					} while (accessor.getNext());
				}

				if (hasThreads)
					continue;
			}

			{
				GenericMap<Pair<NonPooled<FB_API_HANDLE, YAttachment*> > >::Accessor accessor(&attachments);

				if (accessor.getFirst())
				{
					do
					{
						YAttachment* attachment = accessor.current()->second;
						if (attachment->enterCount)
						{
							hasThreads = true;
							continue;
						}

						if (attachment->provider)
						{
							attachment->next = NULL;
							PluginManagerInterfacePtr()->releasePlugin(attachment->provider);
							attachment->provider = NULL;
						}
					} while (accessor.getNext());
				}
			}
		} while (hasThreads);

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

} // namespace Why
