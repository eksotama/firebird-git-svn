/*
 *	PROGRAM:	JRD Access Method
 *	MODULE:		thd.cpp
 *	DESCRIPTION:	Thread support routines
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
 * 2002.10.28 Sean Leyne - Completed removal of obsolete "DGUX" port
 *
 * 2002.10.29 Sean Leyne - Removed obsolete "Netware" port
 *
 */

#include "firebird.h"
#include <stdio.h>
#include <errno.h>
#include "../jrd/common.h"
#include "../jrd/thd.h"
#include "../jrd/isc.h"
#include "../jrd/os/thd_priority.h"
#include "../jrd/gds_proto.h"
#include "../jrd/isc_s_proto.h"
#include "../jrd/gdsassert.h"
#include "../common/classes/fb_tls.h"


#ifdef WIN_NT
#include <process.h>
#include <windows.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif


#ifdef SOLARIS
#include <thread.h>
#include <signal.h>
#endif

#ifdef NOT_USED_OR_REPLACED
#ifdef VMS
// THE SOLE PURPOSE OF THE FOLLOWING DECLARATION IS TO ALLOW THE VMS KIT TO
// COMPILE.  IT IS NOT CORRECT AND MUST BE REMOVED AT SOME POINT.
ThreadData* gdbb;
#endif

#ifdef VMS
#define THREAD_MUTEXES_DEFINED
int THD_mutex_destroy(MUTX_T * mutex)
{
/**************************************
 *
 *	T H D _ m u t e x _ d e s t r o y	( V M S )
 *
 **************************************
 *
 * Functional description
 *
 **************************************/

	return 0;
}


int THD_mutex_init(MUTX_T * mutex)
{
/**************************************
 *
 *	T H D _ m u t e x _ i n i t		( V M S )
 *
 **************************************
 *
 * Functional description
 *
 **************************************/

	return ISC_mutex_init(mutex, 0);
}


int THD_mutex_lock(MUTX_T * mutex)
{
/**************************************
 *
 *	T H D _ m u t e x _ l o c k		( V M S )
 *
 **************************************
 *
 * Functional description
 *
 **************************************/

	return ISC_mutex_lock(mutex);
}


int THD_mutex_unlock(MUTX_T * mutex)
{
/**************************************
 *
 *	T H D _ m u t e x _ u n l o c k		( V M S )
 *
 **************************************
 *
 * Functional description
 *
 **************************************/

	return ISC_mutex_unlock(mutex);
}
#endif //VMS
#endif //NOT_USED_OR_REPLACED


#include "../common/classes/locks.h"
#include "../common/classes/rwlock.h"
Firebird::Mutex ib_mutex;

namespace {

TLS_DECLARE (void*, tSpecific);
TLS_DECLARE (ThreadData*, tData);

}


int API_ROUTINE gds__thread_start(
								  ThreadEntryPoint* entrypoint,
								  void *arg,
								  int priority, int flags, void *thd_id)
{
/**************************************
 *
 *	g d s _ $ t h r e a d _ s t a r t
 *
 **************************************
 *
 * Functional description
 *	Start a thread.
 *
 **************************************/

	int rc = 0;
	try {
		ThreadData::start(entrypoint, arg, priority, flags, thd_id);
	}
	catch(const Firebird::status_exception& status) {
		rc = status.value()[1];
	}
	return rc;
}


FB_THREAD_ID ThreadData::getId(void)
{
/**************************************
 *
 *	T H D _ g e t _ t h r e a d _ i d
 *
 **************************************
 *
 * Functional description
 *	Get platform's notion of a thread ID.
 *
 **************************************/
	FB_THREAD_ID id = 1;
#ifdef WIN_NT
	id = GetCurrentThreadId();
#endif
#ifdef SOLARIS_MT
	id = thr_self();
#endif
#ifdef USE_POSIX_THREADS

/* The following is just a temp. decision.
*/
#ifdef HP10

	id = (FB_THREAD_ID) (pthread_self().field1);

#else

	id = (FB_THREAD_ID) pthread_self();

#endif /* HP10 */
#endif /* USE_POSIX_THREADS */

	return id;
}


ThreadData* ThreadData::getSpecific(void)
{
/**************************************
 *
 *	T H D _ g e t _ s p e c i f i c
 *
 **************************************
 *
 * Functional description
 * Gets thread specific data and returns
 * a pointer to it.
 *
 **************************************/
	return TLS_GET(tData);
}


