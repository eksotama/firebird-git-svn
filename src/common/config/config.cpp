/*
 *	PROGRAM:	Client/Server Common Code
 *	MODULE:		config.cpp
 *	DESCRIPTION:	Configuration manager (generic code)
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

#include "firebird.h"

#include "../../common/config/config.h"
#include "../../common/config/config_impl.h"
#include "../../common/config/config_file.h"

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#include "../jrd/gdsassert.h"

typedef Firebird::string string;

/******************************************************************************
 *
 *	Configuration entries
 */

const ConfigImpl::ConfigEntry ConfigImpl::entries[] =
{
	{TYPE_STRING,		"RootDirectory",			(ConfigValue) 0},
	{TYPE_INTEGER,		"SortMemBlockSize",			(ConfigValue) 1048576},		// bytes
#ifdef SUPERSERVER
	{TYPE_INTEGER,		"SortMemUpperLimit",		(ConfigValue) 67108864},	// bytes
#else
	{TYPE_INTEGER,		"SortMemUpperLimit",		(ConfigValue) 0},			// bytes
#endif
	{TYPE_BOOLEAN,		"RemoteFileOpenAbility",	(ConfigValue) false},
	{TYPE_INTEGER,		"GuardianOption",			(ConfigValue) 1},
	{TYPE_INTEGER,		"CpuAffinityMask",			(ConfigValue) 1},
	{TYPE_BOOLEAN,		"OldParameterOrdering",		(ConfigValue) false},
	{TYPE_INTEGER,		"TcpRemoteBufferSize",		(ConfigValue) 8192},		// bytes
	{TYPE_BOOLEAN,		"TcpNoNagle",				(ConfigValue) false},
	{TYPE_INTEGER,		"IpcMapSize",				(ConfigValue) 4096},		// bytes
#ifdef SUPERSERVER
	{TYPE_INTEGER,		"DefaultDbCachePages",		(ConfigValue) 2048},		// pages
#else
	{TYPE_INTEGER,		"DefaultDbCachePages",		(ConfigValue) 75},			// pages
#endif
	{TYPE_INTEGER,		"ConnectionTimeout",		(ConfigValue) 180},			// seconds
	{TYPE_INTEGER,		"DummyPacketInterval",		(ConfigValue) 60},			// seconds
	{TYPE_INTEGER,		"LockMemSize",				(ConfigValue) 262144},		// bytes
#ifdef SINIXZ
	{TYPE_INTEGER,		"LockSemCount",				(ConfigValue) 25},			// semaphores
#else
	{TYPE_INTEGER,		"LockSemCount",				(ConfigValue) 32},			// semaphores
#endif
	{TYPE_INTEGER,		"LockSignal",				(ConfigValue) 16},			// signal #
	{TYPE_BOOLEAN,		"LockGrantOrder",			(ConfigValue) true},
	{TYPE_INTEGER,		"LockHashSlots",			(ConfigValue) 101},			// slots
	{TYPE_BOOLEAN,		"LockRequireSpins",			(ConfigValue) false},
	{TYPE_INTEGER,		"EventMemSize",				(ConfigValue) 65536},		// bytes
	{TYPE_INTEGER,		"DeadlockTimeout",			(ConfigValue) 10},			// seconds
	{TYPE_INTEGER,		"SolarisStallValue",		(ConfigValue) 60},			// seconds
	{TYPE_BOOLEAN,		"TraceMemoryPools",			(ConfigValue) false},		// for internal use only
	{TYPE_INTEGER,		"PrioritySwitchDelay",		(ConfigValue) 100},			// milliseconds
	{TYPE_INTEGER,		"DeadThreadsCollection",	(ConfigValue) 50},			// number of PrioritySwitchDelay cycles before dead threads collection
	{TYPE_INTEGER,		"PriorityBoost",			(ConfigValue) 5},			// ratio oh high- to low-priority thread ticks in jrd.cpp
	{TYPE_STRING,		"RemoteServiceName",		(ConfigValue) FB_SERVICE_NAME},
	{TYPE_INTEGER,		"RemoteServicePort",		(ConfigValue) FB_SERVICE_PORT},
	{TYPE_STRING,		"RemotePipeName",			(ConfigValue) FB_PIPE_NAME},
	{TYPE_STRING,		"IpcName",					(ConfigValue) FB_IPC_NAME},
#ifdef WIN_NT
	{TYPE_INTEGER,		"MaxUnflushedWrites",		(ConfigValue) 100},
	{TYPE_INTEGER,		"MaxUnflushedWriteTime",	(ConfigValue) 5},
#else
	{TYPE_INTEGER,		"MaxUnflushedWrites",		(ConfigValue) -1},
	{TYPE_INTEGER,		"MaxUnflushedWriteTime",	(ConfigValue) -1},
#endif
	{TYPE_INTEGER,		"ProcessPriorityLevel",		(ConfigValue) 0},
	{TYPE_BOOLEAN,		"CreateInternalWindow",		(ConfigValue) true},
	{TYPE_BOOLEAN,		"CompleteBooleanEvaluation",(ConfigValue) false},
	{TYPE_INTEGER,		"RemoteAuxPort",			(ConfigValue) 0},
	{TYPE_STRING,		"RemoteBindAddress",		(ConfigValue) 0}
};

