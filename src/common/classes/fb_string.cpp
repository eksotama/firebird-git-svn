/*
 *	PROGRAM:	string class definition
 *	MODULE:		fb_string.cpp
 *	DESCRIPTION:	Provides almost that same functionality,
 *			that STL::basic_string<char> does, 
 *			but behaves MemoryPools friendly.
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
 *  The Original Code was created by Alexander Peshkoff
 *  for the Firebird Open Source RDBMS project.
 *
 *  Copyright (c) 2004 Alexander Peshkoff <peshkoff@mail.ru>
 *  and all contributors signed below.
 *
 *  All Rights Reserved.
 *  Contributor(s): ______________________________________.
 */

#include "firebird.h"
#include "fb_string.h"

#include <ctype.h>

#ifdef HAVE_STRCASECMP
#define STRNCASECMP strncasecmp
#else
#ifdef HAVE_STRICMP
#define STRNCASECMP strnicmp
#else
namespace {
	int StringIgnoreCaseCompare(const char* s1, const char* s2, unsigned int l) {
		while (l--) {
			const int delta = toupper(*s1++) - toupper(*s2++);
			if (delta) {
				return delta;
			}
		}
		return 0;
	}
} // namespace
#define STRNCASECMP StringIgnoreCaseCompare
#endif // HAVE_STRICMP
#endif // HAVE_STRCASECMP

namespace {
	class strBitMask {
	private:
		char m[32];
	public:
		strBitMask(Firebird::AbstractString::const_pointer s, Firebird::AbstractString::size_type l) {
			memset(m, 0, sizeof(m));
			if (l == Firebird::AbstractString::npos) {
				l = strlen(s);
			}
			Firebird::AbstractString::const_pointer end = s + l;
			while (s < end) {
				const unsigned char c = static_cast<unsigned char>(*s++);
				m[c >> 3] |= (1 << (c & 7));
			}
		}
		inline bool Contains(const char c) {
			return m[c >> 3] & (1 << (c & 7));
		}
	};
} // namespace

namespace Firebird {
	const AbstractString::size_type AbstractString::npos = (AbstractString::size_type)(-1);

	AbstractString::AbstractString(const AbstractString& v) {
		initialize(v.length());
		memcpy(stringBuffer, v.c_str(), v.length());
	}

	AbstractString::AbstractString(size_type sizeL, const_pointer dataL) {
		initialize(sizeL);
		memcpy(stringBuffer, dataL, sizeL);
	}

	AbstractString::AbstractString(const_pointer p1, size_type n1, 
				 const_pointer p2, size_type n2)
	{
		// CVC: npos must be maximum size_type value for all platforms.
		// fb_assert(npos - n1 > n2 && n1 + n2 <= max_length());
		checkLength(n1);
		checkLength(n2);
		checkLength(n1 + n2);
		initialize(n1 + n2);
		memcpy(stringBuffer, p1, n1);
		memcpy(stringBuffer + n1, p2, n2);
	}

	AbstractString::AbstractString(size_type sizeL, char_type c) {
		initialize(sizeL);
		memset(stringBuffer, c, sizeL);
	}

	void AbstractString::AdjustRange(size_type length, size_type& pos, size_type& n) {
		if (pos == npos) {
			pos = length > n ? length - n : 0;
		}
		if (pos >= length) {
			pos = length;
			n = 0;
		}
		else if (pos + n > length || n == npos) {
			n = length - pos;
		}
	}

	AbstractString::pointer AbstractString::baseAssign(size_type n) {
		reserveBuffer(n);
		stringLength = n;
		stringBuffer[stringLength] = 0;
		shrinkBuffer(); // Shrink buffer if it is unneeded anymore
		return stringBuffer;
	}

	AbstractString::pointer AbstractString::baseAppend(size_type n) {
		reserveBuffer(stringLength + n);
		stringLength += n;
		stringBuffer[stringLength] = 0; // Set null terminator inside the new buffer
		return stringBuffer + stringLength - n;
	}

	AbstractString::pointer AbstractString::baseInsert(size_type p0, size_type n) {
		if (p0 >= length()) {
			return baseAppend(n);
		}
		reserveBuffer(stringLength + n);
		memmove(stringBuffer + p0 + n, stringBuffer + p0, 
				stringLength - p0 + 1); // Do not forget to move null terminator too
		stringLength += n;
		return stringBuffer + p0;
	}

	void AbstractString::baseErase(size_type p0, size_type n) {
		AdjustRange(length(), p0, n);
		memmove(stringBuffer + p0, 
				stringBuffer + p0 + n, stringLength - (p0 + n) + 1);
		stringLength -= n;
		shrinkBuffer();
	}

	void AbstractString::reserve(size_type n) {
		// Do not allow to reserve huge buffers
		if (n > max_length())
			n = max_length();

		reserveBuffer(n);
	}

	void AbstractString::resize(size_type n, char_type c) {
		if (n == length()) {
			return;
		}
		if (n > stringLength) {
			reserveBuffer(n);
			memset(stringBuffer + stringLength, c, n - stringLength);
		}
		stringLength = n;
		stringBuffer[n] = 0;
		shrinkBuffer();
	}

	AbstractString::size_type AbstractString::rfind(const_pointer s, size_type pos) const {
		const size_type l = strlen(s);
		int lastpos = length() - l;
		if (lastpos < 0) {
			return npos;
		}
		if (pos < static_cast<size_type>(lastpos)) {
			lastpos = pos;
		}
		const_pointer start = c_str();
		const_pointer endL = &start[lastpos];
		while (endL >= start) {
			if (memcmp(endL, s, l) == 0) {
				return endL - start;
			}
			--endL;
		}
		return npos;
	}

