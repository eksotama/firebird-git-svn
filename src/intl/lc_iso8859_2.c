/*
 *	PROGRAM:	InterBase International support
 *	MODULE:		lc_iso8859_2.c
 *	DESCRIPTION:	Language Drivers in the iso8859_2 family.
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
 * Contributor(s): Michal Bukovjan_________________.
 */

#include "firebird.h"
#include "../intl/ldcommon.h"

extern	USHORT	LC_NARROW_key_length();
extern	USHORT	LC_NARROW_string_to_key();
extern	SSHORT	LC_NARROW_compare();
STATIC	USHORT	fam2_to_upper();
STATIC	SSHORT	fam2_str_to_upper();
STATIC	USHORT	fam2_to_lower();
extern	SSHORT	LC_DOS_nc_mbtowc();

#define FAMILY2(id_number, name, charset, country) \
	cache->texttype_version =		IB_LANGDRV_VERSION; \
	cache->texttype_type =			(id_number); \
	cache->texttype_character_set =		(charset); \
	cache->texttype_country =		(country); \
	cache->texttype_bytes_per_char =	1; \
	cache->texttype_fn_init =		(FPTR_SHORT) (name); \
	cache->texttype_fn_key_length =		LC_NARROW_key_length; \
	cache->texttype_fn_string_to_key =	LC_NARROW_string_to_key; \
	cache->texttype_fn_compare =		LC_NARROW_compare; \
	cache->texttype_fn_to_upper =		fam2_to_upper; \
	cache->texttype_fn_to_lower =		fam2_to_lower; \
	cache->texttype_fn_str_to_upper =	fam2_str_to_upper; \
	cache->texttype_fn_mbtowc =		LC_DOS_nc_mbtowc; \
	cache->texttype_collation_table =	(BYTE *) NoCaseOrderTbl; \
	cache->texttype_toupper_table =		(BYTE *) ToUpperConversionTbl; \
	cache->texttype_tolower_table =		(BYTE *) ToLowerConversionTbl; \
	cache->texttype_compress_table =	(BYTE *) CompressTbl; \
	cache->texttype_expand_table =		(BYTE *) ExpansionTbl; \
	cache->texttype_name =			POSIX; \
    cache->texttype_flags |= ((LDRV_TIEBREAK) & REVERSE) ? \
            TEXTTYPE_reverse_secondary : 0; 


TEXTTYPE_ENTRY (ISO88592_c1_init)
{
static const ASCII	POSIX[] = "cs_CZ.ISO8859_2";

#include "../intl/collations/xx88592czech.h"

FAMILY2 (parm1, ISO88592_c1_init, CS_ISO8859_2, CC_CZECH);

TEXTTYPE_RETURN;
}
#include "../intl/collations/undef.h"

#undef FAMILY2
#undef NULL_SECONDARY
#undef NULL_TERTIARY

/*
 * Generic base for InterBase 4.0 Language Driver
 */

#define	LOCALE_UPPER(ch)	(obj->texttype_toupper_table[ (unsigned) (ch) ])
#define	LOCALE_LOWER(ch)	(obj->texttype_tolower_table[ (unsigned) (ch) ])


STATIC	USHORT	fam2_to_upper (obj, ch)
TEXTTYPE	obj;
BYTE		ch;
{
	return (LOCALE_UPPER (ch));
}


/*
 *	Returns -1 if output buffer was too small
 */
STATIC	SSHORT	fam2_str_to_upper (obj, iLen, pStr, iOutLen, pOutStr)
TEXTTYPE	obj;
USHORT		iLen;
BYTE		*pStr;
USHORT		iOutLen;
BYTE		*pOutStr;
{
	BYTE	*p;
	assert (pStr != NULL);
	assert (pOutStr != NULL);
	assert (iLen <= 32000);	/* almost certainly an error */
	assert (iOutLen <= 32000);	/* almost certainly an error */
	assert (iOutLen >= iLen);
	p = pOutStr;
	while (iLen && iOutLen) {
		*pOutStr++ = LOCALE_UPPER (*pStr);
		pStr++;
		iLen--;
		iOutLen--;
	};
	if (iLen != 0)
		return (-1);
	return (pOutStr - p);
}


STATIC	USHORT	fam2_to_lower (obj, ch)
TEXTTYPE	obj;
BYTE		ch;
{
	return (LOCALE_LOWER (ch));
}



#undef LOCALE_UPPER
#undef LOCALE_LOWER
