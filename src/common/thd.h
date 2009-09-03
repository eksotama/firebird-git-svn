/*
 *	PROGRAM:	JRD access method
 *	MODULE:		thd.h
 *	DESCRIPTION:	Thread support definitions
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
 * Alex Peshkov
 */

#ifndef JRD_THD_H
#define JRD_THD_H

// thread run-ability control
void	THD_sleep(ULONG);
void	THD_yield();

// thread ID
FB_THREAD_ID getThreadId();

class ThreadCleanup
{
public:
	static void add(FPTR_VOID_PTR cleanup, void* arg);
	static void remove(FPTR_VOID_PTR cleanup, void* arg);
	static void destructor(void*);

private:
	FPTR_VOID_PTR function;
	void* argument;
	ThreadCleanup* next;

	ThreadCleanup(FPTR_VOID_PTR cleanup, void* arg, ThreadCleanup* chain)
		: function(cleanup), argument(arg), next(chain) { }
	~ThreadCleanup() { }

	static ThreadCleanup** findCleanup(FPTR_VOID_PTR cleanup, void* arg);
};

#endif // JRD_THD_H
