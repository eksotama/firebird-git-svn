/*
 *	PROGRAM:	Dynamic SQL runtime support
 *	MODULE:		utld.cpp
 *	DESCRIPTION:	Utility routines for DSQL
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
 * 21 Nov 01 - Ann Harrison - Turn off the code in parse_sqlda that 
 *    decides that two statements are the same based on their message
 *    descriptions because it misleads some code in remote/interface.c
 *    and causes problems when two statements are prepared.
 *
 * 2002.10.29 Sean Leyne - Removed obsolete "Netware" port
 *
 */

/*
$Id: utld.cpp,v 1.25 2004-01-04 14:12:40 dimitr Exp $
*/

#include "firebird.h"
#include "../jrd/ib_stdio.h"
#include <string.h>
#include "../dsql/dsql.h"
#include "../dsql/sqlda.h"
#include "../jrd/blr.h"
#include "../jrd/ibase.h"
#include "../jrd/align.h"
#include "../dsql/utld_proto.h"
#include "../jrd/gds_proto.h"


static void cleanup(void *);
static ISC_STATUS error_dsql_804(ISC_STATUS *, ISC_STATUS);
static SLONG get_numeric_info(SCHAR **);
static SLONG get_string_info(SCHAR **, SCHAR *, int);
#ifdef NOT_USED_OR_REPLACED
#ifdef DEV_BUILD
static void print_xsqlda(XSQLDA *);
#endif
#endif
static void sqlvar_to_xsqlvar(SQLVAR *, XSQLVAR *);
static void xsqlvar_to_sqlvar(XSQLVAR *, SQLVAR *);

static inline void ch_stuff(BLOB_PTR*& p, const SCHAR value, bool& same_flag)
{
	if (*p == value) 
		p++; 
	else {
		*p++ = value;
		same_flag = false;
	}
}

static inline void ch_stuff_word(BLOB_PTR*& p, const SSHORT value, bool& same_flag)
{
	ch_stuff(p, value & 255, same_flag);
	ch_stuff(p, value >> 8, same_flag);
}

/* these statics define a round-robin data area for storing
   textual error messages returned to the user */

static TEXT *DSQL_failures, *DSQL_failures_ptr;

const int  DSQL_FAILURE_SPACE = 2048;


/**
  
 	UTLD_parse_sql_info
  
    @brief	Fill in an SQLDA using data returned
 	by a call to isc_dsql_sql_info.
 

    @param status
    @param dialect
    @param info
    @param xsqlda
    @param return_index

 **/
