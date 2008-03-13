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
#include "fb_atomic.h"

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
#include <errno.h>
#endif

namespace Firebird {

class MemoryPool;	// Needed for ctors that must always ignore it
class Exception;	// Needed for catch

#ifdef WIN_NT

// Generic process-local mutex and spinlock. The latter
// is used to manage memory heaps in a threaded environment.

// Windows version of the class

class TryEnterCS
{
public:
	TryEnterCS();

	static bool tryEnter(LPCRITICAL_SECTION lpCS)
	{
		return ((*m_funct) (lpCS) == TRUE);
	}

private:
	typedef WINBASEAPI BOOL WINAPI tTryEnterCriticalSection 
		(LPCRITICAL_SECTION lpCriticalSection);

	static BOOL WINAPI notImpl(LPCRITICAL_SECTION)
	{
		system_call_failed::raise("TryEnterCriticalSection is not implemented", 0);
		return false;
	}

	static tTryEnterCriticalSection* m_funct;
};

class Mutex
{
protected:
	CRITICAL_SECTION spinlock;

public:
	Mutex() {
		InitializeCriticalSection(&spinlock);
	}
	explicit Mutex(MemoryPool&) {
		InitializeCriticalSection(&spinlock);
	}

	~Mutex() {
#ifdef DEV_BUILD
		if (spinlock.OwningThread != 0)
			DebugBreak();
#endif
		DeleteCriticalSection(&spinlock);
	}

	void enter() {
		EnterCriticalSection(&spinlock);
	}

	bool tryEnter() {
		return TryEnterCS::tryEnter(&spinlock);
	}

	void leave() {
#ifdef DEV_BUILD
		if ((DWORD)spinlock.OwningThread != GetCurrentThreadId())
			DebugBreak();
#endif
		LeaveCriticalSection(&spinlock);
	}
	
public:
	static void initMutexes() { }
};

typedef WINBASEAPI DWORD WINAPI tSetCriticalSectionSpinCount (
	LPCRITICAL_SECTION lpCriticalSection,
	DWORD dwSpinCount
);

class Spinlock : public Mutex
{
private:
	static tSetCriticalSectionSpinCount* SetCriticalSectionSpinCount;

	void init();

public:
	Spinlock()
	{
		init();
	}

	explicit Spinlock(MemoryPool&)
	{
		init();
	}
};

#else //WIN_NT

#ifdef SOLARIS_MT

#error: Make mutexes on Solaris recursive!

class Mutex
{
private:
	mutex_t mlock;

public:
	Mutex() {
		if (mutex_init(&mlock, USYNC_PROCESS, NULL))
			system_call_failed::raise("mutex_init");
	}
	explicit Mutex(MemoryPool&) {
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
	
public:
	static void initMutexes() { }
};

typedef Mutex Spinlock;

#else  //SOLARIS_MT

// Pthreads version of the class
class Mutex
{
private:
	pthread_mutex_t mlock;
	static pthread_mutexattr_t attr;

private:
	void init() {
		int rc = pthread_mutex_init(&mlock, &attr);
		if (rc)
			system_call_failed::raise("pthread_mutex_init", rc);
	}

public:
	Mutex() { init(); }
	explicit Mutex(MemoryPool&) { init(); }
	~Mutex() {
		int rc = pthread_mutex_destroy(&mlock);
		if (rc)
			system_call_failed::raise("pthread_mutex_destroy", rc);
	}
	void enter() {
		int rc = pthread_mutex_lock(&mlock);
		if (rc)
			system_call_failed::raise("pthread_mutex_lock", rc);
	}
	bool tryEnter() {
		int rc = pthread_mutex_trylock(&mlock);
		if (rc == EBUSY)
			return false;
		if (rc)
			system_call_failed::raise("pthread_mutex_trylock", rc);
		return true;
	}
	void leave() {
		int rc = pthread_mutex_unlock(&mlock);
		if (rc)
			system_call_failed::raise("pthread_mutex_unlock", rc);
	}
	
public:
	static void initMutexes();
};

#ifndef DARWIN
class Spinlock
{
private:
	pthread_spinlock_t spinlock;
public:
	Spinlock() {
		if (pthread_spin_init(&spinlock, false))
			system_call_failed::raise("pthread_spin_init");
	}
	explicit Spinlock(MemoryPool&) {
		if (pthread_spin_init(&spinlock, false))
			system_call_failed::raise("pthread_spin_init");
	}
	~Spinlock() {
		if (pthread_spin_destroy(&spinlock))
			system_call_failed::raise("pthread_spin_destroy");
	}
	void enter() {
		if (pthread_spin_lock(&spinlock))
			system_call_failed::raise("pthread_spin_lock");
	}
	void leave() {
		if (pthread_spin_unlock(&spinlock))
			system_call_failed::raise("pthread_spin_unlock");
	}
};
#endif //DARWIN

#endif //SOLARIS_MT

#endif //WIN_NT


// mutex with reference count
// This class is useful if mutex can be "deleted" by the
// code between enter and leave calls
class RefMutex : public Mutex
{
public:
	RefMutex() : Mutex()
	{
		m_refcnt = 0;
	}

