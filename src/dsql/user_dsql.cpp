/*
 *	PROGRAM:	Dynamic SQL runtime support
 *	MODULE:		user_dsql.c
 *	DESCRIPTION:	Above the Y-value entrypoints for DSQL
 *
 *
 * This module contains DSQL related routines that sit on top
 * of the Y-valve.  This includes the original (pre-version 4)
 * DSQL routines as well as the alternate VMS and Ada entrypoints
 * of the new DSQL routines.  The pre-version 4 entrypoints
 * retain their "gds__" prefix while the new routines are prefixed
 * with "isc_dsql_".
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
#include "../jrd/ib_stdio.h"
#include <stdlib.h>
#include "../jrd/common.h"
#include <stdarg.h>
#include "../dsql/dsql.h"
#include "../dsql/chars.h"
#include "../dsql/sqlda.h"
#include "../jrd/blr.h"
#include "gen/codes.h"
#include "gen/iberror.h"
#include "../jrd/inf.h"
#include "../jrd/align.h"
#include "../jrd/gds_proto.h"
#include "../jrd/why_proto.h"
#include "../dsql/user__proto.h"

#ifdef VMS
#include <descrip.h>
#endif


extern "C" {


#define NAME_statement	1
#define NAME_cursor		2

typedef SLONG* HNDL;

/* declare a structure which enables us to associate a cursor with a 
   statement and vice versa */

struct name;	// fwd. decl.

typedef struct stmt
{
	stmt*	stmt_next;			/* next in chain */
	name*	stmt_stmt;			/* symbol table entry for statement name */
	name*	stmt_cursor;		/* symbol table entry for cursor name */
	HNDL	stmt_handle;		/* stmt handle returned by dsql_xxx */
	HNDL	stmt_db_handle;		/* database handle for this statement */
} *STMT;

/* declare a structure to hold the cursor and statement names */

typedef struct name
{
	name*	name_next;
	name*	name_prev;
	STMT	name_stmt;
	USHORT	name_length;
	SCHAR	name_symbol[1];
} *NAME;


static void		cleanup(void*);
static void		cleanup_database(SLONG**, SLONG);
static STATUS	error();
static void		error_post(STATUS, ...);
static NAME		lookup_name(SCHAR*, NAME);
static STMT		lookup_stmt(SCHAR*, NAME, USHORT);
static void		init(HNDL*);
static NAME		insert_name(SCHAR*, NAME *, STMT);
static USHORT	name_length(SCHAR *);
static void		remove_name(NAME, NAME *);
static BOOLEAN	scompare(register SCHAR*, register USHORT, register SCHAR*, USHORT);

/* declare the private data */

#pragma FB_COMPILER_MESSAGE("Dragons ahead. Static data. Not thread safe!")

static BOOLEAN	init_flag		= FALSE;	/* whether we've been initialized */
static ERR		UDSQL_error		= NULL;
static STMT		statements		= NULL;
static NAME		statement_names	= NULL;
static NAME		cursor_names	= NULL;
static DBB		databases		= NULL;

static inline
void set_global_private_status(STATUS* user_status, STATUS* local_status)
{
	UDSQL_error->dsql_user_status = user_status;
	UDSQL_error->dsql_status = (user_status) ? user_status : local_status;
}

static inline BOOLEAN INIT_DSQL(STATUS* user_status, STATUS* local_status)
{
	init(0);
	set_global_private_status(user_status, local_status);
	if (SETJMP (UDSQL_error->dsql_env)) {
		return FALSE;
	}
	return TRUE;
}


//____________________________________________________________
//
//	Close a dynamic SQL cursor.
//
STATUS API_ROUTINE isc_embed_dsql_close(STATUS* user_status, SCHAR* name)
{
	STATUS local_status[20];

	if (!INIT_DSQL(user_status, local_status)) {
		return error();
	}

	// get the symbol table entry

	STMT statement = lookup_stmt(name, cursor_names, NAME_cursor);

	return isc_dsql_free_statement(	user_status,
									reinterpret_cast<hndl**>(&statement->stmt_handle),
									DSQL_close);
}


//____________________________________________________________
//
//	Declare a cursor for a dynamic request.
//
STATUS API_ROUTINE isc_embed_dsql_declare(	STATUS*	user_status,
											SCHAR*	stmt_name,
											SCHAR*	cursor)
{
	STATUS local_status[20];

	if (!INIT_DSQL(user_status, local_status)) {
		return error();
	}

	// get the symbol table entry

	STMT statement = lookup_stmt(stmt_name, statement_names, NAME_statement);

	STATUS s =
		isc_dsql_set_cursor_name(user_status,
								 reinterpret_cast<hndl**>(&statement->stmt_handle),
								 cursor,
								 0);
	if (s) {
		return s;
	}

	statement->stmt_cursor = insert_name(cursor, &cursor_names, statement);

	return s;
}


//____________________________________________________________
//
//	Describe output parameters for a prepared statement.
//
STATUS API_ROUTINE isc_embed_dsql_describe(STATUS* user_status,
										   SCHAR* stmt_name,
										   USHORT dialect, XSQLDA* sqlda)
{
	STATUS local_status[20];

	if (!INIT_DSQL(user_status, local_status)) {
		return error();
	}

	// get the symbol table entry

	STMT statement = lookup_stmt(stmt_name, statement_names, NAME_statement);

	return isc_dsql_describe(user_status,
							 reinterpret_cast<hndl**>(&statement->stmt_handle),
							 dialect,
							 sqlda);
}


