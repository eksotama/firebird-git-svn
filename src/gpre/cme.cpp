//____________________________________________________________
//  
//		PROGRAM:	C preprocessor
//		MODULE:		cme.cpp
//		DESCRIPTION:	Request expression compiler
//  
//  The contents of this file are subject to the Interbase Public
//  License Version 1.0 (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy
//  of the License at http://www.Inprise.com/IPL.html
//  
//  Software distributed under the License is distributed on an
//  "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express
//  or implied. See the License for the specific language governing
//  rights and limitations under the License.
//  
//  The Original Code was created by Inprise Corporation
//  and its predecessors. Portions created by Inprise Corporation are
//  Copyright (C) Inprise Corporation.
//  
//  All Rights Reserved.
//  Contributor(s): ______________________________________.
//  TMN (Mike Nordell) 11.APR.2001 - Reduce compiler warnings, buffer ptr bug
//  
//
//____________________________________________________________
//
//	$Id: cme.cpp,v 1.19 2003-11-11 12:08:12 brodsom Exp $
//

#include "firebird.h"
#include <stdlib.h>
#include <string.h>
#include "../jrd/y_ref.h"
#include "../jrd/ibase.h"
#include "../gpre/gpre.h"
#include "../jrd/intl.h"
#include "../intl/charsets.h"
#include "../gpre/cme_proto.h"
#include "../gpre/cmp_proto.h"
#include "../gpre/gpre_proto.h"
#include "../gpre/gpre_meta.h"
#include "../gpre/movg_proto.h"
#include "../gpre/par_proto.h"
#include "../gpre/prett_proto.h"
#include "../jrd/dsc_proto.h"
#include "../gpre/msc_proto.h"

static GPRE_NOD cmp_array(GPRE_NOD, GPRE_REQ);
static GPRE_NOD cmp_array_element(GPRE_NOD, GPRE_REQ);
static void cmp_cast(GPRE_NOD, GPRE_REQ);
static GPRE_NOD cmp_field(GPRE_NOD, GPRE_REQ);
static GPRE_NOD cmp_literal(GPRE_NOD, GPRE_REQ);
static void cmp_map(MAP, GPRE_REQ);
static void cmp_plan(const gpre_nod*, GPRE_REQ);
static void cmp_sdl_dtype(GPRE_FLD, REF);
static GPRE_NOD cmp_udf(GPRE_NOD, GPRE_REQ);
static GPRE_NOD cmp_value(GPRE_NOD, GPRE_REQ);
static USHORT get_string_len(GPRE_FLD);
static void stuff_cstring(GPRE_REQ, const char *);
static void stuff_sdl_dimension(const dim*, REF, SSHORT);
static void stuff_sdl_element(REF, const gpre_fld*);
static void stuff_sdl_loops(REF, const gpre_fld*);
static void stuff_sdl_number(const SLONG, REF);

const int USER_LENGTH = 32;
#define STUFF(blr)		*request->req_blr++ = (UCHAR) (blr)
#define STUFF_WORD(blr)		STUFF (blr); STUFF (blr >> 8)
#define STUFF_CSTRING(blr)	stuff_cstring (request, blr)

#define STUFF_SDL(sdl)		*reference->ref_sdl++ = (UCHAR) (sdl)
#define STUFF_SDL_WORD(sdl)	STUFF_SDL (sdl); STUFF_SDL (sdl >> 8);
#define STUFF_SDL_LONG(sdl)	STUFF_SDL (sdl); STUFF_SDL (sdl >> 8); STUFF_SDL (sdl >>16); STUFF_SDL (sdl >> 24);

static bool debug_on;

struct op_table
{
	enum nod_t op_type;
	UCHAR op_blr;
};

const op_table operators[] =
{
	{ nod_eq			, blr_eql },
	{ nod_ge			, blr_geq },
	{ nod_gt			, blr_gtr },
	{ nod_le			, blr_leq },
	{ nod_lt			, blr_lss },
	{ nod_ne			, blr_neq },
	{ nod_missing		, blr_missing },
	{ nod_between		, blr_between },
	{ nod_and			, blr_and },
	{ nod_or			, blr_or },
	{ nod_not			, blr_not },
	{ nod_matches		, blr_matching },
	{ nod_starting	, blr_starting },
	{ nod_containing	, blr_containing },
	{ nod_plus		, blr_add },
	{ nod_from		, blr_from },
	{ nod_via			, blr_via },
	{ nod_minus		, blr_subtract },
	{ nod_times		, blr_multiply },
	{ nod_divide		, blr_divide },
	{ nod_negate		, blr_negate },
	{ nod_null		, blr_null },
	{ nod_user_name	, blr_user_name },
//  { count2 }
//   { nod_count, blr_count2 },
//  
	{ nod_count		, blr_count },
	{ nod_max			, blr_maximum },
	{ nod_min			, blr_minimum },
	{ nod_average		, blr_average },
	{ nod_total		, blr_total },
	{ nod_any			, blr_any },
	{ nod_unique		, blr_unique },
	{ nod_agg_count	, blr_agg_count2 },
	{ nod_agg_count	, blr_agg_count },
	{ nod_agg_max		, blr_agg_max },
	{ nod_agg_min		, blr_agg_min },
	{ nod_agg_total	, blr_agg_total },
	{ nod_agg_average	, blr_agg_average },
	{ nod_upcase		, blr_upcase },
	{ nod_sleuth		, blr_matching2 },
	{ nod_concatenate	, blr_concatenate },
	{ nod_cast		, blr_cast },
	{ nod_ansi_any	, blr_ansi_any },
	{ nod_gen_id		, blr_gen_id },
	{ nod_ansi_all	, blr_ansi_all },
	{ nod_current_date, blr_current_date },
	{ nod_current_time, blr_current_time },
	{ nod_current_timestamp, blr_current_timestamp },
	{ nod_any, 0 }
};


static inline void assign_dtype(gpre_fld* f, const gpre_fld* field)
{
	f->fld_dtype = field->fld_dtype;
	f->fld_length = field->fld_length;
	f->fld_scale = field->fld_scale;
	f->fld_precision = field->fld_precision;
	f->fld_sub_type = field->fld_sub_type;
	f->fld_charset_id = field->fld_charset_id;
	f->fld_collate_id = field->fld_collate_id;
	f->fld_ttype = field->fld_ttype;
}

/* One of d1,d2 is time, the other is date */
static inline bool is_date_and_time(const USHORT d1, const USHORT d2)
{
	return (d1 == dtype_sql_time && d2 == dtype_sql_date) ||
		   (d2 == dtype_sql_time && d1 == dtype_sql_date);
}

//____________________________________________________________
//  
//		Compile a random expression.
//  

