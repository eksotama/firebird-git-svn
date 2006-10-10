/*
 *	PROGRAM:	JRD International support
 *	MODULE:		IntlUtil.cpp
 *	DESCRIPTION:	INTL Utility functions
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
 *  The Original Code was created by Adriano dos Santos Fernandes
 *  for the Firebird Open Source RDBMS project.
 *
 *  Copyright (c) 2006 Adriano dos Santos Fernandes <adrianosf@uol.com.br>
 *  and all contributors signed below.
 *
 *  All Rights Reserved.
 *  Contributor(s): ______________________________________.
 */

#include "firebird.h"
#include "../jrd/IntlUtil.h"
#include "../jrd/unicode_util.h"
#include "../jrd/intl_classes.h"
#include "../intl/country_codes.h"


using Jrd::UnicodeUtil;


namespace
{
	struct TextTypeImpl
	{
		TextTypeImpl(charset* a_cs, UnicodeUtil::Utf16Collation* a_collation)
			: cs(a_cs),
			  collation(a_collation)
		{
		}

		~TextTypeImpl()
		{
			if (cs->charset_fn_destroy)
				cs->charset_fn_destroy(cs);

			delete cs;
			delete collation;
		}

		charset* cs;
		UnicodeUtil::Utf16Collation* collation;
	};
}


