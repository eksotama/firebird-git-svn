/*
 *	PROGRAM:		Client/Server Common Code
 *	MODULE:			semaphore.h
 *	DESCRIPTION:	Semaphore lock
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * You may obtain a copy of the Licence at
 * http://www.gnu.org/licences/lgpl.html
 * 
 * As a special exception this file can also be included in modules
 * with other source code as long as that source code has been 
 * released under an Open Source Initiative certificed licence.  
 * More information about OSI certification can be found at: 
 * http://www.opensource.org 
 * 
 * This module is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public Licence for more details.
 * 
 * This module was created by members of the firebird development 
 * team.  All individual contributions remain the Copyright (C) of 
 * those individuals and all rights are reserved.  Contributors to 
 * this file are either listed below or can be obtained from a CVS 
 * history command.
 *
 *  Created by: Nickolay Samofatov <skidder@bssys.com>
 *
 *  Contributor(s):
 * 
 *
 *  $Id: semaphore.h,v 1.9 2004-03-18 05:54:22 robocop Exp $
 *
 */

#ifndef CLASSES_SEMAPHORE_H
#define CLASSES_SEMAPHORE_H

#ifdef WIN_NT

#include <windows.h>
#include <limits.h>

namespace Firebird {

class Semaphore {
private:
	HANDLE hSemaphore;
public:
	Semaphore() { 
		hSemaphore = CreateSemaphore(NULL, 0 /*initial count*/, 
			INT_MAX, NULL); 
		if (hSemaphore == NULL)
			system_call_failed::raise("CreateSemaphore");
	}
	~Semaphore() {
		if (hSemaphore && !CloseHandle(hSemaphore))
			system_call_failed::raise("CloseHandle");
	}	

	bool tryEnter(int seconds = 0) {
		DWORD result = WaitForSingleObject(
			hSemaphore, seconds >= 0 ? seconds * 1000 : INFINITE);
		if (result == WAIT_FAILED)
			system_call_failed::raise("WaitForSingleObject");
		return result != WAIT_TIMEOUT;
	}

	void release(SLONG count = 1) {
		if (!ReleaseSemaphore(hSemaphore, count, NULL))
			system_call_failed::raise("ReleaseSemaphore");
	}
};


}

#else

#ifdef MULTI_THREAD

#include <semaphore.h>
#include <errno.h>

namespace Firebird {

class Semaphore {
private:
	sem_t sem;
	bool  init;
public:
	Semaphore() : init(false) {
		if (sem_init(&sem, 0, 0) == -1) {
			//gds__log("Error on semaphore.h: constructor");
			system_call_failed::raise("sem_init");
		}
		init = true;
	}
	
	~Semaphore() {
		fb_assert(init == true);
		if (sem_destroy(&sem) == -1) {
			//gds__log("Error on semaphore.h: destructor");
			system_call_failed::raise("sem_destroy");
		}
		init = false;

	}
	
	bool tryEnter(int seconds = 0) {
		// Return true in case of success
		fb_assert(init == true);
		if (seconds == 0) {
			// Instant try
			if (sem_trywait(&sem) != -1) 
				return true;
			if (errno == EAGAIN) 
				return false;
			system_call_failed::raise("sem_trywait");
		}
		if (seconds < 0) {
			// Unlimited wait, like enter()
			if (sem_wait(&sem) != -1)
				return true;
			system_call_failed::raise("sem_wait");
		}
		// Wait with timeout
		struct timespec timeout;
		timeout.tv_sec = time(NULL) + seconds;
		timeout.tv_nsec = 0;
		if (sem_timedwait(&sem, &timeout) == 0) 
			return true;
		if (errno == ETIMEDOUT) {
			return false;
		}
		system_call_failed::raise("sem_timedwait");
	}
	
	void enter() {
		fb_assert(init == true);
		if (sem_wait(&sem) == -1) {
			//gds__log("Error on semaphore.h: enter");
			system_call_failed::raise("sem_wait");
		}
	}
	
	void release(SLONG count = 1) {
		fb_assert(init == true);
		for (int i = 0; i < count; i++) 
			if (sem_post(&sem) == -1) {
				//gds__log("Error on semaphore.h: release");
				system_call_failed::raise("sem_post");
			}
	}
};

}

#endif /*MULTI_THREAD*/

#endif /*!WIN_NT*/

#endif // CLASSES_SEMAPHORE_H

