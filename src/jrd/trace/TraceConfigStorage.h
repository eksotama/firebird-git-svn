/*
 *	PROGRAM:	Firebird Trace Services
 *	MODULE:		TraceConfigStorage.h
 *	DESCRIPTION:	Trace API shared configurations storage
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
 *  The Original Code was created by Khorsun Vladyslav
 *  for the Firebird Open Source RDBMS project.
 *
 *  Copyright (c) 2008 Khorsun Vladyslav <hvlad@users.sourceforge.net>
 *  and all contributors signed below.
 *
 *  All Rights Reserved.
 *  Contributor(s): ______________________________________.
 *
 */

#ifndef JRD_TRACECONFIGSTORAGE_H
#define JRD_TRACECONFIGSTORAGE_H

#include <io.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <share.h>

#include <stdio.h>

#include "../../common/classes/array.h"
#include "../../common/classes/fb_string.h"
#include "../../common/classes/init.h"
#include "../../jrd/isc.h"
#include "../../jrd/trace/TraceSession.h"

namespace Jrd {


class ConfigStorage
{
public:
	ConfigStorage();
	~ConfigStorage();

	void addSession(Firebird::TraceSession &session);
	bool getNextSession(Firebird::TraceSession &session);
	void removeSession(ULONG id);
	void restart();
	void updateSession(Firebird::TraceSession &session);

	ULONG getChangeNumber() const
	{ return m_base ? m_base->change_number : 0; }

	void acquire();
	void release();

private:
	static void checkMutex(const TEXT*, int);
	static void initShMem(void*, SH_MEM_T*, bool);

	void checkFile();

	void checkDirty()
	{
		if (m_dirty) 
		{
			//_commit(m_cfg_file);
			m_dirty = false;
		}
	}

	void setDirty()
	{
		if (!m_dirty) 
		{
			m_base->change_number++;
			m_dirty = true;
		}
	}

	struct ShMemHeader
	{
		ULONG version;
		volatile ULONG change_number;
		volatile ULONG session_number;
		char  cfg_file_name[MAXPATHLEN];
#ifndef WIN_NT
		struct mtx mutex;
#endif
	};

	// items in every session record at sessions file
	enum ITEM
	{
		tagID = 1,			// session ID
		tagName,			// session Name
		tagUserName,		// creator user name
		tagFlags,			// tracked by every attachment
		tagConfig,			// configuration
		tagStartTS,			// date+time when started
		tagLogFile,			// log file name
		tagEnd
	};

	void putItem(ITEM tag, size_t len, const void* data);
	bool getItemLength(ITEM &tag, size_t &len);

	SH_MEM_T m_handle;
	ShMemHeader *m_base;
#ifdef WIN_NT
	struct mtx m_mutex;
#endif
	int  m_cfg_file;
	bool m_dirty;
};


class StorageInstance : private Firebird::InstanceControl
{
	Firebird::Mutex initMtx;
	ConfigStorage *storage;

	virtual void dtor()
	{
		delete storage;
		storage = 0;
	}

public:
	StorageInstance() :
	  Firebird::InstanceControl(Firebird::InstanceControl::PRIORITY_REGULAR),
	  storage(NULL)
	{}

	ConfigStorage *getStorage()
	{
		if (!storage)
		{
			Firebird::MutexLockGuard guard(initMtx);
			if (!storage)
				storage = FB_NEW(*getDefaultMemoryPool()) ConfigStorage;
		}
		return storage;
	}
};



class StorageGuard
{
public:
	StorageGuard(ConfigStorage* storage) :
		m_storage(storage)
	{
		m_storage->acquire();
	}

	~StorageGuard()
	{
		m_storage->release();
	}
private:
	ConfigStorage *m_storage;
};

}

#endif
