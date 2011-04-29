/*
 *	PROGRAM:		Client/Server Common Code
 *	MODULE:			fb_atomic.h
 *	DESCRIPTION:	Atomic counters
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
 *  The Original Code was created by Nickolay Samofatov
 *  for the Firebird Open Source RDBMS project.
 *
 *  Copyright (c) 2004 Nickolay Samofatov <nickolay@broadviewsoftware.com>
 *  and all contributors signed below.
 *
 *  All Rights Reserved.
 *  Contributor(s): ______________________________________.
 *
 */

#ifndef CLASSES_FB_ATOMIC_H
#define CLASSES_FB_ATOMIC_H

// Here we define an atomic type, which is to be used in interface for all OS
#if SIZEOF_VOID_P == 8
typedef SINT64 AtomicType;
#else
typedef SLONG AtomicType;
#endif

// IMPORTANT !
// Most of the interlocked functions returns "old" value of operand (except of 
// InterlockedIncrement and InterlockedDecrement on Windows) and this is correct 
// as "old" value impossible to restore from "new" value and operation parameters 
// and "new" value could be changed at time when code looks at it.
//   This (returning of original value) is not how operators such as "=", "+=", 
// "&=" etc, usually works. Therefore overloaded operators in AtomicCounter class
// are void and all of them have corresponding equivalent functions returning
// "old" value of operand. 
//   The only exceptions from this rule is unary increment and decrement (for  
// historical reasons). Use it with care ! If one need old value of just 
// incremented atomic variable he should use exchangeAdd, not operator++.

#if defined(WIN_NT)

#include <windows.h>
#include <intrin.h>

namespace Firebird {

// Win95 is not supported unless compiled conditionally and
// redirected to generic version below
class PlatformAtomicCounter
{
public:
#ifdef _WIN64
	typedef LONGLONG counter_type;
#else
	typedef LONG counter_type;
#endif

	explicit PlatformAtomicCounter(counter_type val = 0) : counter(val) {}
	~PlatformAtomicCounter() {}

	// returns old value
	counter_type exchangeAdd(counter_type val)
	{
#ifdef _WIN64
		return _InterlockedExchangeAdd64(&counter, val);
#else
		return _InterlockedExchangeAdd(&counter, val);
#endif
	}

	bool compareExchange(counter_type oldVal, counter_type newVal)
	{
#ifdef _WIN64
		return (_InterlockedCompareExchange64(&counter, newVal, oldVal) == oldVal);
#else
		return (_InterlockedCompareExchange(&counter, newVal, oldVal) == oldVal);
#endif
	}

	// returns old value
	counter_type setValue(counter_type val)
	{
#ifdef _WIN64
		return _InterlockedExchange64(&counter, val);
#else
		return _InterlockedExchange(&counter, val);
#endif
	}

protected:
#ifndef MINGW
	volatile
#endif
		counter_type counter;
};


template <typename T>
class AtomicPointer
{
public:
	explicit AtomicPointer(T* val = NULL) : pointer(val) {}
	~AtomicPointer() {}

	T* value() const { return (T*)pointer; }

	// returns old value
	T* setValue(T* val)
	{
#ifdef _WIN64
		return _InterlockedExchangePointer((volatile PVOID*)&pointer, val);
#else
		//InterlockedExchangePointer((volatile PVOID*)&pointer, val);
	    retutn _InterlockedExchange((LONG volatile*)&pointer, (LONG)val);
#endif
	}

	bool compareExchange(T* oldVal, T* newVal)
	{
#ifdef _WIN64
		return (_InterlockedCompareExchangePointer((PVOID volatile*)&pointer, newVal, oldVal) == oldVal);
#else
		//return (InterlockedCompareExchangePointer((PVOID volatile*)&pointer, newVal, oldVal) == oldVal);
		return ((PVOID)(LONG_PTR)_InterlockedCompareExchange((LONG volatile*)&pointer, (LONG)newVal, (LONG)oldVal)) == oldVal;
#endif
	}

	operator T* () const	{ return value(); }
	T* operator ->() const	{ return value(); }
	void operator =(T* val)	{ setValue(val); }

protected:
	volatile T* pointer;
};

} // namespace Firebird

#elif defined(AIX)

#if SIZEOF_VOID_P != 8
#error: Only 64-bit mode supported on AIX
#endif

#include <sys/atomic_op.h>

namespace Firebird {

// AIX version - uses AIX atomic API
class PlatformAtomicCounter
{
public:
	typedef long counter_type;

