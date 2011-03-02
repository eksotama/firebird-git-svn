/*
 *	PROGRAM:	JRD Access Method
 *	MODULE:		jrd_pwd.h
 *	DESCRIPTION:	User information database name
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
 * 2002.10.29 Sean Leyne - Removed obsolete "Netware" port
 * 2003.02.02 Dmitry Yemanov: Implemented cached security database connection
 */

#ifndef AUTH_LEGACY_SERVER_H
#define AUTH_LEGACY_SERVER_H

#include "../jrd/ibase.h"
#include "../common/utils_proto.h"
#include "../common/sha.h"
#include "gen/iberror.h"
#include "../common/classes/ClumpletWriter.h"
#include "../common/classes/ImplementHelper.h"

#include "../auth/AuthInterface.h"

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include <time.h>

namespace Auth {

const size_t MAX_PASSWORD_LENGTH = 64;			// used to store passwords internally
static const char* const PASSWORD_SALT = "9z";	// for old ENC_crypt()
const size_t SALT_LENGTH = 12;					// measured after base64 coding

class SecurityDatabase : public Firebird::GlobalStorage
{
public:
	Result verify(WriterInterface* authBlock,
				  Firebird::ClumpletReader& originalDpb);

	static void shutdown(void*);

	static void hash(Firebird::string& h, const Firebird::string& userName, const TEXT* passwd)
	{
		Firebird::string salt;
		Jrd::CryptSupport::random(salt, SALT_LENGTH);
		hash(h, userName, passwd, salt);
	}

	static void hash(Firebird::string& h,
					 const Firebird::string& userName,
					 const Firebird::string& passwd,
					 const Firebird::string& oldHash)
	{
		Firebird::string salt(oldHash);
		salt.resize(SALT_LENGTH, '=');
		Firebird::string allData(salt);
		allData += userName;
		allData += passwd;
		Jrd::CryptSupport::hash(h, allData);
		h = salt + h;
	}

	char secureDbName[MAXPATHLEN];

	SecurityDatabase()
		: lookup_db(0), lookup_req(0)
	{
	}

private:
	Firebird::Mutex mutex;

	ISC_STATUS_ARRAY status;

	isc_db_handle lookup_db;
	isc_req_handle lookup_req;

	void init();
	void fini();
	bool lookup_user(const char*, char*);
	void prepare();
	void checkStatus(const char* callName, ISC_STATUS userError = isc_psw_db_error);
};

class SecurityDatabaseServerFactory : public Firebird::StdIface<Firebird::PluginsFactory, FB_PLUGINS_FACTORY_VERSION>
{
public:
	Firebird::Plugin* FB_CARG createPlugin(const char* name, const char* configFile);
};

class SecurityDatabaseServer : public Firebird::StdPlugin<Server, FB_AUTH_SERVER_VERSION>
{
public:
	explicit SecurityDatabaseServer(Firebird::IFactoryParameter* p)
		: iParameter(p)
	{ }

	Result FB_CARG startAuthentication(Firebird::Status* status, bool isService, const char* dbName,
									   const unsigned char* dpb, unsigned int dpbSize,
									   WriterInterface* writerInterface);
	Result FB_CARG contAuthentication(Firebird::Status* status, WriterInterface* writerInterface,
									  const unsigned char* data, unsigned int size);
	void FB_CARG getData(const unsigned char** data, unsigned short* dataSize);
	int FB_CARG release();

private:
	Firebird::RefPtr<Firebird::IFactoryParameter> iParameter;
};

void registerLegacyServer(Firebird::IPlugin* iPlugin);

} // namespace Auth

#endif // AUTH_LEGACY_SERVER_H
