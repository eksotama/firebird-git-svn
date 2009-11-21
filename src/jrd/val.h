/*
 *	PROGRAM:	JRD access method
 *	MODULE:		val.h
 *	DESCRIPTION:	Definitions associated with value handling
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
 *
 * 2002.10.29 Sean Leyne - Removed obsolete "Netware" port
 *
 * 2002.10.30 Sean Leyne - Removed support for obsolete "PC_PLATFORM" define
 *
 */

#ifndef JRD_VAL_H
#define JRD_VAL_H

#include "../include/fb_blk.h"
#include "../common/classes/array.h"
#include "../common/classes/MetaName.h"
#include "../common/classes/QualifiedName.h"

#include "../jrd/blb.h"
#include "../jrd/dsc.h"
#include "../jrd/ExtEngineManager.h"

#define FLAG_BYTES(n)	(((n + BITS_PER_LONG) & ~((ULONG)BITS_PER_LONG - 1)) >> 3)

// Random string block -- as long as impure areas don't have
// constructors and destructors, the need this varying string

class VaryingString : public pool_alloc_rpt<SCHAR, type_str>
{
public:
	USHORT str_length;
	UCHAR str_data[2];			// one byte for ALLOC and one for the NULL
};

const UCHAR DEFAULT_DOUBLE  = dtype_double;
const ULONG MAX_FORMAT_SIZE	= 65535;

namespace Jrd {

class ArrayField;
class blb;
class jrd_req;
class jrd_tra;
class PatternMatcher;
class Symbol;

// Various structures in the impure area

struct impure_state
{
	SSHORT sta_state;
};

struct impure_value
{
	dsc vlu_desc;
	USHORT vlu_flags; // Computed/invariant flags
	VaryingString* vlu_string;
	union
	{
		UCHAR vlu_uchar;
		SSHORT vlu_short;
		SLONG vlu_long;
		SINT64 vlu_int64;
		SQUAD vlu_quad;
		SLONG vlu_dbkey[2];
		float vlu_float;
		double vlu_double;
		GDS_TIMESTAMP vlu_timestamp;
		GDS_TIME vlu_sql_time;
		GDS_DATE vlu_sql_date;
		bid vlu_bid;

		// Pre-compiled invariant object for nod_like and other string functions
		Jrd::PatternMatcher* vlu_invariant;
	} vlu_misc;

	void make_long(const SLONG val, const signed char scale = 0);
	void make_int64(const SINT64 val, const signed char scale = 0);
};

// Do not use these methods where dsc_sub_type is not explicitly set to zero.
inline void impure_value::make_long(const SLONG val, const signed char scale)
{
	this->vlu_misc.vlu_long = val;
	this->vlu_desc.dsc_dtype = dtype_long;
	this->vlu_desc.dsc_length = sizeof(SLONG);
	this->vlu_desc.dsc_scale = scale;
	this->vlu_desc.dsc_sub_type = 0;
	this->vlu_desc.dsc_address = reinterpret_cast<UCHAR*>(&this->vlu_misc.vlu_long);
}

inline void impure_value::make_int64(const SINT64 val, const signed char scale)
{
	this->vlu_misc.vlu_int64 = val;
	this->vlu_desc.dsc_dtype = dtype_int64;
	this->vlu_desc.dsc_length = sizeof(SINT64);
	this->vlu_desc.dsc_scale = scale;
	this->vlu_desc.dsc_sub_type = 0;
	this->vlu_desc.dsc_address = reinterpret_cast<UCHAR*>(&this->vlu_misc.vlu_int64);
}

struct impure_value_ex : public impure_value
{
	SLONG vlux_count;
	blb* vlu_blob;
};

const int VLU_computed	= 1;	// An invariant sub-query has been computed
const int VLU_null		= 2;	// An invariant sub-query computed to null
const int VLU_checked	= 4;	// Constraint already checked in first read or assignment to argument/variable


class Format : public pool_alloc<type_fmt>
{
public:
	Format(MemoryPool& p, int len)
		: fmt_count(len), fmt_desc(p, fmt_count), fmt_defaults(p, fmt_count)
	{
		fmt_desc.resize(fmt_count);
		fmt_defaults.resize(fmt_count);

		for (fmt_defaults_iterator impure = fmt_defaults.begin();
			 impure != fmt_defaults.end(); ++impure)
		{
			memset(&*impure, 0, sizeof(*impure));
		}
	}