ISC_STATUS	UTLD_parse_sql_info(
				ISC_STATUS* status,
				USHORT dialect,
				SCHAR* info,
				XSQLDA* xsqlda,
				USHORT* return_index)
{
	XSQLVAR *xvar, xsqlvar;
	SQLDA *sqlda;
	SQLVAR *var;
	USHORT last_index = 0;

	if (return_index)
		*return_index = 0;

	if (!xsqlda)
		return 0;

/* The first byte of the returned buffer is assumed to be either a
   isc_info_sql_select or isc_info_sql_bind item.  The second byte
   is assumed to be isc_info_sql_describe_vars. */

	info += 2;

	SSHORT n = static_cast<SSHORT> (get_numeric_info(&info));
	if (dialect >= DIALECT_xsqlda)
	{
		if (xsqlda->version != SQLDA_VERSION1)
			return error_dsql_804(status, isc_dsql_sqlda_err);
		xsqlda->sqld = n;

		// If necessary, inform the application that more sqlda items are needed 
		if (xsqlda->sqld > xsqlda->sqln)
			return 0;
	}
	else
	{
		sqlda = (SQLDA *) xsqlda;
		sqlda->sqld = n;

		// If necessary, inform the application that more sqlda items are needed 
		if (sqlda->sqld > sqlda->sqln)
			return 0;

		xsqlda = NULL;
		xvar = &xsqlvar;
	}

// Loop over the variables being described.

	USHORT index = 0;
	while (*info != isc_info_end)
	{
	   SCHAR item;
	   while ((item = *info++) != isc_info_sql_describe_end)
			switch (item)
			{
			case isc_info_sql_sqlda_seq:
				index = static_cast<USHORT>(get_numeric_info(&info));
				if (xsqlda)
					xvar = xsqlda->sqlvar + index - 1;
				else
				{
					var = sqlda->sqlvar + index - 1;
					memset(xvar, 0, sizeof(XSQLVAR));
				}
				break;

			case isc_info_sql_type:
				xvar->sqltype =
					static_cast<SSHORT>(get_numeric_info(&info));
				break;

			case isc_info_sql_sub_type:
				xvar->sqlsubtype =
					static_cast<SSHORT>(get_numeric_info(&info));
				break;

			case isc_info_sql_scale:
				xvar->sqlscale =
					static_cast<SSHORT>(get_numeric_info(&info));
				break;

			case isc_info_sql_length:
				xvar->sqllen =
					static_cast<SSHORT>(get_numeric_info(&info));
				break;

			case isc_info_sql_field:
				xvar->sqlname_length =
					static_cast<SSHORT>(get_string_info(&info, xvar->sqlname, sizeof(xvar->sqlname)));
				break;

			case isc_info_sql_relation:
				xvar->relname_length =
					static_cast<SSHORT>(get_string_info(&info, xvar->relname, sizeof(xvar->relname)));
				break;

			case isc_info_sql_owner:
				xvar->ownname_length =
					static_cast<SSHORT>(get_string_info(&info, xvar->ownname, sizeof(xvar->ownname)));
				break;

			case isc_info_sql_alias:
				xvar->aliasname_length =
					static_cast<SSHORT>
					(get_string_info
					 (&info, xvar->aliasname, sizeof(xvar->aliasname)));
				break;

			case isc_info_truncated:
				if (return_index)
					*return_index = last_index;

			default:
				return error_dsql_804(status, isc_dsql_sqlda_err);
			}

		if (!xsqlda)
			xsqlvar_to_sqlvar(xvar, var);

		if (index > last_index)
			last_index = index;
	}

	if (*info != isc_info_end)
		return error_dsql_804(status, isc_dsql_sqlda_err);

	return FB_SUCCESS;
}


/**
  
 	UTLD_parse_sqlda
  
    @brief	This routine creates a blr message that describes
 	a SQLDA as well as moving data from the SQLDA
 	into a message buffer or from the message buffer
 	into the SQLDA.
 

    @param status
    @param dasup
    @param blr_length
    @param msg_type
    @param msg_length
    @param dialect
    @param xsqlda
    @param clause

 **/
