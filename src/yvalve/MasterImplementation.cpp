/*
 *	PROGRAM:		Firebird interface.
 *	MODULE:			MasterImplementation.cpp
 *	DESCRIPTION:	Main firebird interface, used to get other interfaces.
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
 *  The Original Code was created by Alex Peshkov
 *  for the Firebird Open Source RDBMS project.
 *
 *  Copyright (c) 2010 Alex Peshkov <peshkoff at mail.ru>
 *  and all contributors signed below.
 *
 *  All Rights Reserved.
 *  Contributor(s): ______________________________________.
 *
 *
 */

#include "firebird.h"
#include "Interface.h"
#include "../common/classes/ImplementHelper.h"
#include "../common/classes/init.h"
#include "../common/StatusHolder.h"
#include "../yvalve/PluginManager.h"
#include "../common/classes/GenericMap.h"
#include "../common/classes/fb_pair.h"
#include "../common/classes/rwlock.h"
#include "../common/isc_proto.h"

namespace Firebird {

class MasterImplementation : public StackIface<IMaster, IMaster::VERSION>
{
public:
	Status* FB_CARG getStatusInstance();
	IPlugin* FB_CARG getPluginInterface();
	int FB_CARG upgradeInterface(Interface* toUpgrade, int desiredVersion, void* missingFunctionClass);
	const char* FB_CARG circularAlloc(const char* s, size_t len, intptr_t thr);
};

//
// getStatusInstance()
//

class UserStatus : public Firebird::StdIface<Firebird::BaseStatus, FB_STATUS_VERSION>
{
private:
	int FB_CARG release()
	{
		if (--refCounter == 0)
		{
			delete this;
			return 0;
		}
		return 1;
	}
};

Firebird::Status* FB_CARG Firebird::MasterImplementation::getStatusInstance()
{
	return new UserStatus;
}

//
// getPluginInterface()
//

IPlugin* FB_CARG MasterImplementation::getPluginInterface()
{
	static Static<PluginManager> manager;

	manager->addRef();
	return &manager;
}

//
// upgradeInterface()
//

namespace {
	typedef void function();
	typedef function* FunctionPtr;

	struct CVirtualClass
	{
		FunctionPtr* vTab;
	};

	typedef Firebird::Pair<Firebird::NonPooled<U_IPTR, FunctionPtr*> > FunctionPair;
	GlobalPtr<GenericMap<FunctionPair> > functionMap;
	GlobalPtr<RWLock> mapLock;
}

int FB_CARG MasterImplementation::upgradeInterface(Interface* toUpgrade,
												   int desiredVersion,
												   void* missingFunctionClass)
{
	if (toUpgrade->version() >= desiredVersion)
		return 0;

	FunctionPtr* newTab = NULL;
	try
	{
		CVirtualClass* target = (CVirtualClass*) toUpgrade;

		{ // sync scope
			ReadLockGuard sync(mapLock);
			if (functionMap->get((U_IPTR) target->vTab, newTab))
			{
				target->vTab = newTab;
				return 0;
			}
		}

		WriteLockGuard sync(mapLock);

		if (!functionMap->get((U_IPTR) target->vTab, newTab))
		{
			CVirtualClass* miss = (CVirtualClass*) missingFunctionClass;
			newTab = FB_NEW(*getDefaultMemoryPool()) FunctionPtr[desiredVersion];

			for (int i = 0; i < toUpgrade->version(); ++i)
				newTab[i] = target->vTab[i];
			for (int j = toUpgrade->version(); j < desiredVersion; ++j)
				newTab[j] = miss->vTab[0];

			functionMap->put((U_IPTR) target->vTab, newTab);
		}

		target->vTab = newTab;
	}
	catch (const Exception& ex)
	{
		ISC_STATUS_ARRAY s;
		ex.stuff_exception(s);
		iscLogStatus("upgradeInterface", s);
		if (newTab)
		{
			delete[] newTab;
		}
		return -1;
	}

	return 0;
}

} // namespace Firebird

//
// circularAlloc()
//

#ifdef WIN_NT
#include <windows.h>
#else
#ifndef USE_THREAD_DESTRUCTOR
#include <pthread.h>
#include <signal.h>
#endif
#endif

