/*
 *	PROGRAM:	InterBase International support
 *	MODULE:		lc_iso8859_1.cpp
 *	DESCRIPTION:	Language Drivers in the is8859_1 family.
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
 * The Original Code was created by Inprise Corporation
 * and its predecessors. Portions created by Inprise Corporation are
 * Copyright (C) Inprise Corporation.
 *
 * All Rights Reserved.
 * Contributor(s): ______________________________________.
 */

#include "firebird.h"
#include "../intl/ldcommon.h"
#include "ld_proto.h"

static USHORT fam2_to_upper(TEXTTYPE obj, BYTE ch);
static SSHORT fam2_str_to_upper(TEXTTYPE obj, USHORT iLen, const BYTE* pStr, USHORT iOutLen, BYTE *pOutStr);
static USHORT fam2_to_lower(TEXTTYPE obj, BYTE ch);

#include "lc_narrow.h"
#include "lc_dos.h"

static inline void FAMILY2(TEXTTYPE cache,
							TTYPE_ID id_number,
							pfn_INTL_init name,
							CHARSET_ID charset,
							SSHORT country,
							USHORT flags,
							const SortOrderTblEntry* NoCaseOrderTbl,
							const BYTE* ToUpperConversionTbl,
							const BYTE* ToLowerConversionTbl,
							const CompressPair* CompressTbl,
							const ExpandChar* ExpansionTbl,
							const ASCII* POSIX)
//#define FAMILY2(id_number, name, charset, country)
{
	cache->texttype_version			= IB_LANGDRV_VERSION;
	cache->texttype_type			= id_number;
	cache->texttype_character_set	= charset;
	cache->texttype_country			= country;
	cache->texttype_bytes_per_char	= 1;
	cache->texttype_fn_init			= name;
	cache->texttype_fn_key_length	= LC_NARROW_key_length;
	cache->texttype_fn_string_to_key= LC_NARROW_string_to_key;
	cache->texttype_fn_compare		= LC_NARROW_compare;
	cache->texttype_fn_to_upper		= fam2_to_upper;
	cache->texttype_fn_to_lower		= fam2_to_lower;
	cache->texttype_fn_str_to_upper = fam2_str_to_upper;
	cache->texttype_fn_mbtowc		= LC_DOS_nc_mbtowc;
	cache->texttype_collation_table = (const BYTE*) NoCaseOrderTbl;
	cache->texttype_toupper_table	= ToUpperConversionTbl;
	cache->texttype_tolower_table	= ToLowerConversionTbl;
	cache->texttype_compress_table	= (const BYTE*) CompressTbl;
	cache->texttype_expand_table	= (const BYTE*) ExpansionTbl;
	cache->texttype_name			= POSIX;
	cache->texttype_flags			|= ((flags) & REVERSE) ?
									TEXTTYPE_reverse_secondary : 0;
}



TEXTTYPE_ENTRY(ISO88591_39_init)
{
	static const ASCII POSIX[] = "da_DA.ISO8859_1";

#include "../intl/collations/bl88591da0.h"

	FAMILY2(cache, parm1, ISO88591_39_init, CS_ISO8859_1, CC_DENMARK, LDRV_TIEBREAK,
			NoCaseOrderTbl, ToUpperConversionTbl, ToLowerConversionTbl,
			CompressTbl, ExpansionTbl, POSIX);

	TEXTTYPE_RETURN;
}


TEXTTYPE_ENTRY(ISO88591_40_init)
{
	static const ASCII POSIX[] = "du_NL.ISO8859_1";

#include "../intl/collations/bl88591nl0.h"

	FAMILY2(cache, parm1, ISO88591_40_init, CS_ISO8859_1, CC_NEDERLANDS, LDRV_TIEBREAK,
			NoCaseOrderTbl, ToUpperConversionTbl, ToLowerConversionTbl,
			CompressTbl, ExpansionTbl, POSIX);

	TEXTTYPE_RETURN;
}


TEXTTYPE_ENTRY(ISO88591_41_init)
{
	static const ASCII POSIX[] = "fi_FI.ISO8859_1";

#include "../intl/collations/bl88591fi0.h"

	FAMILY2(cache, parm1, ISO88591_41_init, CS_ISO8859_1, CC_FINLAND, LDRV_TIEBREAK,
			NoCaseOrderTbl, ToUpperConversionTbl, ToLowerConversionTbl,
			CompressTbl, ExpansionTbl, POSIX);

	TEXTTYPE_RETURN;
}