//____________________________________________________________
//
//	isc_embed_dsql_descr_bind
//
STATUS API_ROUTINE isc_embed_dsql_descr_bind(	STATUS*	user_status,
												SCHAR*	stmt_name,
												USHORT	dialect,
												XSQLDA*	sqlda)
{
	return isc_embed_dsql_describe_bind(user_status,
										stmt_name,
										dialect,
										sqlda);
}


//____________________________________________________________
//
//	Describe input parameters for a prepared statement.
//
STATUS API_ROUTINE isc_embed_dsql_describe_bind(STATUS*	user_status,
												SCHAR*	stmt_name,
												USHORT	dialect,
												XSQLDA*	sqlda)
{
	STATUS local_status[20];

	if (!INIT_DSQL(user_status, local_status)) {
		return error();
	}

	// get the symbol table entry

	STMT statement = lookup_stmt(stmt_name, statement_names, NAME_statement);

	return isc_dsql_describe_bind(user_status,
								  reinterpret_cast<hndl**>(&statement->stmt_handle),
								  dialect,
								  sqlda);
}


//____________________________________________________________
//
//	Execute a non-SELECT dynamic SQL statement.
//
STATUS API_ROUTINE isc_embed_dsql_execute(STATUS*	user_status,
										  SLONG**	trans_handle,
										  SCHAR*	stmt_name,
										  USHORT	dialect,
										  XSQLDA*	sqlda)
{
	return isc_embed_dsql_execute2(	user_status,
									trans_handle,
									stmt_name,
									dialect,
									sqlda,
									NULL);
}


//____________________________________________________________
//
//	Execute a non-SELECT dynamic SQL statement.
//
STATUS API_ROUTINE isc_embed_dsql_execute2(STATUS*	user_status,
										   SLONG**	trans_handle,
										   SCHAR*	stmt_name,
										   USHORT	dialect,
										   XSQLDA*	in_sqlda,
										   XSQLDA*	out_sqlda)
{
	STATUS local_status[20];

	if (!INIT_DSQL(user_status, local_status)) {
		return error();
	}

	// get the symbol table entry

	STMT statement = lookup_stmt(stmt_name, statement_names, NAME_statement);

	return isc_dsql_execute2(	user_status,
								reinterpret_cast<hndl**>(trans_handle),
								reinterpret_cast<hndl**>(&statement->stmt_handle),
								dialect,
								in_sqlda,
								out_sqlda);
}


//____________________________________________________________
//
//	isc_embed_dsql_exec_immed
//
STATUS API_ROUTINE isc_embed_dsql_exec_immed(STATUS*	user_status,
											 SLONG**	db_handle,
											 SLONG**	trans_handle,
											 USHORT		length,
											 SCHAR*		string,
											 USHORT		dialect,
											 XSQLDA*	sqlda)
{
	return isc_embed_dsql_execute_immed(user_status,
										db_handle,
										trans_handle,
										length,
										string,
										dialect,
										sqlda);
}


//____________________________________________________________
//
//	Prepare a statement for execution.
//
STATUS API_ROUTINE isc_embed_dsql_execute_immed(STATUS*	user_status,
												SLONG**	db_handle,
												SLONG**	trans_handle,
												USHORT	length,
												SCHAR*	string,
												USHORT	dialect,
												XSQLDA*	sqlda)
{
	return isc_embed_dsql_exec_immed2(	user_status,
										db_handle,
										trans_handle,
										length,
										string,
										dialect,
										sqlda,
										NULL);
}


//____________________________________________________________
//
//	Prepare a statement for execution.
//
STATUS API_ROUTINE isc_embed_dsql_exec_immed2(	STATUS*	user_status,
												SLONG**	db_handle,
												SLONG**	trans_handle,
												USHORT	length,
												SCHAR*	string,
												USHORT	dialect,
												XSQLDA*	in_sqlda,
												XSQLDA*	out_sqlda)
{
	return isc_dsql_exec_immed2(user_status,
								reinterpret_cast<hndl**>(db_handle),
								reinterpret_cast<hndl**>(trans_handle),
								length,
								string,
								dialect,
								in_sqlda,
								out_sqlda);
}


#ifdef VMS
//____________________________________________________________
//
//	An execute immediate for COBOL to call
//
STATUS API_ROUTINE isc_embed_dsql_execute_immed_d(STATUS* user_status,
												  SLONG** db_handle,
												  SLONG** trans_handle,
												  struct dsc$descriptor_s *
												  string, USHORT dialect,
												  XSQLDA* sqlda)
{
	return isc_embed_dsql_exec_immed2_d(user_status,
										db_handle, trans_handle, string,
										dialect, sqlda, NULL);
}
#endif


#ifdef VMS
//____________________________________________________________
//
//	An execute immediate for COBOL to call
//
STATUS API_ROUTINE isc_embed_dsql_exec_immed2_d(STATUS* user_status,
												SLONG** db_handle,
												SLONG** trans_handle,
												struct dsc$descriptor_s *
												string, USHORT dialect,
												XSQLDA* in_sqlda,
												XSQLDA* out_sqlda)
{
	USHORT len = string->dsc$w_length;

	return isc_embed_dsql_exec_immed2(user_status, db_handle,
									  trans_handle, len,
									  string->dsc$a_pointer, dialect,
									  in_sqlda, out_sqlda);
}
#endif


