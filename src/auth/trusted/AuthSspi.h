#ifndef AUTH_SSPI_H
#define AUTH_SSPI_H

#include <firebird.h>

#ifdef TRUSTED_AUTH

#include <../common/classes/fb_string.h>
#include <../common/classes/array.h>
#include <../jrd/ibase.h>
#include "../auth/AuthInterface.h"

#define SECURITY_WIN32

#include <windows.h>
#include <Security.h>
#include <stdio.h>

namespace Auth {

class AuthSspi
{
private:
	enum {BUFSIZE = 4096};

	SecHandle secHndl;
	bool hasCredentials;
	CtxtHandle ctxtHndl;
	bool hasContext;
	Firebird::string ctName;
	bool wheel;

	// Handle of library
	static HINSTANCE library;

	// declare entries, required from secur32.dll
	ACQUIRE_CREDENTIALS_HANDLE_FN_A fAcquireCredentialsHandle;
	DELETE_SECURITY_CONTEXT_FN fDeleteSecurityContext;
	FREE_CREDENTIALS_HANDLE_FN fFreeCredentialsHandle;
	QUERY_CONTEXT_ATTRIBUTES_FN_A fQueryContextAttributes;
	FREE_CONTEXT_BUFFER_FN fFreeContextBuffer;
	INITIALIZE_SECURITY_CONTEXT_FN_A fInitializeSecurityContext;
	ACCEPT_SECURITY_CONTEXT_FN fAcceptSecurityContext;

	bool checkAdminPrivilege(PCtxtHandle phContext) const;
	bool initEntries();

public:
	typedef Firebird::Array<unsigned char> DataHolder;

	AuthSspi();
	~AuthSspi();

	// true when has non-empty security context,
	// ready to be sent to the other side
	bool isActive() const
	{
		return hasContext;
	}

	// prepare security context to be sent to the server (used by client)
	bool request(DataHolder& data);

	// accept security context from the client (used by server)
	bool accept(DataHolder& data);

	// returns Windows user name, matching accepted security context
	bool getLogin(Firebird::string& login, bool& wh);
};

class WinSspiServer : public ServerPlugin
{
public:
	ServerInstance* instance();
    void getName(unsigned char** data, unsigned short* dataSize);
    void release();
};

class WinSspiClient : public ClientPlugin
{
public:
	ClientInstance* instance();
    void getName(unsigned char** data, unsigned short* dataSize);
    void release();
};

class WinSspiServerInstance : public ServerInstance
{
public:
    Result startAuthentication(bool isService, const char* dbName,
                               const unsigned char* dpb, unsigned int dpbSize,
                               WriterInterface* writerInterface);
    Result contAuthentication(WriterInterface* writerInterface,
                              const unsigned char* data, unsigned int size);
    void getData(unsigned char** data, unsigned short* dataSize);
    void release();

	WinSspiServerInstance();

private:
	AuthSspi::DataHolder sspiData;
	AuthSspi sspi;
};

class WinSspiClientInstance : public ClientInstance
{
public:
	Result startAuthentication(bool isService, const char* dbName, DpbInterface* dpb);
	Result contAuthentication(const unsigned char* data, unsigned int size);
    void getData(unsigned char** data, unsigned short* dataSize);
    void release();

	WinSspiClientInstance();

private:
	AuthSspi::DataHolder sspiData;
	AuthSspi sspi;
};

} // namespace Auth

#endif // TRUSTED_AUTH
#endif // AUTH_SSPI_H