TEXTTYPE_ENTRY(ISO88591_42_init)
{
	static const ASCII POSIX[] = "fr_FR.ISO8859_1";

#include "../intl/collations/bl88591fr0.h"

	FAMILY2(cache, parm1, ISO88591_42_init, CS_ISO8859_1, CC_FRANCE, LDRV_TIEBREAK,
			NoCaseOrderTbl, ToUpperConversionTbl, ToLowerConversionTbl,
			CompressTbl, ExpansionTbl, POSIX);

	TEXTTYPE_RETURN;
}


TEXTTYPE_ENTRY(ISO88591_43_init)
{
	static const ASCII POSIX[] = "fr_CA.ISO8859_1";

#include "../intl/collations/bl88591ca0.h"

	FAMILY2(cache, parm1, ISO88591_43_init, CS_ISO8859_1, CC_FRENCHCAN, LDRV_TIEBREAK,
			NoCaseOrderTbl, ToUpperConversionTbl, ToLowerConversionTbl,
			CompressTbl, ExpansionTbl, POSIX);

	TEXTTYPE_RETURN;
}


TEXTTYPE_ENTRY(ISO88591_44_init)
{
	static const ASCII POSIX[] = "de_DE.ISO8859_1";

#include "../intl/collations/bl88591de0.h"

	FAMILY2(cache, parm1, ISO88591_44_init, CS_ISO8859_1, CC_GERMANY, LDRV_TIEBREAK,
			NoCaseOrderTbl, ToUpperConversionTbl, ToLowerConversionTbl,
			CompressTbl, ExpansionTbl, POSIX);

	TEXTTYPE_RETURN;
}


TEXTTYPE_ENTRY(ISO88591_45_init)
{
	static const ASCII POSIX[] = "is_IS.ISO8859_1";

#include "../intl/collations/bl88591is0.h"

	FAMILY2(cache, parm1, ISO88591_45_init, CS_ISO8859_1, CC_ICELAND, LDRV_TIEBREAK,
			NoCaseOrderTbl, ToUpperConversionTbl, ToLowerConversionTbl,
			CompressTbl, ExpansionTbl, POSIX);

	TEXTTYPE_RETURN;
}


TEXTTYPE_ENTRY(ISO88591_46_init)
{
	static const ASCII POSIX[] = "it_IT.ISO8859_1";

#include "../intl/collations/bl88591it0.h"

	FAMILY2(cache, parm1, ISO88591_46_init, CS_ISO8859_1, CC_ITALY, LDRV_TIEBREAK,
			NoCaseOrderTbl, ToUpperConversionTbl, ToLowerConversionTbl,
			CompressTbl, ExpansionTbl, POSIX);

	TEXTTYPE_RETURN;
}



TEXTTYPE_ENTRY(ISO88591_48_init)
{
	static const ASCII POSIX[] = "no_NO.ISO8859_1";

#include "../intl/collations/bl88591no0.h"

	FAMILY2(cache, parm1, ISO88591_48_init, CS_ISO8859_1, CC_NORWAY, LDRV_TIEBREAK,
			NoCaseOrderTbl, ToUpperConversionTbl, ToLowerConversionTbl,
			CompressTbl, ExpansionTbl, POSIX);

	TEXTTYPE_RETURN;
}


TEXTTYPE_ENTRY(ISO88591_49_init)
{
	static const ASCII POSIX[] = "es_ES.ISO8859_1";

#include "../intl/collations/bl88591es0.h"

	FAMILY2(cache, parm1, ISO88591_49_init, CS_ISO8859_1, CC_SPAIN, LDRV_TIEBREAK,
			NoCaseOrderTbl, ToUpperConversionTbl, ToLowerConversionTbl,
			CompressTbl, ExpansionTbl, POSIX);

	TEXTTYPE_RETURN;
}


TEXTTYPE_ENTRY(ISO88591_51_init)
{
	static const ASCII POSIX[] = "sv_SV.ISO8859_1";

#include "../intl/collations/bl88591sv0.h"

	FAMILY2(cache, parm1, ISO88591_51_init, CS_ISO8859_1, CC_SWEDEN, LDRV_TIEBREAK,
			NoCaseOrderTbl, ToUpperConversionTbl, ToLowerConversionTbl,
			CompressTbl, ExpansionTbl, POSIX);

	TEXTTYPE_RETURN;
}