	~Format()
	{
		for (fmt_defaults_iterator impure = fmt_defaults.begin();
			 impure != fmt_defaults.end(); ++impure)
		{
			delete impure->vlu_string;
		}
	}

	static Format* newFormat(MemoryPool& p, int len = 0)
	{
		return FB_NEW(p) Format(p, len);
	}

	USHORT fmt_length;
	USHORT fmt_count;
	USHORT fmt_version;
	Firebird::Array<dsc> fmt_desc;
	Firebird::Array<impure_value> fmt_defaults;

	typedef Firebird::Array<dsc>::iterator fmt_desc_iterator;
	typedef Firebird::Array<dsc>::const_iterator fmt_desc_const_iterator;

	typedef Firebird::Array<impure_value>::iterator fmt_defaults_iterator;
};


// Parameter passing mechanism. Also used for returning values, except for scalar_array.
enum FUN_T {
	FUN_value,
	FUN_reference,
	FUN_descriptor,
	FUN_blob_struct,
	FUN_scalar_array,
	FUN_ref_with_null
};


/* Function definition block */

struct fun_repeat
{
	DSC fun_desc;			/* Datatype info */
	FUN_T fun_mechanism;	/* Passing mechanism */
};


class UserFunction : public pool_alloc_rpt<fun_repeat, type_fun>
{
public:
	Firebird::QualifiedName fun_name;		// Function name
	Firebird::string fun_exception_message;	/* message containing the exception error message */
	UserFunction*	fun_homonym;	/* Homonym functions */
	Symbol*		fun_symbol;			/* Symbol block */
	int (*fun_entrypoint) ();		/* Function entrypoint */
	USHORT		fun_count;			/* Number of arguments (including return) */
	USHORT		fun_args;			/* Number of input arguments */
	USHORT		fun_return_arg;		/* Return argument */
	USHORT		fun_type;			/* Type of function */
	ULONG		fun_temp_length;	/* Temporary space required */
	Jrd::ExtEngineManager::Function* fun_external;
	fun_repeat fun_rpt[1];

public:
	explicit UserFunction(MemoryPool& p)
		: fun_name(p),
		  fun_exception_message(p)
	{
	}
};

// Those two defines seems an intention to do something that wasn't completed.
// UDfs that return values like now or boolean Udfs. See rdb$functions.rdb$function_type.
//#define FUN_value	0
//#define FUN_boolean	1

/* Blob passing structure */
// CVC: Moved to fun.epp where it belongs.

/* Scalar array descriptor, "external side" seen by UDF's */

struct scalar_array_desc
{
	DSC sad_desc;
	SLONG sad_dimensions;
	struct sad_repeat
	{
		SLONG sad_lower;
		SLONG sad_upper;
	} sad_rpt[1];
};

// Sorry for the clumsy name, but in blk.h this is referred as array description.
// Since we already have Scalar Array Descriptor and Array Description [Slice],
// it was too confusing. Therefore, it was renamed ArrayField, since ultimately,
// it represents an array field the user can manipulate.
// There was also confusion for the casual reader due to the presence of
// the structure "slice" in sdl.h that was renamed array_slice.

class ArrayField : public pool_alloc_rpt<Ods::InternalArrayDesc::iad_repeat, type_arr>
{
public:
	UCHAR*				arr_data;			/* Data block, if allocated */
	blb*				arr_blob;			/* Blob for data access */
	jrd_tra*			arr_transaction;	/* Parent transaction block */
	ArrayField*			arr_next;			/* Next array in transaction */
	jrd_req*			arr_request;		/* request */
	SLONG				arr_effective_length;	/* Length of array instance */
	USHORT				arr_desc_length;	/* Length of array descriptor */
	ULONG				arr_temp_id;		// Temporary ID for open array inside the transaction

	// Keep this field last as it is C-style open array !
	Ods::InternalArrayDesc	arr_desc;		/* Array descriptor. ! */
};

} //namespace Jrd

#endif /* JRD_VAL_H */
