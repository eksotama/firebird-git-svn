/*
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
 *  The Original Code was created by Dmitry Yemanov
 *  for the Firebird Open Source RDBMS project.
 *
 *  Copyright (c) 2002 Dmitry Yemanov <dimitr@users.sf.net>
 *  and all contributors signed below.
 *
 *  All Rights Reserved.
 *  Contributor(s): ______________________________________.
 */

#include "firebird.h"

#include "../common/config/config.h"
#include "../common/config/config_file.h"
#include "../jrd/os/config_root.h"
#include "../common/classes/init.h"
#include "../jrd/os/fbsyslog.h"

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

// config_file works with OS case-sensitivity
typedef Firebird::PathName String;

namespace {

/******************************************************************************
 *
 *	firebird.conf implementation
 */

class ConfigImpl : public ConfigRoot
{
public:
	explicit ConfigImpl(Firebird::MemoryPool& p) : ConfigRoot(p)
	{
		try
		{
			root_dir = getRootDirectory();

			ConfigFile file(getConfigFilePath(), ConfigFile::EXCEPTION_ON_ERROR | ConfigFile::NO_MACRO);
			defaultConfig = new Config(file);
		}
		catch (const Firebird::fatal_exception& ex)
		{
			Firebird::Syslog::Record(Firebird::Syslog::Error, ex.what());
			(Firebird::Arg::Gds(isc_random) << "Problems with master configuration file - "
											   "inform server admin please").raise();
		}
	}

/*	void changeDefaultConfig(Config* newConfig)
	{
		defaultConfig = newConfig;
	}
 */
	Firebird::RefPtr<Config> getDefaultConfig() const
	{
		return defaultConfig;
	}

	const char* getCachedRootDir()
	{
		return root_dir;
	}

private:
	const char* root_dir;
	Firebird::RefPtr<Config> defaultConfig;

    ConfigImpl(const ConfigImpl&);
    void operator=(const ConfigImpl&);

};

/******************************************************************************
 *
 *	Static instance of the system configuration file
 */

Firebird::InitInstance<ConfigImpl> firebirdConf;

}	// anonymous namespace

/******************************************************************************
 *
 *	Configuration entries
 */

const char*	GCPolicyCooperative	= "cooperative";
const char*	GCPolicyBackground	= "background";
const char*	GCPolicyCombined	= "combined";
#ifdef SUPERSERVER
const char*	GCPolicyDefault	= GCPolicyCombined;
#else
const char*	GCPolicyDefault	= GCPolicyCooperative;
#endif

const char*	AmNative	= "native";
const char*	AmTrusted	= "trusted";
const char*	AmMixed		= "mixed";