//____________________________________________________________
//
//	Fetch next record from a dynamic SQL cursor
//
STATUS API_ROUTINE isc_embed_dsql_fetch(STATUS* user_status,
										SCHAR* cursor_name,
										USHORT dialect, XSQLDA* sqlda)
{
	STATUS local_status[20];

	if (!INIT_DSQL(user_status, local_status)) {
		return error();
	}

	STMT statement = lookup_stmt(cursor_name, cursor_names, NAME_cursor);

	return isc_dsql_fetch(user_status,
						  reinterpret_cast<hndl**>(&statement->stmt_handle),
						  dialect,
						  sqlda);
}


#ifdef SCROLLABLE_CURSORS
//____________________________________________________________
//
//	Fetch next record from a dynamic SQL cursor
//
STATUS API_ROUTINE isc_embed_dsql_fetch2(	STATUS*	user_status,
											SCHAR*	cursor_name,
											USHORT	dialect,
											XSQLDA*	sqlda,
											USHORT	direction,
											SLONG	offset)
{
	STATUS local_status[20];

	if (!INIT_DSQL(user_status, local_status)) {
		return error();
	}

	// get the symbol table entry

	STMT statement = lookup_stmt(cursor_name, cursor_names, NAME_cursor);

	return isc_dsql_fetch2(	user_status,
							&statement->stmt_handle,
							dialect,
							sqlda,
							direction,
							offset);
}
#endif


//____________________________________________________________
//
//	Fetch next record from a dynamic SQL cursor (ADA version)
//
STATUS API_ROUTINE isc_embed_dsql_fetch_a(	STATUS*	user_status,
											int*	sqlcode,
											SCHAR*	cursor_name,
											USHORT	dialect,
											XSQLDA*	sqlda)
{
	*sqlcode = 0;

	STATUS s = isc_embed_dsql_fetch(user_status, cursor_name, dialect, sqlda);
	if (s == 100) {
		*sqlcode = 100;
	}

	return SUCCESS;
}


#ifdef SCROLLABLE_CURSORS
STATUS API_ROUTINE isc_embed_dsql_fetch2_a(STATUS* user_status,
										   int *sqlcode,
										   SCHAR* cursor_name,
										   USHORT dialect,
										   XSQLDA* sqlda,
										   USHORT direction, SLONG offset)
{
/**************************************
 *
 *	i s c _ e m b e d _ d s q l _ f e t c h 2 _ a
 *
 **************************************
 *
 * Functional description
 *	Fetch next record from a dynamic SQL cursor (ADA version)
 *
 **************************************/
	STATUS s;

	*sqlcode = 0;

	s =
		isc_embed_dsql_fetch2(user_status, cursor_name, dialect, sqlda,
							  direction, offset);
	if (s == 100)
		*sqlcode = 100;

	return SUCCESS;
}
#endif


STATUS API_ROUTINE isc_embed_dsql_insert(STATUS* user_status,
										 SCHAR* cursor_name,
										 USHORT dialect, XSQLDA* sqlda)
{
/**************************************
 *
 *	i s c _ e m b e d _ d s q l _ i n s e r t
 *
 **************************************
 *
 * Functional description
 *	Insert next record into a dynamic SQL cursor
 *
 **************************************/
	STATUS local_status[20];
	STMT statement;

	if (!INIT_DSQL(user_status, local_status)) {
		return error();
	}

/* get the symbol table entry */

	statement = lookup_stmt(cursor_name, cursor_names, NAME_cursor);

	return isc_dsql_insert(user_status,
						   reinterpret_cast<hndl**>(&statement->stmt_handle),
						   dialect,
						   sqlda);
}


void API_ROUTINE isc_embed_dsql_length( UCHAR * string, USHORT * length)
{
/**************************************
 *
 *	i s c _ e m b e d _ d s q l _ l e n g t h
 *
 **************************************
 *
 * Functional description
 *	Determine length of a ';' or null terminated string
 *
 **************************************/
	UCHAR *p, quote, prev;

	for (p = string; *p && *p != ';'; p++)
		if (classes[*p] & CHR_QUOTE) {
			for (prev = 0, quote = *p++; *p == quote || prev == quote;)
				prev = *p++;
			p--;
		}

	*length = p - string + 1;
}


STATUS API_ROUTINE isc_embed_dsql_open(STATUS* user_status,
									   SLONG** trans_handle,
									   SCHAR* cursor_name,
									   USHORT dialect, XSQLDA* sqlda)
{
/**************************************
 *
 *	i s c _ e m b e d _ d s q l _ o p e n
 *
 **************************************
 *
 * Functional description
 *	Open a dynamic SQL cursor.
 *
 **************************************/

	return isc_embed_dsql_open2(user_status, trans_handle, cursor_name,
								dialect, sqlda, NULL);
}


STATUS API_ROUTINE isc_embed_dsql_open2(STATUS* user_status,
										SLONG** trans_handle,
										SCHAR* cursor_name,
										USHORT dialect,
										XSQLDA* in_sqlda, XSQLDA* out_sqlda)
{
/**************************************
 *
 *	i s c _ e m b e d _ d s q l _ o p e n 2
 *
 **************************************
 *
 * Functional description
 *	Open a dynamic SQL cursor.
 *
 **************************************/
	STATUS local_status[20];
	STMT stmt;

	if (!INIT_DSQL(user_status, local_status)) {
		return error();
	}

/* get the symbol table entry */

	stmt = lookup_stmt(cursor_name, cursor_names, NAME_cursor);

	return isc_dsql_execute2(user_status,
							 reinterpret_cast<hndl**>(trans_handle),
							 reinterpret_cast<hndl**>(&stmt->stmt_handle),
							 dialect, in_sqlda, out_sqlda);
}