void CME_expr(GPRE_NOD node, GPRE_REQ request)
{
	GPRE_NOD *ptr, *end;
	MEL element;
	GPRE_CTX context;
	REF reference;
	TEXT *p, s[128];

	switch (node->nod_type)
	{
	case nod_field:
		if (!(reference = (REF) node->nod_arg[0]))
		{
			CPR_error("CME_expr: reference missing");
			return;
		}
		cmp_field(node, request);
		if (reference->ref_flags & REF_fetch_array)
			cmp_array(node, request);
		return;

	case nod_array:
		cmp_array_element(node, request);
		return;

	case nod_index:
		CME_expr(node->nod_arg[0], request);
		return;

	case nod_value:
		cmp_value(node, request);
		if ((reference = (REF) node->nod_arg[0]) &&
			(reference->ref_flags & REF_fetch_array))
		{
			cmp_array(node, request);
		}
		return;

	case nod_negate:
		if (node->nod_arg[0]->nod_type != nod_literal)
			break;
	case nod_literal:
		cmp_literal(node, request);
		return;

	case nod_like:
		STUFF((node->nod_count == 2) ? blr_like : blr_ansi_like);
		ptr = node->nod_arg;
		for (end = ptr + node->nod_count; ptr < end; ptr++)
			CME_expr(*ptr, request);
		return;

	case nod_udf:
		cmp_udf(node, request);
		return;

	case nod_gen_id:
		STUFF(blr_gen_id);
		p = (TEXT *) (node->nod_arg[1]);

		/* check if this generator really exists */

		if (!MET_generator(p, request->req_database))
		{
			sprintf(s, "generator %s not found", p);
			CPR_error(s);
		}
		STUFF(strlen(p));
		while (*p)
			STUFF(*p++);
		CME_expr(node->nod_arg[0], request);
		return;

	case nod_cast:
		cmp_cast(node, request);
		return;

	case nod_agg_count:
		if ((node->nod_arg[0]) &&
			!(request->req_database->dbb_flags & DBB_v3))
		{
			if (node->nod_arg[1])
				STUFF(blr_agg_count_distinct);
			else
				STUFF(blr_agg_count2);
			CME_expr(node->nod_arg[0], request);
		}
		else
			STUFF(blr_agg_count);
		return;

// ** Begin date/time/timestamp support *
	case nod_extract:
		STUFF(blr_extract);
		switch ((KWWORDS) (int) node->nod_arg[0])
		{
		case KW_YEAR:
			STUFF(blr_extract_year);
			break;
		case KW_MONTH:
			STUFF(blr_extract_month);
			break;
		case KW_DAY:
			STUFF(blr_extract_day);
			break;
		case KW_HOUR:
			STUFF(blr_extract_hour);
			break;
		case KW_MINUTE:
			STUFF(blr_extract_minute);
			break;
		case KW_SECOND:
			STUFF(blr_extract_second);
			break;
		case KW_WEEKDAY:
			STUFF(blr_extract_weekday);
			break;
		case KW_YEARDAY:
			STUFF(blr_extract_yearday);
			break;
		default:
			CPR_error("CME_expr:Invalid extract part");
		}
		CME_expr(node->nod_arg[1], request);
		return;

// ** End date/time/timestamp support *
//  count2
//   case nod_count:
// if (node->nod_arg [1])
//    break;
// STUFF (blr_count);
// CME_rse (node->nod_arg [0], request);
// return;
//  

	case nod_agg_total:
		if ((node->nod_arg[1]) &&
			!(request->req_database->dbb_flags & DBB_v3))
				STUFF(blr_agg_total_distinct);
		else
			STUFF(blr_agg_total);
		CME_expr(node->nod_arg[0], request);
		return;

	case nod_agg_average:
		if ((node->nod_arg[1]) &&
			!(request->req_database->dbb_flags & DBB_v3))
				STUFF(blr_agg_average_distinct);
		else
			STUFF(blr_agg_average);
		CME_expr(node->nod_arg[0], request);
		return;

	case nod_dom_value:
		STUFF(blr_fid);
		STUFF(0);				/* Context   */
		STUFF_WORD(0);			/* Field id  */
		return;

	case nod_map_ref:
		element = (MEL) node->nod_arg[0];
		context = element->mel_context;
		STUFF(blr_fid);
		STUFF(context->ctx_internal);
		STUFF_WORD(element->mel_position);
		return;
	}

	const op_table* operator_;
	for (operator_ = operators;
		operator_->op_type != node->nod_type;
		++operator_)
	{
		if (!operator_->op_blr)
		{
			CPR_bugcheck("node type not implemented");
			return;
		}
	}

	STUFF(operator_->op_blr);
	ptr = node->nod_arg;

	for (end = ptr + node->nod_count; ptr < end; ptr++)
		CME_expr(*ptr, request);

	switch (node->nod_type)
	{
	case nod_any:
	case nod_ansi_any:
	case nod_ansi_all:
	case nod_unique:
//  count2 next line would be deleted 
	case nod_count:
		CME_rse((GPRE_RSE) node->nod_arg[0], request);
		break;

	case nod_max:
	case nod_min:
	case nod_average:
	case nod_total:
	case nod_from:
//  
//   case nod_count:
//  
		CME_rse((GPRE_RSE) node->nod_arg[0], request);
		CME_expr(node->nod_arg[1], request);
		break;

	case nod_via:
		CME_rse((GPRE_RSE) node->nod_arg[0], request);
		CME_expr(node->nod_arg[1], request);
		CME_expr(node->nod_arg[2], request);
	}
}


//____________________________________________________________
//  
//		Compute datatype, length, and scale of an expression.
//  