ISC_STATUS	UTLD_parse_sqlda(
				ISC_STATUS* status,
				DASUP dasup,
				USHORT* blr_length,
				USHORT* msg_type,
				USHORT* msg_length,
				USHORT dialect,
				XSQLDA* xsqlda,
				USHORT clause)
{
	USHORT i, n, blr_len, par_count, dtype, msg_len, len, align,
		null_offset;
	XSQLVAR *xvar, xsqlvar;
	SQLDA *sqlda;
	SQLVAR *var;
	//BLOB_PTR *p;				// one huge pointer per line for LIBS 

	if (!xsqlda)
		n = 0;
	else
		if (dialect >= DIALECT_xsqlda)
		{
			if (xsqlda->version != SQLDA_VERSION1)
				return error_dsql_804(status, isc_dsql_sqlda_err);
			n = xsqlda->sqld;
		}
		else
		{
			sqlda = (SQLDA *) xsqlda;
			n = sqlda->sqld;
			xsqlda = NULL;
			xvar = &xsqlvar;
		}

	if (!n)
	{
		// If there isn't an SQLDA, don't bother with anything else. 

		if (blr_length)
			*blr_length = dasup->dasup_clauses[clause].dasup_blr_length = 0;
		if (msg_length)
			*msg_length = 0;
		if (msg_type)
			*msg_type = 0;
		return 0;
	}

	if (msg_length)
	{
		/* This is a call from execute or open, or the first call from fetch.
		   Determine the size of the blr, allocate a blr buffer, create the
		   blr, and finally allocate a message buffer. */

		/* The message buffer we are describing could be for a message to
		   either IB 4.0 or IB 3.3 - thus we need to describe the buffer
		   with the right set of blr.
		   As the BLR is only used to describe space allocation and alignment,
		   we can safely use blr_text for both 4.0 and 3.3. */

		/* Make a quick pass through the SQLDA to determine the amount of
		   blr that will be generated. */

		blr_len = 8;
		par_count = 0;
		if (xsqlda)
			xvar = xsqlda->sqlvar - 1;
		else
			var = sqlda->sqlvar - 1;
		for (i = 0; i < n; i++)
		{
			if (xsqlda)
				xvar++;
			else {
				var++;
				sqlvar_to_xsqlvar(var, xvar);
			}
			dtype = xvar->sqltype & ~1;
			if (dtype == SQL_VARYING || dtype == SQL_TEXT)
				blr_len += 3;
			else
				if (dtype == SQL_SHORT ||
					dtype == SQL_LONG ||
					dtype == SQL_INT64 ||
					dtype == SQL_QUAD ||
					dtype == SQL_BLOB
					|| dtype == SQL_ARRAY)
				{
					blr_len += 2;
				}
				else
					blr_len++;
			blr_len += 2;
			par_count += 2;
		}

		/* Make sure the blr buffer is large enough.  If it isn't, allocate
		   a new one. */

		if (blr_len > dasup->dasup_clauses[clause].dasup_blr_buf_len)
		{
			if (dasup->dasup_clauses[clause].dasup_blr) {
				gds__free(dasup->dasup_clauses[clause].dasup_blr);
			}
			dasup->dasup_clauses[clause].dasup_blr =
				reinterpret_cast<char*>(gds__alloc((SLONG) blr_len));
			// FREE: unknown
			if (!dasup->dasup_clauses[clause].dasup_blr)	// NOMEM:
				return error_dsql_804(status, isc_virmemexh);
			memset(dasup->dasup_clauses[clause].dasup_blr, 0, blr_len);
			dasup->dasup_clauses[clause].dasup_blr_buf_len = blr_len;
			dasup->dasup_clauses[clause].dasup_blr_length = 0;
		}

		bool same_flag = (blr_len == dasup->dasup_clauses[clause].dasup_blr_length);

		/* turn off same_flag because it breaks execute & execute2 when
		   more than one statement is prepared */

		same_flag = false;

		dasup->dasup_clauses[clause].dasup_blr_length = blr_len;

		/* Generate the blr for the message and at the same time, determine
		   the size of the message buffer.  Allow for a null indicator with
		   each variable in the SQLDA. */

		// one huge pointer per line for LIBS
		BLOB_PTR *p = reinterpret_cast <UCHAR*>(dasup->dasup_clauses[clause].dasup_blr);

	/** The define SQL_DIALECT_V5 is not available here, Hence using
	constant 1.
	**/
		if (dialect > 1) {
			ch_stuff(p, blr_version5, same_flag);
		}
		else {
			ch_stuff(p, blr_version4, same_flag);
		}
		//else if ((SCHAR) *(p) == (SCHAR) (blr_version4)) {
		//	(p)++; 
		//}
		//else {
		//	*(p)++ = (blr_version4); 
		//	same_flag = false;
		//}
		
		ch_stuff(p, blr_begin, same_flag);
		ch_stuff(p, blr_message, same_flag);
		ch_stuff(p, 0, same_flag);
		ch_stuff_word(p, par_count, same_flag);
		msg_len = 0;
		if (xsqlda)
			xvar = xsqlda->sqlvar - 1;
		else
			var = sqlda->sqlvar - 1;
		for (i = 0; i < n; i++)
		{
			if (xsqlda)
				xvar++;
			else
			{
				var++;
				sqlvar_to_xsqlvar(var, xvar);
			}
			dtype = xvar->sqltype & ~1;
			len = xvar->sqllen;
			switch (dtype)
			{
			case SQL_VARYING:
				ch_stuff(p, blr_varying, same_flag);
				ch_stuff_word(p, len, same_flag);
				dtype = dtype_varying;
				len += sizeof(USHORT);
				break;
			case SQL_TEXT:
				ch_stuff(p, blr_text, same_flag);
				ch_stuff_word(p, len, same_flag);
				dtype = dtype_text;
				break;
			case SQL_DOUBLE:
				ch_stuff(p, blr_double, same_flag);
				dtype = dtype_double;
				break;
			case SQL_FLOAT:
				ch_stuff(p, blr_float, same_flag);
				dtype = dtype_real;
				break;
			case SQL_D_FLOAT:
				ch_stuff(p, blr_d_float, same_flag);
				dtype = dtype_d_float;
				break;
			case SQL_TYPE_DATE:
				ch_stuff(p, blr_sql_date, same_flag);
				dtype = dtype_sql_date;
				break;
			case SQL_TYPE_TIME:
				ch_stuff(p, blr_sql_time, same_flag);
				dtype = dtype_sql_time;
				break;
			case SQL_TIMESTAMP:
				ch_stuff(p, blr_timestamp, same_flag);
				dtype = dtype_timestamp;
				break;
			case SQL_BLOB:
				ch_stuff(p, blr_quad, same_flag);
				ch_stuff(p, 0, same_flag);
				dtype = dtype_blob;
				break;
			case SQL_ARRAY:
				ch_stuff(p, blr_quad, same_flag);
				ch_stuff(p, 0, same_flag);
				dtype = dtype_array;
				break;
			case SQL_LONG:
				ch_stuff(p, blr_long, same_flag);
				ch_stuff(p, xvar->sqlscale, same_flag);
				dtype = dtype_long;
				break;
			case SQL_SHORT:
				ch_stuff(p, blr_short, same_flag);
				ch_stuff(p, xvar->sqlscale, same_flag);
				dtype = dtype_short;
				break;
			case SQL_INT64:
				ch_stuff(p, blr_int64, same_flag);
				ch_stuff(p, xvar->sqlscale, same_flag);
				dtype = dtype_int64;
				break;
			case SQL_QUAD:
				ch_stuff(p, blr_quad, same_flag);
				ch_stuff(p, xvar->sqlscale, same_flag);
				dtype = dtype_quad;
				break;
			default:
				return error_dsql_804(status, isc_dsql_sqlda_value_err);
			}

			ch_stuff(p, blr_short, same_flag);
			ch_stuff(p, 0, same_flag);

			align = type_alignments[dtype];
			if (align)
				msg_len = FB_ALIGN(msg_len, align);
			msg_len += len;
			align = type_alignments[dtype_short];
			if (align)
				msg_len = FB_ALIGN(msg_len, align);
			msg_len += sizeof(SSHORT);
		}

		ch_stuff(p, blr_end, same_flag);
		ch_stuff(p, blr_eoc, same_flag);

		/* Make sure the message buffer is large enough.  If it isn't, allocate
		   a new one. */

		if (msg_len > dasup->dasup_clauses[clause].dasup_msg_buf_len)
		{
			if (dasup->dasup_clauses[clause].dasup_msg)
				gds__free(dasup->dasup_clauses[clause].dasup_msg);
			dasup->dasup_clauses[clause].dasup_msg =
				reinterpret_cast<char*>(gds__alloc((SLONG) msg_len));
			// FREE: unknown
			if (!dasup->dasup_clauses[clause].dasup_msg)	// NOMEM:
				return error_dsql_804(status, isc_virmemexh);
			memset(dasup->dasup_clauses[clause].dasup_msg, 0, msg_len);
			dasup->dasup_clauses[clause].dasup_msg_buf_len = msg_len;
		}

		// Fill in the return values to the caller.

		*blr_length = (same_flag) ? 0 : blr_len;
		*msg_length = msg_len;
		*msg_type = 0;

		// If this is the first call from fetch, we're done.

		if (clause == DASUP_CLAUSE_select)
			return 0;
	}

// Move the data between the SQLDA and the message buffer.

	USHORT offset = 0;
	// one huge pointer per line for LIBS
	BLOB_PTR *msg_buf =
		reinterpret_cast<UCHAR*>(dasup->dasup_clauses[clause].dasup_msg);
	if (xsqlda)
		xvar = xsqlda->sqlvar - 1;
	else
		var = sqlda->sqlvar - 1;
	for (i = 0; i < n; i++)
	{
		if (xsqlda)
			xvar++;
		else
		{
			var++;
			sqlvar_to_xsqlvar(var, xvar);
		}
		dtype = xvar->sqltype & ~1;
		len = xvar->sqllen;
		switch (dtype)
		{
		case SQL_VARYING:
			dtype = dtype_varying;
			len += sizeof(USHORT);
			break;
		case SQL_TEXT:
			dtype = dtype_text;
			break;
		case SQL_DOUBLE:
			dtype = dtype_double;
			break;
		case SQL_FLOAT:
			dtype = dtype_real;
			break;
		case SQL_D_FLOAT:
			dtype = dtype_d_float;
			break;
		case SQL_TYPE_DATE:
			dtype = dtype_sql_date;
			break;
		case SQL_TYPE_TIME:
			dtype = dtype_sql_time;
			break;
		case SQL_TIMESTAMP:
			dtype = dtype_timestamp;
			break;
		case SQL_BLOB:
			dtype = dtype_blob;
			break;
		case SQL_ARRAY:
			dtype = dtype_array;
			break;
		case SQL_LONG:
			dtype = dtype_long;
			break;
		case SQL_SHORT:
			dtype = dtype_short;
			break;
		case SQL_INT64:
			dtype = dtype_int64;
			break;
		case SQL_QUAD:
			dtype = dtype_quad;
		}

		align = type_alignments[dtype];
		if (align)
			offset = FB_ALIGN(offset, align);
		null_offset = offset + len;

		align = type_alignments[dtype_short];
		if (align)
			null_offset = FB_ALIGN(null_offset, align);

		SSHORT *null_ind = (SSHORT *) (msg_buf + null_offset);
		if (clause == DASUP_CLAUSE_select)
		{
			// Move data from the message into the SQLDA. 

			// Make sure user has specified a data location 
			if (!xvar->sqldata)
				return error_dsql_804(status, isc_dsql_sqlda_value_err);

			memcpy(xvar->sqldata, msg_buf + offset, len);
			if (xvar->sqltype & 1)
			{
				// Make sure user has specified a location for null indicator 
				if (!xvar->sqlind)
					return error_dsql_804(status, isc_dsql_sqlda_value_err);
				*xvar->sqlind = *null_ind;
			}
		}
		else
		{
			/* Move data from the SQLDA into the message.  If the
			   indicator variable identifies a null value, permit
			   the data value to be missing. */

			if (xvar->sqltype & 1)
			{
				// Make sure user has specified a location for null indicator 
				if (!xvar->sqlind)
					return error_dsql_804(status, isc_dsql_sqlda_value_err);
				*null_ind = *xvar->sqlind;
			}
			else
				*null_ind = 0;

			/* Make sure user has specified a data location (unless NULL) */
			if (!xvar->sqldata && !*null_ind)
				return error_dsql_804(status, isc_dsql_sqlda_value_err);

			// Copy data - unless known to be NULL 
			if ((offset + len) > dasup->dasup_clauses[clause].dasup_msg_buf_len)
				return error_dsql_804(status, isc_dsql_sqlda_value_err);

			if (!*null_ind)
				memcpy(msg_buf + offset, xvar->sqldata, len);
		}

		offset = null_offset + sizeof(SSHORT);
	}

	return 0;
}