TEXTTYPE_ENTRY(ISO88591_52_init)
{
	static const ASCII POSIX[] = "en_UK.ISO8859_1";

#include "../intl/collations/bl88591uk0.h"

	FAMILY2(cache, parm1, ISO88591_52_init, CS_ISO8859_1, CC_UK, LDRV_TIEBREAK,
			NoCaseOrderTbl, ToUpperConversionTbl, ToLowerConversionTbl,
			CompressTbl, ExpansionTbl, POSIX);

	TEXTTYPE_RETURN;
}


TEXTTYPE_ENTRY(ISO88591_53_init)
{
	static const ASCII POSIX[] = "en_US.ISO8859_1";

#include "../intl/collations/bl88591us0.h"

	FAMILY2(cache, parm1, ISO88591_53_init, CS_ISO8859_1, CC_US, LDRV_TIEBREAK,
			NoCaseOrderTbl, ToUpperConversionTbl, ToLowerConversionTbl,
			CompressTbl, ExpansionTbl, POSIX);

	TEXTTYPE_RETURN;
}


TEXTTYPE_ENTRY(ISO88591_54_init)
{
	static const ASCII POSIX[] = "pt_PT.ISO8859_1";

#include "../intl/collations/bl88591pt0.h"

	FAMILY2(cache, parm1, ISO88591_54_init, CS_ISO8859_1, CC_PORTUGAL, LDRV_TIEBREAK,
			NoCaseOrderTbl, ToUpperConversionTbl, ToLowerConversionTbl,
			CompressTbl, ExpansionTbl, POSIX);

	TEXTTYPE_RETURN;
}


TEXTTYPE_ENTRY(WIN1250_c1_init)
{
	static const ASCII POSIX[] = "PXW_CSY.WIN1250";

#include "../intl/collations/pw1250czech.h"

	FAMILY2(cache, parm1, WIN1250_c1_init, CS_WIN1250, CC_CZECH, LDRV_TIEBREAK,
			NoCaseOrderTbl, ToUpperConversionTbl, ToLowerConversionTbl,
			CompressTbl, ExpansionTbl, POSIX);

	TEXTTYPE_RETURN;
}


TEXTTYPE_ENTRY(WIN1250_c2_init)
{
	static const ASCII POSIX[] = "PXW_HUNDC.WIN1250";

#include "../intl/collations/pw1250hundc.h"

	FAMILY2(cache, parm1, WIN1250_c2_init, CS_WIN1250, CC_HUNGARY, LDRV_TIEBREAK,
			NoCaseOrderTbl, ToUpperConversionTbl, ToLowerConversionTbl,
			CompressTbl, ExpansionTbl, POSIX);

	TEXTTYPE_RETURN;
}


TEXTTYPE_ENTRY(WIN1250_c3_init)
{
	static const ASCII POSIX[] = "PXW_PLK.WIN1250";

#include "../intl/collations/pw1250polish.h"

	FAMILY2(cache, parm1, WIN1250_c3_init, CS_WIN1250, CC_POLAND, LDRV_TIEBREAK,
			NoCaseOrderTbl, ToUpperConversionTbl, ToLowerConversionTbl,
			CompressTbl, ExpansionTbl, POSIX);

	TEXTTYPE_RETURN;
}


TEXTTYPE_ENTRY(WIN1250_c4_init)
{
	static const ASCII POSIX[] = "PXW_SLOV.WIN1250";

#include "../intl/collations/pw1250slov.h"

	FAMILY2(cache, parm1, WIN1250_c4_init, CS_WIN1250, CC_YUGOSLAVIA, LDRV_TIEBREAK,
			NoCaseOrderTbl, ToUpperConversionTbl, ToLowerConversionTbl,
			CompressTbl, ExpansionTbl, POSIX);

	TEXTTYPE_RETURN;
}


TEXTTYPE_ENTRY (WIN1250_c5_init)
{
	static const ASCII	  POSIX[] = "PXW_HUN.WIN1250";
	
#include "../intl/collations/pw1250hun.h"
	
	FAMILY2(cache, parm1, WIN1250_c5_init, CS_WIN1250, CC_HUNGARY, LDRV_TIEBREAK,
			NoCaseOrderTbl, ToUpperConversionTbl, ToLowerConversionTbl,
			CompressTbl, ExpansionTbl, POSIX);
 
	TEXTTYPE_RETURN;
}						   