void CME_get_dtype(const gpre_nod* node, gpre_fld* f)
{
	gpre_fld field1, field2;
	SSHORT dtype_max;
	const TEXT* string;
	MEL element;
	REF reference;
	GPRE_FLD tmp_field;
	const udf* a_udf;

	f->fld_dtype = 0;
	f->fld_length = 0;
	f->fld_scale = 0;
	f->fld_sub_type = 0;
	f->fld_charset_id = 0;
	f->fld_collate_id = 0;
	f->fld_ttype = 0;

	switch (node->nod_type)
	{
	case nod_null:
		/* This occurs when SQL statement specifies a literal NULL, eg:
		 *  SELECT NULL FROM TABLE1;
		 * As we don't have a <dtype_null, HOSTTYPE> datatype pairing,
		 * we don't know how to map this NULL to a host-language
		 * datatype.  Therefore we now describe it as a 
		 * CHAR(1) CHARACTER SET NONE type.
		 * No value will ever be sent back, as the value of the select
		 * will be NULL - this is only for purposes of allocating
		 * values in the message DESCRIBING
		 * the statement.  
		 * Other parts of gpre aren't too happy with a dtype_null datatype
		 */
		f->fld_dtype = dtype_text;
		f->fld_length = 1;
		f->fld_ttype = ttype_none;
		f->fld_charset_id = CS_NONE;
		return;

	case nod_map_ref:
		element = (MEL) node->nod_arg[0];
		CME_get_dtype(element->mel_expr, f);
		return;

	case nod_value:
	case nod_field:
	case nod_array:
		reference = (REF) node->nod_arg[0];
		if (!(tmp_field = reference->ref_field))
			CPR_error("CME_get_dtype: node type not supported");
		if (!(tmp_field->fld_dtype) || !(tmp_field->fld_length))
			PAR_error("Inappropriate self-reference of field");

		assign_dtype(f, tmp_field);
		return;

	case nod_agg_count:
	case nod_count:
		f->fld_dtype = dtype_long;
		f->fld_length = sizeof(SLONG);
		return;

	case nod_gen_id:
		if ((sw_sql_dialect == SQL_DIALECT_V5) || (sw_server_version < 6))
		{
			f->fld_dtype = dtype_long;
			f->fld_length = sizeof(SLONG);
		}
		else
		{
			f->fld_dtype = dtype_int64;
			f->fld_length = sizeof(ISC_INT64);
		}
		return;

	case nod_max:
	case nod_min:
	case nod_from:
		CME_get_dtype(node->nod_arg[1], f);
		return;

	case nod_agg_max:
	case nod_agg_min:
	case nod_negate:
		CME_get_dtype(node->nod_arg[0], f);
		if ((sw_sql_dialect == SQL_DIALECT_V5)
			&& (f->fld_dtype == dtype_int64))
		{
			f->fld_precision = 0;
			f->fld_scale = 0;
			f->fld_dtype = dtype_double;
			f->fld_length = sizeof(double);
		}
		return;

// ** Begin date/time/timestamp support *
	case nod_extract:
		{
			KWWORDS kw_word = (KWWORDS) (int) node->nod_arg[0];
			CME_get_dtype(node->nod_arg[1], f);
			switch (f->fld_dtype)
			{
			case dtype_timestamp:
				break;

			case dtype_sql_date:
				if (kw_word == KW_HOUR || kw_word == KW_MINUTE
					|| kw_word ==
					KW_SECOND)
		  CPR_error("Invalid extract part for SQL DATE type");
				break;

			case dtype_sql_time:
				if (kw_word != KW_HOUR && kw_word != KW_MINUTE
					&& kw_word !=
					KW_SECOND)
		  CPR_error("Invalid extract part for SQL TIME type");
				break;

			default:
				CPR_error("Invalid use of EXTRACT function");
			}
			switch (kw_word)
			{
			case KW_YEAR:
			case KW_MONTH:
			case KW_DAY:
			case KW_WEEKDAY:
			case KW_YEARDAY:
			case KW_HOUR:
			case KW_MINUTE:
				f->fld_dtype = dtype_short;
				f->fld_length = sizeof(short);
				break;
			case KW_SECOND:
				f->fld_dtype = dtype_long;
				f->fld_length = sizeof(long);
				f->fld_scale = ISC_TIME_SECONDS_PRECISION_SCALE;
				break;
			default:
				CPR_error("Invalid EXTRACT part");
			}
			return;
		}

	case nod_current_date:
		f->fld_dtype = dtype_sql_date;
		f->fld_length = sizeof(ISC_DATE);
		return;

	case nod_current_time:
		f->fld_dtype = dtype_sql_time;
		f->fld_length = sizeof(ISC_TIME);
		return;

	case nod_current_timestamp:
		f->fld_dtype = dtype_timestamp;
		f->fld_length = sizeof(ISC_TIMESTAMP);
		return;

// ** End date/time/timestamp support *

	case nod_times:
		CME_get_dtype(node->nod_arg[0], &field1);
		CME_get_dtype(node->nod_arg[1], &field2);
		if (sw_sql_dialect == SQL_DIALECT_V5)
		{
			if (field1.fld_dtype == dtype_int64)
			{
				field1.fld_dtype = dtype_double;
				field1.fld_scale = 0;
				field1.fld_length = sizeof(double);
			}
			if (field2.fld_dtype == dtype_int64)
			{
				field2.fld_dtype = dtype_double;
				field2.fld_scale = 0;
				field2.fld_length = sizeof(double);
			}
			dtype_max = MAX(field1.fld_dtype, field2.fld_dtype);
			if (DTYPE_IS_DATE(dtype_max) || DTYPE_IS_BLOB(dtype_max))
			{
				CPR_error("Invalid use of date/blob/array value");
			}
		}
		else
		{
			dtype_max =
				DSC_multiply_result[field1.fld_dtype][field2.fld_dtype];
			if (dtype_max == dtype_null)
			{
				CPR_error("Invalid operand used in multiplication");
			} else if (dtype_max == DTYPE_CANNOT)
			{
				CPR_error("expression evaluation not supported");
			}
		}
		if (dtype_max == dtype_short || dtype_max == dtype_long)
		{
			f->fld_dtype = dtype_long;
			f->fld_scale = field1.fld_scale + field2.fld_scale;
			f->fld_length = sizeof(SLONG);
		}
#ifdef NATIVE_QUAD
		else if (dtype_max == dtype_quad)
		{
			f->fld_dtype = dtype_quad;
			f->fld_scale = field1.fld_scale + field2.fld_scale;
			f->fld_length = sizeof(ISC_QUAD);
		}
#endif
		else if (dtype_max == dtype_int64)
		{
			f->fld_dtype = dtype_int64;
			f->fld_scale = field1.fld_scale + field2.fld_scale;
			f->fld_length = sizeof(ISC_INT64);
		}
		else
		{
			f->fld_dtype = dtype_double;
			f->fld_scale = 0;
			f->fld_length = sizeof(double);
		}
		return;

	case nod_concatenate:
		CME_get_dtype(node->nod_arg[0], &field1);
		CME_get_dtype(node->nod_arg[1], &field2);
		dtype_max = MAX(field1.fld_dtype, field2.fld_dtype);
		if (field1.fld_dtype > dtype_any_text)
		{
			f->fld_dtype		= dtype_varying;
			f->fld_char_length	=
				get_string_len(&field1) + get_string_len(&field2);
			f->fld_length		= f->fld_char_length + sizeof(USHORT);
			f->fld_charset_id	= CS_ASCII;
			f->fld_ttype		= ttype_ascii;
			return;
		}
		assign_dtype(f, &field1);
		f->fld_length = get_string_len(&field1) + get_string_len(&field2);
		if (f->fld_dtype == dtype_cstring)
			f->fld_length += 1;
		else if (f->fld_dtype == dtype_varying)
			f->fld_length += sizeof(USHORT);
		return;

	case nod_plus:
	case nod_minus:
		CME_get_dtype(node->nod_arg[0], &field1);
		CME_get_dtype(node->nod_arg[1], &field2);
		if (sw_sql_dialect == SQL_DIALECT_V5)
		{
			if (field1.fld_dtype == dtype_int64)
			{
				field1.fld_dtype = dtype_double;
				field1.fld_scale = 0;
				field1.fld_length = sizeof(double);
			}
			if (field2.fld_dtype == dtype_int64)
			{
				field2.fld_dtype = dtype_double;
				field2.fld_scale = 0;
				field2.fld_length = sizeof(double);
			}

			if (DTYPE_IS_DATE(field1.fld_dtype) &&
				DTYPE_IS_DATE(field2.fld_dtype) &&
				!((node->nod_type == nod_minus) ||
				  is_date_and_time(field1.fld_dtype, field2.fld_dtype)))
			{
				CPR_error("Invalid use of timestamp/date/time value");
			}
			else
				dtype_max = MAX(field1.fld_dtype, field2.fld_dtype);
			if (DTYPE_IS_BLOB(dtype_max))
				CPR_error("Invalid use of blob/array value");
		}
		else
		{
// ** Dialect is > 1 *

			if (node->nod_type == nod_plus)
				dtype_max =
					DSC_add_result[field1.fld_dtype][field2.fld_dtype];
			else
				dtype_max =
					DSC_sub_result[field1.fld_dtype][field2.fld_dtype];
			if (dtype_max == dtype_null)
				CPR_error("Illegal operands used in addition");
			else if (dtype_max == DTYPE_CANNOT)
				CPR_error("expression evaluation not supported");
		}

		if (dtype_max == dtype_short || dtype_max == dtype_long)
		{
			f->fld_dtype = dtype_long;
			f->fld_scale = MIN(field1.fld_scale, field2.fld_scale);
			f->fld_length = sizeof(SLONG);
		}
#ifdef NATIVE_QUAD
		else if (dtype_max == dtype_quad)
		{
			f->fld_dtype = dtype_quad;
			f->fld_scale = MIN(field1.fld_scale, field2.fld_scale);
			f->fld_length = sizeof(ISC_QUAD);
		}
#endif
// ** Begin date/time/timestamp support *
		else if (dtype_max == dtype_sql_date)
		{
			f->fld_dtype = dtype_sql_date;
			f->fld_scale = 0;
			f->fld_length = sizeof(ISC_DATE);
		}
		else if (dtype_max == dtype_sql_time)
		{
			f->fld_dtype = dtype_sql_time;
			f->fld_scale = 0;
			f->fld_length = sizeof(ISC_TIME);
		}
		else if (dtype_max == dtype_timestamp)
		{
			f->fld_dtype = dtype_timestamp;
			f->fld_scale = 0;
			f->fld_length = sizeof(ISC_TIMESTAMP);
		}
// ** End date/time/timestamp support *
		else if (dtype_max == dtype_int64)
		{
			f->fld_dtype = dtype_int64;
			f->fld_scale = MIN(field1.fld_scale, field2.fld_scale);
			f->fld_length = sizeof(ISC_INT64);
		}
		else
		{
			f->fld_dtype = dtype_double;
			f->fld_scale = 0;
			f->fld_length = sizeof(double);
		}
		return;

	case nod_via:
		CME_get_dtype(node->nod_arg[1], &field1);
		CME_get_dtype(node->nod_arg[2], &field2);
		if (field1.fld_dtype >= field2.fld_dtype)
		{
			assign_dtype(f, &field1);
		}
		else
		{
			assign_dtype(f, &field2);
		}
		return;


	case nod_divide:
		CME_get_dtype(node->nod_arg[0], &field1);
		CME_get_dtype(node->nod_arg[1], &field2);
		if (sw_sql_dialect == SQL_DIALECT_V5)
		{
			if (field1.fld_dtype == dtype_int64)
			{
				field1.fld_dtype = dtype_double;
				field1.fld_scale = 0;
				field1.fld_length = sizeof(double);
			}
			if (field2.fld_dtype == dtype_int64)
			{
				field2.fld_dtype = dtype_double;
				field2.fld_scale = 0;
				field2.fld_length = sizeof(double);
			}
			dtype_max = MAX(field1.fld_dtype, field2.fld_dtype);
			if (DTYPE_IS_DATE(dtype_max) || DTYPE_IS_BLOB(dtype_max))
				CPR_error("Invalid use of date/blob/array value");
			f->fld_dtype = dtype_double;
			f->fld_length = sizeof(double);
			return;
		}
		else
		{
			dtype_max =
				DSC_multiply_result[field1.fld_dtype][field2.fld_dtype];
			if (dtype_max == dtype_null)
				CPR_error("Illegal operands used in division");
			else if (dtype_max == DTYPE_CANNOT)
				CPR_error("expression evaluation not supported");
		}

		if (dtype_max == dtype_int64)
		{
			f->fld_dtype = dtype_int64;
			f->fld_scale = field1.fld_scale + field2.fld_scale;
			f->fld_length = sizeof(ISC_INT64);
		}
		else
		{
			f->fld_dtype = dtype_double;
			f->fld_scale = 0;
			f->fld_length = sizeof(double);
		}
		return;

	case nod_average:
	case nod_agg_average:
		if (node->nod_type == nod_average)
			CME_get_dtype(node->nod_arg[1], f);
		else
			CME_get_dtype(node->nod_arg[0], f);
		if (!DTYPE_CAN_AVERAGE(f->fld_dtype))
			CPR_error("expression evaluation not supported");
		if (sw_sql_dialect != SQL_DIALECT_V5)
		{
			if (DTYPE_IS_EXACT(f->fld_dtype))
			{
				f->fld_dtype = dtype_int64;
				f->fld_length = sizeof(SINT64);
			}
			else
			{
				f->fld_dtype = dtype_double;
				f->fld_length = sizeof(double);
			}
		}
		return;

	case nod_agg_total:
	case nod_total:
		if (node->nod_type == nod_total)
			CME_get_dtype(node->nod_arg[1], f);
		else
			CME_get_dtype(node->nod_arg[0], f);
		if (sw_sql_dialect == SQL_DIALECT_V5)
		{
			if ((f->fld_dtype == dtype_short) || (f->fld_dtype == dtype_long))
			{
				f->fld_dtype = dtype_long;
				f->fld_length = sizeof(SLONG);
			}
			else
			{
				f->fld_precision = 0;
				f->fld_scale = 0;
				f->fld_dtype = dtype_double;
				f->fld_length = sizeof(double);
			}
		}
		else
		{
// **  Dialect is 2 or 3 *

			if (DTYPE_IS_EXACT(f->fld_dtype))
			{
				f->fld_dtype = dtype_int64;
				f->fld_length = sizeof(SINT64);
			}
			else
			{
				f->fld_dtype = dtype_double;
				f->fld_length = sizeof(double);
			}
		}
		return;

	case nod_literal:
		reference = (REF) node->nod_arg[0];
		string = reference->ref_value;
		if (*string != '"' && *string != '\'')
		{
			/* Value didn't start with a quotemark - must be a numeric
			   that we stuffed away as a string during the parse */
			if (strpbrk(string, "Ee"))
			{
				f->fld_dtype = dtype_double;
				f->fld_length = sizeof(double);
			}
			else
			{
				int scale;

				const char* s_ptr = string;

			/** Get the scale **/
				const char* ptr = strpbrk(string, ".");
				if (!ptr)
					scale = 0;
				else
				{
					scale = (string + (strlen(string) - 1)) - ptr;
					scale = -scale;
				}

			/** Get rid of the decimal point **/
				UINT64 uint64_val = 0;
				while (*s_ptr)
				{
					if (*s_ptr != '.')
						uint64_val = (uint64_val * 10) + (*s_ptr - '0');
					s_ptr++;
				}

				if (uint64_val <= MAX_SLONG)
				{
					f->fld_dtype = dtype_long;
					f->fld_scale = scale;
					f->fld_length = sizeof(SLONG);
				}
				else
				{
					f->fld_dtype = dtype_int64;
					f->fld_scale = scale;
					f->fld_length = sizeof(SINT64);
				}
			}
		}
		else
		{
			/* Did the reference include a character set specification? */

			if (reference->ref_flags & REF_ttype)
				f->fld_ttype = reference->ref_ttype;

			if (reference->ref_flags & REF_sql_date)
			{
				f->fld_dtype = dtype_sql_date;
				f->fld_length = sizeof(ISC_DATE);
			}
			else if (reference->ref_flags & REF_sql_time)
			{
				f->fld_dtype = dtype_sql_time;
				f->fld_length = sizeof(ISC_TIME);
			}
			else if (reference->ref_flags & REF_timestamp)
			{
				f->fld_dtype = dtype_timestamp;
				f->fld_length = sizeof(ISC_TIMESTAMP);
			}
			else
			{
				/* subtract 2 for starting & terminating quote */

				f->fld_length = strlen(string) - 2;
				if (sw_cstring)
				{
					/* add 1 back for the NULL byte */

					f->fld_length += 1;
					f->fld_dtype = dtype_cstring;
				}
				else
					f->fld_dtype = dtype_text;
			}
		}
		return;

	case nod_user_name:
		f->fld_dtype = dtype_text;
		f->fld_length = USER_LENGTH;
		f->fld_ttype = ttype_ascii;
		f->fld_charset_id = CS_ASCII;
		return;

	case nod_udf:
		a_udf = (udf*) node->nod_arg[1];
		f->fld_dtype = a_udf->udf_dtype;
		f->fld_length = a_udf->udf_length;
		f->fld_scale = a_udf->udf_scale;
		f->fld_sub_type = a_udf->udf_sub_type;
		f->fld_ttype = a_udf->udf_ttype;
		f->fld_charset_id = a_udf->udf_charset_id;
		return;

	case nod_cast:
		CME_get_dtype(node->nod_arg[0], &field1);
		tmp_field = (GPRE_FLD) node->nod_arg[1];
		assign_dtype(f, tmp_field);
		if (f->fld_length == 0)
			f->fld_length = field1.fld_length;
		return;

	case nod_upcase:
		CME_get_dtype(node->nod_arg[0], f);
		if (f->fld_dtype <= dtype_any_text)
			return;

		/* User has specified UPPER(5) - while silly, we'll cast
		   the value into a string, and upcase it anyway */

		f->fld_length = get_string_len(f) + sizeof(USHORT);
		f->fld_dtype = dtype_varying;
		f->fld_ttype = ttype_ascii;
		f->fld_charset_id = CS_ASCII;
		return;

	default:
		CPR_error("CME_get_dtype: node type not supported");
	}
}