	explicit PlatformAtomicCounter(AtomicType value = 0) : counter(value) {}
	~PlatformAtomicCounter() {}

	counter_type exchangeAdd(AtomicType value)
	{
		return fetch_and_addlp(&counter, static_cast<unsigned long>(value));
	}

	counter_type value() const { return counter; }

	bool compareExchange(counter_type oldVal, counter_type newVal)
	{
		return compare_and_swaplp(&counter, &oldVal, newVal);
	}

	counter_type setValue(AtomicType val)
	{
		counter_type old;
		do
		{
			old = counter;
		} while (!compare_and_swaplp(&counter, &old, val));
		return old;
	}

protected:
	counter_type counter;
};

} // namespace Firebird

#elif defined(HPUX) && defined(HAVE_ATOMIC_H)

#include <atomic.h>

namespace Firebird {

// HPUX version - uses atomic API library
class PlatformAtomicCounter
{
public:
	typedef uint64_t counter_type;

	explicit PlatformAtomicCounter(counter_type value = 0) : counter(value) {}
	~PlatformAtomicCounter() {}

	counter_type exchangeAdd(counter_type value)
	{
		counter_type old = counter;
		do
		{
			old = counter;
			errno = 0;
		} while (atomic_cas_64(&counter, old, old + value) != old);
		return old;
	}

	bool compareExchange(counter_type oldVal, counter_type newVal)
	{
		return atomic_cas_64(&counter, oldVal, newVal) == oldVal;
	}

	counter_type setValue(counter_type val)
	{
		return atomic_swap_64(&counter, val);
	}

protected:
	volatile counter_type counter;
};

} // namespace Firebird

#elif defined(SOLARIS) && defined(HAVE_ATOMIC_H)

#include <atomic.h>

namespace Firebird {

// Solaris version - uses Solaris atomic_ops
class PlatformAtomicCounter
{
public:
	typedef ulong_t counter_type;
	typedef long delta_type;

	explicit PlatformAtomicCounter(counter_type value = 0) : counter(value) {}
	~PlatformAtomicCounter() {}

	counter_type exchangeAdd(delta_type value)
	{
		return atomic_add_long_nv(&counter, value);
	}

	counter_type setValue(delta_type value)
	{
		return atomic_swap_ulong(&counter, value);
	}

	bool compareExchange(counter_type oldVal, counter_type newVal)
	{
		TODO: implement method for Solaris
	}

protected:
	volatile counter_type counter;
};

} // namespace Firebird


#elif defined(__SUNPRO_CC) && !defined(HAVE_ATOMIC_H) && defined(__sparc)
// Solaris 9 does not have nice <atomic.h> header file, so we provide
// an assembly language, Sun Studio Pro only, implementation.

// assembly versions of fetch_and_add_il, compare_and_swap_il,
// are in fb_atomic_*.il

// No GNU CC implementation on Solaris 9 is planned!
extern "C"
{
	extern int fetch_and_add_il(volatile unsigned int* word_addr, int value);
	extern boolean_t compare_and_swap_il(volatile unsigned int* word_addr,
		unsigned int* old_val_addr, unsigned int new_val);
}

namespace Firebird {

class PlatformAtomicCounter
{
public:
	typedef uint_t counter_type;
	typedef int delta_type;

	explicit PlatformAtomicCounter(counter_type value = 0) : counter(value) {}
	~PlatformAtomicCounter() {}

	counter_type exchangeAdd(delta_type value)
	{
		return fetch_and_add_il(&counter, value);
	}

	counter_type setValue(delta_type value)
	{
		counter_type old;
		do
		{
			old = counter;
		} while (!compare_and_swap_il(&counter, &old, value));
		return old;
	}

	bool compareExchange(counter_type oldVal, counter_type newVal)
	{
		return compare_and_swap_il(&counter, &oldVal, newVal);
	}

protected:
	counter_type counter;
};

} // namespace Firebird

#elif defined(__GNUC__) && (defined(i386) || defined(I386) || defined(_M_IX86) || defined(AMD64) || defined(__x86_64__))

namespace Firebird {

// Assembler version for x86 and AMD64. Note it uses xaddl thus it requires i486
class PlatformAtomicCounter
{
public:
	typedef AtomicType counter_type;

	explicit PlatformAtomicCounter(AtomicType value = 0) : counter(value) {}
	~PlatformAtomicCounter() {}