	explicit RefMutex(MemoryPool& pool) : Mutex(pool)
	{
		m_refcnt = 0;
	}

	int addRef()
	{
		return ++m_refcnt;
	}
	
	int release()
	{
		const int ret = --m_refcnt;
		if (!ret) {
			delete this;
		}
		return ret;
	}

private:
	~RefMutex() {}

	AtomicCounter m_refcnt;
};


// RAII holder
class MutexLockGuard
{
public:
	explicit MutexLockGuard(Mutex &alock) 
		: lock(&alock)
	{
		lock->enter();
	}

	~MutexLockGuard() 
	{ 
		try {
			lock->leave();
		}
		catch (const Exception&)
		{
			onDtorException();
		}
	}

	static void onDtorException()
	{
		// If LockGuard's dtor is executed during exception processing,
		// (remember - this guards live on the stack), exception 
		// in leave() causes std::terminate() to be called, therefore
		// loosing original exception information. Not good for us. 
		// Therefore ignore in release and abort in debug.
#ifdef DEV_BUILD
		halt();
#endif
	}

private:
	// Forbid copy constructor
	MutexLockGuard(const MutexLockGuard& source);
	MutexLockGuard(const RefMutex*);
	MutexLockGuard(const RefMutex&);

#ifdef DEV_BUILD
	static void halt();
#endif

	Mutex* lock;
};


class RefMutexGuard
{
public:
	explicit RefMutexGuard(RefMutex& alock) 
		: lock(&alock)
	{
		lock->addRef();
		lock->enter();
	}

	~RefMutexGuard()
	{
		try {
			lock->leave();
		}
		catch (const Exception&)
		{
			MutexLockGuard::onDtorException();
		}
		lock->release();
	}

private:
	// Forbid copy constructor
	RefMutexGuard(const RefMutexGuard& source);
	
	RefMutex* lock;
};


template <typename T>
class DefaultRefCounted
{
public:
	static int addRef(T *object) {
		return object->addRef();
	}

	static int release(T *object) {
		return object->release();
	}
};

template <typename T>
class NotRefCounted
{
public:
	static int addRef(void*) {
		return 0;
	}

	static int release(void*) {
		return 0;
	}
};


template <typename Mtx, typename RefCounted = DefaultRefCounted<Mtx> >
class EnsureUnlock
{
public:
	explicit EnsureUnlock(Mtx &mutex)
	{
		m_mutex = &mutex;
		RefCounted::addRef(m_mutex);
		m_locked = 0;
	}

	~EnsureUnlock()
	{
		while (m_locked)
			leave();
		RefCounted::release(m_mutex);
	}

	void enter() 
	{
		m_mutex->enter();
		m_locked++;
	}

	bool tryEnter() 
	{
		if (m_mutex->tryEnter())
		{
			m_locked++;
			return true;
		}
		return false;
	}

	void leave() 
	{
		m_mutex->leave();
		m_locked--;
	}

private:
	Mtx *m_mutex;
	int m_locked;
};

typedef EnsureUnlock<Mutex, NotRefCounted<Mutex> > MutexEnsureUnlock;
typedef EnsureUnlock<RefMutex> RefMutexEnsureUnlock;

} //namespace Firebird

#endif // CLASSES_LOCKS_H