//____________________________________________________________
//  
//		Generate a relation reference.
//  

void CME_relation(GPRE_CTX context, GPRE_REQ request)
{
	GPRE_PRC procedure;

	CMP_check(request, 0);

	GPRE_RSE rs_stream = context->ctx_stream;
	if (rs_stream)
	{
		CME_rse(rs_stream, request);
		return;
	}

	GPRE_REL relation = context->ctx_relation;
	if (relation)
	{
		if (sw_ids)
		{
			if ((context->ctx_alias) &&
				!(request->req_database->dbb_flags & DBB_v3))
			{
				STUFF(blr_rid2);
			}
			else
			{
				STUFF(blr_rid);
			}
			STUFF_WORD(relation->rel_id);
		}
		else
		{
			if ((context->ctx_alias) &&
				!(request->req_database->dbb_flags & DBB_v3))
					STUFF(blr_relation2);
			else
				STUFF(blr_relation);
			CMP_stuff_symbol(request, relation->rel_symbol);
		}

		if ((context->ctx_alias) &&
			!(request->req_database->dbb_flags & DBB_v3))
				stuff_cstring(request, context->ctx_alias);
		STUFF(context->ctx_internal);
	}
	else if (procedure = context->ctx_procedure)
	{
		if (sw_ids)
		{
			STUFF(blr_pid);
			STUFF_WORD(procedure->prc_id);
		}
		else
		{
			STUFF(blr_procedure);
			CMP_stuff_symbol(request, procedure->prc_symbol);
		}
		STUFF(context->ctx_internal);
		STUFF_WORD(procedure->prc_in_count);
		gpre_nod* inputs = context->ctx_prc_inputs;
		if (inputs) {
			gpre_nod** ptr = inputs->nod_arg;
			for (gpre_nod** const end = ptr + inputs->nod_count;
				 ptr < end; ptr++)
			{
				CME_expr(*ptr, request);
			}
		}
	}
}


