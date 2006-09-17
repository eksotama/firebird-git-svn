/*
 *	PROGRAM:	InterBase International support
 *	MODULE:		lc_iso8859_13.c
 *	DESCRIPTION:	Language Drivers in the iso8859_13 family.
 *			(full International collation)
 *
 * The contents of this file are subject to the Interbase Public
 * License Version 1.0 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy
 * of the License at http://www.Inprise.com/IPL.html
 *
 * Software distributed under the License is distributed on an
 * "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express
 * or implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * All Rights Reserved.
 * Contributor(s): Jonas Jasas
 */

#include "firebird.h"
#include "../intl/ldcommon.h"
#include "ld_proto.h"
#include "lc_narrow.h"


static ULONG fam2_str_to_upper(texttype* obj, ULONG iLen, const BYTE* pStr, ULONG iOutLen, BYTE *pOutStr);
static ULONG fam2_str_to_lower(texttype* obj, ULONG iLen, const BYTE* pStr, ULONG iOutLen, BYTE *pOutStr);


static inline bool FAMILY2(texttype* cache,
							SSHORT country,
							USHORT flags,
							const SortOrderTblEntry* NoCaseOrderTbl,
							const BYTE* ToUpperConversionTbl,
							const BYTE* ToLowerConversionTbl,
							const CompressPair* CompressTbl,
							const ExpandChar* ExpansionTbl,
							const ASCII* POSIX,
							USHORT attributes,
							const UCHAR* specific_attributes,
							ULONG specific_attributes_length)
{
	if ((attributes & ~TEXTTYPE_ATTR_PAD_SPACE) || specific_attributes_length)
		return false;

	cache->texttype_version			= TEXTTYPE_VERSION_1;
	cache->texttype_name			= POSIX;
	cache->texttype_country			= country;
	cache->texttype_pad_option		= (attributes & TEXTTYPE_ATTR_PAD_SPACE) ? true : false;
	cache->texttype_fn_key_length	= LC_NARROW_key_length;
	cache->texttype_fn_string_to_key= LC_NARROW_string_to_key;
	cache->texttype_fn_compare		= LC_NARROW_compare;
	cache->texttype_fn_str_to_upper = fam2_str_to_upper;
	cache->texttype_fn_str_to_lower = fam2_str_to_lower;
	cache->texttype_fn_destroy		= LC_NARROW_destroy;
	cache->texttype_impl			= new TextTypeImpl;
	cache->texttype_impl->texttype_collation_table	= (const BYTE*) NoCaseOrderTbl;
	cache->texttype_impl->texttype_toupper_table	= ToUpperConversionTbl;
	cache->texttype_impl->texttype_tolower_table	= ToLowerConversionTbl;
	cache->texttype_impl->texttype_compress_table	= (const BYTE*) CompressTbl;
	cache->texttype_impl->texttype_expand_table		= (const BYTE*) ExpansionTbl;
	cache->texttype_impl->texttype_flags			= ((flags) & REVERSE) ? TEXTTYPE_reverse_secondary : 0;
	cache->texttype_impl->texttype_bytes_per_key	= 0;

	return true;
}


TEXTTYPE_ENTRY (ISO885913_c1_init)
{
	static const ASCII	POSIX[] = "lt_LT.ISO8859_13";

#include "../intl/collations/xx885913lt.h"

	return FAMILY2(cache, CC_LITHUANIA, LDRV_TIEBREAK,
			NoCaseOrderTbl, ToUpperConversionTbl, ToLowerConversionTbl,
			CompressTbl, ExpansionTbl, POSIX, attributes, specific_attributes, specific_attributes_length);
}


/*
 * Generic base for InterBase 4.0 Language Driver
 */

#define	LOCALE_UPPER(ch)	(obj->texttype_impl->texttype_toupper_table[ (unsigned) (ch) ])
#define	LOCALE_LOWER(ch)	(obj->texttype_impl->texttype_tolower_table[ (unsigned) (ch) ])




/*
 *	Returns INTL_BAD_STR_LENGTH if output buffer was too small
 */
static ULONG fam2_str_to_upper(texttype* obj, ULONG iLen, const BYTE* pStr, ULONG iOutLen, BYTE *pOutStr)
{
	fb_assert(pStr != NULL);
	fb_assert(pOutStr != NULL);
	fb_assert(iOutLen >= iLen);
	const BYTE* const p = pOutStr;
	while (iLen && iOutLen) {
		*pOutStr++ = LOCALE_UPPER(*pStr);
		pStr++;
		iLen--;
		iOutLen--;
	}
	if (iLen != 0)
		return (INTL_BAD_STR_LENGTH);
	return (pOutStr - p);
}


/*
 *	Returns INTL_BAD_STR_LENGTH if output buffer was too small
 */
static ULONG fam2_str_to_lower(texttype* obj, ULONG iLen, const BYTE* pStr, ULONG iOutLen, BYTE *pOutStr)
{
	fb_assert(pStr != NULL);
	fb_assert(pOutStr != NULL);
	fb_assert(iOutLen >= iLen);
	const BYTE* const p = pOutStr;
	while (iLen && iOutLen) {
		*pOutStr++ = LOCALE_LOWER(*pStr);
		pStr++;
		iLen--;
		iOutLen--;
	}
	if (iLen != 0)
		return (INTL_BAD_STR_LENGTH);
	return (pOutStr - p);
}
