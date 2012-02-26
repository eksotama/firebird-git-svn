/*
 *	PROGRAM:		Firebird authentication
 *	MODULE:			Auth.cpp
 *	DESCRIPTION:	Implementation of interfaces, passed to plugins
 *					Plugins loader
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
 *  The Original Code was created by Alex Peshkov
 *  for the Firebird Open Source RDBMS project.
 *
 *  Copyright (c) 2010 Alex Peshkov <peshkoff at mail.ru>
 *  and all contributors signed below.
 *
 *  All Rights Reserved.
 *  Contributor(s): ______________________________________.
 */

#include "firebird.h"
#include "../auth/AuthDbg.h"
#include "../jrd/ibase.h"

#ifdef AUTH_DEBUG

//#define AUTH_VERBOSE

static Firebird::MakeUpgradeInfo<> upInfo;

// register plugin
static Firebird::SimpleFactory<Auth::DebugClient> clientFactory;
static Firebird::SimpleFactory<Auth::DebugServer> serverFactory;

extern "C" void FB_PLUGIN_ENTRY_POINT(Firebird::IMaster* master)
{
	const char* name = "Auth_Debug";

	Firebird::PluginManagerInterfacePtr iPlugin(master);

	iPlugin->registerPluginFactory(Firebird::PluginType::AuthClient, name, &clientFactory);
	iPlugin->registerPluginFactory(Firebird::PluginType::AuthServer, name, &serverFactory);
}


namespace Auth {

DebugServer::DebugServer(Firebird::IPluginConfig*)
	: str(getPool())
{ }

int FB_CARG DebugServer::authenticate(Firebird::IStatus* status, IServerBlock* sBlock,
                               IWriter* writerInterface)
{
	/***
	try
	{
		Firebird::MasterInterfacePtr()->upgradeInterface(dpb, FB_AUTH_CLUMPLETS_VERSION, upInfo);
		str.erase();

#ifdef AUTH_VERBOSE
		fprintf(stderr, "DebugServer::startAuthentication: tA-tag=%d dpb=%p\n", tags->trustedAuth, dpb);
#endif
		if (tags->trustedAuth && dpb && dpb->find(tags->trustedAuth))
		{
			unsigned int len;
			const UCHAR* s = dpb->get(&len);
#ifdef AUTH_VERBOSE
			fprintf(stderr, "DebugServer::startAuthentication: get()=%.*s\n", len, s);
#endif
			str.assign(s, len);
		}

		str += '_';
#ifdef AUTH_VERBOSE
		fprintf(stderr, "DebugServer::startAuthentication: %s\n", str.c_str());
#endif
		return AUTH_MORE_DATA;
	}
	catch (const Firebird::Exception& ex)
	{
		ex.stuffException(status);
	}
	***/
	return AUTH_FAILED;
}

/***
int FB_CARG DebugServer::contAuthentication(Firebird::IStatus* status, const unsigned char* data,
											   unsigned int size, IWriter* writerInterface)
{
	try
	{
#ifdef AUTH_VERBOSE
		fprintf(stderr, "DebugServer::contAuthentication: %.*s\n", size, data);
#endif
		Firebird::MasterInterfacePtr()->upgradeInterface(writerInterface, FB_AUTH_WRITER_VERSION, upInfo);
		writerInterface->add(Firebird::string((const char*) data, size).c_str());
		return AUTH_SUCCESS;
	}
	catch (const Firebird::Exception& ex)
	{
		ex.stuffException(status);
		return AUTH_FAILED;
	}
}
***/

int FB_CARG DebugServer::release()
{
	if (--refCounter == 0)
	{
		delete this;
		return 0;
	}

	return 1;
}

DebugClient::DebugClient(Firebird::IPluginConfig*)
	: str(getPool())
{ }

int FB_CARG DebugClient::authenticate(Firebird::IStatus* status, IClientBlock* cBlock)
{
	/***
	try
	{
		str = "HAND";
#ifdef AUTH_VERBOSE
		fprintf(stderr, "DebugClient::startAuthentication: %s\n", str.c_str());
#endif
		if (dpb && tags->trustedAuth)
		{
			Firebird::MasterInterfacePtr()->upgradeInterface(dpb, FB_AUTH_CLUMPLETS_VERSION, upInfo);
			dpb->add(tags->trustedAuth, str.c_str(), str.length());
#ifdef AUTH_VERBOSE
			fprintf(stderr, "DebugClient::startAuthentication: DPB filled\n");
#endif
			return AUTH_SUCCESS;
		}
		return AUTH_MORE_DATA;
	}
	catch (const Firebird::Exception& ex)
	{
		ex.stuffException(status);
	}
	***/
	return AUTH_FAILED;
}

/***
int FB_CARG DebugClient::contAuthentication(Firebird::IStatus* status, const unsigned char* data, unsigned int size)
{
	try
	{
#ifdef AUTH_VERBOSE
		fprintf(stderr, "DebugClient::contAuthentication: %.*s\n", size, data);
#endif
		str.assign(data, size);
		const char* env = getenv("ISC_DEBUG_AUTH");
		if (env && env[0] && str.length() > 0 && str[str.length() - 1] == '_')
			str = env;
		else
			str += "SHAKE";
		return AUTH_CONTINUE;
	}
	catch (const Firebird::Exception& ex)
	{
		ex.stuffException(status);
		return AUTH_FAILED;
	}
}
***/

int FB_CARG DebugClient::release()
{
	if (--refCounter == 0)
	{
		delete this;
		return 0;
	}

	return 1;
}

} // namespace Auth

#endif // AUTH_DEBUG