//____________________________________________________________
//  
//		Generate blr for an rse node.
//  

void CME_rse(gpre_rse* selection, GPRE_REQ request)
{
	GPRE_NOD *ptr, *end;
	GPRE_RSE sub_rse;
	SSHORT i;

	if (selection->rse_join_type == (NOD_T) 0)
	{
		if ((selection->rse_flags & RSE_singleton) &&
			!(request->req_database->dbb_flags & DBB_v3))
		{
			STUFF(blr_singular);
		}
		STUFF(blr_rse);
	}
	else
		STUFF(blr_rs_stream);

//  Process unions, if any, otherwise process relations 

	gpre_nod* union_node = selection->rse_union;
	if (union_node)
	{
		STUFF(1);
		STUFF(blr_union);
		STUFF(selection->rse_context[0]->ctx_internal);
		STUFF(union_node->nod_count);
		ptr = union_node->nod_arg;
		for (end = ptr + union_node->nod_count; ptr < end; ptr++)
		{
			sub_rse = (GPRE_RSE) * ptr;
			CME_rse(sub_rse, request);
			cmp_map(sub_rse->rse_map, request);
		}
	}
	else if (sub_rse = selection->rse_aggregate)
	{
		STUFF(1);
		STUFF(blr_aggregate);
		STUFF(sub_rse->rse_map->map_context->ctx_internal);
		CME_rse(sub_rse, request);
		STUFF(blr_group_by);
		gpre_nod* list = sub_rse->rse_group_by;
		if (list)
		{
			STUFF(list->nod_count);
			ptr = list->nod_arg;
			for (end = ptr + list->nod_count; ptr < end; ptr++)
				CME_expr(*ptr, request);
		}
		else
			STUFF(0);
		cmp_map(sub_rse->rse_map, request);
	}
	else
	{
		STUFF(selection->rse_count);
		for (i = 0; i < selection->rse_count; i++)
			CME_relation(selection->rse_context[i], request);
	}

//  Process the clauses present 

	if (selection->rse_first)
	{
		STUFF(blr_first);
		CME_expr(selection->rse_first, request);
	}

	if (selection->rse_boolean)
	{
		STUFF(blr_boolean);
		CME_expr(selection->rse_boolean, request);
	}

	gpre_nod* temp = selection->rse_sort;
	if (temp)
	{
		STUFF(blr_sort);
		STUFF(temp->nod_count);
		ptr = temp->nod_arg;
		for (i = 0; i < temp->nod_count; i++)
		{
			STUFF((*ptr++) ? blr_descending : blr_ascending);
			CME_expr(*ptr++, request);
		}
	}

	if (temp = selection->rse_reduced)
	{
		STUFF(blr_project);
		STUFF(temp->nod_count);
		ptr = temp->nod_arg;
		for (i = 0; i < temp->nod_count; i++)
			CME_expr(*ptr++, request);
	}

	if (temp = selection->rse_plan)
	{
		STUFF(blr_plan);
		cmp_plan(temp, request);
	}

	if (selection->rse_join_type != (NOD_T) 0
		&& selection->rse_join_type != nod_join_inner)
	{
		STUFF(blr_join_type);
		if (selection->rse_join_type == nod_join_left)
			STUFF(blr_left);
		else if (selection->rse_join_type == nod_join_right)
			STUFF(blr_right);
		else
			STUFF(blr_full);
	}

#ifdef SCROLLABLE_CURSORS
//  generate a statement to be executed if the user scrolls 
//  in a direction other than forward; a message is sent outside 
//  the normal send/receive protocol to specify the direction 
//  and offset to scroll; note that we do this only on a SELECT 
//  type statement and only when talking to a 4.1 engine or greater 

	if (request->req_flags & REQ_sql_cursor &&
		request->req_database->dbb_base_level >= 5)
	{
		STUFF(blr_receive);
		STUFF(request->req_aport->por_msg_number);
		STUFF(blr_seek);
		STUFF(blr_parameter);
		STUFF(request->req_aport->por_msg_number);
		STUFF_WORD(1);
		STUFF(blr_parameter);
		STUFF(request->req_aport->por_msg_number);
		STUFF_WORD(0);
	}
#endif

//  Finish up by making a BLR_END 

	STUFF(blr_end);
}