/******************************************************************************
 *
 *	Static instance of the system configuration file
 */

// was: const static ConfigImpl sysConfig;

const ConfigImpl& ConfigImpl::instance()
{
	static ConfigImpl *config = 0;
	if (!config) {
		config = FB_NEW(*getDefaultMemoryPool()) ConfigImpl;
	}
	return *config;
}

#define sysConfig ConfigImpl::instance()

/******************************************************************************
 *
 *	Implementation interface
 */

ConfigImpl::ConfigImpl()
{
	/* Prepare some stuff */

	ConfigFile file;
	root_dir = getRootDirectory();
	MemoryPool *pool = getDefaultMemoryPool();
	int size = FB_NELEM(entries);
	values = FB_NEW(*pool) ConfigValue[size];

	string val_sep = ",";
	file.setConfigFile(getConfigFile());

	/* Iterate through the known configuration entries */

	for (int i = 0; i < size; i++)
	{
		ConfigEntry entry = entries[i];
		string value = getValue(file, entries[i].key);

		if (!value.length())
		{
			/* Assign the default value */

			values[i] = entries[i].default_value;
			continue;
		}

		/* Assign the actual value */

		switch (entry.data_type)
		{
		case TYPE_BOOLEAN:
			values[i] = (ConfigValue) asBoolean(value);
			break;
		case TYPE_INTEGER:
			values[i] = (ConfigValue) asInteger(value);
			break;
		case TYPE_STRING:
			{
			const char *src = asString(value);
			char *dst = FB_NEW(*pool) char[strlen(src) + 1];
			strcpy(dst, src);
			values[i] = (ConfigValue) dst;
			}
			break;
		case TYPE_STRING_VECTOR:
			;
		}
	}
}

ConfigImpl::~ConfigImpl()
{
	int size = FB_NELEM(entries);

	/* Free allocated memory */

	for (int i = 0; i < size; i++)
	{
		if (values[i] == entries[i].default_value)
			continue;

		switch (entries[i].data_type)
		{
		case TYPE_STRING:
			delete[] (char*)values[i];
			break;
		case TYPE_STRING_VECTOR:
			;
		}
	}
	delete[] values;
}

string ConfigImpl::getValue(ConfigFile& file, ConfigKey key)
{
	return file.doesKeyExist(key) ? file.getString(key) : "";
}

int ConfigImpl::asInteger(const string &value)
{
	return atoi(value.data());
}

bool ConfigImpl::asBoolean(const string &value)
{
	return (atoi(value.data()) != 0);
}

const char* ConfigImpl::asString(const string &value)
{
	return value.c_str();
}

/******************************************************************************
 *
 *	Public interface
 */

const char* Config::getRootDirectory()
{
	const char* result = (char*) sysConfig.values[KEY_ROOT_DIRECTORY];
	return result ? result : sysConfig.root_dir;
}

int Config::getSortMemBlockSize()
{
	return (int) sysConfig.values[KEY_SORT_MEM_BLOCK_SIZE];
}

int Config::getSortMemUpperLimit()
{
	return (int) sysConfig.values[KEY_SORT_MEM_UPPER_LIMIT];
}

bool Config::getRemoteFileOpenAbility()
{
	return (bool) sysConfig.values[KEY_REMOTE_FILE_OPEN_ABILITY];
}

int Config::getGuardianOption()
{
	return (int) sysConfig.values[KEY_GUARDIAN_OPTION];
}

int Config::getCpuAffinityMask()
{
	return (int) sysConfig.values[KEY_CPU_AFFINITY_MASK];
}