const Config::ConfigEntry Config::entries[MAX_CONFIG_KEY] =
{
	{TYPE_STRING,		"RootDirectory",			(ConfigValue) 0},
	{TYPE_INTEGER,		"TempBlockSize",			(ConfigValue) 1048576},		// bytes
#ifdef SUPERSERVER
	{TYPE_INTEGER,		"TempCacheLimit",			(ConfigValue) 67108864},	// bytes
#elif defined(WIN_NT) // win32 CS
	{TYPE_INTEGER,		"TempCacheLimit",			(ConfigValue) 8388608},		// bytes
#else // non-win32 CS
	{TYPE_INTEGER,		"TempCacheLimit",			(ConfigValue) 0},			// bytes
#endif
#ifdef BOOT_BUILD
	{TYPE_BOOLEAN,		"RemoteFileOpenAbility",	(ConfigValue) true},
#else
	{TYPE_BOOLEAN,		"RemoteFileOpenAbility",	(ConfigValue) false},
#endif
	{TYPE_INTEGER,		"GuardianOption",			(ConfigValue) 1},
	{TYPE_INTEGER,		"CpuAffinityMask",			(ConfigValue) 1},
	{TYPE_INTEGER,		"TcpRemoteBufferSize",		(ConfigValue) 8192},		// bytes
	{TYPE_BOOLEAN,		"TcpNoNagle",				(ConfigValue) true},
#ifdef SUPERSERVER
	{TYPE_INTEGER,		"DefaultDbCachePages",		(ConfigValue) 2048},		// pages
#else
	{TYPE_INTEGER,		"DefaultDbCachePages",		(ConfigValue) 75},			// pages
#endif
	{TYPE_INTEGER,		"ConnectionTimeout",		(ConfigValue) 180},			// seconds
	{TYPE_INTEGER,		"DummyPacketInterval",		(ConfigValue) 0},			// seconds
	{TYPE_INTEGER,		"LockMemSize",				(ConfigValue) 1048576},		// bytes
	{TYPE_BOOLEAN,		"LockGrantOrder",			(ConfigValue) true},
	{TYPE_INTEGER,		"LockHashSlots",			(ConfigValue) 1009},		// slots
	{TYPE_INTEGER,		"LockAcquireSpins",			(ConfigValue) 0},
	{TYPE_INTEGER,		"EventMemSize",				(ConfigValue) 65536},		// bytes
	{TYPE_INTEGER,		"DeadlockTimeout",			(ConfigValue) 10},			// seconds
	{TYPE_INTEGER,		"PrioritySwitchDelay",		(ConfigValue) 100},			// milliseconds
	{TYPE_BOOLEAN,		"UsePriorityScheduler",		(ConfigValue) true},
	{TYPE_INTEGER,		"PriorityBoost",			(ConfigValue) 5},			// ratio oh high- to low-priority thread ticks in jrd.cpp
	{TYPE_STRING,		"RemoteServiceName",		(ConfigValue) FB_SERVICE_NAME},
	{TYPE_INTEGER,		"RemoteServicePort",		(ConfigValue) 0},
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
	{TYPE_INTEGER,		"RemoteAuxPort",			(ConfigValue) 0},
	{TYPE_STRING,		"RemoteBindAddress",		(ConfigValue) 0},
	{TYPE_STRING,		"ExternalFileAccess",		(ConfigValue) "None"},	// location(s) of external files for tables
	{TYPE_STRING,		"DatabaseAccess",			(ConfigValue) "Full"},	// location(s) of databases
#define UDF_DEFAULT_CONFIG_VALUE "Restrict UDF"
	{TYPE_STRING,		"UdfAccess",				(ConfigValue) UDF_DEFAULT_CONFIG_VALUE},	// location(s) of UDFs
	{TYPE_STRING,		"TempDirectories",			(ConfigValue) 0},
#ifdef DEV_BUILD
 	{TYPE_BOOLEAN,		"BugcheckAbort",			(ConfigValue) true},	// whether to abort() engine when internal error is found
#else
 	{TYPE_BOOLEAN,		"BugcheckAbort",			(ConfigValue) false},	// whether to abort() engine when internal error is found
#endif
	{TYPE_BOOLEAN,		"LegacyHash",				(ConfigValue) true},	// let use old passwd hash verification
	{TYPE_STRING,		"GCPolicy",					(ConfigValue) GCPolicyDefault},	// garbage collection policy
	{TYPE_BOOLEAN,		"Redirection",				(ConfigValue) false},
	{TYPE_STRING,		"Authentication",			(ConfigValue) AmNative},	// use native, trusted or mixed
	{TYPE_INTEGER,		"DatabaseGrowthIncrement",	(ConfigValue) 128 * 1048576},	// bytes
	{TYPE_INTEGER,		"FileSystemCacheThreshold",	(ConfigValue) 65536},	// page buffers
	{TYPE_BOOLEAN,		"RelaxedAliasChecking",		(ConfigValue) false},	// if true relax strict alias checking rules in DSQL a bit
	{TYPE_BOOLEAN,		"OldSetClauseSemantics",	(ConfigValue) false},	// if true disallow SET A = B, B = A to exchange column values
	{TYPE_STRING,		"AuditTraceConfigFile",		(ConfigValue) ""},		// location of audit trace configuration file
	{TYPE_INTEGER,		"MaxUserTraceLogSize",		(ConfigValue) 10},		// maximum size of user session trace log
	{TYPE_INTEGER,		"FileSystemCacheSize",		(ConfigValue) 30}		// percent
};

/******************************************************************************
 *
 *	Config routines
 */

Config::Config(const ConfigFile& file)
{
	// Iterate through the known configuration entries

	for (unsigned int i = 0; i < MAX_CONFIG_KEY; i++)
	{
		values[i] = entries[i].default_value;
	}

	loadValues(file);
}

Config::Config(const ConfigFile& file, const Config& base)
{
	// Iterate through the known configuration entries

	for (unsigned int i = 0; i < MAX_CONFIG_KEY; i++)
	{
		values[i] = base.values[i];
	}

	loadValues(file);
}

void Config::loadValues(const ConfigFile& file)
{
	// Iterate through the known configuration entries

	for (int i = 0; i < MAX_CONFIG_KEY; i++)
	{
		const ConfigEntry& entry = entries[i];
		const String value = getValue(file, entry.key);

		if (value.length())
		{
			// Assign the actual value

			switch (entry.data_type)
			{
			case TYPE_BOOLEAN:
				values[i] = (ConfigValue) asBoolean(value);
				break;
			case TYPE_INTEGER:
				values[i] = (ConfigValue) asInteger(value);
				break;
			case TYPE_STRING:
				values[i] = (ConfigValue) asString(value);
				break;
			//case TYPE_STRING_VECTOR:
			//	break;
			}

			if (entry.data_type == TYPE_STRING && values[i] != entry.default_value)
			{
				const char* src = (const char*) values[i];
				char* dst = FB_NEW(getPool()) char[strlen(src) + 1];
				strcpy(dst, src);
				values[i] = (ConfigValue) dst;
			}
		}
	}
}