//____________________________________________________________
//  
//		Compile up an array reference putting
//       out sdl (slice description language)
//  

static GPRE_NOD cmp_array( GPRE_NOD node, GPRE_REQ request)
{
	CMP_check(request, 0);

	ref* reference = (REF) node->nod_arg[0];

	if (!reference->ref_context)
	{
		CPR_error("cmp_array: context missing");
		return NULL;
	}

	gpre_fld* field = reference->ref_field;
	if (!field)
	{
		CPR_error("cmp_array: field missing");
		return NULL;
	}
	else
	{
		/*  Header stuff  */

		reference->ref_sdl = reference->ref_sdl_base = 
			reinterpret_cast<UCHAR*>(MSC_alloc(500));
		reference->ref_sdl_length = 500;
		reference->ref_sdl_ident = CMP_next_ident();
		STUFF_SDL(isc_sdl_version1);
		STUFF_SDL(isc_sdl_struct);
		STUFF_SDL(1);

		/*  The datatype of the array elements  */

		cmp_sdl_dtype(field->fld_array, reference);

		/*  The relation and field identifiers or strings  */

		if (sw_ids)
		{
			STUFF_SDL(isc_sdl_rid);
			STUFF_SDL(reference->ref_id);
			STUFF_SDL(isc_sdl_fid);
			STUFF_SDL(field->fld_id);
		}
		else
		{
			STUFF_SDL(isc_sdl_relation);
			STUFF_SDL(strlen(field->fld_relation->rel_symbol->sym_string));
			const TEXT* p;
			for (p = field->fld_relation->rel_symbol->sym_string; *p; p++)
				STUFF_SDL(*p);
			STUFF_SDL(isc_sdl_field);
			STUFF_SDL(strlen(field->fld_symbol->sym_string));
			for (p = field->fld_symbol->sym_string; *p; p++)
				STUFF_SDL(*p);
		}

		/*  The loops for the dimensions  */

		stuff_sdl_loops(reference, field);

		/*  The array element and its "subscripts" */

		stuff_sdl_element(reference, field);

		STUFF_SDL(isc_sdl_eoc);
	}

	reference->ref_sdl_length = reference->ref_sdl - reference->ref_sdl_base;
	reference->ref_sdl = reference->ref_sdl_base;

	if (debug_on)
		PRETTY_print_sdl(reference->ref_sdl, 0, 0, 0);

	return node;
}


//____________________________________________________________
//  
//		Compile up a subscripted array reference
//       from an GPRE_RSE and output blr for this reference
//  

static GPRE_NOD cmp_array_element( GPRE_NOD node, GPRE_REQ request)
{
	STUFF(blr_index);

	cmp_field(node, request);

	STUFF(node->nod_count - 1);

	for (USHORT index_count = 1; index_count < node->nod_count; index_count++)
		CME_expr(node->nod_arg[index_count], request);

	return node;
}


//____________________________________________________________
//  
//  

static void cmp_cast( GPRE_NOD node, GPRE_REQ request)
{

	STUFF(blr_cast);
	CMP_external_field(request, (GPRE_FLD) node->nod_arg[1]);
	CME_expr(node->nod_arg[0], request);
}


//____________________________________________________________
//  
//		Compile up a field reference.
//  

static GPRE_NOD cmp_field( GPRE_NOD node, GPRE_REQ request)
{
	CMP_check(request, 0);

	ref* reference = (REF) node->nod_arg[0];
	if (!reference)
	{
		CPR_error("cmp_field: reference missing");
		return NULL;
	}

	gpre_ctx* context = reference->ref_context;
	if (!context)
	{
		CPR_error("cmp_field: context missing");
		return NULL;
	}

	gpre_fld* field = reference->ref_field;
	if (!field)
	{
		CPR_error("cmp_field: field missing");
		return NULL;
	}

	if (!field)
		ib_puts("cmp_field: symbol missing");

	if (field->fld_flags & FLD_dbkey)
	{
		STUFF(blr_dbkey);
		STUFF(context->ctx_internal);
	}
	else if (reference->ref_flags & REF_union)
	{
		STUFF(blr_fid);
		STUFF(context->ctx_internal);
		STUFF_WORD(reference->ref_id);
	}
	else if (sw_ids)
	{
		STUFF(blr_fid);
		STUFF(context->ctx_internal);
		STUFF_WORD(field->fld_id);
	}
	else
	{
		STUFF(blr_field);
		STUFF(context->ctx_internal);
		CMP_stuff_symbol(request, field->fld_symbol);
	}

	return node;
}


//____________________________________________________________
//  
//		Handle a literal expression.
//  