void ThreadData::getSpecificData(void **t_data)
{
/**************************************
 *
 *	T H D _ g e t s p e c i f i c _ d a t a
 *
 **************************************
 *
 * Functional description
 *	return the previously stored t_data.
 *
 **************************************/

	*t_data = TLS_GET(tSpecific);
}


int THD_wlck_lock(WLCK_T* wlock, WLCK_type type)
{
/**************************************
 *
 *	T H D _ w l c k _ l o c k
 *
 **************************************
 *
 * Functional description
 *
 **************************************/

	switch (type)
	{
	case WLCK_read:
#ifdef DEBUG_THREAD
		fprintf(stderr, "calling rwlock_rdlock %x\n", wlock);
#endif
		wlock->rwLock.beginRead();
		break;

	case WLCK_write:
#ifdef DEBUG_THREAD
		fprintf(stderr, "calling rwlock_wrlock %x\n", wlock);
#endif
		wlock->rwLock.beginWrite();
		break;

#ifdef DEV_BUILD
	default:
		fb_assert(false);
		break;
#endif
	}

	wlock->type = type;
	return 0;
}


int THD_wlck_unlock(WLCK_T* wlock)
{
/**************************************
 *
 *	T H D _ w l c k _ u n l o c k
 *
 **************************************
 *
 * Functional description
 *
 **************************************/

#ifdef DEBUG_THREAD
	fprintf(stderr, "calling rwlock_unlock %x\n", wlock);
#endif

	switch (wlock->type)
	{
	case WLCK_read:
		wlock->rwLock.endRead();
		break;

	case WLCK_write:
		wlock->rwLock.endWrite();
		break;
	}

	return 0;
}


void ThreadData::putSpecific()
{
/**************************************
 *
 *	T H D _ p u t _ s p e c i f i c
 *
 **************************************
 *
 * Functional description
 *
 **************************************/

	threadDataPriorContext = TLS_GET(tData);
	TLS_SET(tData, this);
}


void ThreadData::putSpecificData(void *t_data)
{
/**************************************
 *
 *	T H D _ p u t s p e c i f i c _ d a t a
 *
 **************************************
 *
 * Functional description
 *	Store the passed t_data
 *
 **************************************/

	TLS_SET(tSpecific, t_data);
}


void ThreadData::restoreSpecific()
{
/**************************************
 *
 *	T H D _ r e s t o r e _ s p e c i f i c
 *
 **************************************
 *
 * Functional description
 *
 **************************************/
	ThreadData* current_context = getSpecific();

	TLS_SET(tData, current_context->threadDataPriorContext);
}


#ifdef SUPERSERVER
int THD_rec_mutex_destroy(REC_MUTX_T * rec_mutex)
{
/**************************************
 *
 *	T H D _ r e c _ m u t e x _ d e s t r o y   ( S U P E R S E R V E R )
 *
 **************************************
 *
 * Functional description
 *
 **************************************/

	return 0;
}


int THD_rec_mutex_init(REC_MUTX_T * rec_mutex)
{
/**************************************
 *
 *	T H D _ r e c _ m u t e x _ i n i t   ( S U P E R S E R V E R )
 *
 **************************************
 *
 * Functional description
 *
 **************************************/

	rec_mutex->rec_mutx_id = 0;
	rec_mutex->rec_mutx_count = 0;
	return 0;
}


int THD_rec_mutex_lock(REC_MUTX_T * rec_mutex)
{
/**************************************
 *
 *	T H D _ r e c _ m u t e x _ l o c k   ( S U P E R S E R V E R )
 *
 **************************************
 *
 * Functional description
 *
 **************************************/
	int ret;

	if (rec_mutex->rec_mutx_id == ThreadData::getId())
		rec_mutex->rec_mutx_count++;
	else {
		if (ret = THD_mutex_lock(rec_mutex->rec_mutx_mtx))
			return ret;
		rec_mutex->rec_mutx_id = ThreadData::getId();
		rec_mutex->rec_mutx_count = 1;
	}
	return 0;
}