/**
  
 	UTLD_save_status_strings
  
    @brief	Strings in status vectors may be stored in stack variables
 	or memory pools that are transient.  To perserve the information,
 	copy any included strings to a special buffer.
 

    @param vector

 **/
void	UTLD_save_status_strings(
			ISC_STATUS* vector
		)
{
	TEXT *p;
	USHORT l;

// allocate space for failure strings if it hasn't already been allocated 

	if (!DSQL_failures)
	{
		DSQL_failures = (TEXT *) ALLOC_LIB_MEMORY((SLONG) DSQL_FAILURE_SPACE);
		/* FREE: by exit handler cleanup() */
		if (!DSQL_failures)		// NOMEM: don't try to copy the strings 
			return;
		DSQL_failures_ptr = DSQL_failures;
		gds__register_cleanup(cleanup, 0);

#ifdef DEBUG_GDS_ALLOC
		gds_alloc_flag_unfreed((void *) DSQL_failures);
#endif
	}

	while (*vector)
	{
		ISC_STATUS status = *vector;
		vector++;
		switch (status)
		{
		case isc_arg_cstring:
			l = static_cast<USHORT>(*vector++);

		case isc_arg_interpreted:
		case isc_arg_string:
			p = (TEXT *) * vector;
			if (status != isc_arg_cstring)
				l = strlen(p) + 1;

			/* If there isn't any more room in the buffer,
			   start at the beginning again */

			if (DSQL_failures_ptr + l > DSQL_failures + DSQL_FAILURE_SPACE)
				DSQL_failures_ptr = DSQL_failures;
			*vector++ = (ISC_STATUS) DSQL_failures_ptr;
			if (l)
				do
					*DSQL_failures_ptr++ = *p++;
				while (--l && (DSQL_failures_ptr < DSQL_failures + DSQL_FAILURE_SPACE));
			if (l)
				*(DSQL_failures_ptr - 1) = '\0';
			break;

		default:
			++vector;
			break;
		}
	}
}