static GPRE_NOD cmp_literal( GPRE_NOD node, GPRE_REQ request)
{
	bool negate = false;

	if (node->nod_type == nod_negate)
	{
		node = node->nod_arg[0];
		negate = true;
	}

	STUFF(blr_literal);
	const ref* reference = (REF) node->nod_arg[0];
	const char* string = (char *) reference->ref_value;

	if (*string != '"' && *string != '\'')
	{
	/** If the numeric string contains an 'E' or 'e' in it
	then the datatype is double.
    **/
		if (strpbrk(string, "Ee"))
		{
			string = (char *) reference->ref_value;

			if (!(request->req_database->dbb_flags & DBB_v3))
				STUFF(blr_double);
			else if (sw_know_interp)
			{	/* then must be using blr_version5 */
				STUFF(blr_text2);
				STUFF_WORD(ttype_ascii);
			}
			else
				STUFF(blr_text);

			STUFF_WORD(strlen(string));
			while (*string)
				STUFF(*string++);
		}
		else
		{
	/** The numeric string doesn't contain 'E' or 'e' in it.
	    Then this must be a scaled int.  Figure out if there
	    is a '.' in it and calculate its scale.
	**/
			const char* s_ptr = string;

	/** Get the scale **/
			int scale;
			const char* ptr = strpbrk(string, ".");
			if (!ptr)
		/**  No '.' ?, Scale is 0 **/
				scale = 0;
			else
			{
		/** Aha!, there is a '.'. find the scale **/
				scale = (string + (strlen(string) - 1)) - ptr;
				scale = -scale;
			}

			UINT64 uint64_val = 0;
			while (*s_ptr)
			{
				if (*s_ptr != '.')
					uint64_val = (uint64_val * 10) + (*s_ptr - '0');
				s_ptr++;
			}

	/** see if we can fit the value in a long or INT64.  **/
			if ((uint64_val <= MAX_SLONG) ||
				((uint64_val == (MAX_SLONG + (UINT64) 1))
				 && (negate == true)))
			{
				long long_val;
				if (negate == true)
					long_val = -((long) uint64_val);
				else
					long_val = (long) uint64_val;
				STUFF(blr_long);
				STUFF(scale);	/* scale factor */
				STUFF_WORD(long_val);
				STUFF_WORD(long_val >> 16);
			}
			else if ((uint64_val <= MAX_SINT64) ||
					 ((uint64_val == ((UINT64) MAX_SINT64 + 1))
					  && (negate == true)))
			{
				SINT64 sint64_val;
				if (negate == true)
					sint64_val = -((SINT64) uint64_val);
				else
					sint64_val = (SINT64) uint64_val;
				STUFF(blr_int64);
				STUFF(scale);	/* scale factor */
				STUFF_WORD(sint64_val);
				STUFF_WORD(sint64_val >> 16);
				STUFF_WORD(sint64_val >> 32);
				STUFF_WORD(sint64_val >> 48);
			}
			else
				CPR_error("cmp_literal : Numeric Value too big");

		}
	}
	else
	{
		/* Remove surrounding quotes from string, etc. */
		char buffer[MAXSYMLEN];
		char* p = buffer;

		/* Skip introducing quote mark */
		if (*string)
			string++;

		while (*string)
			*p++ = *string++;

		/* Zap out terminating quote mark */
		*--p = 0;
		const SSHORT length = p - buffer;

		dsc from;
		from.dsc_sub_type = ttype_ascii;
		from.dsc_flags = 0;
		from.dsc_dtype = dtype_text;
		from.dsc_length = length;
		from.dsc_address = (UCHAR *) buffer;
		dsc to;
		to.dsc_sub_type = 0;
		to.dsc_flags = 0;
		if (reference->ref_flags & REF_sql_date)
		{
			ISC_DATE dt;
			STUFF(blr_sql_date);
			to.dsc_dtype = dtype_sql_date;
			to.dsc_length = sizeof(ISC_DATE);
			to.dsc_address = (UCHAR *) & dt;
			MOVG_move(&from, &to);
			STUFF_WORD(dt);
			STUFF_WORD(dt >> 16);
			return node;
		}
		else if (reference->ref_flags & REF_timestamp)
		{
			ISC_TIMESTAMP ts;
			STUFF(blr_timestamp);
			to.dsc_dtype = dtype_timestamp;
			to.dsc_length = sizeof(ISC_TIMESTAMP);
			to.dsc_address = (UCHAR *) & ts;
			MOVG_move(&from, &to);
			STUFF_WORD(ts.timestamp_date);
			STUFF_WORD(ts.timestamp_date >> 16);
			STUFF_WORD(ts.timestamp_time);
			STUFF_WORD(ts.timestamp_time >> 16);
			return node;
		}
		else if (reference->ref_flags & REF_sql_time)
		{
			ISC_TIME itim;
			STUFF(blr_sql_time);
			to.dsc_dtype = dtype_sql_time;
			to.dsc_length = sizeof(ISC_DATE);
			to.dsc_address = (UCHAR *) & itim;
			MOVG_move(&from, &to);
			STUFF_WORD(itim);
			STUFF_WORD(itim >> 16);
			return node;
		}
		else if (!(reference->ref_flags & REF_ttype))
			STUFF(blr_text);
		else
		{
			STUFF(blr_text2);
			STUFF_WORD(reference->ref_ttype);
		}

		STUFF_WORD(length);
		for (string = buffer; *string;)
			STUFF(*string++);
	}

	return node;
}


//____________________________________________________________
//  
//		Generate a map for a union or aggregate rse.
//  

static void cmp_map(map* a_map, GPRE_REQ request)
{
	STUFF(blr_map);
	STUFF_WORD(a_map->map_count);

	for (MEL element = a_map->map_elements; element; element = element->mel_next)
	{
		STUFF_WORD(element->mel_position);
		CME_expr(element->mel_expr, request);
	}
}


//____________________________________________________________
//  
//		Generate an access plan for a query.
//  

static void cmp_plan(const gpre_nod* plan_expression, GPRE_REQ request)
{
//  stuff the join type 

	const gpre_nod* list = plan_expression->nod_arg[1];
	if (list->nod_count > 1)
	{
		const gpre_nod* node = plan_expression->nod_arg[0];
		if (node)
			STUFF(blr_merge);
		else
			STUFF(blr_join);
		STUFF(list->nod_count);
	}

//  stuff one or more plan items 

	gpre_nod* const* ptr = list->nod_arg;
	for (gpre_nod* const* const end = ptr + list->nod_count; ptr < end; ptr++)
	{
		GPRE_NOD node = *ptr;
		if (node->nod_type == nod_plan_expr)
		{
			cmp_plan(node, request);
			continue;
		}

		/* if we're here, it must be a nod_plan_item */

		STUFF(blr_retrieve);

		/* stuff the relation--the relation id itself is redundant except 
		   when there is a need to differentiate the base tables of views */

		CME_relation((GPRE_CTX) node->nod_arg[2], request);

		/* now stuff the access method for this stream */

		const gpre_nod* arg = node->nod_arg[1];
		switch (arg->nod_type)
		{
		case nod_natural:
			STUFF(blr_sequential);
			break;

		case nod_index_order:
			STUFF(blr_navigational);
			stuff_cstring(request, (TEXT*) arg->nod_arg[0]);
			break;

		case nod_index:
			{
				STUFF(blr_indices);
				arg = arg->nod_arg[0];
				STUFF(arg->nod_count);
				const gpre_nod* const* ptr2 = arg->nod_arg;
				for (const gpre_nod* const* const end2 = ptr2 + arg->nod_count;
					 ptr2 < end2; ptr2++)
				{
					stuff_cstring(request, (TEXT*) *ptr2);
				}
				break;
			}
		}
	}
}


//____________________________________________________________
//  
//		Print out the correct blr for
//       this datatype.
//  