STATUS API_ROUTINE isc_embed_dsql_prepare(STATUS*	user_status,
										  SLONG**	db_handle,
										  SLONG**	trans_handle,
										  SCHAR*	stmt_name,
										  USHORT	length,
										  SCHAR*	string,
										  USHORT	dialect,
										  XSQLDA*	sqlda)
 {
/**************************************
 *
 *	i s c _ e m b e d _ d s q l _ p r e p a r e
 *
 **************************************
 *
 * Functional description
 *	Prepare a statement for execution.
 *
 **************************************/
	STATUS s, local_status[20], local_status2[20];
	STMT statement;
	HNDL stmt_handle;

	init(db_handle);
	set_global_private_status(user_status, local_status);

	try {

	NAME name = lookup_name(stmt_name, statement_names);

	if (name && name->name_stmt->stmt_db_handle == *db_handle)
	{
		/* The statement name already exists for this database.
		   Re-use its statement handle. */

		statement = name->name_stmt;
		stmt_handle = statement->stmt_handle;
	}
	else
	{
		/* This is a new statement name for this database.
		   Allocate a statement handle for it. */

		if (name) {
			isc_embed_dsql_release(user_status, stmt_name);
		}
		statement = NULL;
		stmt_handle = NULL;
		s = isc_dsql_allocate_statement(user_status,
										reinterpret_cast<hndl**>(db_handle),
										reinterpret_cast<hndl**>(&stmt_handle));
		if (s)
		{
			return s;
		}
	}

	s = isc_dsql_prepare(user_status,
						 reinterpret_cast<hndl**>(trans_handle),
						 reinterpret_cast<hndl**>(&stmt_handle),
						 length, string, dialect, sqlda);

	if (s) {
		/* An error occurred.  Free any newly allocated statement handle. */

		if (!statement) {
			isc_dsql_free_statement(local_status2,
									reinterpret_cast<hndl**>(&stmt_handle),
									DSQL_drop);
		}
		return error();
	}

/* If a new statement was allocated, add it to the symbol table and insert it
   into the list of statements */

	if (!statement)
	{
		statement = (STMT) gds__alloc((SLONG) sizeof(stmt));
		/* FREE: by user calling isc_embed_dsql_release() */
		if (!statement)			/* NOMEM: */
			error_post(isc_virmemexh, 0);

#ifdef DEBUG_GDS_ALLOC
		gds_alloc_flag_unfreed((void *) statement);
#endif	/* DEBUG_GDS_ALLOC */

		statement->stmt_next = statements;
		statements = statement;

		statement->stmt_db_handle = (HNDL) * db_handle;
		statement->stmt_stmt =
			insert_name(stmt_name, &statement_names, statement);
	}
	else if (statement->stmt_cursor)
		remove_name(statement->stmt_cursor, &cursor_names);

	statement->stmt_handle = stmt_handle;
	statement->stmt_cursor = NULL;

	return s;

	}	// try
	catch (...)
	{
		return error();
	}

}


#ifdef VMS
STATUS API_ROUTINE isc_embed_dsql_prepare_d(STATUS* user_status,
											SLONG** db_handle,
											SLONG** trans_handle,
											SCHAR* stmt_name,
											struct dsc$descriptor_s * string,
											USHORT dialect, XSQLDA* sqlda)
{
/**************************************
 *
 *	i s c _ e m b e d _ d s q l _ p r e p a r e _ d
 *
 **************************************
 *
 * Functional description
 *	A prepare for COBOL to call
 *
 **************************************/
	USHORT len;

	len = string->dsc$w_length;

	return isc_embed_dsql_prepare(user_status, db_handle,
								  trans_handle, stmt_name, len,
								  string->dsc$a_pointer, dialect, sqlda);
}
#endif


STATUS API_ROUTINE isc_embed_dsql_release(STATUS* user_status,
										  SCHAR* stmt_name)
{
/**************************************
 *
 *	i s c _ e m b e d _ d s q l _ r e l e a s e
 *
 **************************************
 *
 * Functional description
 *	Release request for a dsql statement
 *
 **************************************/
	STATUS	local_status[20];
	STMT*	stmt_ptr, p;

	if (!INIT_DSQL(user_status, local_status)) {
		return error();
	}

/* If a request already exists under that name, purge it out */

	STMT statement = lookup_stmt(stmt_name, statement_names, NAME_statement);

	STATUS s =
		isc_dsql_free_statement(user_status,
								reinterpret_cast<hndl**>(&statement->stmt_handle),
								DSQL_drop);
	if (s) {
		return s;
	}

	// remove the statement from the symbol tables

	if (statement->stmt_stmt)
		remove_name(statement->stmt_stmt, &statement_names);
	if (statement->stmt_cursor)
		remove_name(statement->stmt_cursor, &cursor_names);

	// and remove this statement from the local list

	for (stmt_ptr = &statements; p = *stmt_ptr; stmt_ptr = &p->stmt_next) {
		if (p == statement) {
			*stmt_ptr = statement->stmt_next;
			gds__free(statement);
			break;
		}
	}

	return s;
}