/**
  
 	cleanup
  
    @brief	Exit handler to cleanup dynamically allocated memory.
 

    @param arg

 **/
static void cleanup( void *arg)
{

	if (DSQL_failures)
		FREE_LIB_MEMORY(DSQL_failures);

	gds__unregister_cleanup(cleanup, 0);
	DSQL_failures = NULL;
}


/**
  
 	error_dsql_804
  
    @brief	Move a DSQL -804 error message into a status vector.
 

    @param status
    @param err

 **/
static ISC_STATUS error_dsql_804( ISC_STATUS * status, ISC_STATUS err)
{
	ISC_STATUS *p = status;

	*p++ = isc_arg_gds;
	*p++ = isc_dsql_error;
	*p++ = isc_arg_gds;
	*p++ = isc_sqlerr;
	*p++ = isc_arg_number;
	*p++ = -804;
	*p++ = isc_arg_gds;
	*p++ = (err);
	*p = isc_arg_end;

	return status[1];
}


/**
  
 	get_numeric_info
  
    @brief	Pick up a VAX format numeric info item
 	with a 2 byte length.
 

    @param ptr

 **/
static SLONG get_numeric_info( SCHAR ** ptr)
{
	SSHORT l = static_cast<SSHORT>(gds__vax_integer(reinterpret_cast<UCHAR*>(*ptr), 2));
	*ptr += 2;
	int item = gds__vax_integer(reinterpret_cast<UCHAR*>(*ptr), l);
	*ptr += l;

	return item;
}