namespace {

class StringsBuffer
{
private:
	class ThreadBuffer : public Firebird::GlobalStorage
	{
	private:
		const static size_t BUFFER_SIZE = 4096;
		char buffer[BUFFER_SIZE];
		char* buffer_ptr;
		FB_THREAD_ID thread;

	public:
		explicit ThreadBuffer(FB_THREAD_ID thr) : buffer_ptr(buffer), thread(thr) { }

		const char* alloc(const char* string, size_t length)
		{
			// if string is already in our buffer - return it
			// it was already saved in our buffer once
			if (string >= buffer && string < &buffer[BUFFER_SIZE])
				return string;

			// if string too long, truncate it
			if (length > BUFFER_SIZE / 4)
				length = BUFFER_SIZE / 4;

			// If there isn't any more room in the buffer, start at the beginning again
			if (buffer_ptr + length + 1 > buffer + BUFFER_SIZE)
				buffer_ptr = buffer;

			char* new_string = buffer_ptr;
			memcpy(new_string, string, length);
			new_string[length] = 0;
			buffer_ptr += length + 1;

			return new_string;
		}

		bool thisThread(FB_THREAD_ID currTID)
		{
#ifdef WIN_NT
			if (thread != currTID)
			{
				HANDLE hThread = OpenThread(THREAD_QUERY_INFORMATION, false, thread);
				// commented exit code check - looks like OS does not return handle
				// for already exited thread
				//DWORD exitCode = STILL_ACTIVE;
				if (hThread)
				{
					//GetExitCodeThread(hThread, &exitCode);
					CloseHandle(hThread);
				}

				//if ((!hThread) || (exitCode != STILL_ACTIVE))
				if (!hThread)
				{
					// Thread does not exist any more
					thread = currTID;
				}
			}
#else
#ifndef USE_THREAD_DESTRUCTOR
			if (thread != currTID)
			{
				if (pthread_kill(thread, 0) == ESRCH)
				{
					// Thread does not exist any more
					thread = currTID;
				}
			}
#endif // USE_THREAD_DESTRUCTOR
#endif // WIN_NT

			return thread == currTID;
		}
	};

	typedef Firebird::Array<ThreadBuffer*> ProcessBuffer;

	ProcessBuffer processBuffer;
	Firebird::Mutex mutex;

public:
	explicit StringsBuffer(Firebird::MemoryPool& p) : processBuffer(p) { }

	~StringsBuffer()
	{
		ThreadCleanup::remove(cleanupAllStrings, this);
	}

private:
	size_t position(FB_THREAD_ID thr)
	{
		// mutex should be locked when this function is called

		for (size_t i = 0; i < processBuffer.getCount(); ++i)
		{
			if (processBuffer[i]->thisThread(thr))
			{
				return i;
			}
		}

		return processBuffer.getCount();
	}

	ThreadBuffer* getThreadBuffer(FB_THREAD_ID thr)
	{
		Firebird::MutexLockGuard guard(mutex);

		size_t p = position(thr);
		if (p < processBuffer.getCount())
		{
			return processBuffer[p];
		}

		ThreadBuffer* b = new ThreadBuffer(thr);
		processBuffer.add(b);
		return b;
	}

	void cleanup()
	{
		Firebird::MutexLockGuard guard(mutex);

		size_t p = position(getThreadId());
		if (p >= processBuffer.getCount())
		{
			return;
		}

		delete processBuffer[p];
		processBuffer.remove(p);
	}

	static void cleanupAllStrings(void* toClean)
	{
		static_cast<StringsBuffer*>(toClean)->cleanup();
	}

public:
	const char* alloc(const char* s, size_t len, FB_THREAD_ID thr = getThreadId())
	{
		ThreadCleanup::add(cleanupAllStrings, this);
		return getThreadBuffer(thr)->alloc(s, len);
	}
};

Firebird::GlobalPtr<StringsBuffer> allStrings;

} // anonymous namespace

namespace Firebird {

const char* FB_CARG MasterImplementation::circularAlloc(const char* s, size_t len, intptr_t thr)
{
	return allStrings->alloc(s, len, thr);
}

} // namespace Firebird

//
// get master
//

Firebird::IMaster* ISC_EXPORT fb_get_master_interface()
{
	static Firebird::Static<Firebird::MasterImplementation> master;

	master->addRef();
	return &master;
}