int THD_rec_mutex_unlock(REC_MUTX_T * rec_mutex)
{
/**************************************
 *
 *	T H D _ r e c _ m u t e x _ u n l o c k   ( S U P E R S E R V E R )
 *
 **************************************
 *
 * Functional description
 *
 **************************************/

	if (rec_mutex->rec_mutx_id != ThreadData::getId())
		return FB_FAILURE;

	rec_mutex->rec_mutx_count--;

	if (rec_mutex->rec_mutx_count)
		return 0;
	else {
		rec_mutex->rec_mutx_id = 0;
		return THD_mutex_unlock(rec_mutex->rec_mutx_mtx);
	}
}
#endif /* SUPERSERVER */


#ifdef WIN_NT
#define THREAD_SUSPEND_DEFINED
int THD_resume(THD_T thread)
{
/**************************************
 *
 *	T H D _ r e s u m e			( W I N _ N T )
 *
 **************************************
 *
 * Functional description
 *	Resume execution of a thread that has been
 *	suspended.
 *
 **************************************/

	if (ResumeThread(thread) == 0xFFFFFFFF)
		return GetLastError();

	return 0;
}


int THD_suspend(THD_T thread)
{
/**************************************
 *
 *	T H D _ s u s p e n d			( W I N _ N T )
 *
 **************************************
 *
 * Functional description
 *	Suspend execution of a thread.
 *
 **************************************/

	if (SuspendThread(thread) == 0xFFFFFFFF)
		return GetLastError();

	return 0;
}
#endif


#ifndef THREAD_SUSPEND_DEFINED
int THD_resume(THD_T thread)
{
/**************************************
 *
 *	T H D _ r e s u m e			( G e n e r i c )
 *
 **************************************
 *
 * Functional description
 *	Resume execution of a thread that has been
 *	suspended.
 *
 **************************************/

	return 0;
}


int THD_suspend(THD_T thread)
{
/**************************************
 *
 *	T H D _ s u s p e n d			( G e n e r i c )
 *
 **************************************
 *
 * Functional description
 *	Suspend execution of a thread.
 *
 **************************************/

	return 0;
}
#endif


void THD_sleep(ULONG milliseconds)
{
/**************************************
 *
 *	T H D _ s l e e p
 *
 **************************************
 *
 * Functional description
 *	Thread sleeps for requested number
 *	of milliseconds.
 *
 **************************************/
#ifdef WIN_NT
	SleepEx(milliseconds, FALSE);
#else

#ifdef ANY_THREADING
	event_t timer;
	event_t* timer_ptr = &timer;

	ISC_event_init(&timer, 0, 0);
	SLONG count = ISC_event_clear(&timer);

	ISC_event_wait(1, &timer_ptr, &count, milliseconds * 1000, NULL, 0);
	ISC_event_fini(&timer);
#else /* !ANY_THREADING */
	int seconds;

/* Insure that process sleeps some amount of time. */

	if (!(seconds = milliseconds / 1000))
		++seconds;

/* Feedback unslept time due to premature wakeup from signals. */

	while (seconds = sleep(seconds));

#endif /* !ANY_THREADING */

#endif /* !WIN_NT */
}


void THD_yield(void)
{
/**************************************
 *
 *	T H D _ y i e l d
 *
 **************************************
 *
 * Functional description
 *	Thread relinquishes the processor.
 *
 **************************************/
#ifdef ANY_THREADING

#ifdef USE_POSIX_THREADS
/* use sched_yield() instead of pthread_yield(). Because pthread_yield() 
   is not part of the (final) POSIX 1003.1c standard. Several drafts of 
   the standard contained pthread_yield(), but then the POSIX guys 
   discovered it was redundant with sched_yield() and dropped it. 
   So, just use sched_yield() instead. POSIX systems on which  
   sched_yield() is available define _POSIX_PRIORITY_SCHEDULING 
   in <unistd.h>.  Darwin defined _POSIX_THREAD_PRIORITY_SCHEDULING
   instead of _POSIX_PRIORITY_SCHEDULING.
*/
#if (defined _POSIX_PRIORITY_SCHEDULING || defined _POSIX_THREAD_PRIORITY_SCHEDULING)
	sched_yield();
#else
	pthread_yield();
#endif /* _POSIX_PRIORITY_SCHEDULING */
#endif

#ifdef SOLARIS_MT
	thr_yield();
#endif

#ifdef WIN_NT
	SleepEx(0, FALSE);
#endif
#endif /* ANY_THREADING */
}