Config::~Config()
{
	// Free allocated memory

	for (int i = 0; i < MAX_CONFIG_KEY; i++)
	{
		if (values[i] == entries[i].default_value)
			continue;

		switch (entries[i].data_type)
		{
		case TYPE_STRING:
			delete[] (char*) values[i];
			break;
		//case TYPE_STRING_VECTOR:
		//	break;
		}
	}
}

String Config::getValue(const ConfigFile& file, ConfigName key)
{
	const ConfigFile::Parameter* p = file.findParameter(key);
	return p ? p->value : "";
}

int Config::asInteger(const String &value)
{
	return atoi(value.data());
}

bool Config::asBoolean(const String &value)
{
	return (atoi(value.data()) != 0);
}

const char* Config::asString(const String &value)
{
	return value.c_str();
}

template <typename T>
T Config::get(Config::ConfigKey key) const
{
	return (T) values[key];
}

/******************************************************************************
 *
 *	Public interface
 */

const Firebird::RefPtr<Config> Config::getDefaultConfig()
{
	return firebirdConf().getDefaultConfig();
}

const char* Config::getInstallDirectory()
{
	return firebirdConf().getInstallDirectory();
}

static Firebird::PathName* rootFromCommandLine = 0;

void Config::setRootDirectoryFromCommandLine(const Firebird::PathName& newRoot)
{
	delete rootFromCommandLine;
	rootFromCommandLine = FB_NEW(*getDefaultMemoryPool())
		Firebird::PathName(*getDefaultMemoryPool(), newRoot);
}

const Firebird::PathName* Config::getCommandLineRootDirectory()
{
	return rootFromCommandLine;
}

const char* Config::getRootDirectory()
{
	// must check it here - command line must override any other root settings, including firebird.conf
	if (rootFromCommandLine)
	{
		return rootFromCommandLine->c_str();
	}

	const char* result = (char*) getDefaultConfig()->values[KEY_ROOT_DIRECTORY];
	return result ? result : firebirdConf().getCachedRootDir();
}

int Config::getTempBlockSize() const
{
	return get<int>(KEY_TEMP_BLOCK_SIZE);
}

int Config::getTempCacheLimit() const
{
	int v = get<int>(KEY_TEMP_CACHE_LIMIT);
	return v < 0 ? 0 : v;
}

bool Config::getRemoteFileOpenAbility()
{
	return (bool) getDefaultConfig()->values[KEY_REMOTE_FILE_OPEN_ABILITY];
}

int Config::getGuardianOption()
{
	return (int) getDefaultConfig()->values[KEY_GUARDIAN_OPTION];
}

int Config::getCpuAffinityMask()
{
	return (int) getDefaultConfig()->values[KEY_CPU_AFFINITY_MASK];
}

int Config::getTcpRemoteBufferSize()
{
	int rc = (int) getDefaultConfig()->values[KEY_TCP_REMOTE_BUFFER_SIZE];
	if (rc < 1448)
		rc = 1448;
	if (rc > MAX_SSHORT)
		rc = MAX_SSHORT;
	return rc;
}

bool Config::getTcpNoNagle()
{
	return (bool) getDefaultConfig()->values[KEY_TCP_NO_NAGLE];
}

int Config::getDefaultDbCachePages() const
{
	return get<int>(KEY_DEFAULT_DB_CACHE_PAGES);
}

int Config::getConnectionTimeout()
{
	return (int) getDefaultConfig()->values[KEY_CONNECTION_TIMEOUT];
}

int Config::getDummyPacketInterval()
{
	return (int) getDefaultConfig()->values[KEY_DUMMY_PACKET_INTERVAL];
}

int Config::getLockMemSize() const
{
	return get<int>(KEY_LOCK_MEM_SIZE);
}

bool Config::getLockGrantOrder() const
{
	return get<bool>(KEY_LOCK_GRANT_ORDER);
}

int Config::getLockHashSlots() const
{
	return get<int>(KEY_LOCK_HASH_SLOTS);
}

int Config::getLockAcquireSpins() const
{
	return get<int>(KEY_LOCK_ACQUIRE_SPINS);
}

int Config::getEventMemSize() const
{
	return get<int>(KEY_EVENT_MEM_SIZE);
}

int Config::getDeadlockTimeout() const
{
	return get<int>(KEY_DEADLOCK_TIMEOUT);
}

int Config::getPrioritySwitchDelay()
{
	int rc = (int) getDefaultConfig()->values[KEY_PRIORITY_SWITCH_DELAY];
	if (rc < 1)
		rc = 1;
	return rc;
}