/**
  
 	get_string_info
  
    @brief	Pick up a string valued info item and return
 	its length.  The buffer_len argument is assumed
 	to include space for the terminating null.
 

    @param ptr
    @param buffer
    @param buffer_len

 **/
static SLONG get_string_info( SCHAR ** ptr, SCHAR * buffer, int buffer_len)
{
	SCHAR *p = *ptr;
	SSHORT l = static_cast<SSHORT>(gds__vax_integer(reinterpret_cast<UCHAR*>(p), 2));
	*ptr += l + 2;
	p += 2;

	if (l >= buffer_len)
		l = buffer_len - 1;

	SSHORT len = l;
	if (len)
		do
			*buffer++ = *p++;
		while (--l);
	*buffer = 0;

	return len;
}

#ifdef NOT_USED_OR_REPLACED
#ifdef DEV_BUILD
static void print_xsqlda( XSQLDA * xsqlda)
{
/*****************************************
 *
 *	p r i n t _ x s q l d a 
 *
 *****************************************
 *
 * print an sqlda
 *
 *****************************************/
	XSQLVAR *xvar, *end_var;

	if (!xsqlda)
		return;

	ib_printf("SQLDA Version %d\n", xsqlda->version);
	ib_printf("      sqldaid %.8s\n", xsqlda->sqldaid);
	ib_printf("      sqldabc %d\n", xsqlda->sqldabc);
	ib_printf("      sqln    %d\n", xsqlda->sqln);
	ib_printf("      sqld    %d\n", xsqlda->sqld);

	xvar = xsqlda->sqlvar;
	for (end_var = xvar + xsqlda->sqld; xvar < end_var; xvar++)
		ib_printf("         %.31s %.31s type: %d, scale %d, len %d subtype %d\n",
			   xvar->sqlname, xvar->relname, xvar->sqltype,
			   xvar->sqlscale, xvar->sqllen, xvar->sqlsubtype);
}
#endif
#endif