	AtomicType exchangeAdd(AtomicType value)
	{
		register counter_type result;

		__asm __volatile ("lock; xadd %0, %1"
			: "=r" (result), "=m" (counter)
			: "0" (value), "m" (counter)
			: "cc", "memory");

		return result;
	}

	AtomicType setValue(AtomicType val)
	{
		register counter_type result;

		__asm __volatile ("lock; xchg %0, %1"
			: "=r" (result), "=m" (counter)
			: "0" (val), "m" (counter)
			: "cc", "memory");

		return result;
	}

	bool compareExchange(counter_type oldVal, counter_type newVal)
	{
		register char ret;

		__asm __volatile ("lock; cmpxchg %2, %1 ; sete %0"
			: "=r" (ret), "+m" (counter)
			: "r" (newVal), "a" (oldVal)
			: "cc", "memory");

	    return ret;
	}

protected:
	volatile counter_type counter;
};

} // namespace Firebird

#elif defined(HAVE_AO_COMPARE_AND_SWAP_FULL) && !defined(HPUX)

// Sometimes in the future it can become the best choice for all platforms.
// Currently far not CPUs/OSs/compilers are supported well.
// Therefore use it as last chance to build successfully.

// wbo 2009-10 - We tested libatomic_ops against Firebird 2.5/HPUX and the
// atomic implementation was incomplete on IA-64/HPPA

extern "C" {
#define AO_REQUIRE_CAS
#include <atomic_ops.h>
}

namespace Firebird {

class PlatformAtomicCounter
{
public:
	typedef AO_t counter_type;		// AO_t should match maximum native in type

	explicit PlatformAtomicCounter(counter_type value = 0) : counter(value) {}
	~PlatformAtomicCounter() {}

	AtomicType exchangeAdd(AtomicType value)
	{
#ifdef DEV_BUILD
		// check that AO_t size is as we expect (can't fb_assert here)
		if (sizeof(AtomicType) != sizeof(AO_t)) abort();
#endif

		counter_type old;
		do
		{
			old = counter;
		} while (!AO_compare_and_swap_full(&counter, old, old + counter_type(value)));
		return AtomicType(old);
	}

	AtomicType setValue(AtomicType val)
	{
#ifdef DEV_BUILD
		// check that AO_t size is as we expect (can't fb_assert here)
		if (sizeof(AtomicType) != sizeof(AO_t)) abort();
#endif

		counter_type old;
		do
		{
			old = counter;
		} while (!AO_compare_and_swap_full(&counter, old, counter_type(val)));
		return AtomicType(old);
	}

	bool compareExchange(counter_type oldVal, counter_type newVal)
	{
		return AO_compare_and_swap_full(&counter, oldVal, newVal);
	}

protected:
	counter_type counter;
};

} // namespace Firebird

#else

#error AtomicCounter: implement appropriate code for your platform!

#endif

// platform-independent code

namespace Firebird {

class AtomicCounter : public PlatformAtomicCounter
{
public:
	explicit AtomicCounter(counter_type value = 0) : PlatformAtomicCounter(value) {}
	~AtomicCounter() {}

	counter_type value() const { return counter; }

	// returns old value
	counter_type exchangeBitAnd(counter_type val)
	{
		while (true)
		{
			volatile counter_type oldVal = counter;

			if (compareExchange(oldVal, oldVal & val))
				return oldVal;
		}
	}

	// returns old value
	counter_type exchangeBitOr(counter_type val)
	{
		while (true)
		{
			volatile counter_type oldVal = counter;

			if (compareExchange(oldVal, oldVal | val))
				return oldVal;
		}
	}

	// returns old value
	counter_type exchangeGreater(counter_type val)
	{
		while (true)
		{
			volatile counter_type oldVal = counter;

			if (oldVal >= val)
				return oldVal;

			if (compareExchange(oldVal, val))
				return oldVal;
		}
	}

	operator counter_type () const
	{
		return value();
	}

	void operator =(counter_type val)
	{
		setValue(val);
	}

	// returns new value !
	counter_type operator ++()
	{
		return exchangeAdd(1) + 1;
	}

	// returns new value !
	counter_type operator --()
	{
		return exchangeAdd(-1) - 1;
	}

	void operator +=(counter_type val)
	{
		exchangeAdd(val);
	}

	void operator -=(counter_type val)
	{
		exchangeAdd(-val);
	}

	void operator &=(counter_type val)
	{
		exchangeBitAnd(val);
	}

	void operator |=(counter_type val)
	{
		exchangeBitOr(val);
	}

};

} // namespace Firebird

#endif // CLASSES_FB_ATOMIC_H