#ifdef VMS
int API_ROUTINE isc_dsql_execute_immediate_d(
											 STATUS* user_status,
											 int db_handle,
											 int trans_handle,
											 struct dsc$descriptor_s *string,
											 USHORT dialect, XSQLDA* sqlda)
{
/**************************************
 *
 *	i s c _ d s q l _ e x e c u t e _ i m m e d i a t e _ d
 *
 **************************************
 *
 * Functional description
 *	An execute immediate for COBOL to call
 *
 **************************************/
	USHORT len;

	len = string->dsc$w_length;

	return isc_dsql_execute_immediate(user_status, db_handle, trans_handle,
									  len, string->dsc$a_pointer, dialect,
									  sqlda);
}
#endif


STATUS API_ROUTINE isc_dsql_fetch_a(STATUS* user_status,
									int *sqlcode,
									int *stmt_handle,
									USHORT dialect, int *sqlda)
{
/**************************************
 *
 *	i s c _ d s q l _ f e t c h _ a
 *
 **************************************
 *
 * Functional description
 *	Fetch next record from a dynamic SQL cursor (ADA version)
 *
 **************************************/
	STATUS s;

	*sqlcode = 0;

	s = isc_dsql_fetch(	user_status,
						reinterpret_cast<hndl**>(stmt_handle),
						dialect,
						reinterpret_cast<XSQLDA*>(sqlda));
	if (s == 100)
		*sqlcode = 100;

	return SUCCESS;
}


#ifdef SCROLLABLE_CURSORS
STATUS API_ROUTINE isc_dsql_fetch2_a(STATUS* user_status,
									 int *sqlcode,
									 int *stmt_handle,
									 USHORT dialect,
									 int *sqlda,
									 USHORT direction, SLONG offset)
{
/**************************************
 *
 *	i s c _ d s q l _ f e t c h 2 _ a
 *
 **************************************
 *
 * Functional description
 *	Fetch next record from a dynamic SQL cursor (ADA version)
 *
 **************************************/
	STATUS s;

	*sqlcode = 0;

	s =
		isc_dsql_fetch2(user_status, stmt_handle, dialect, sqlda, direction,
						offset);
	if (s == 100)
		*sqlcode = 100;

	return SUCCESS;
}
#endif


#ifdef VMS
int API_ROUTINE isc_dsql_prepare_d(	STATUS*						user_status,
									int							trans_handle,
									int							stmt_handle,
									struct dsc$descriptor_s*	string,
									USHORT						dialect,
									XSQLDA*						sqlda)
{
/**************************************
 *
 *	i s c _ d s q l _ p r e p a r e _ d
 *
 **************************************
 *
 * Functional description
 *	A prepare for COBOL to call
 *
 **************************************/
	USHORT len;

	len = string->dsc$w_length;

	return isc_dsql_prepare(user_status, trans_handle, stmt_handle,
							len, string->dsc$a_pointer, dialect, sqlda);
}
#endif


/**************************************
 *
 *	i s c _ . . .
 *
 **************************************
 *
 * Functional description
 *	The following routines define the
 *	old isc_ entrypoints.
 *
 **************************************/
STATUS API_ROUTINE isc_close(STATUS* status_vector, SCHAR* statement_name)
{
	return isc_embed_dsql_close(status_vector, statement_name);
}

STATUS API_ROUTINE isc_declare(	STATUS*	status_vector,
								SCHAR*	statement_name,
								SCHAR*	cursor_name)
{
	return isc_embed_dsql_declare(status_vector, statement_name, cursor_name);
}

STATUS API_ROUTINE isc_describe(STATUS* status_vector,
								SCHAR* statement_name, SQLDA* sqlda)
{
	return isc_embed_dsql_describe(status_vector,
								   statement_name,
								   (USHORT) DIALECT_sqlda,
								   reinterpret_cast<XSQLDA*>(sqlda));
}

STATUS API_ROUTINE isc_describe_bind(STATUS* status_vector,
									 SCHAR* statement_name,
									 SQLDA* sqlda)
{
	return isc_embed_dsql_describe_bind(status_vector,
										statement_name,
										(USHORT) DIALECT_sqlda,
										reinterpret_cast<XSQLDA*>(sqlda));
}

STATUS API_ROUTINE isc_dsql_finish(HNDL * db_handle)
{
	return 0;
}

STATUS API_ROUTINE isc_execute(STATUS* status_vector,
							   SLONG* tra_handle,
							   SCHAR* statement_name, SQLDA* sqlda)
{
	return isc_embed_dsql_execute(status_vector,
								  (SLONG**) tra_handle,
								  statement_name,
								  (USHORT) DIALECT_sqlda,
								  reinterpret_cast<XSQLDA*>(sqlda));
}

STATUS API_ROUTINE isc_execute_immediate(STATUS* status_vector,
										 SLONG* db_handle,
										 SLONG* tra_handle,
										 SSHORT * sql_length, SCHAR* sql)
{
	return isc_embed_dsql_execute_immed(status_vector,
										(SLONG**) db_handle,
										(SLONG**) tra_handle,
										(USHORT) ((sql_length) ? *sql_length :
												  0), sql,
										(USHORT) DIALECT_sqlda, NULL);
}

#ifdef VMS
STATUS API_ROUTINE isc_execute_immediate_d(STATUS* status_vector,
										   SLONG* db_handle,
										   SLONG* tra_handle,
										   struct dsc$descriptor_s * string)
{
	USHORT len;

	len = string->dsc$w_length;