TEXTTYPE_ENTRY(WIN1251_c1_init)
{
	static const ASCII POSIX[] = "PXW_CYRL.WIN1251";

#include "../intl/collations/pw1251cyrr.h"

	FAMILY2(cache, parm1, WIN1251_c1_init, CS_WIN1251, CC_RUSSIA, LDRV_TIEBREAK,
			NoCaseOrderTbl, ToUpperConversionTbl, ToLowerConversionTbl,
			CompressTbl, ExpansionTbl, POSIX);

	TEXTTYPE_RETURN;
}


TEXTTYPE_ENTRY(WIN1251_c2_init)
{
	static const ASCII POSIX[] = "WIN1251_UA.WIN1251";

#include "../intl/collations/xx1251_ua.h"

	FAMILY2(cache, parm1, WIN1251_c2_init, CS_WIN1251, CC_RUSSIA, LDRV_TIEBREAK,
			NoCaseOrderTbl, ToUpperConversionTbl, ToLowerConversionTbl,
			CompressTbl, ExpansionTbl, POSIX);

	TEXTTYPE_RETURN;
}


TEXTTYPE_ENTRY(WIN1252_c1_init)
{
	static const ASCII POSIX[] = "PXW_INTL.WIN1252";

#include "../intl/collations/pw1252intl.h"

	FAMILY2(cache, parm1, WIN1252_c1_init, CS_WIN1252, CC_INTL, LDRV_TIEBREAK,
			NoCaseOrderTbl, ToUpperConversionTbl, ToLowerConversionTbl,
			CompressTbl, ExpansionTbl, POSIX);

	TEXTTYPE_RETURN;
}


TEXTTYPE_ENTRY(WIN1252_c2_init)
{
	static const ASCII POSIX[] = "PXW_INTL850.WIN1252";

#include "../intl/collations/pw1252i850.h"

	FAMILY2(cache, parm1, WIN1252_c2_init, CS_WIN1252, CC_INTL, LDRV_TIEBREAK,
			NoCaseOrderTbl, ToUpperConversionTbl, ToLowerConversionTbl,
			CompressTbl, ExpansionTbl, POSIX);

	TEXTTYPE_RETURN;
}


TEXTTYPE_ENTRY(WIN1252_c3_init)
{
	static const ASCII POSIX[] = "PXW_NORDAN4.WIN1252";

#include "../intl/collations/pw1252nor4.h"

	FAMILY2(cache, parm1, WIN1252_c3_init, CS_WIN1252, CC_NORDAN, LDRV_TIEBREAK,
			NoCaseOrderTbl, ToUpperConversionTbl, ToLowerConversionTbl,
			CompressTbl, ExpansionTbl, POSIX);

	TEXTTYPE_RETURN;
}


TEXTTYPE_ENTRY(WIN1252_c4_init)
{
	static const ASCII POSIX[] = "PXW_SPAN.WIN1252";

#include "../intl/collations/pw1252span.h"

	FAMILY2(cache, parm1, WIN1252_c4_init, CS_WIN1252, CC_SPAIN, LDRV_TIEBREAK,
			NoCaseOrderTbl, ToUpperConversionTbl, ToLowerConversionTbl,
			CompressTbl, ExpansionTbl, POSIX);

	TEXTTYPE_RETURN;
}


TEXTTYPE_ENTRY(WIN1252_c5_init)
{
	static const ASCII POSIX[] = "PXW_SWEDFIN.WIN1252";

#include "../intl/collations/pw1252swfn.h"

	FAMILY2(cache, parm1, WIN1252_c5_init, CS_WIN1252, CC_SWEDFIN, LDRV_TIEBREAK,
			NoCaseOrderTbl, ToUpperConversionTbl, ToLowerConversionTbl,
			CompressTbl, ExpansionTbl, POSIX);

	TEXTTYPE_RETURN;
}


TEXTTYPE_ENTRY(WIN1253_c1_init)
{
	static const ASCII POSIX[] = "PXW_GREEK.WIN1253";

#include "../intl/collations/pw1253greek1.h"

	FAMILY2(cache, parm1, WIN1253_c1_init, CS_WIN1253, CC_GREECE, LDRV_TIEBREAK,
			NoCaseOrderTbl, ToUpperConversionTbl, ToLowerConversionTbl,
			CompressTbl, ExpansionTbl, POSIX);

	TEXTTYPE_RETURN;
}


