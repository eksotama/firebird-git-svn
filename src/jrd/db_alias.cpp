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
#include "../common/classes/init.h"
#include "../common/config/config.h"
#include "../common/config/config_file.h"
#include "../common/config/ConfigCache.h"
#include "../common/config/dir_list.h"
#include "../jrd/os/path_utils.h"
#include "../jrd/gds_proto.h"
#include "../jrd/isc_proto.h"
#include "../jrd/Database.h"
#include "../common/utils_proto.h"
#include "../common/classes/Hash.h"

#include <ctype.h>

using namespace Firebird;

namespace
{
	class DatabaseDirectoryList : public DirectoryList
	{
	private:
		const PathName getConfigString() const
		{
			return PathName(Config::getDatabaseAccess());
		}
	public:
		explicit DatabaseDirectoryList(MemoryPool& p)
			: DirectoryList(p)
		{
			initialize();
		}
	};
	InitInstance<DatabaseDirectoryList> databaseDirectoryList;

	const char* const ALIAS_FILE = "aliases.conf";

	void replace_dir_sep(PathName& s)
	{
		const char correct_dir_sep = PathUtils::dir_sep;
		const char incorrect_dir_sep = (correct_dir_sep == '/') ? '\\' : '/';
		for (char* itr = s.begin(); itr < s.end(); ++itr)
		{
			if (*itr == incorrect_dir_sep)
			{
				*itr = correct_dir_sep;
			}
		}
	}

	template <typename T>
	struct PathHash
	{
		static const PathName& generate(const void* /*sender*/, const T& item)
		{
			return item.name;
		}

		static size_t hash(const PathName& value, size_t hashSize)
		{
			return hash(value.c_str(), value.length(), hashSize);
		}

		static void upcpy(size_t* toPar, const char* from, size_t length)
		{
			char* to = reinterpret_cast<char*>(toPar);
			while (length--)
			{
				if (CASE_SENSITIVITY)
				{
					*to++ = *from++;
				}
				else
				{
					*to++ = toupper(*from++);
				}
			}
		}

		static size_t hash(const char* value, size_t length, size_t hashSize)
		{
			size_t sum = 0;
			size_t val;

			while (length >= sizeof(size_t))
			{
				upcpy(&val, value, sizeof(size_t));
				sum += val;
				value += sizeof(size_t);
				length -= sizeof(size_t);
			}

			if (length)
			{
				val = 0;
				upcpy(&val, value, length);
				sum += val;
			}

			size_t rc = 0;
			while (sum)
			{
				rc += (sum % hashSize);
				sum /= hashSize;
			}

			return rc % hashSize;
		}
	};

	struct DbName;
	typedef Hash<DbName, 127, PathName, PathHash<DbName>, PathHash<DbName> > DbHash;
	struct DbName : public DbHash::Entry
	{
		DbName(MemoryPool& p, const PathName& db) 
			: name(p, db) { }

		DbName* get()
		{
			return this;
		}

		bool isEqual(const PathName& val) const
		{
			return val == name;
		}

		PathName name;
		RefPtr<Config> config;
	};

	struct AliasName;
	typedef Hash<AliasName, 251, PathName, PathHash<AliasName>, PathHash<AliasName> > AliasHash;
	struct AliasName : public AliasHash::Entry
	{
		AliasName(MemoryPool& p, const PathName& al, DbName* db) 
			: name(p, al), database(db) { }

		AliasName* get()
		{
			return this;
		}

		bool isEqual(const PathName& val) const
		{
			return val == name;
		}

		PathName name;
		DbName* database;
	};

	class AliasesConf : public ConfigCache
	{
	public:
		explicit AliasesConf(MemoryPool& p)
			: ConfigCache(p, fb_utils::getPrefix(fb_utils::FB_DIR_CONF, ALIAS_FILE)),
			  databases(getPool()), aliases(getPool())
		{ }

		void loadConfig()
		{
			size_t n;

			// clean old data
			for (n = 0; n < aliases.getCount(); ++n)
			{
				delete aliases[n];
			}
			aliases.clear();
			for (n = 0; n < databases.getCount(); ++n)
			{
				delete databases[n];
			}
			databases.clear();

			ConfigFile aliasConfig(fileName, ConfigFile::HAS_SUB_CONF);
			const ConfigFile::Parameters& params = aliasConfig.getParameters();

			for (n = 0; n < params.getCount(); ++n)
			{
				const ConfigFile::Parameter* par = &params[n];

				PathName file(par->value);
				replace_dir_sep(file);
				if (PathUtils::isRelative(file))
				{
					gds__log("Value %s configured for alias %s "
						"is not a fully qualified path name, ignored",
								file.c_str(), par->name.c_str());
					continue;
				}
				DbName* db = dbHash.lookup(file);
				if (! db)
				{
					db = FB_NEW(getPool()) DbName(getPool(), file);
					databases.add(db);
					dbHash.add(db);
				}
				else 
				{
					// check for duplicated config
					if (par->sub && db->config.hasData())
					{
						fatal_exception::raiseFmt("Duplicated configuration for database %s\n", 
												  file.c_str());
					}
				}
				if (par->sub)
				{
					// load per-database configuration
					db->config = new Config(*par->sub, *Config::getDefaultConfig());
				}
				
				PathName correctedAlias(par->name);
				AliasName* alias = aliasHash.lookup(correctedAlias);
				if (alias)
				{
					fatal_exception::raiseFmt("Duplicated alias %s\n", correctedAlias.c_str());
				}
				alias = FB_NEW(getPool()) AliasName(getPool(), correctedAlias, db);
				aliases.add(alias);
				aliasHash.add(alias);
			}
		}

	private:
		HalfStaticArray<DbName*, 100> databases;
		HalfStaticArray<AliasName*, 200> aliases;

	public:
		DbHash dbHash;
		AliasHash aliasHash;
	};

	InitInstance<AliasesConf> aliasesConf;
}

bool ResolveDatabaseAlias(const PathName& alias, PathName& file, RefPtr<Config>* config)
{
	try
	{
		aliasesConf().checkLoadConfig();
	}
	catch (const fatal_exception& ex)
	{
		gds__log("File aliases.conf contains bad data: %s", ex.what());
		(Arg::Gds(isc_random) << "Server misconfigured - contact administrator please").raise();
	}

	ReadLockGuard guard(aliasesConf().rwLock);

	PathName corrected_alias = alias;
	replace_dir_sep(corrected_alias);

	bool rc = true;
	AliasName* a = aliasesConf().aliasHash.lookup(corrected_alias);
	DbName* db = a ? a->database : NULL;
	if (db)
	{
		file = db->name;
	}
	else
	{
		// If file_name has no path part, expand it in DatabasesPath
		PathName path, name;
		PathUtils::splitLastComponent(path, name, corrected_alias);

		// if path component not present in file_name
		if (path.isEmpty())
		{
			// try to expand to existing file
			if (!databaseDirectoryList().expandFileName(file, name))
			{
				// try to use default path
				if (!databaseDirectoryList().defaultName(file, name))
				{
					rc = false;
				}
			}
		}
		else
		{
			rc = false;
		}

		if (! rc)
		{
			file = corrected_alias;
		}
		db = aliasesConf().dbHash.lookup(file);
	}

	if (config)
	{
		*config = (db && db->config.hasData()) ? db->config : Config::getDefaultConfig();
	}

	return rc;
}
