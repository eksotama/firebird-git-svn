/*
 *	PROGRAM:		Client/Server Common Code
 *	MODULE:			locks.h
 *	DESCRIPTION:	Single-state locks
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
 *
 */

#ifndef CLASSES_LOCKS_H
#define CLASSES_LOCKS_H

#include "firebird.h"

#ifdef MULTI_THREAD
#ifdef WIN_NT
// It is relatively easy to avoid using this header. Maybe do the same stuff like
// in thd.h ? This is Windows platform maintainers choice
#include <windows.h>
#else
#ifndef SOLARIS_MT
#include <pthread.h>
#else
#include <thread.h>
#include <synch.h>
#endif
#endif
#endif /* MULTI_THREAD */

namespace Firebird {

#ifdef MULTI_THREAD
#ifdef WIN_NT

// Process-local spinlock. Used to manage memory heaps in threaded environment.
// Windows version of the class

typedef WINBASEAPI DWORD WINAPI tSetCriticalSectionSpinCount (
	LPCRITICAL_SECTION lpCriticalSection,
	DWORD dwSpinCount
);

class Mutex {
private:
	CRITICAL_SECTION spinlock;
	static tSetCriticalSectionSpinCount* SetCriticalSectionSpinCount;
public:
	Mutex();
	~Mutex() {
		DeleteCriticalSection(&spinlock);
	}
	void enter() {
		EnterCriticalSection(&spinlock);
	}
	void leave() {
		LeaveCriticalSection(&spinlock);
	}
};

#else //WIN_NT

#ifdef SOLARIS_MT

class Mutex {
private:
	mutex_t mlock;
public:
	Mutex() {
		if (mutex_init(&mlock, USYNC_PROCESS, NULL))
			system_call_failed::raise("mutex_init");
	}
	~Mutex() {
		if (mutex_destroy(&mlock))
			system_call_failed::raise("mutex_destroy");
	}
	void enter() {
		if (mutex_lock(&mlock))
			system_call_failed::raise("mutex_lock");
	}
	void leave() {
		if (mutex_unlock(&mlock))
			system_call_failed::raise("mutex_unlock");
	}
};

#else  //SOLARIS_MT

// Pthreads version of the class
class Mutex {
private:
	pthread_mutex_t mlock;
public:
	Mutex() {
		if (pthread_mutex_init(&mlock, 0))
			system_call_failed::raise("pthread_mutex_init");
	}
	~Mutex() {
		if (pthread_mutex_destroy(&mlock))
			system_call_failed::raise("pthread_mutex_destroy");
	}
	void enter() {
		if (pthread_mutex_lock(&mlock))
			system_call_failed::raise("pthread_mutex_lock");
	}
	void leave() {
		if (pthread_mutex_unlock(&mlock))
			system_call_failed::raise("pthread_mutex_unlock");
	}
};

#endif //SOLARIS_MT

#endif //WIN_NT

#else //MULTI_THREAD

// Non-MT version
class Mutex {
public:
	Mutex() {
	}
	~Mutex() {
	}
	void enter() {
	}
	void leave() {
	}
};

#endif //MULTI_THREAD

// RAII holder of mutex lock
class MutexLockGuard {
public:
	explicit MutexLockGuard(Mutex &alock) 
		: lock(&alock) { lock->enter(); }
	~MutexLockGuard() { lock->leave(); }
private:
	// Forbid copy constructor
	MutexLockGuard(const MutexLockGuard& source);
	Mutex *lock;
};

} //namespace Firebird

#endif // CLASSES_LOCKS_H