TEXTTYPE_ENTRY(WIN1254_c1_init)
{
	static const ASCII POSIX[] = "PXW_TURK.WIN1254";

#include "../intl/collations/pw1254turk.h"

	FAMILY2(cache, parm1, WIN1254_c1_init, CS_WIN1254, CC_TURKEY, LDRV_TIEBREAK,
			NoCaseOrderTbl, ToUpperConversionTbl, ToLowerConversionTbl,
			CompressTbl, ExpansionTbl, POSIX);

	TEXTTYPE_RETURN;
}


TEXTTYPE_ENTRY(NEXT_c1_init)
{
	static const ASCII POSIX[] = "NXT_US.NEXT";

#include "../intl/collations/blNEXTus0.h"

	FAMILY2(cache, parm1, NEXT_c1_init, CS_NEXT, CC_US, LDRV_TIEBREAK,
			NoCaseOrderTbl, ToUpperConversionTbl, ToLowerConversionTbl,
			CompressTbl, ExpansionTbl, POSIX);

	TEXTTYPE_RETURN;
}


TEXTTYPE_ENTRY(NEXT_c2_init)
{
	static const ASCII POSIX[] = "NXT_DEU.NEXT";

#include "../intl/collations/blNEXTde0.h"

	FAMILY2(cache, parm1, NEXT_c2_init, CS_NEXT, CC_GERMANY, LDRV_TIEBREAK,
			NoCaseOrderTbl, ToUpperConversionTbl, ToLowerConversionTbl,
			CompressTbl, ExpansionTbl, POSIX);

	TEXTTYPE_RETURN;
}


TEXTTYPE_ENTRY(NEXT_c3_init)
{
	static const ASCII POSIX[] = "NXT_FRA.NEXT";

#include "../intl/collations/blNEXTfr0.h"

	FAMILY2(cache, parm1, NEXT_c3_init, CS_NEXT, CC_FRANCE, LDRV_TIEBREAK,
			NoCaseOrderTbl, ToUpperConversionTbl, ToLowerConversionTbl,
			CompressTbl, ExpansionTbl, POSIX);

	TEXTTYPE_RETURN;
}


TEXTTYPE_ENTRY(NEXT_c4_init)
{
	static const ASCII POSIX[] = "NXT_ITA.NEXT";

#include "../intl/collations/blNEXTit0.h"

	FAMILY2(cache, parm1, NEXT_c4_init, CS_NEXT, CC_ITALY, LDRV_TIEBREAK,
			NoCaseOrderTbl, ToUpperConversionTbl, ToLowerConversionTbl,
			CompressTbl, ExpansionTbl, POSIX);

	TEXTTYPE_RETURN;
}


/*
 * Generic base for InterBase 4.0 Language Driver
 */

#define	LOCALE_UPPER(ch)	(obj->texttype_toupper_table[ (unsigned) (ch) ])
#define	LOCALE_LOWER(ch)	(obj->texttype_tolower_table[ (unsigned) (ch) ])




static USHORT fam2_to_upper(TEXTTYPE obj, BYTE ch)
{
	return (LOCALE_UPPER(ch));
}


/*
 *	Returns -1 if output buffer was too small
 */
static SSHORT fam2_str_to_upper(TEXTTYPE obj, USHORT iLen, const BYTE* pStr, USHORT iOutLen, BYTE *pOutStr)
{
	fb_assert(pStr != NULL);
	fb_assert(pOutStr != NULL);
	fb_assert(iLen <= 32000);		/* almost certainly an error */
	fb_assert(iOutLen <= 32000);	/* almost certainly an error */
	fb_assert(iOutLen >= iLen);
	const BYTE* const p = pOutStr;
	while (iLen && iOutLen) {
		*pOutStr++ = LOCALE_UPPER(*pStr);
		pStr++;
		iLen--;
		iOutLen--;
	}
	if (iLen != 0)
		return (-1);
	return (pOutStr - p);
}




static USHORT fam2_to_lower(TEXTTYPE obj, BYTE ch)
{
	return (LOCALE_LOWER(ch));
}

