/*
 *	PROGRAM:	JRD International support
 *	MODULE:		intlobj_new.h
 *	DESCRIPTION:	New international text handling definitions (DRAFT)
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

#ifndef JRD_INTLOBJ_NEW_H
#define JRD_INTLOBJ_NEW_H

#ifndef INCLUDE_FB_TYPES_H
typedef unsigned short USHORT;
typedef short SSHORT;
typedef unsigned char UCHAR;
typedef char CHAR;
typedef unsigned char BYTE;

typedef unsigned int ULONG;
typedef int LONG;
typedef signed int SLONG;
#endif

typedef SCHAR ASCII;

typedef USHORT INTL_BOOL;

/* Forward declarations to be implemented in collation driver */
struct CollationImpl;
struct CharSetImpl;
struct CsConvertImpl;

struct texttype; /* forward decl for the fc signatures before the struct itself. */
struct csconvert;

#define INTL_BAD_KEY_LENGTH ((USHORT)(-1))
#define INTL_BAD_STR_LENGTH ((ULONG)(-1))

/* Returned value of INTL_BAD_KEY_LENGTH means that proposed key is too long */
typedef USHORT (*pfn_INTL_keylength) (
	CollationImpl* collation, 
	USHORT len
);

/* Returned value of INTL_BAD_KEY_LENGTH means that key error happened during key construction */
typedef USHORT (*pfn_INTL_str2key) (
	CollationImpl* collation, 
	USHORT srcLen, 
	const UCHAR* src, 
	USHORT dstLen, 
	UCHAR* dst, 
	INTL_BOOL partial
);

/* Compare two potentially long strings using the same rules as in str2key transformation */
typedef SSHORT (*pfn_INTL_compare) (
	CollationImpl* collation, 
	ULONG len1, 
	const UCHAR* str1, 
	ULONG len2, 
	const UCHAR* str2,
	INTL_BOOL* error_flag
);

/* Returns resulting string length in bytes or INTL_BAD_STR_LENGTH in case of error */
typedef ULONG (*pfn_INTL_str2case) (
	CollationImpl* collation, 
	ULONG srcLen, 
	const UCHAR* src, 
	ULONG dstLen, 
	UCHAR* dst
);

/* Returns FALSE in case of error */
typedef INTL_BOOL (*pfn_INTL_canonical) (
	CollationImpl* collation, 
	ULONG srcLen,
	const UCHAR* src,
	ULONG dstLen,
	const UCHAR* dst
);

/* Extracts a portion from a string. Returns INTL_BAD_STR_LENGTH in case of problems. */
typedef ULONG (*pfn_INTL_substring) (
	CollationImpl* collation, 
	ULONG srcLen,
	const UCHAR* src,
	ULONG dstLen,
	UCHAR* dst,
	ULONG startPos,
	ULONG length
);

/* Measures the length of string in characters. Returns INTL_BAD_STR_LENGTH in case of problems. */
typedef ULONG (*pfn_INTL_length) (
	CollationImpl* collation, 
	ULONG srcLen,
	const UCHAR* src
);

/* Releases resources associated with collation */
typedef void (*pfn_INTL_tt_destroy) (
	CollationImpl*
);


typedef struct texttype {
	// Data which needs to be initialized by collation driver	
	USHORT texttype_version;	/* version ID of object */
	CollationImpl* collation;   /* collation object implemented in driver */
	const ASCII* texttype_name;
	SSHORT texttype_country;	    /* ID of base country values */
	BYTE texttype_canonical_width;  /* number bytes in canonical character representation */
	BYTE texttype_pad_length;       /* Length of pad character in bytes */
	const BYTE* texttype_pad_character; /* Character using for string padding */

	/* If not set key length is assumed to be equal to string length */
	pfn_INTL_keylength	texttype_fn_key_length; /* Return key length for given string */

	/* If not set string itself is used as a key */
	pfn_INTL_str2key	texttype_fn_string_to_key;

	/* If not set string is assumed to be binary-comparable for sorting purposes */
	pfn_INTL_compare	texttype_fn_compare;

	/* If not set string is converted to Unicode and then uppercased via default case folding table */
	pfn_INTL_str2case	texttype_fn_str_to_upper;	/* Convert string to uppercase */

	/* If not set string is converted to Unicode and then lowercased via default case folding table */
	pfn_INTL_str2case	texttype_fn_str_to_lower;	/* Convert string to lowercase */

	/* If not set string is assumed to be binary-comparable for equality purposes */
	pfn_INTL_canonical	texttype_fn_canonical;	/* convert string to canonical representation for equality */

	/* May be omitted if not needed */
	pfn_INTL_tt_destroy	texttype_fn_destroy;	/* release resources associated with collation */
} *TEXTTYPE;

// Returns resulting string length or INTL_BAD_STR_LENGTH in case of error
typedef ULONG (*pfn_INTL_convert) (
	CsConvertImpl* csConvert, 
	ULONG srcLen,
	const UCHAR* src,
	ULONG dstLen,
	UCHAR* dst,
	USHORT* error_code,
	ULONG* offending_source_character	
);

struct csconvert {
	USHORT csconvert_version;
	const ASCII* csconvert_name;

	pfn_INTL_convert csconvert_convert;
};

/* Conversion error codes */

#define	CS_TRUNCATION_ERROR	1	/* output buffer too small  */
#define	CS_CONVERT_ERROR	2	/* can't remap a character      */
#define	CS_BAD_INPUT		3	/* input string detected as bad */

#define	CS_CANT_MAP		0		/* Flag table entries that don't map */


/* Returns whether string is well-formed or not */
typedef INTL_BOOL (*pfn_INTL_well_formed) (
	ULONG len
	const UCHAR* str, 
);

struct charset
{
	USHORT charset_version;
	const ASCII* charset_name;
	BYTE charset_min_bytes_per_char;
	BYTE charset_max_bytes_per_char;

	/* If omitted any string is considered well-formed */
	pfn_INTL_well_formed	charset_well_formed;
	csconvert		charset_to_unicode;
	csconvert		charset_from_unicode;

	/* If not set Unicode representation is used to measure string length. */
	pfn_INTL_length		texttype_fn_length;	/* get length of string in characters */

	/* May be omitted for fixed-width character sets. 
	   If not present for MBCS charset string operation is performed by the engine
       via intermediate translation of string to Unicode */
	pfn_INTL_substring	charset_fn_substring;	/* get a portion of string */
};

#endif /* JRD_INTLOBJ_NEW_H */

