/*
 *	PROGRAM:	Client/Server Common Code
 *	MODULE:		config.h
 *	DESCRIPTION:	Configuration manager (public interface)
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
 * Created by: Dmitry Yemanov <yemanov@yandex.ru>
 *
 * All Rights Reserved.
 * Contributor(s): ______________________________________.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include "fb_string.h"
#include "fb_vector.h"

/**
	Since the original (isc.cpp) code wasn't able to provide powerful and
	easy-to-use abilities to work with complex configurations, a decision
	has been made to create a completely new one.

	This class is a public interface for our generic configuration manager
	and allows to access all configuration values by its getXXX() member
	functions. Each of these functions corresponds to one and only one key
	and has one input argument - default value, which is used when the 
	requested key is missing or the configuration file is not found. Supported
	value datatypes are "const char*", "int" and "bool". Usual default values for
	these datatypes are empty string, zero and false respectively. There are
	two types of member functions - scalar and vector. The former ones return
	single value of the given type. The latter ones return vector which
	contains an ordered array of values.

	There's one exception - getRootDirectory() member function, which returns
	root pathname of the current installation. This value isn't stored in the
	configuration file, but is managed by the code itself. But there's a way
	to override this value via the configuration file as well.

	To add new configuration item, you have to take the following steps:

		1. Add key description to ConfigImpl::entries[] array (config.cpp)
		2. Add logical key to Config::ConfigKey enumeration (config.h)
		   (note: both physical and logical keys MUST have the same ordinal
				  position within appropriate structures)
		3. Add member function to Config class (config.h) and implement it
		   in config.cpp module.
**/

class Config
{
	enum ConfigKey
	{
		KEY_ROOT_DIRECTORY,							// 0
		KEY_SORT_MEM_BLOCK_SIZE,					// 1
		KEY_SORT_MEM_UPPER_LIMIT,					// 2
		KEY_REMOTE_FILE_OPEN_ABILITY,				// 3
		KEY_GUARDIAN_OPTION,						// 4
		KEY_CPU_AFFINITY_MASK,						// 5
		KEY_OLD_PARAMETER_ORDERING,					// 6
		KEY_TCP_REMOTE_BUFFER_SIZE,					// 7
		KEY_TCP_NO_NAGLE,							// 8
		KEY_IPC_MAP_SIZE,							// 9
		KEY_DEFAULT_DB_CACHE_PAGES,					// 10
		KEY_CONNECTION_TIMEOUT,						// 11
		KEY_DUMMY_PACKET_INTERVAL,					// 12
		KEY_LOCK_MEM_SIZE,							// 13
		KEY_LOCK_SEM_COUNT,							// 14
		KEY_LOCK_SIGNAL,							// 15
		KEY_LOCK_GRANT_ORDER,						// 16
		KEY_LOCK_HASH_SLOTS,						// 17
		KEY_LOCK_ACQUIRE_SPINS,						// 18
		KEY_EVENT_MEM_SIZE,							// 19
		KEY_DEADLOCK_TIMEOUT,						// 20
		KEY_SOLARIS_STALL_VALUE,					// 21
		KEY_TRACE_MEMORY_POOLS,						// 22	
		KEY_PRIORITY_SWITCH_DELAY,					// 23
		KEY_DEAD_THREADS_COLLECTION,				// 24
		KEY_PRIORITY_BOOST,							// 25
		KEY_REMOTE_SERVICE_NAME,					// 26
		KEY_REMOTE_SERVICE_PORT,					// 27
		KEY_REMOTE_PIPE_NAME,						// 28
		KEY_IPC_NAME								// 29
	};

public:

	/*
		Root directory of current installation
	*/
	static const char* getRootDirectory();

	/*
		Block size for the sorting manager
	*/
	static int getSortMemBlockSize();

	/*
		Memory usage limit for the sorting manager
	*/
	static int getSortMemUpperLimit();

	/*
		Whether remote (NFS) files can be opened
	*/
	static bool getRemoteFileOpenAbility();

	/*
		Startup option for the guardian
	*/
	static int getGuardianOption();

	/*
		CPU affinity mask
	*/
	static int getCpuAffinityMask();

	/*
		Old parameter ordering for backward compatibility with FB1/IB6.X
	*/
	static bool getOldParameterOrdering();

	/*
		XDR buffer size
	*/
	static int getTcpRemoteBufferSize();

	/*
		Disable Nagle algorithm
	*/
	static bool getTcpNoNagle();

	/*
		IPC client mapping
	*/
	static int getIpcMapSize();

	/*
		Default database cache size
	*/
	static int getDefaultDbCachePages();

	/*
		Connection timeout
	*/
	static int getConnectionTimeout();

	/*
		Dummy packet interval
	*/
	static int getDummyPacketInterval();

	/*
		Lock manager memory size
	*/
	static int getLockMemSize();

	/*
		Lock manager semaphore count
	*/
	static int getLockSemCount();

	/*
		Lock manager signal number
	*/
	static int getLockSignal();

	/*
		Lock manager grant order
	*/
	static bool getLockGrantOrder();

	/*
		Lock manager hash slots
	*/
	static int getLockHashSlots();

	/*
		Lock manager acquire spins
	*/
	static bool getLockAcquireSpins();

	/*
		Event manager memory size
	*/
	static int getEventMemSize();

	/*
		Deadlock timeout
	*/
	static int getDeadlockTimeout();

	/*
		Solaris stall value
	*/
	static int getSolarisStallValue();

	/*
		Trace memory pools
	*/
	static bool getTraceMemoryPools();

	/*
		Priority switch delay
	*/
	static int getPrioritySwitchDelay();

	/*
		Dead threads collection
	*/
	static int getDeadThreadsCollection();

	/*
		Priority boost
	*/
	static int getPriorityBoost();

	/*
		Service name for remote protocols
	*/
	static const char *getRemoteServiceName();

	/*
		Service port for INET
	*/
	static int getRemoteServicePort();

	/*
		Pipe name for WNET
	*/
	static const char *getRemotePipeName();

	/*
		Name for IPC-related objects
	*/
	static const char *getIpcName();
};

#endif // CONFIG_H
