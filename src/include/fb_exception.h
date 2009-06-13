/*
 *	PROGRAM:		Firebird exceptions classes
 *	MODULE:			fb_exception.h
 *	DESCRIPTION:	Firebird's exception classes
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
 *  The Original Code was created by Mike Nordell
 *  for the Firebird Open Source RDBMS project.
 *
 *  Copyright (c) 2001 Mike Nordell <tamlin at algonet.se>
 *  and all contributors signed below.
 *
 *  All Rights Reserved.
 *  Contributor(s): ______________________________________.
 *
 *
 */


#ifndef FB_EXCEPTION_H
#define FB_EXCEPTION_H

#include <stddef.h>
#include <string.h>
#include <new>
#include "fb_types.h"
#include "../common/StatusArg.h"

namespace Firebird {

class MemoryPool;

class StringsBuffer
{
public:
	virtual const char* alloc(const char* string, size_t& length) = 0;
	virtual ~StringsBuffer() {}

	void makePermanentVector(ISC_STATUS* perm, const ISC_STATUS* temp);
	static void makeEnginePermanentVector(ISC_STATUS* v);
};

template <size_t BUFFER_SIZE>
class CircularStringsBuffer : public StringsBuffer
{
public:
	CircularStringsBuffer() throw() { init(); }
	explicit CircularStringsBuffer(MemoryPool&) throw() { init(); }

	virtual const char* alloc(const char* string, size_t& length)
	{
		// if string is already in our buffer - return it
		// it was already saved in our buffer once
		if (string >= buffer && string < &buffer[BUFFER_SIZE])
			return string;

		// if string too long, truncate it
		if (length > BUFFER_SIZE / 4)
			length = BUFFER_SIZE / 4;

		// If there isn't any more room in the buffer, start at the beginning again
		if (buffer_ptr + length + 1 > buffer + BUFFER_SIZE)
			buffer_ptr = buffer;

		char* new_string = buffer_ptr;
		memcpy(new_string, string, length);
		new_string[length] = 0;
		buffer_ptr += length + 1;

		return new_string;
	}

private:
	void init() throw()
	{
		// This is to ensure we have zero at the end of buffer in case of buffer overflow
		memset(buffer, 0, BUFFER_SIZE);
		buffer_ptr = buffer;
	}

	char buffer[BUFFER_SIZE];
	char* buffer_ptr;
};

class Exception
{
protected:
	Exception() throw() { }
public:
	virtual ~Exception() throw() { }
	virtual ISC_STATUS stuff_exception(ISC_STATUS* const status_vector, StringsBuffer* sb = NULL) const throw() = 0;
	virtual const char* what() const throw() = 0;
};

// Used as jmpbuf to unwind when needed
class LongJump : public Exception
{
public:
	virtual ISC_STATUS stuff_exception(ISC_STATUS* const status_vector, StringsBuffer* sb = NULL) const throw();
	virtual const char* what() const throw() { return "Firebird::LongJump"; }
	static void raise();
	LongJump() throw() : Exception() { }
};

// Used in MemoryPool
class BadAlloc : public std::bad_alloc, public Exception
{
public:
	virtual ISC_STATUS stuff_exception(ISC_STATUS* const status_vector, StringsBuffer* sb = NULL) const throw();
	virtual const char* what() const throw() { return "Firebird::BadAlloc"; }
	static void raise();
	BadAlloc() throw() : std::bad_alloc(), Exception() { }
};

// Main exception class in firebird
class status_exception : public Exception
{
public:
	// This version of constructor receives status vector pointing to permanent or
	// temp strings, depending upon second parameter.
	status_exception(const ISC_STATUS *status_vector, bool permanent) throw();
	virtual ~status_exception() throw();

	virtual ISC_STATUS stuff_exception(ISC_STATUS* const status_vector, StringsBuffer* sb = NULL) const throw();
	virtual const char* what() const throw() { return "Firebird::status_exception"; }

	const ISC_STATUS* value() const throw() { return m_status_vector; }

	// Returns true if strings contained in status vector are located in magical
	// permanent circular buffer. False means that exception object owns strings
	// and is about to deallocate them in its destructor
	bool strings_permanent() const throw() { return m_strings_permanent; }

	// Takes permanent strings
	static void raise(const ISC_STATUS *status_vector);

	// Takes transient strings
	static void raise(const Arg::StatusVector& statusVector);

protected:
	// Create exception with undefined status vector, this constructor allows
	// derived classes create empty exception ...
	status_exception() throw();
	// .. and adjust it later using somehow created status vector.
	void set_status(const ISC_STATUS *new_vector, bool permanent) throw();

private:
	ISC_STATUS_ARRAY m_status_vector;
	bool m_strings_permanent;
	void release_vector() throw();
};

// use this class if exception can be handled
class system_error : public status_exception
{
private:
	int errorCode;
public:
	system_error(const char* syscall, int error_code);

	static void raise(const char* syscall, int error_code);
	static void raise(const char* syscall);

	int getErrorCode() const
	{
		return errorCode;
	}

	static int getSystemError();
};

// use this class if exception can't be handled
// it will call abort() in DEV_BUILD to create core dump
class system_call_failed : public system_error
{
public:
	system_call_failed(const char* syscall, int error_code);

	static void raise(const char* syscall, int error_code);
	static void raise(const char* syscall);
};

// Moved what() here due to gpre. Didn't want to use macros for gpre_static.
class fatal_exception : public status_exception
{
public:
	explicit fatal_exception(const char* message);
	static void raiseFmt(const char* format, ...);
	// Keep in sync with the constructor above, please; "message" becomes 4th element
	// after initialization of status vector in constructor.
	const char* what() const throw()
	{
		return reinterpret_cast<const char*>(value()[3]);
	}
	static void raise(const char* message);
};


// Serialize exception into status_vector, put transient strings from exception into given StringsBuffer
ISC_STATUS stuff_exception(ISC_STATUS *status_vector, const Firebird::Exception& ex, StringsBuffer* sb = NULL) throw();

}	// namespace Firebird


#endif	// FB_EXCEPTION_H