	return isc_execute_immediate(status_vector,
								 db_handle,
								 tra_handle, &len, string->dsc$a_pointer);
}
#endif

STATUS API_ROUTINE isc_fetch(STATUS* status_vector,
							 SCHAR* cursor_name, SQLDA* sqlda)
{
	return isc_embed_dsql_fetch(status_vector,
								cursor_name,
								(USHORT) DIALECT_sqlda,
								reinterpret_cast<XSQLDA*>(sqlda));
}

STATUS API_ROUTINE isc_fetch_a(STATUS* status_vector,
							   int *sqlcode,
							   SCHAR* cursor_name, SQLDA* sqlda)
{
	return isc_embed_dsql_fetch_a(status_vector,
								  sqlcode,
								  cursor_name,
								  (USHORT) DIALECT_sqlda,
								  reinterpret_cast<XSQLDA*>(sqlda));
}

STATUS API_ROUTINE isc_open(STATUS* status_vector,
							SLONG* tra_handle,
							SCHAR* cursor_name, SQLDA* sqlda)
{
	return isc_embed_dsql_open(status_vector,
							   (SLONG**) tra_handle,
							   cursor_name,
							   (USHORT) DIALECT_sqlda,
							   reinterpret_cast<XSQLDA*>(sqlda));
}

STATUS API_ROUTINE isc_prepare(	STATUS*	status_vector,
								SLONG*	db_handle,
								SLONG*	tra_handle,
								SCHAR*	statement_name,
								SSHORT*	sql_length,
								SCHAR*	sql,
								SQLDA*	sqlda)
{
	return isc_embed_dsql_prepare(	status_vector,
									(SLONG**) db_handle,
									(SLONG**) tra_handle,
									statement_name,
									(USHORT) ((sql_length) ? *sql_length : 0),
									sql,
									(USHORT) DIALECT_sqlda,
									reinterpret_cast<XSQLDA*>(sqlda));
}

#ifdef VMS
STATUS API_ROUTINE isc_prepare_d(STATUS* status_vector,
								 SLONG* db_handle,
								 SLONG* tra_handle,
								 SCHAR* statement_name,
								 struct dsc$descriptor_s * string,
								 SQLDA* sqlda)
{
	USHORT len;

	len = string->dsc$w_length;

	return isc_prepare(status_vector,
					   db_handle,
					   tra_handle,
					   statement_name, &len, string->dsc$a_pointer, sqlda);
}
#endif

STATUS API_ROUTINE isc_dsql_release(STATUS*	status_vector,
									SCHAR*	statement_name)
{
	return isc_embed_dsql_release(status_vector, statement_name);
}

int API_ROUTINE isc_to_sqlda(	SQLDA*	sqlda,
								int		number,
								SCHAR*	host_var,
								int		host_var_size,
								SCHAR*	name)
{
/* no longer supported */
	return 0;
}


//____________________________________________________________
//
//	gds_...
//
//	The following routines define the gds__ style names for isc_ entrypoints.
//
//	Please note that these names - strictly speaking - are illegal C++
//	identifiers, and as such should probably be moved into a C file.
//

STATUS API_ROUTINE gds__close(STATUS* status_vector, SCHAR* statement_name)
{
	return isc_close(status_vector, statement_name);
}

STATUS API_ROUTINE gds__declare(STATUS*	status_vector,
								SCHAR*	statement_name,
								SCHAR*	cursor_name)
{
	return isc_declare(status_vector, statement_name, cursor_name);
}

STATUS API_ROUTINE gds__describe(	STATUS*	status_vector,
									SCHAR*	statement_name,
									SQLDA*	sqlda)
{
	return isc_describe(status_vector, statement_name, sqlda);
}

STATUS API_ROUTINE gds__describe_bind(STATUS*	status_vector,
									  SCHAR*	statement_name,
									  SQLDA*	sqlda)
{
	return isc_describe_bind(status_vector, statement_name, sqlda);
}

STATUS API_ROUTINE gds__dsql_finish(HNDL* db_handle)
{
	return isc_dsql_finish(db_handle);
}

STATUS API_ROUTINE gds__execute(STATUS*	status_vector,
								SLONG*	tra_handle,
								SCHAR*	statement_name,
								SQLDA*	sqlda)
{
	return isc_execute(status_vector, tra_handle, statement_name, sqlda);
}

STATUS API_ROUTINE gds__execute_immediate(STATUS*	status_vector,
										  SLONG*	db_handle,
										  SLONG*	tra_handle,
										  SSHORT*	sql_length,
										  SCHAR*	sql)
{
	return isc_execute_immediate(	status_vector,
									db_handle,
									tra_handle,
									sql_length,
									sql);
}

#ifdef VMS
STATUS API_ROUTINE gds__execute_immediate_d(STATUS*	status_vector,
											SLONG*	db_handle,
											SLONG*	tra_handle,
											SCHAR*	sql_string)
{
	return isc_execute_immediate_d(status_vector,
								   db_handle, tra_handle, sql_string);
}
#endif

STATUS API_ROUTINE gds__fetch(STATUS*	status_vector,
							  SCHAR*	statement_name,
							  SQLDA*	sqlda)
{
	return isc_fetch(status_vector, statement_name, sqlda);
}

STATUS API_ROUTINE gds__fetch_a(STATUS*	status_vector,
								int*	sqlcode,
								SCHAR*	statement_name,
								SQLDA*	sqlda)
{
	return isc_fetch_a(status_vector, sqlcode, statement_name, sqlda);
}

