/*
 *	PROGRAM:	JRD International support
 *	MODULE:		IntlUtil.h
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

#ifndef JRD_INTLUTIL_H
#define JRD_INTLUTIL_H

#include "../common/classes/array.h"
#include "../common/classes/GenericMap.h"
#include "../common/classes/fb_string.h"
#include "../jrd/intlobj_new.h"

namespace Jrd
{
	class CharSet;
}

namespace Firebird {

class IntlUtil
{
public:
	typedef Pair<Full<string, string> > SpecificAttribute;
	typedef GenericMap<SpecificAttribute> SpecificAttributesMap;

public:
	static string generateSpecificAttributes(
		Jrd::CharSet* cs, SpecificAttributesMap& map);
	static bool parseSpecificAttributes(
		Jrd::CharSet* cs, ULONG len, const UCHAR* s, SpecificAttributesMap* map);

	static string convertAsciiToUtf16(const string& ascii);
	static string convertUtf16ToAscii(const string& utf16, bool* error);

	static bool initUnicodeCollation(texttype* tt, charset* cs, const ASCII* name,
		USHORT attributes, const UCharBuffer& specificAttributes);

private:
	static string escapeAttribute(Jrd::CharSet* cs, const string& s);
	static string unescapeAttribute(Jrd::CharSet* cs, const string& s);

	static bool isAttributeEscape(Jrd::CharSet* cs, const UCHAR* s, ULONG size);

	static bool readAttributeChar(Jrd::CharSet* cs, const UCHAR** s, const UCHAR* end, ULONG* size, bool returnEscape);
	static bool readOneChar(Jrd::CharSet* cs, const UCHAR** s, const UCHAR* end, ULONG* size);
};

}	// namespace Firebird

#endif	// JRD_INTLUTIL_H