#ifdef THREAD_PSCHED
static THREAD_ENTRY_DECLARE threadStart(THREAD_ENTRY_PARAM arg) {
	ThreadPriorityScheduler* tps = 
		reinterpret_cast<ThreadPriorityScheduler*>(arg);
	try {
		tps->run();
	}
	catch (...) {
		tps->detach();
		throw;
	}
	tps->detach();
	return 0;
}

#define THREAD_ENTRYPOINT threadStart
#define THREAD_ARG tps

#else  //THREAD_PSCHED

// due to same names of parameters for various ThreadData::start(...),
// we may use common macro for various platforms
#define THREAD_ENTRYPOINT reinterpret_cast<THREAD_ENTRY_RETURN (THREAD_ENTRY_CALL *) (THREAD_ENTRY_PARAM)>(routine)
#define THREAD_ARG reinterpret_cast<THREAD_ENTRY_PARAM>(arg)

#endif //THREAD_PSCHED

#ifdef ANY_THREADING
#ifdef USE_POSIX_THREADS
#define START_THREAD
void ThreadData::start(ThreadEntryPoint* routine,
				void *arg, 
				int priority_arg, 
				int flags, 
				void *thd_id)
{
/**************************************
 *
 *	t h r e a d _ s t a r t		( P O S I X )
 *
 **************************************
 *
 * Functional description
 *	Start a new thread.  Return 0 if successful,
 *	status if not.
 *
 **************************************/
	pthread_t thread;
	pthread_attr_t pattr;
	int state;

#if ( !defined HP10 && !defined LINUX && !defined FREEBSD )
	state = pthread_attr_init(&pattr);
	if (state)
		Firebird::system_call_failed::raise("pthread_attr_init", state);

	// Do not make thread bound for superserver/client
#if (!defined (SUPERCLIENT) && !defined (SUPERSERVER))
	pthread_attr_setscope(&pattr, PTHREAD_SCOPE_SYSTEM);
#endif

	pthread_attr_setdetachstate(&pattr, PTHREAD_CREATE_DETACHED);
	state = pthread_create(&thread, &pattr, THREAD_ENTRYPOINT, THREAD_ARG);
	pthread_attr_destroy(&pattr);
	if (state)
		Firebird::system_call_failed::raise("pthread_create", state);

#else
#if ( defined LINUX || defined FREEBSD )

	if (state = pthread_create(&thread, NULL, THREAD_ENTRYPOINT, THREAD_ARG))
		Firebird::system_call_failed::raise("pthread_create", state);
	if (state = pthread_detach(thread))
		Firebird::system_call_failed::raise("pthread_detach", state);

#else
	
	long stack_size;

	state = pthread_attr_create(&pattr);
	if (state)
		Firebird::system_call_failed::raise("pthread_attr_create", state);

/* The default HP's stack size is too small. HP's documentation
   says it is "machine specific". My test showed it was less
   than 64K. We definitly need more stack to be able to execute
   concurrently many (at least 100) copies of the same request
   (like, for example in case of recursive stored prcedure).
   The following code sets threads stack size up to 256K if the
   default stack size is less than this number
*/
	stack_size = pthread_attr_getstacksize(pattr);
	if (stack_size == -1)
		Firebird::system_call_failed::raise("pthread_attr_getstacksize");

	if (stack_size < 0x40000L) {
		state = pthread_attr_setstacksize(&pattr, 0x40000L);
		if (state)
			Firebird::system_call_failed::raise("pthread_attr_setstacksize", state);
	}

/* HP's Posix threads implementation does not support
   bound attribute. It just a user level library.
*/
	state = pthread_create(&thread, pattr, THREAD_ENTRYPOINT, THREAD_ARG);
	if (state)
		Firebird::system_call_failed::raise("pthread_create", state);

	state = pthread_detach(&thread);
	if (state)
		Firebird::system_call_failed::raise("pthread_detach", state);
	state = pthread_attr_delete(&pattr);
	if (state)
		Firebird::system_call_failed::raise("pthread_attr_delete", state);

#endif /* linux */
#endif /* HP10 */
}
#endif /* USE_POSIX_THREADS */