STATUS API_ROUTINE gds__open(STATUS*	status_vector,
							 SLONG*		tra_handle,
							 SCHAR*		cursor_name,
							 SQLDA*		sqlda)
{
	return isc_open(status_vector, tra_handle, cursor_name, sqlda);
}

STATUS API_ROUTINE gds__prepare(STATUS*	status_vector,
								SLONG*	db_handle,
								SLONG*	tra_handle,
								SCHAR*	statement_name,
								SSHORT*	sql_length,
								SCHAR*	sql,
								SQLDA*	sqlda)
{
	return isc_prepare(	status_vector,
						db_handle,
						tra_handle,
						statement_name,
						sql_length,
						sql,
						sqlda);
}

#ifdef VMS
STATUS API_ROUTINE gds__prepare_d(STATUS* status_vector,
								  SLONG* db_handle,
								  SLONG* tra_handle,
								  SCHAR* statement_name,
								  SLONG* sql_desc, SQLDA* sqlda)
{
	return isc_prepare_d(status_vector,
						 db_handle,
						 tra_handle, statement_name, sql_desc, sqlda);
}
#endif

STATUS API_ROUTINE gds__to_sqlda(SQLDA* sqlda,
								 int number,
								 SCHAR* host_variable,
								 int host_variable_size,
								 SCHAR* host_variable_name)
{
	return isc_to_sqlda(sqlda,
						number,
						host_variable,
						host_variable_size, host_variable_name);
}


//____________________________________________________________
//
//	Helper functions for cleanup().
//
static void free_all_databases(DBB& databases)
{
	while (databases) {
		DBB database = databases;
		databases = database->dbb_next;
		gds__free(database);
	}
}

static void free_all_statements(STMT& statements)
{
	while (statements) {
		STMT statement = statements;
		statements = statement->stmt_next;
		gds__free(statement);
	}
}

static void free_all_names(NAME& names)
{
	while (names) {
		NAME name = names;
		names = name->name_next;
		gds__free(name);
	}
}


//____________________________________________________________
//
//		Cleanup handler to free all dynamically allocated memory.
//
static void cleanup(void* arg)
{
	if (!init_flag) {
		return;
	}

	init_flag = FALSE;

	gds__free(UDSQL_error);
	UDSQL_error = NULL;

	free_all_databases(databases);
	free_all_statements(statements);
	free_all_names(statement_names);
	free_all_names(cursor_names);

	gds__unregister_cleanup(cleanup, 0);
}


//____________________________________________________________
//
//		the cleanup handler called when a database is closed
//
static void cleanup_database(SLONG** db_handle, SLONG dummy)
{
	if (!db_handle || !databases) {
		return;
	}

	// for each of the statements in this database, remove it
	// from the local list and from the hash table

	STMT* stmt_ptr = &statements;
	STMT p;

	while (p = *stmt_ptr)
	{
		if (p->stmt_db_handle == *db_handle)
		{
			*stmt_ptr = p->stmt_next;
			if (p->stmt_stmt) {
				remove_name(p->stmt_stmt, &statement_names);
			}
			if (p->stmt_cursor) {
				remove_name(p->stmt_cursor, &cursor_names);
			}
			gds__free(p);
		}
		else
		{
			stmt_ptr = &p->stmt_next;
		}
	}

	DBB dbb;

	for (DBB* dbb_ptr = &databases; dbb = *dbb_ptr; dbb_ptr = &dbb->dbb_next)
	{
		if (dbb->dbb_database_handle == *db_handle)
		{
			*dbb_ptr = dbb->dbb_next;
			gds__free(dbb);
			break;
		}
	}
}


//____________________________________________________________
//
//	An error returned has been trapped.  If the user specified
//	a status vector, return a status code.  Otherwise print the
//	error code(s) and abort.
//
static STATUS error()
{
	if (UDSQL_error->dsql_user_status) {
		return UDSQL_error->dsql_user_status[1];
	}

	gds__print_status(UDSQL_error->dsql_status);

	exit(UDSQL_error->dsql_status[1]);
}


//____________________________________________________________
//
//	Post an error sequence to the status vector.  Since an error
//	sequence can, in theory, be arbitrarily lock, pull a cheap
//	trick to get the address of the argument vector.
//
//	this is a copy of the routine found in err.c with the
//	exception that it uses a different error block - one which
//	is local to the V3 DSQL routines...
//
static void error_post(STATUS status, ...)
{
	va_list	args;
	STATUS*	p;
	int		type;

/* Get the addresses of the argument vector and the status vector, and do
   word-wise copy. */

	VA_START(args, status);
	p = UDSQL_error->dsql_status;

/* Copy first argument */

	*p++ = gds_arg_gds;
	*p++ = status;

/* Pick up remaining args */

	while (type = va_arg(args, int))
	{
		switch (*p++ = type) {
		case gds_arg_gds:
			*p++ = (STATUS) va_arg(args, STATUS);
			break;

		case gds_arg_number:
			*p++ = (STATUS) va_arg(args, SLONG);
			break;

		case gds_arg_vms:
		case gds_arg_unix:
		case gds_arg_domain:
		case gds_arg_dos:
		case gds_arg_mpexl:
		case gds_arg_mpexl_ipc:
		case gds_arg_netware:
		case gds_arg_win32:
			*p++ = va_arg(args, int);
			break;

		case gds_arg_string:
		case gds_arg_interpreted:
			*p++ = (STATUS) va_arg(args, TEXT *);
			break;

		case gds_arg_cstring:
			*p++ = (STATUS) va_arg(args, int);
			*p++ = (STATUS) va_arg(args, TEXT *);
			break;
		}
	}

	*p = gds_arg_end;

	// Give up whatever we were doing and return to the user.

	Firebird::status_exception::raise(UDSQL_error->dsql_status[1]);
}