namespace Firebird {


static void unicodeDestroy(texttype* tt);
static USHORT unicodeKeyLength(texttype* tt, USHORT len);
static USHORT unicodeStrToKey(texttype* tt, USHORT srcLen, const UCHAR* src,
	USHORT dstLen, UCHAR* dst, USHORT keyType);
static SSHORT unicodeCompare(texttype* tt, ULONG len1, const UCHAR* str1,
	ULONG len2, const UCHAR* str2, INTL_BOOL* errorFlag);
static ULONG unicodeCanonical(texttype* tt, ULONG srcLen, const UCHAR* src,
	ULONG dstLen, UCHAR* dst);


string IntlUtil::generateSpecificAttributes(
	Jrd::CharSet* cs, SpecificAttributesMap& map)
{
	bool found = map.getFirst();
	string s;

	while (found)
	{
		UCHAR c[sizeof(ULONG)];
		ULONG size;

		SpecificAttribute* attribute = map.current();

		s += escapeAttribute(cs, attribute->first);

		const USHORT equalChar = '=';

		size = cs->getConvFromUnicode().convert(
			sizeof(equalChar), (const UCHAR*)&equalChar,
			sizeof(c), c);

		s += string((const char*)&c, size);

		s += escapeAttribute(cs, attribute->second);

		found = map.getNext();

		if (found)
		{
			const USHORT semiColonChar = ';';
			size = cs->getConvFromUnicode().convert(
				sizeof(semiColonChar), (const UCHAR*)&semiColonChar, sizeof(c), c);

			s += string((const char*)&c, size);
		}
	}

	return s;
}


bool IntlUtil::parseSpecificAttributes(
	Jrd::CharSet* cs, ULONG len, const UCHAR* s, SpecificAttributesMap* map)
{
	// Note that the map isn't cleared.
	// Old attributes will be combined with the new ones.

	const UCHAR* p = s;
	const UCHAR* const end = s + len;
	ULONG size = 0;

	readAttributeChar(cs, &p, end, &size, true);

	while (p < end)
	{
		while (p < end && size == cs->getSpaceLength() &&
			   memcmp(p, cs->getSpace(), cs->getSpaceLength()) == 0)
		{
			if (!readAttributeChar(cs, &p, end, &size, true))
				return true;
		}

		const UCHAR* start = p;

		UCHAR uc[sizeof(ULONG)];
		ULONG uSize;

		while (p < end)
		{
			uSize = cs->getConvToUnicode().convert(size, p, sizeof(uc), uc);
				
			if (uSize == 2 &&
					 ((*(USHORT*)uc >= 'A' && *(USHORT*)uc <= 'Z') ||
					  (*(USHORT*)uc >= 'a' && *(USHORT*)uc <= 'z') ||
					  *(USHORT*)uc == '-' || *(USHORT*)uc == '_'))
			{
				if (!readAttributeChar(cs, &p, end, &size, true))
					return false;
			}
			else
				break;
		}

		if (p - start == 0)
			return false;

		string name = string((const char*)start, p - start);
		name = unescapeAttribute(cs, name);

		while (p < end && size == cs->getSpaceLength() &&
			   memcmp(p, cs->getSpace(), cs->getSpaceLength()) == 0)
		{
			if (!readAttributeChar(cs, &p, end, &size, true))
				return false;
		}

		uSize = cs->getConvToUnicode().convert(size, p, sizeof(uc), uc);

		if (uSize != 2 || *(USHORT*)uc != '=')
			return false;

		string value;

		if (readAttributeChar(cs, &p, end, &size, true))
		{
			while (p < end && size == cs->getSpaceLength() &&
				   memcmp(p, cs->getSpace(), cs->getSpaceLength()) == 0)
			{
				if (!readAttributeChar(cs, &p, end, &size, true))
					return false;
			}

			const UCHAR* endNoSpace = start = p;

			while (p < end)
			{
				uSize = cs->getConvToUnicode().convert(size, p, sizeof(uc), uc);

				if (uSize != 2 || *(USHORT*)uc != ';')
				{
					if (!(size == cs->getSpaceLength() &&
						  memcmp(p, cs->getSpace(), cs->getSpaceLength()) == 0))
					{
						endNoSpace = p + size;
					}

					if (!readAttributeChar(cs, &p, end, &size, true))
						break;
				}
				else
					break;
			}

			value = unescapeAttribute(cs,
				string((const char*)start, endNoSpace - start));

			if (p < end)
				readAttributeChar(cs, &p, end, &size, true);	// skip the semicolon
		}

		if (value.isEmpty())
			map->remove(name);
		else
			map->put(name, value);
	}

	return true;
}


string IntlUtil::convertAsciiToUtf16(const string& ascii)
{
	string s;
	const char* end = ascii.c_str() + ascii.length();

	for (const char* p = ascii.c_str(); p < end; ++p)
	{
		USHORT c = *(UCHAR*) p;
		s.append((char*) &c, sizeof(c));
	}

	return s;
}


string IntlUtil::convertUtf16ToAscii(const string& utf16, bool* error)
{
	fb_assert(utf16.length() % sizeof(USHORT) == 0);

	string s;
	const USHORT* end = (const USHORT*) (utf16.c_str() + utf16.length());

	for (const USHORT* p = (const USHORT*) utf16.c_str(); p < end; ++p)
	{
		if (*p <= 0xFF)
			s.append((UCHAR) *p);
		else
		{
			*error = true;
			return "";
		}
	}

	*error = false;

	return s;
}


bool IntlUtil::initUnicodeCollation(texttype* tt, charset* cs, const ASCII* name,
	USHORT attributes, const UCharBuffer& specificAttributes)
{
	// name comes from stack. Copy it.
	ASCII* nameCopy = new ASCII[strlen(name) + 1];
	strcpy(nameCopy, name);
	tt->texttype_name = nameCopy;

	tt->texttype_version = TEXTTYPE_VERSION_1;
	tt->texttype_country = CC_INTL;
	tt->texttype_fn_destroy = unicodeDestroy;
	tt->texttype_fn_compare = unicodeCompare;
	tt->texttype_fn_key_length = unicodeKeyLength;
	tt->texttype_fn_string_to_key = unicodeStrToKey;

	IntlUtil::SpecificAttributesMap map;

	Jrd::CharSet* charSet = NULL;

	try
	{
		charSet = Jrd::CharSet::createInstance(*getDefaultMemoryPool(), 0, cs);
		IntlUtil::parseSpecificAttributes(charSet, specificAttributes.getCount(),
			specificAttributes.begin(), &map);
		delete charSet;
	}
	catch (...)
	{
		delete charSet;
		return false;
	}

	IntlUtil::SpecificAttributesMap map16;

	bool found = map.getFirst();

	while (found)
	{
		UCharBuffer s1, s2;
		USHORT errCode;
		ULONG errPosition;

		s1.resize(cs->charset_to_unicode.csconvert_fn_convert(
			&cs->charset_to_unicode, map.current()->first.length(), NULL, 0, NULL, &errCode, &errPosition));
		s1.resize(cs->charset_to_unicode.csconvert_fn_convert(
			&cs->charset_to_unicode, map.current()->first.length(), (UCHAR*) map.current()->first.c_str(),
			s1.getCapacity(), s1.begin(), &errCode, &errPosition));

		s2.resize(cs->charset_to_unicode.csconvert_fn_convert(
			&cs->charset_to_unicode, map.current()->second.length(), NULL, 0, NULL, &errCode, &errPosition));
		s2.resize(cs->charset_to_unicode.csconvert_fn_convert(
			&cs->charset_to_unicode, map.current()->second.length(), (UCHAR*) map.current()->second.c_str(),
			s2.getCapacity(), s2.begin(), &errCode, &errPosition));

		map16.put(string((char*) s1.begin(), s1.getCount()), string((char*) s2.begin(), s2.getCount()));

		found = map.getNext();
	}

	UnicodeUtil::Utf16Collation* collation =
		UnicodeUtil::Utf16Collation::create(tt, attributes, map16);

	if (!collation)
		return false;

	tt->texttype_impl = new TextTypeImpl(cs, collation);

	if (tt->texttype_canonical_width != 0)
		tt->texttype_fn_canonical = unicodeCanonical;

	return true;
}


string IntlUtil::escapeAttribute(Jrd::CharSet* cs, const string& s)
{
	string ret;
	const UCHAR* p = (const UCHAR*)s.begin();
	const UCHAR* end = (const UCHAR*)s.end();
	ULONG size = 0;

	while (readOneChar(cs, &p, end, &size))
	{
		UCHAR uc[sizeof(ULONG)];

		ULONG uSize = cs->getConvToUnicode().convert(size, p, sizeof(uc), uc);

		if (uSize == 2)
		{
			if (*(USHORT*)uc == '\\' || *(USHORT*)uc == '=' || *(USHORT*)uc == ';')
			{
				*(USHORT*)uc = '\\';
				UCHAR bytes[sizeof(ULONG)];
				
				ULONG bytesSize = cs->getConvFromUnicode().convert(
					sizeof(USHORT), uc, sizeof(bytes), bytes);

				ret.append(string((const char*)bytes, bytesSize));
			}
		}

		ret.append(string((const char*)p, size));
	}

	return ret;
}


string IntlUtil::unescapeAttribute(Jrd::CharSet* cs, const string& s)
{
	string ret;
	const UCHAR* p = (const UCHAR*)s.begin();
	const UCHAR* end = (const UCHAR*)s.end();
	ULONG size = 0;

	while (readAttributeChar(cs, &p, end, &size, false))
		ret.append(string((const char*)p, size));

	return ret;
}


bool IntlUtil::isAttributeEscape(Jrd::CharSet* cs, const UCHAR* s, ULONG size)
{
	UCHAR uc[sizeof(ULONG)];
	ULONG uSize = cs->getConvToUnicode().convert(size, s, sizeof(uc), uc);

	if (uSize == 2 && *(USHORT*)uc == '\\')
		return true;

	return false;
}


bool IntlUtil::readAttributeChar(Jrd::CharSet* cs, const UCHAR** s, const UCHAR* end, ULONG* size, bool returnEscape)
{
	if (readOneChar(cs, s, end, size))
	{
		if (isAttributeEscape(cs, *s, *size))
		{
			const UCHAR* p = *s;
			ULONG firstSize = *size;

			if (readOneChar(cs, s, end, size))
			{
				if (returnEscape)
				{
					*s = p;
					*size += firstSize;
				}
			}
			else
				return false;
		}

		return true;
	}

	return false;
}


bool IntlUtil::readOneChar(Jrd::CharSet* cs, const UCHAR** s, const UCHAR* end, ULONG* size)
{
	(*s) += *size;

	if (*s >= end)
	{
		(*s) = end;
		*size = 0;
		return false;
	}

	UCHAR c[sizeof(ULONG)];
	*size = cs->substring(end - *s, *s, sizeof(c), c, 0, 1);

	return true;
}


static void unicodeDestroy(texttype* tt)
{
	delete [] const_cast<ASCII*>(tt->texttype_name);
	delete tt->texttype_impl;
}


static USHORT unicodeKeyLength(texttype* tt, USHORT len)
{
	return tt->texttype_impl->collation->keyLength(
		len / tt->texttype_impl->cs->charset_min_bytes_per_char * 4);
}


static USHORT unicodeStrToKey(texttype* tt, USHORT srcLen, const UCHAR* src,
	USHORT dstLen, UCHAR* dst, USHORT keyType)
{
	try
	{
		charset* cs = tt->texttype_impl->cs;

		HalfStaticArray<UCHAR, BUFFER_SMALL> utf16Str;
		USHORT errorCode;
		ULONG offendingPos;

		utf16Str.getBuffer(
			cs->charset_to_unicode.csconvert_fn_convert(
				&cs->charset_to_unicode,
				srcLen,
				src,
				0,
				NULL,
				&errorCode,
				&offendingPos));

		ULONG utf16Len = cs->charset_to_unicode.csconvert_fn_convert(
			&cs->charset_to_unicode,
			srcLen,
			src,
			utf16Str.getCapacity(),
			utf16Str.begin(),
			&errorCode,
			&offendingPos);

		return tt->texttype_impl->collation->stringToKey(
			utf16Len, (USHORT*)utf16Str.begin(), dstLen, dst, keyType);
	}
	catch (BadAlloc)
	{
		fb_assert(false);
		return INTL_BAD_KEY_LENGTH;
	}
}


static SSHORT unicodeCompare(texttype* tt, ULONG len1, const UCHAR* str1,
	ULONG len2, const UCHAR* str2, INTL_BOOL* errorFlag)
{
	try
	{
		*errorFlag = false;

		charset* cs = tt->texttype_impl->cs;

		HalfStaticArray<UCHAR, BUFFER_SMALL> utf16Str1;
		HalfStaticArray<UCHAR, BUFFER_SMALL> utf16Str2;
		USHORT errorCode;
		ULONG offendingPos;

		utf16Str1.getBuffer(
			cs->charset_to_unicode.csconvert_fn_convert(
				&cs->charset_to_unicode,
				len1,
				str1,
				0,
				NULL,
				&errorCode,
				&offendingPos));

		ULONG utf16Len1 = cs->charset_to_unicode.csconvert_fn_convert(
			&cs->charset_to_unicode,
			len1,
			str1,
			utf16Str1.getCapacity(),
			utf16Str1.begin(),
			&errorCode,
			&offendingPos);

		utf16Str2.getBuffer(
			cs->charset_to_unicode.csconvert_fn_convert(
				&cs->charset_to_unicode,
				len2,
				str2,
				0,
				NULL,
				&errorCode,
				&offendingPos));

		ULONG utf16Len2 = cs->charset_to_unicode.csconvert_fn_convert(
			&cs->charset_to_unicode,
			len2,
			str2,
			utf16Str2.getCapacity(),
			utf16Str2.begin(),
			&errorCode,
			&offendingPos);

		return tt->texttype_impl->collation->compare(
			utf16Len1, (USHORT*)utf16Str1.begin(),
			utf16Len2, (USHORT*)utf16Str2.begin(), errorFlag);
	}
	catch (BadAlloc)
	{
		fb_assert(false);
		return 0;
	}
}


static ULONG unicodeCanonical(texttype* tt, ULONG srcLen, const UCHAR* src, ULONG dstLen, UCHAR* dst)
{
	try
	{
		charset* cs = tt->texttype_impl->cs;

		HalfStaticArray<UCHAR, BUFFER_SMALL> utf16Str;
		USHORT errorCode;
		ULONG offendingPos;

		utf16Str.getBuffer(
			cs->charset_to_unicode.csconvert_fn_convert(
				&cs->charset_to_unicode,
				srcLen,
				src,
				0,
				NULL,
				&errorCode,
				&offendingPos));

		ULONG utf16Len = cs->charset_to_unicode.csconvert_fn_convert(
			&cs->charset_to_unicode,
			srcLen,
			src,
			utf16Str.getCapacity(),
			utf16Str.begin(),
			&errorCode,
			&offendingPos);

		return tt->texttype_impl->collation->canonical(
			utf16Len, reinterpret_cast<USHORT*>(utf16Str.begin()),
			dstLen, reinterpret_cast<ULONG*>(dst));
	}
	catch (BadAlloc)
	{
		fb_assert(false);
		return INTL_BAD_KEY_LENGTH;
	}
}


}	// namespace Firebird
