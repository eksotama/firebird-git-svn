/*
 *	PROGRAM:	InterBase International support
 *	MODULE:		lc_jis.c
 *	DESCRIPTION:	Language Drivers in the JIS family.
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
 * The Original Code was created by Inprise Corporation
 * and its predecessors. Portions created by Inprise Corporation are
 * Copyright (C) Inprise Corporation.
 *
 * All Rights Reserved.
 * Contributor(s): ______________________________________.
 */

#include "firebird.h"
#include "../intl/ldcommon.h"
#include "../intl/kanji.h"

#include "lc_ascii.h"
#include "cv_jis.h"
#include "ld_proto.h"

static USHORT sjis_to_upper(TEXTTYPE obj, UCS2_CHAR ch);
static USHORT sjis_to_lower(TEXTTYPE obj, UCS2_CHAR ch);
static SSHORT sjis_str_to_upper(TEXTTYPE obj, USHORT iLen, BYTE *pStr, USHORT iOutLen, BYTE *pOutStr);

static inline void FAMILY_MULTIBYTE(TEXTTYPE cache,
									TTYPE_ID id_number,
									pfn_INTL_init name,
									CHARSET_ID charset,
									SSHORT country,
									const ASCII *POSIX)
//#define FAMILY_MULTIBYTE(id_number, name, charset, country)
{
	cache->texttype_version			= IB_LANGDRV_VERSION;
	cache->texttype_type			= id_number;
	cache->texttype_character_set	= charset;
	cache->texttype_country			= country;
	cache->texttype_bytes_per_char	= 2;
	cache->texttype_fn_init			= (FPTR_SHORT) name;
	cache->texttype_fn_key_length	= (FPTR_SHORT) famasc_key_length;
	cache->texttype_fn_string_to_key= (FPTR_SHORT) famasc_string_to_key;
	cache->texttype_fn_compare		= (FPTR_short) famasc_compare;
	cache->texttype_collation_table = NULL;
	cache->texttype_toupper_table	= NULL;
	cache->texttype_tolower_table	= NULL;
	cache->texttype_compress_table	= NULL;
	cache->texttype_expand_table	= NULL;
	cache->texttype_name			= POSIX;
}


TEXTTYPE_ENTRY(JIS220_init)
{
	static const ASCII POSIX[] = "C.SJIS";

	FAMILY_MULTIBYTE(cache, 220, JIS220_init, CS_SJIS, CC_C, POSIX);
	cache->texttype_fn_to_wc = (FPTR_SHORT) CVJIS_sjis_byte2short;
	cache->texttype_fn_mbtowc = (FPTR_short) CVJIS_sjis_mbtowc;

	cache->texttype_fn_to_upper = (FPTR_SHORT) sjis_to_upper;
	cache->texttype_fn_to_lower = (FPTR_SHORT) sjis_to_lower;
	cache->texttype_fn_str_to_upper = (FPTR_short) sjis_str_to_upper;

	TEXTTYPE_RETURN;
}


TEXTTYPE_ENTRY(JIS230_init)
{
	static const ASCII POSIX[] = "C.EUC_J";

	FAMILY_MULTIBYTE(cache, 230, JIS230_init, CS_EUCJ, CC_C, POSIX);
	cache->texttype_fn_to_wc = (FPTR_SHORT) CVJIS_euc_byte2short;
	cache->texttype_fn_mbtowc = (FPTR_short) CVJIS_euc_mbtowc;

	cache->texttype_fn_to_upper = (FPTR_SHORT) famasc_to_upper;
	cache->texttype_fn_to_lower = (FPTR_SHORT) famasc_to_lower;
	cache->texttype_fn_str_to_upper = (FPTR_short) famasc_str_to_upper;

	TEXTTYPE_RETURN;
}


#define ASCII_LOWER_A	'a'
#define ASCII_UPPER_A	'A'
#define ASCII_LOWER_Z	'z'
#define ASCII_UPPER_Z	'Z'

/*
 *	Note: This function expects Wide-Char input, not
 *	Multibyte input
 */
static USHORT sjis_to_upper(TEXTTYPE obj, UCS2_CHAR ch)
{
	if (ch >= (UCS2_CHAR) ASCII_LOWER_A && ch <= (UCS2_CHAR) ASCII_LOWER_Z)
		return (ch - (UCS2_CHAR) ASCII_LOWER_A + (UCS2_CHAR) ASCII_UPPER_A);
	return ch;
}


/*
 *	Returns -1 if output buffer was too small
 */
/*
 *	Note: This function expects Multibyte input
 */
static SSHORT sjis_str_to_upper(TEXTTYPE obj, USHORT iLen, BYTE *pStr, USHORT iOutLen, BYTE *pOutStr)
{
	BYTE *p;
	USHORT waiting_for_sjis2 = FALSE;
	BYTE c;

	fb_assert(pStr != NULL);
	fb_assert(pOutStr != NULL);
	fb_assert(iLen <= 32000);		/* almost certainly an error */
	fb_assert(iOutLen <= 32000);	/* almost certainly an error */
	fb_assert(iOutLen >= iLen);
	p = pOutStr;
	while (iLen && iOutLen) {
		c = *pStr++;
		if (waiting_for_sjis2 || SJIS1(c)) {
			waiting_for_sjis2 = !waiting_for_sjis2;
		}
		else {
			if (c >= ASCII_LOWER_A && c <= ASCII_LOWER_Z)
				c = (c - ASCII_LOWER_A + ASCII_UPPER_A);
		}
		*pOutStr++ = c;
		iLen--;
		iOutLen--;
	}
	if (iLen != 0)
		return (-1);			/* Must have ran out of output space */
	return (pOutStr - p);
}




/*
 *	Note: This function expects Wide-Char input, not
 *	Multibyte input
 */
static USHORT sjis_to_lower(TEXTTYPE obj, UCS2_CHAR ch)
{
	if (ch >= (UCS2_CHAR) ASCII_UPPER_A && ch <= (UCS2_CHAR) ASCII_UPPER_Z)
		return (ch - (UCS2_CHAR) ASCII_UPPER_A + (UCS2_CHAR) ASCII_LOWER_A);
	return ch;
}