static void cmp_sdl_dtype( GPRE_FLD field, REF reference)
{
	TEXT s[50];

	switch (field->fld_dtype)
	{
	case dtype_cstring:
		/* 3.2j has new, tagged blr intruction for cstring */

		if (sw_know_interp)
		{
			STUFF_SDL(blr_cstring2);
			STUFF_SDL_WORD(sw_interp);
			STUFF_SDL_WORD(field->fld_length);
		}
		else
		{
			STUFF_SDL(blr_cstring);
			STUFF_SDL_WORD(field->fld_length);
		}
		break;

	case dtype_text:
		/* 3.2j has new, tagged blr intruction for text too */

		if (sw_know_interp)
		{
			STUFF_SDL(blr_text2);
			STUFF_SDL_WORD(sw_interp);
			STUFF_SDL_WORD(field->fld_length);
		}
		else
		{
			STUFF_SDL(blr_text);
			STUFF_SDL_WORD(field->fld_length);
		}
		break;

	case dtype_varying:
		/* 3.2j has new, tagged blr intruction for varying also */

		if (sw_know_interp)
		{
			STUFF_SDL(blr_varying2);
			STUFF_SDL_WORD(sw_interp);
			STUFF_SDL_WORD(field->fld_length);
		}
		else
		{
			STUFF_SDL(blr_varying);
			STUFF_SDL_WORD(field->fld_length);
		}
		break;

	case dtype_short:
		STUFF_SDL(blr_short);
		STUFF_SDL(field->fld_scale);
		break;

	case dtype_long:
		STUFF_SDL(blr_long);
		STUFF_SDL(field->fld_scale);
		break;

	case dtype_quad:
		STUFF_SDL(blr_quad);
		STUFF_SDL(field->fld_scale);
		break;

// ** Begin date/time/timestamp support *
	case dtype_sql_date:
		STUFF_SDL(blr_sql_date);
		break;
	case dtype_sql_time:
		STUFF_SDL(blr_sql_time);
		break;

	case dtype_timestamp:
		STUFF_SDL(blr_timestamp);
		break;
// ** End date/time/timestamp support *

	case dtype_int64:
		STUFF_SDL(blr_int64);
		break;

	case dtype_real:
		STUFF_SDL(blr_float);
		break;

	case dtype_double:
		if (sw_d_float)
			STUFF_SDL(blr_d_float);
		else
			STUFF_SDL(blr_double);
		break;

	default:
		sprintf(s, "datatype %d not understood", field->fld_dtype);
		CPR_error(s);
	}
}


//____________________________________________________________
//  
//		Compile a reference to a user defined function.
//  

static GPRE_NOD cmp_udf( GPRE_NOD node, GPRE_REQ request)
{
	const udf* an_udf = (udf*) node->nod_arg[1];
	STUFF(blr_function);
	const TEXT* p = an_udf->udf_function;
	STUFF(strlen(p));

	while (*p)
		STUFF(*p++);

	gpre_nod* list = node->nod_arg[0];
	if (list)
	{
		STUFF(list->nod_count);

		gpre_nod** ptr = list->nod_arg;
		for (gpre_nod** const end = ptr + list->nod_count;
			ptr < end; ++ptr)
		{
			CME_expr(*ptr, request);
		}
	}
	else
		STUFF(0);

	return node;
}


//____________________________________________________________
//  
//		Process a random value expression.
//  

static GPRE_NOD cmp_value( GPRE_NOD node, GPRE_REQ request)
{
	ref* reference = (REF) node->nod_arg[0];

	if (!reference)
		ib_puts("cmp_value: missing reference");

	if (!reference->ref_port)
		ib_puts("cmp_value: port missing");

	ref* flag = reference->ref_null;
	if (flag)
	{
		STUFF(blr_parameter2);
		STUFF(reference->ref_port->por_msg_number);
		STUFF_WORD(reference->ref_parameter);
		STUFF_WORD(flag->ref_parameter);
	}
	else
	{
		STUFF(blr_parameter);
		STUFF(reference->ref_port->por_msg_number);
		STUFF_WORD(reference->ref_parameter);
	}

	return node;
}


//____________________________________________________________
//  
//		Figure out a text length from a datatype and a length
//  

static USHORT get_string_len( GPRE_FLD field)
{
	fb_assert(field->fld_dtype <= MAX_UCHAR);

	dsc tmp_dsc;
	tmp_dsc.dsc_dtype = (UCHAR) field->fld_dtype;
	tmp_dsc.dsc_length = field->fld_length;
	tmp_dsc.dsc_scale = 0;
	tmp_dsc.dsc_sub_type = 0;
	tmp_dsc.dsc_flags = 0;
	tmp_dsc.dsc_address = NULL;

	return DSC_string_length(&tmp_dsc);
}


//____________________________________________________________
//  
//		Write out a null-terminated 
//		string with one byte of length.
//  

static void stuff_cstring( GPRE_REQ request, const char *string)
{
	STUFF(strlen(string));

	UCHAR c;
	while (c = *string++) {
		STUFF(c);
	}
}


//____________________________________________________________
//  
//		Write to the sdl string, the do
//       loop for a particular dimension.
//  

static void stuff_sdl_dimension(const dim* dimension,
								ref* reference, SSHORT dimension_count)
{

//   In the future, when we support slices, new code to handle the
//   user-defined slice ranges will be here.  

	if (dimension->dim_lower == 1)
	{
		STUFF_SDL(isc_sdl_do1);
		STUFF_SDL(dimension_count);
		stuff_sdl_number(dimension->dim_upper, reference);
	}
	else
	{
		STUFF_SDL(isc_sdl_do2);
		STUFF_SDL(dimension_count);
		stuff_sdl_number(dimension->dim_lower, reference);
		stuff_sdl_number(dimension->dim_upper, reference);
	}
}


//____________________________________________________________
//  
//		Write the element information
//       (including the subscripts) to
//       the SDL string for the array.
//  

static void stuff_sdl_element(ref* reference, const gpre_fld* field)
{
	SSHORT i;

	STUFF_SDL(isc_sdl_element);
	STUFF_SDL(1);
	STUFF_SDL(isc_sdl_scalar);
	STUFF_SDL(0);

	STUFF_SDL(field->fld_array_info->ary_dimension_count);

//  Fortran needs the array in column-major order 

	if (sw_language == lang_fortran)
	{
		for (i = field->fld_array_info->ary_dimension_count - 1; i >= 0; i--)
		{
			STUFF_SDL(isc_sdl_variable);
			STUFF_SDL(i);
		}
	}
	else
	{
		for (i = 0; i < field->fld_array_info->ary_dimension_count; i++)
		{
			STUFF_SDL(isc_sdl_variable);
			STUFF_SDL(i);
		}
	}
}


//____________________________________________________________
//  
//		Write loop information to the SDL
//       string for the array dimensions.
//  

static void stuff_sdl_loops(ref* reference, const gpre_fld* field)
{
	SSHORT i;
	const dim* dimension;

//  Fortran needs the array in column-major order 

	if (sw_language == lang_fortran)
	{
		for (dimension = field->fld_array_info->ary_dimension;
			 dimension->dim_next; dimension = dimension->dim_next);

		for (i = 0; i < field->fld_array_info->ary_dimension_count;
			 i++, dimension = dimension->dim_previous)
			stuff_sdl_dimension(dimension, reference, i);
	}
	else {
		for (i = 0, dimension = field->fld_array_info->ary_dimension;
			 i < field->fld_array_info->ary_dimension_count;
			 i++, dimension = dimension->dim_next)
		{
				stuff_sdl_dimension(dimension, reference, i);
		}
	}
}


//____________________________________________________________
//  
//		Write the number in the 'smallest'
//       form possible to the SDL string.
//  

static void stuff_sdl_number(const SLONG number, REF reference)
{

	if ((number > -16) && (number < 15))
	{
		STUFF_SDL(isc_sdl_tiny_integer);
		STUFF_SDL(number);
	}
	else if ((number > -32768) && (number < 32767))
	{
		STUFF_SDL(isc_sdl_short_integer);
		STUFF_SDL_WORD(number);
	}
	else
	{
		STUFF_SDL(isc_sdl_long_integer);
		STUFF_SDL_LONG(number);
	}
}