//____________________________________________________________
//
//	Initialize dynamic SQL.  This is called only once.
//
static void init(HNDL* db_handle)
{
	// If we haven't been initialized yet, do it now
	if (!init_flag)
	{
		UDSQL_error = (ERR) gds__alloc((SLONG) sizeof(err));
		// FREE: by exit cleanup()
		if (!UDSQL_error) {		// NOMEM:
			return;				// Don't set the init_flag
		}
		init_flag = TRUE;
		gds__register_cleanup(cleanup, 0);
	}

	if (!db_handle) {
		return;
	}

	DBB dbb;
	for (dbb = databases; dbb; dbb = dbb->dbb_next) {
		if (dbb->dbb_database_handle == *db_handle) {
			return;
		}
	}

	dbb = (DBB) gds__alloc((SLONG) sizeof(dbb));

	// FREE: by database exit handler cleanup_database()
	if (!dbb) {					// NOMEM
		return;					// Not a great error handler
	}

	dbb->dbb_next = databases;
	databases = dbb;
	dbb->dbb_database_handle = *db_handle;

	STATUS local_status[20];
	gds__database_cleanup(local_status,
						  reinterpret_cast<hndl**>(db_handle),
						  reinterpret_cast<void(*)()>(cleanup_database),
						  (SLONG) FALSE);
}


static NAME insert_name( TEXT * symbol, NAME* list_ptr, STMT stmt)
{
/**************************************
 *
 *	i n s e r t _ n a m e
 *
 **************************************
 *
 * Functional description
 * 	Add the name to the designated list.
 *
 **************************************/
	USHORT l;
	NAME name;
	TEXT *p;

	l = name_length(symbol);
	name = (NAME) gds__alloc((SLONG) sizeof(name) + l);
/* FREE: by exit handler cleanup() or database_cleanup() */
	if (!name)					/* NOMEM: */
		error_post(gds_virmemexh, 0);
	name->name_stmt = stmt;
	name->name_length = l;
	p = name->name_symbol;
	while (l--)
		*p++ = *symbol++;

	if (name->name_next = *list_ptr)
		name->name_next->name_prev = name;
	*list_ptr = name;
	name->name_prev = NULL;

	return name;
}


static NAME lookup_name(TEXT* name, NAME list)
{
/**************************************
 *
 *	l o o k u p _ n a m e
 *
 **************************************
 *
 * Functional description
 * 	Look up the given name in the designated list.
 *
 **************************************/

	const USHORT l = name_length(name);
	for (; list; list = list->name_next) {
		if (scompare(name, l, list->name_symbol, list->name_length)) {
			break;
		}
	}

	return list;
}


static STMT lookup_stmt(TEXT* name, NAME list, USHORT type)
{
/**************************************
 *
 *	l o o k u p _ s t m t
 *
 **************************************
 *
 * Functional description
 * 	Look up the given statement in the designated list.
 *
 **************************************/
	NAME found;

	if (found = lookup_name(name, list))
		return found->name_stmt;

	if (type == NAME_statement) {
		error_post(gds_dsql_error,
				   gds_arg_gds, gds_sqlerr, gds_arg_number, (SLONG) - 518,
				   gds_arg_gds, gds_dsql_request_err, 0);
	} else {
		error_post(gds_dsql_error,
				   gds_arg_gds, gds_sqlerr, gds_arg_number, (SLONG) - 504,
				   gds_arg_gds, gds_dsql_cursor_err, 0);
	}
	return NULL;
}


static USHORT name_length( TEXT * name)
{
/**************************************
 *
 *	n a m e _ l e n g t h
 *
 **************************************
 *
 * Functional description
 *	Compute length of user supplied name.
 *
 **************************************/
	TEXT *p;

	for (p = name; *p && *p != ' '; p++);

	return (USHORT) (p - name);
}


static void remove_name(NAME name, NAME* list_ptr)
{
/**************************************
 *
 *	r e m o v e _ n a m e
 *
 **************************************
 *
 * Functional description
 * 	Remove a name from the designated list.
 *
 **************************************/

	if (name->name_next) {
		name->name_next->name_prev = name->name_prev;
	}

	if (name->name_prev) {
		name->name_prev->name_next = name->name_next;
	} else {
		*list_ptr = name->name_next;
	}

	gds__free(name);
}


static BOOLEAN scompare(register SCHAR*	string1,
						register USHORT	length1,
						register SCHAR*	string2,
						register USHORT	length2)
{
/**************************************
 *
 *	s c o m p a r e
 *
 **************************************
 *
 * Functional description
 *	Compare two strings case insensitive according to the C locale rules.
 *
 **************************************/

	if (length1 != length2) {
		return FALSE;
	}

	SCHAR c1, c2;
	while (length1--)
	{
		c1 = *string1++;
		c2 = *string2++;
		if (c1 != c2 && UPPER7(c1) != UPPER7(c2))
		{
			return FALSE;
		}
	}

	return TRUE;
}


}	// extern "C"