bool Config::getOldParameterOrdering()
{
	return (bool) sysConfig.values[KEY_OLD_PARAMETER_ORDERING];
}

int Config::getTcpRemoteBufferSize()
{
	return (int) sysConfig.values[KEY_TCP_REMOTE_BUFFER_SIZE];
}

bool Config::getTcpNoNagle()
{
	return (bool) sysConfig.values[KEY_TCP_NO_NAGLE];
}

int Config::getIpcMapSize()
{
	return (int) sysConfig.values[KEY_IPC_MAP_SIZE];
}

int Config::getDefaultDbCachePages()
{
	return (int) sysConfig.values[KEY_DEFAULT_DB_CACHE_PAGES];
}

int Config::getConnectionTimeout()
{
	return (int) sysConfig.values[KEY_CONNECTION_TIMEOUT];
}

int Config::getDummyPacketInterval()
{
	return (int) sysConfig.values[KEY_DUMMY_PACKET_INTERVAL];
}

int Config::getLockMemSize()
{
	return (int) sysConfig.values[KEY_LOCK_MEM_SIZE];
}

int Config::getLockSemCount()
{
	return (int) sysConfig.values[KEY_LOCK_SEM_COUNT];
}

int Config::getLockSignal()
{
	return (int) sysConfig.values[KEY_LOCK_SIGNAL];
}

bool Config::getLockGrantOrder()
{
	return (bool) sysConfig.values[KEY_LOCK_GRANT_ORDER];
}

int Config::getLockHashSlots()
{
	return (int) sysConfig.values[KEY_LOCK_HASH_SLOTS];
}

bool Config::getLockAcquireSpins()
{
	return (bool) sysConfig.values[KEY_LOCK_ACQUIRE_SPINS];
}

int Config::getEventMemSize()
{
	return (int) sysConfig.values[KEY_EVENT_MEM_SIZE];
}

int Config::getDeadlockTimeout()
{
	return (int) sysConfig.values[KEY_DEADLOCK_TIMEOUT];
}

int Config::getSolarisStallValue()
{
	return (int) sysConfig.values[KEY_SOLARIS_STALL_VALUE];
}

bool Config::getTraceMemoryPools()
{
	return (bool) sysConfig.values[KEY_TRACE_MEMORY_POOLS];
}

int Config::getPrioritySwitchDelay()
{
	int rc = (int) sysConfig.values[KEY_PRIORITY_SWITCH_DELAY];
	if (rc < 1)
		rc = 1;
	return rc;
}

int Config::getDeadThreadsCollection()
{
	int rc = (int) sysConfig.values[KEY_DEAD_THREADS_COLLECTION];
	if (rc < 1)
		rc = 1;
	return rc;
}

int Config::getPriorityBoost()
{
	int rc = (int) sysConfig.values[KEY_PRIORITY_BOOST];
	if (rc < 1)
		rc = 1;
	if (rc > 1000)
		rc = 1000;
	return rc;
}

const char *Config::getRemoteServiceName()
{
	return (const char*) sysConfig.values[KEY_REMOTE_SERVICE_NAME];
}

int Config::getRemoteServicePort()
{
	return (int) sysConfig.values[KEY_REMOTE_SERVICE_PORT];
}

const char *Config::getRemotePipeName()
{
	return (const char*) sysConfig.values[KEY_REMOTE_PIPE_NAME];
}

const char *Config::getIpcName()
{
	return (const char*) sysConfig.values[KEY_IPC_NAME];
}

int Config::getMaxUnflushedWrites()
{
	return (int) sysConfig.values[KEY_MAX_UNFLUSHED_WRITES];
}

int Config::getMaxUnflushedWriteTime()
{
	return (int) sysConfig.values[KEY_MAX_UNFLUSHED_WRITE_TIME];
}

int Config::getProcessPriorityLevel()
{
	return (int) sysConfig.values[KEY_PROCESS_PRIORITY_LEVEL];
}

bool Config::getCreateInternalWindow()
{
	return (bool) sysConfig.values[KEY_CREATE_INTERNAL_WINDOW];
}

bool Config::getCompleteBooleanEvaluation()
{
	return (bool) sysConfig.values[KEY_COMPLETE_BOOLEAN_EVALUATION];
}

int Config::getRemoteAuxPort()
{
	return (int) sysConfig.values[KEY_REMOTE_AUX_PORT];
}

const char *Config::getRemoteBindAddress()
{
	return (const char*) sysConfig.values[KEY_REMOTE_BIND_ADDRESS];
}