/**
  
 	sqlvar_to_xsqlvar
  

    @param sqlvar
    @param xsqlvar


    @param sqlvar
    @param xsqlvar

 **/
static void sqlvar_to_xsqlvar( SQLVAR * sqlvar, XSQLVAR * xsqlvar)
{

	xsqlvar->sqltype = sqlvar->sqltype;
	xsqlvar->sqldata = sqlvar->sqldata;
	xsqlvar->sqlind = sqlvar->sqlind;

	xsqlvar->sqlsubtype = 0;
	xsqlvar->sqlscale = 0;
	xsqlvar->sqllen = sqlvar->sqllen;
	if ((xsqlvar->sqltype & ~1) == SQL_LONG) {
		xsqlvar->sqlscale = xsqlvar->sqllen >> 8;
		xsqlvar->sqllen = sizeof(SLONG);
	}
	else if ((xsqlvar->sqltype & ~1) == SQL_SHORT) {
		xsqlvar->sqlscale = xsqlvar->sqllen >> 8;
		xsqlvar->sqllen = sizeof(SSHORT);
	}
	else if ((xsqlvar->sqltype & ~1) == SQL_INT64) {
		xsqlvar->sqlscale = xsqlvar->sqllen >> 8;
		xsqlvar->sqllen = sizeof(SINT64);
	}
	else if ((xsqlvar->sqltype & ~1) == SQL_QUAD) {
		xsqlvar->sqlscale = xsqlvar->sqllen >> 8;
		xsqlvar->sqllen = sizeof(ISC_QUAD);
	}
}


/**
  
 	xsqlvar_to_sqlvar
  
    @brief	Move an XSQLVAR to an SQLVAR.
 

    @param xsqlvar
    @param sqlvar

 **/
static void xsqlvar_to_sqlvar( XSQLVAR * xsqlvar, SQLVAR * sqlvar)
{

	sqlvar->sqltype = xsqlvar->sqltype;
	sqlvar->sqlname_length = xsqlvar->aliasname_length;

// N.B., this may not NULL-terminate the name... 

	memcpy(sqlvar->sqlname, xsqlvar->aliasname, sizeof(sqlvar->sqlname));

	sqlvar->sqllen = xsqlvar->sqllen;
	if ((sqlvar->sqltype & ~1) == SQL_LONG)
		sqlvar->sqllen = sizeof(SLONG) | (xsqlvar->sqlscale << 8);
	else if ((sqlvar->sqltype & ~1) == SQL_SHORT)
		sqlvar->sqllen = sizeof(SSHORT) | (xsqlvar->sqlscale << 8);
	else if ((sqlvar->sqltype & ~1) == SQL_INT64)
		sqlvar->sqllen = sizeof(SINT64) | (xsqlvar->sqlscale << 8);
	else if ((sqlvar->sqltype & ~1) == SQL_QUAD)
		sqlvar->sqllen = sizeof(ISC_QUAD) | (xsqlvar->sqlscale << 8);
}