	AbstractString::size_type AbstractString::rfind(char_type c, size_type pos) const {
		int lastpos = length() - 1;
		if (lastpos < 0) {
			return npos;
		}
		if (pos < static_cast<size_type>(lastpos)) {
			lastpos = pos;
		}
		const_pointer start = c_str();
		const_pointer endL = &start[lastpos];
		while (endL >= start) {
			if (*endL == c) {
				return endL - start;
			}
			--endL;
		}
		return npos;
	}

	AbstractString::size_type AbstractString::find_first_of(const_pointer s, size_type pos, size_type n) const {
		strBitMask sm(s, n);
		const_pointer p = &c_str()[pos];
		while (pos < length()) {
			if (sm.Contains(*p++)) {
				return pos;
			}
			++pos;
		}
		return npos;
	}

	AbstractString::size_type AbstractString::find_last_of(const_pointer s, size_type pos, size_type n) const {
		strBitMask sm(s, n);
		int lpos = length() - 1;
		if (static_cast<int>(pos) < lpos && pos != npos) {
			lpos = pos;
		}
		const_pointer p = &c_str()[lpos];
		while (lpos >= 0) {
			if (sm.Contains(*p--)) {
				return lpos;
			}
			--lpos;
		}
		return npos;
	}

	AbstractString::size_type AbstractString::find_first_not_of(const_pointer s, size_type pos, size_type n) const {
		strBitMask sm(s, n);
		const_pointer p = &c_str()[pos];
		while (pos < length()) {
			if (! sm.Contains(*p++)) {
				return pos;
			}
			++pos;
		}
		return npos;
	}

	AbstractString::size_type AbstractString::find_last_not_of(const_pointer s, size_type pos, size_type n) const {
		strBitMask sm(s, n);
		int lpos = length() - 1;
		if (static_cast<int>(pos) < lpos && pos != npos) {
			lpos = pos;
		}
		const_pointer p = &c_str()[lpos];
		while (lpos >= 0) {
			if (! sm.Contains(*p--)) {
				return lpos;
			}
			--lpos;
		}
		return npos;
	}

	bool AbstractString::LoadFromFile(FILE *file) {
		baseErase(0, length());
		if (! file)
			return false;
		bool rc = false;
		int c;
		while ((c = getc(file)) != EOF) {
			rc = true;
			if (c == '\n') {
				break;
			}
			*baseAppend(1) = c;
		}
		return rc;
	}

#ifdef WIN_NT
// The following is used instead of including huge windows.h
extern "C" {
	__declspec(dllimport) unsigned long __stdcall CharUpperBuffA
		(char* lpsz, unsigned long cchLength);
	__declspec(dllimport) unsigned long __stdcall CharLowerBuffA
		(char* lpsz, unsigned long cchLength);
}
#endif // WIN_NT

	void AbstractString::upper() {
#ifdef WIN_NT
			CharUpperBuffA(Modify(), length());
#else  // WIN_NT
		for (pointer p = Modify(); *p; p++) {
			*p = toupper(*p);
		}
#endif // WIN_NT
	}

	void AbstractString::lower() {
#ifdef WIN_NT
			CharLowerBuffA(Modify(), length());
#else  // WIN_NT
		for (pointer p = Modify(); *p; p++) {
			*p = tolower(*p);
		}
#endif // WIN_NT
	}

	void AbstractString::baseTrim(TrimType WhereTrim, const_pointer ToTrim) {
		strBitMask sm(ToTrim, strlen(ToTrim));
		const_pointer b = c_str();
		const_pointer e = &c_str()[length() - 1];
		if (WhereTrim != TrimRight) {
			while (b <= e) {
				if (! sm.Contains(*b)) {
					break;
				}
				++b;
			}
		}
		if (WhereTrim != TrimLeft) {
			while (b <= e) {
				if (! sm.Contains(*e)) {
					break;
				}
				--e;
			}
		}
		size_type NewLength = e - b + 1;

		if (NewLength == length())
			return;

		if (b != c_str())
		{
			memmove(stringBuffer, b, NewLength);
		}
		stringLength = NewLength;
		stringBuffer[NewLength] = 0;
		shrinkBuffer();
	}

	void AbstractString::printf(const char* format,...) {
		enum {tempsize = 256};
		char temp[tempsize];
		va_list params;
		va_start(params, format);
		int l = VSNPRINTF(temp, tempsize, format, params);
		if (l < 0) {
			int n = sizeof(temp);
			do {
				n *= 2;
				l = VSNPRINTF(baseAssign(n), n + 1, format, params);
				if (l > 16 * 1024) {
					const char *errLine = "String size overflow in .printf()";
					memcpy(baseAssign(strlen(errLine)), errLine, strlen(errLine));
					return;
				}
			} while (l < 0);
			resize(l);
			return;	
		}
		temp[tempsize - 1] = 0;
		if (l < tempsize) {
			memcpy(baseAssign(l), temp, l);
		}
		else {
			resize(l);
			VSNPRINTF(begin(), l + 1, format, params);
		}
		va_end(params);
	}

	int PathNameComparator::compare(AbstractString::const_pointer s1, AbstractString::const_pointer s2, AbstractString::size_type n) {
		if (CASE_SENSITIVITY)
			return memcmp(s1, s2, n);
		return STRNCASECMP(s1, s2, n);
	} 
}	// namespace Firebird
