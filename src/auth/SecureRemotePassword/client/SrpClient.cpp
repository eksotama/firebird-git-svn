/*
 *	PROGRAM:		Firebird authentication.
 *	MODULE:			SrpClient.cpp
 *	DESCRIPTION:	SPR authentication plugin.
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
 *  Copyright (c) 2011 Alex Peshkov <peshkoff at mail.ru>
 *  and all contributors signed below.
 *
 *  All Rights Reserved.
 *  Contributor(s): ______________________________________.
 */

#include "firebird.h"

#include "../auth/SecureRemotePassword/client/SrpClient.h"
#include "../auth/SecureRemotePassword/srp.h"
#include "../common/classes/ImplementHelper.h"

using namespace Firebird;

namespace Auth {

class SrpClient : public Firebird::StdPlugin<IClient, FB_AUTH_CLIENT_VERSION>
{
public:
	explicit SrpClient(Firebird::IPluginConfig*)
		: client(NULL), data(getPool()),
		  sessionKey(getPool())
	{ }

	// IClient implementation
	Result FB_CARG authenticate(Firebird::IStatus*, IClientBlock* cb);
	Result FB_CARG getSessionKey(Firebird::IStatus* status,
								 const unsigned char** key, unsigned int* keyLen);
    int FB_CARG release();

private:
	RemotePassword* client;
	string data;
	UCharBuffer sessionKey;
};

Result SrpClient::authenticate(Firebird::IStatus* status, IClientBlock* cb)
{
	try
	{
		if (sessionKey.hasData())
		{
			// Why are we called when auth is completed?
			(Firebird::Arg::Gds(isc_random) << "Auth sync failure - SRP's authenticate called more times than supported").raise();
		}

		if (!client)
		{
			HANDSHAKE_DEBUG(fprintf(stderr, "Cln SRP1: %s %s\n", cb->getLogin(), cb->getPassword()));
			if (!(cb->getLogin() && cb->getPassword()))
			{
				return AUTH_CONTINUE;
			}

			client = new RemotePassword;
			client->genClientKey(data);
			dumpIt("Clnt: clientPubKey", data);
			cb->putData(data.length(), data.begin());
			return AUTH_MORE_DATA;
		}

		HANDSHAKE_DEBUG(fprintf(stderr, "Cln SRP2\n"));
		unsigned int length;
		const unsigned char* saltAndKey = cb->getData(&length);
		if (length > (RemotePassword::SRP_SALT_SIZE + RemotePassword::SRP_KEY_SIZE + 2) * 2)
		{
			string msg;
			msg.printf("Wrong length (%d) of data from server", length);
			(Arg::Gds(isc_random) << msg).raise();
		}

		string salt, key;
		unsigned charSize = *saltAndKey++;
		charSize += ((unsigned)*saltAndKey++) << 8;
		if (charSize > RemotePassword::SRP_SALT_SIZE * 2)
		{
			string msg;
			msg.printf("Wrong length (%d) of salt from server", charSize);
			(Arg::Gds(isc_random) << msg).raise();
		}
		salt.assign(saltAndKey, charSize);
		dumpIt("Clnt: salt", salt);
		saltAndKey += charSize;
		length -= (charSize + 2);

		charSize = *saltAndKey++;
		charSize += ((unsigned)*saltAndKey++) << 8;
		if (charSize + 2 != length)
		{
			string msg;
			msg.printf("Wrong length (%d) of key from server", charSize);
			(Arg::Gds(isc_random) << msg).raise();
		}

		key.assign(saltAndKey, charSize);
		dumpIt("Clnt: key(srvPub)", key);

		dumpIt("Clnt: login", string(cb->getLogin()));
		dumpIt("Clnt: pass", string(cb->getPassword()));
		client->clientSessionKey(sessionKey, cb->getLogin(), salt.c_str(), cb->getPassword(), key.c_str());
		dumpIt("Clnt: sessionKey", sessionKey);

		BigInteger cProof = client->clientProof(cb->getLogin(), salt.c_str(), sessionKey);
		cProof.getText(data);

		cb->putData(data.length(), data.c_str());
	}
	catch(const Exception& ex)
	{
		ex.stuffException(status);
		return AUTH_FAILED;
	}

	return AUTH_SUCCESS;
}


Result SrpClient::getSessionKey(Firebird::IStatus*,
								const unsigned char** key, unsigned int* keyLen)
{
	if (!sessionKey.hasData())
	{
		return AUTH_MORE_DATA;
	}

	*key = sessionKey.begin();
	*keyLen = sessionKey.getCount();

	return AUTH_SUCCESS;
}

int SrpClient::release()
{
	if (--refCounter == 0)
	{
		delete this;
		return 0;
	}
	return 1;
}

namespace
{
	Firebird::SimpleFactory<SrpClient> factory;
}

void registerSrpClient(Firebird::IPluginManager* iPlugin)
{
	iPlugin->registerPluginFactory(PluginType::AuthClient, RemotePassword::plugName, &factory);
}

} // namespace Auth