int Config::getPriorityBoost()
{
	int rc = (int) getDefaultConfig()->values[KEY_PRIORITY_BOOST];
	if (rc < 1)
		rc = 1;
	if (rc > 1000)
		rc = 1000;
	return rc;
}

bool Config::getUsePriorityScheduler()
{
	return (bool) getDefaultConfig()->values[KEY_USE_PRIORITY_SCHEDULER];
}

const char *Config::getRemoteServiceName()
{
	return (const char*) getDefaultConfig()->values[KEY_REMOTE_SERVICE_NAME];
}

unsigned short Config::getRemoteServicePort()
{
	return (unsigned short) getDefaultConfig()->values[KEY_REMOTE_SERVICE_PORT];
}

const char *Config::getRemotePipeName()
{
	return (const char*) getDefaultConfig()->values[KEY_REMOTE_PIPE_NAME];
}

const char *Config::getIpcName()
{
	return (const char*) getDefaultConfig()->values[KEY_IPC_NAME];
}

int Config::getMaxUnflushedWrites() const
{
	return get<int>(KEY_MAX_UNFLUSHED_WRITES);
}

int Config::getMaxUnflushedWriteTime() const
{
	return get<int>(KEY_MAX_UNFLUSHED_WRITE_TIME);
}

int Config::getProcessPriorityLevel()
{
	return (int) getDefaultConfig()->values[KEY_PROCESS_PRIORITY_LEVEL];
}

int Config::getRemoteAuxPort()
{
	return (int) getDefaultConfig()->values[KEY_REMOTE_AUX_PORT];
}

const char *Config::getRemoteBindAddress()
{
	return (const char*) getDefaultConfig()->values[KEY_REMOTE_BIND_ADDRESS];
}

const char *Config::getExternalFileAccess() const
{
	return get<const char*>(KEY_EXTERNAL_FILE_ACCESS);
}

const char *Config::getDatabaseAccess()
{
	return (const char*) getDefaultConfig()->values[KEY_DATABASE_ACCESS];
}

const char *Config::getUdfAccess()
{
	static Firebird::GlobalPtr<Firebird::Mutex> udfMutex;
	static Firebird::GlobalPtr<Firebird::string> udfValue;
	static const char* volatile value = 0;

	if (value)
	{
		return value;
	}

	Firebird::MutexLockGuard guard(udfMutex);

	if (value)
	{
		return value;
	}

	const char* v = (const char*) getDefaultConfig()->values[KEY_UDF_ACCESS];
	if (CASE_SENSITIVITY ? (! strcmp(v, UDF_DEFAULT_CONFIG_VALUE) && FB_UDFDIR[0]) :
						   (! fb_utils::stricmp(v, UDF_DEFAULT_CONFIG_VALUE) && FB_UDFDIR[0]))
	{
		udfValue->printf("Restrict %s", FB_UDFDIR);
		value = udfValue->c_str();
	}
	else
	{
		value = v;
	}
	return value;
}

const char *Config::getTempDirectories()
{
	return (const char*) getDefaultConfig()->values[KEY_TEMP_DIRECTORIES];
}

bool Config::getBugcheckAbort()
{
	return (bool) getDefaultConfig()->values[KEY_BUGCHECK_ABORT];
}

bool Config::getLegacyHash()
{
	return (bool) getDefaultConfig()->values[KEY_LEGACY_HASH];
}

const char *Config::getGCPolicy() const
{
	return get<const char*>(KEY_GC_POLICY);
}

bool Config::getRedirection()
{
	return (bool) getDefaultConfig()->values[KEY_REDIRECTION];
}

const char *Config::getAuthMethod()
{
	return (const char*) getDefaultConfig()->values[KEY_AUTH_METHOD];
}

int Config::getDatabaseGrowthIncrement() const
{
	return get<int>(KEY_DATABASE_GROWTH_INCREMENT);
}

int Config::getFileSystemCacheThreshold() const
{
	int rc = get<int>(KEY_FILESYSTEM_CACHE_THRESHOLD);
	return rc < 0 ? 0 : rc;
}

bool Config::getRelaxedAliasChecking()
{
	return (bool) getDefaultConfig()->values[KEY_RELAXED_ALIAS_CHECKING];
}

bool Config::getOldSetClauseSemantics()
{
	return (bool) getDefaultConfig()->values[KEY_OLD_SET_CLAUSE_SEMANTICS];
}

int Config::getFileSystemCacheSize()
{
	return (int) getDefaultConfig()->values[KEY_FILESYSTEM_CACHE_SIZE];
}

const char *Config::getAuditTraceConfigFile()
{
	return (const char*) getDefaultConfig()->values[KEY_TRACE_CONFIG];
}

int Config::getMaxUserTraceLogSize()
{
	return (int) getDefaultConfig()->values[KEY_MAX_TRACELOG_SIZE];
}
