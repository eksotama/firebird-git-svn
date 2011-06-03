/*
 *	PROGRAM:	Firebird Trace Services
 *	MODULE:		TraceLog.cpp
 *	DESCRIPTION:	Trace API shared log file
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

#include "firebird.h"
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_IO_H
#include <io.h>
#endif
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "../../common/StatusArg.h"
#include "../../common/classes/TempFile.h"
#include "../../common/common.h"
#include "../../common/isc_proto.h"
#include "../../common/isc_s_proto.h"
#include "../../common/os/path_utils.h"
#include "../../common/os/os_utils.h"
#include "../../jrd/trace/TraceLog.h"

using namespace Firebird;

namespace Jrd {

const off_t MAX_LOG_FILE_SIZE = 1024 * 1024;
const unsigned int MAX_FILE_NUM = (unsigned int) -1;

TraceLog::TraceLog(MemoryPool& pool, const PathName& fileName, bool reader) :
	m_baseFileName(pool)
{
	m_fileNum = 0;
	m_fileHandle = -1;
	m_reader = reader;

	Arg::StatusVector statusVector;
	if (!mapFile(statusVector, fileName.c_str(), sizeof(TraceLogHeader)))
	{
		iscLogStatus("TraceLog: cannot initialize the shared memory region", statusVector.value());
		statusVector.raise();
	}

	char dir[MAXPATHLEN];
	gds__prefix_lock(dir, "");
	PathUtils::concatPath(m_baseFileName, dir, fileName);

	TraceLogGuard guard(this);
	if (m_reader)
		m_fileNum = 0;
	else
		m_fileNum = sh_mem_header->writeFileNum;

	m_fileHandle = openFile(m_fileNum);
}

TraceLog::~TraceLog()
{
	::close(m_fileHandle);

	if (m_reader)
	{
		// indicate reader is gone
		sh_mem_header->readFileNum = MAX_FILE_NUM;

		for (; m_fileNum <= sh_mem_header->writeFileNum; m_fileNum++)
			removeFile(m_fileNum);
	}
	else if (m_fileNum < sh_mem_header->readFileNum) {
		removeFile(m_fileNum);
	}

	const bool readerDone = (sh_mem_header->readFileNum == MAX_FILE_NUM);

	Arg::StatusVector statusVector;
	unmapFile(statusVector);

	if (m_reader || readerDone) {
		unlink(m_baseFileName.c_str());
	}
}

int TraceLog::openFile(int fileNum)
{
	PathName fileName;
	fileName.printf("%s.%07ld", m_baseFileName.c_str(), fileNum);

	int file = os_utils::openCreateSharedFile(fileName.c_str(),
#ifdef WIN_NT
		O_BINARY | O_SEQUENTIAL | _O_SHORT_LIVED);
#else
		0);
#endif

	return file;
}

int TraceLog::removeFile(int fileNum)
{
	PathName fileName;
	fileName.printf("%s.%07ld", m_baseFileName.c_str(), fileNum);
	return unlink(fileName.c_str());
}

size_t TraceLog::read(void* buf, size_t size)
{
	fb_assert(m_reader);

	char* p = (char*) buf;
	unsigned int readLeft = size;
	while (readLeft)
	{
		const int reads = ::read(m_fileHandle, p, readLeft);

		if (reads == 0)
		{
			// EOF reached, check the reason
			const off_t len = lseek(m_fileHandle, 0, SEEK_CUR);
			if (len >= MAX_LOG_FILE_SIZE)
			{
				// this file was read completely, go to next one
				::close(m_fileHandle);
				removeFile(m_fileNum);

				fb_assert(sh_mem_header->readFileNum == m_fileNum);
				m_fileNum = ++sh_mem_header->readFileNum;
				m_fileHandle = openFile(m_fileNum);
			}
			else
			{
				// nothing to read, return what we have
				break;
			}
		}
		else if (reads > 0)
		{
			p += reads;
			readLeft -= reads;
		}
		else
		{
			// io error
			system_call_failed::raise("read", errno);
			break;
		}
	}

	return (size - readLeft);
}

size_t TraceLog::write(const void* buf, size_t size)
{
	fb_assert(!m_reader);

	// if reader already gone, don't write anything
	if (sh_mem_header->readFileNum == MAX_FILE_NUM)
		return size;

	TraceLogGuard guard(this);

	const char* p = (const char*) buf;
	unsigned int writeLeft = size;
	while (writeLeft)
	{
		const long len = lseek(m_fileHandle, 0, SEEK_END);
		const unsigned int toWrite = MIN(writeLeft, MAX_LOG_FILE_SIZE - len);
		if (!toWrite)
		{
			// While this instance of writer was idle, new log file was created.
			// More, if current file was already read by reader, we must delete it.
			::close(m_fileHandle);
			if (m_fileNum < sh_mem_header->readFileNum)
			{
				removeFile(m_fileNum);
			}
			if (m_fileNum == sh_mem_header->writeFileNum)
			{
				++sh_mem_header->writeFileNum;
			}
			m_fileNum = sh_mem_header->writeFileNum;
			m_fileHandle = openFile(m_fileNum);
			continue;
		}

		const int written = ::write(m_fileHandle, p, toWrite);
		if (written == -1 || size_t(written) != toWrite)
			system_call_failed::raise("write", errno);

		p += toWrite;
		writeLeft -= toWrite;
		if (writeLeft || (len + toWrite == MAX_LOG_FILE_SIZE))
		{
			::close(m_fileHandle);
			m_fileNum = ++sh_mem_header->writeFileNum;
			m_fileHandle = openFile(m_fileNum);
		}
	}

	return size - writeLeft;
}

size_t TraceLog::getApproxLogSize() const
{
	return (sh_mem_header->writeFileNum - sh_mem_header->readFileNum + 1) *
			(MAX_LOG_FILE_SIZE / (1024 * 1024));
}

void TraceLog::mutexBug(int state, const char* string)
{
	TEXT msg[BUFFER_TINY];

	sprintf(msg, "TraceLog: mutex %s error, status = %d", string, state);
	gds__log(msg);

	fprintf(stderr, "%s\n", msg);
	exit(FINI_ERROR);
}

bool TraceLog::initialize(bool initialize)
{
	if (initialize)
	{
		sh_mem_header->mhb_type = SRAM_TRACE_LOG;
		sh_mem_header->mhb_version = 1;
		sh_mem_header->readFileNum = 0;
		sh_mem_header->writeFileNum = 0;
	}
	else
	{
		fb_assert(sh_mem_header->mhb_type == SRAM_TRACE_LOG);
		fb_assert(sh_mem_header->mhb_version == 1);
	}

	return true;
}

void TraceLog::lock()
{
	mutexLock();
}

void TraceLog::unlock()
{
	mutexUnlock();
}

} // namespace Jrd