#ifdef SOLARIS_MT
#define START_THREAD
void ThreadData::start(ThreadEntryPoint* routine,
				void *arg, 
				int priority_arg, 
				int flags, 
				void *thd_id)
{
/**************************************
 *
 *	t h r e a d _ s t a r t		( S o l a r i s )
 *
 **************************************
 *
 * Functional description
 *	Start a new thread.  Return 0 if successful,
 *	status if not.
 *
 **************************************/
	int rval;
	thread_t thread_id;
	sigset_t new_mask, orig_mask;

	sigfillset(&new_mask);
	sigdelset(&new_mask, SIGALRM);
	if (rval = thr_sigsetmask(SIG_SETMASK, &new_mask, &orig_mask))
		Firebird::system_call_failed::raise("thr_sigsetmask", rval);
#if (defined SUPERCLIENT || defined SUPERSERVER)
	rval = thr_create(NULL, 0, THREAD_ENTRYPOINT, THREAD_ARG, THR_DETACHED | THR_NEW_LWP,
					 &thread_id);
#else
	rval =
		thr_create(NULL, 0, THREAD_ENTRYPOINT, THREAD_ARG, THR_DETACHED | THR_BOUND,
					 &thread_id);
#endif
	thr_sigsetmask(SIG_SETMASK, &orig_mask, NULL);

	if (rval)
		Firebird::system_call_failed::raise("thr_create", rval);
}
#endif
#endif


#ifdef WIN_NT
#define START_THREAD
void ThreadData::start(ThreadEntryPoint* routine,
				void *arg, 
				int priority_arg, 
				int flags, 
				void *thd_id)
{
/**************************************
 *
 *	t h r e a d _ s t a r t		( W I N _ N T )
 *
 **************************************
 *
 * Functional description
 *	Start a new thread.  Return 0 if successful,
 *	status if not.
 *
 **************************************/

	HANDLE handle;
	DWORD thread_id;
	int priority;

	switch (priority_arg) {
	case THREAD_critical:
		priority = THREAD_PRIORITY_TIME_CRITICAL;
		break;
	case THREAD_high:
		priority = THREAD_PRIORITY_HIGHEST;
		break;
	case THREAD_medium_high:
		priority = THREAD_PRIORITY_ABOVE_NORMAL;
		break;
	case THREAD_medium:
		priority = THREAD_PRIORITY_NORMAL;
		break;
	case THREAD_medium_low:
		priority = THREAD_PRIORITY_BELOW_NORMAL;
		break;
	case THREAD_low:
	default:
		priority = THREAD_PRIORITY_LOWEST;
		break;
	}

#ifdef THREAD_PSCHED
	ThreadPriorityScheduler::Init();

	ThreadPriorityScheduler *tps = FB_NEW(*getDefaultMemoryPool()) 
		ThreadPriorityScheduler(routine, arg, 
			ThreadPriorityScheduler::adjustPriority(priority));
#endif //THREAD_PSCHED

	/* I have changed the CreateThread here to _beginthreadex() as using
	 * CreateThread() can lead to memory leaks caused by C-runtime library.
	 * Advanced Windows by Richter pg. # 109. */

	unsigned long real_handle = 
		_beginthreadex(NULL, 0, THREAD_ENTRYPOINT, THREAD_ARG, CREATE_SUSPENDED,
					   reinterpret_cast <unsigned *>(&thread_id));
	if (!real_handle)
	{
		Firebird::system_call_failed::raise("_beginthreadex", GetLastError());
	}
	handle = reinterpret_cast<HANDLE>(real_handle);

	SetThreadPriority(handle, priority);

	if (! (flags & THREAD_wait))
	{
		ResumeThread(handle);
	}
	if (thd_id)
	{
		*(HANDLE *) thd_id = handle;
	}
	else
	{
		CloseHandle(handle);
	}
}
#endif


#ifdef ANY_THREADING
#ifdef VMS
#ifndef USE_POSIX_THREADS
#define START_THREAD
/**************************************
 *
 *	t h r e a d _ s t a r t		( V M S )
 *
 **************************************
 *
 * Functional description
 *	Start a new thread.  Return 0 if successful,
 *	status if not.  This routine is coded in macro.
 *
 **************************************/
#endif
#endif
#endif


#ifndef START_THREAD
void ThreadData::start(ThreadEntryPoint* routine,
				void *arg, 
				int priority_arg, 
				int flags, 
				void *thd_id)
{
/**************************************
 *
 *	t h r e a d _ s t a r t		( G e n e r i c )
 *
 **************************************
 *
 * Functional description
 *	Wrong attempt to start a new thread.
 *
 **************************************/

}
#endif


