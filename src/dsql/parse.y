%{
/* 
 *	PROGRAM:	Dynamic SQL runtime support
 *	MODULE:		parse.y
 *	DESCRIPTION:	Dynamic SQL parser
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
 * 2002-02-24 Sean Leyne - Code Cleanup of old Win 3.1 port (WINDOWS_ONLY)
 * 2001.05.20 Neil McCalden: Allow a udf to be used in a 'group by' clause.
 * 2001.05.30 Claudio Valderrama: DROP TABLE and DROP VIEW lead now to two
 *   different node types so DDL can tell which is which.
 * 2001.06.13 Claudio Valderrama: SUBSTRING is being surfaced.
 * 2001.06.30 Claudio valderrama: Feed (line,column) for each node. See node.h.
 * 2001.07.10 Claudio Valderrama: Better (line,column) report and "--" for comments.
 * 2001.07.28 John Bellardo: Changes to support parsing LIMIT and FIRST
 * 2001.08.03 John Bellardo: Finalized syntax for LIMIT, change LIMIT to SKIP
 * 2001.08.05 Claudio Valderrama: closed Bug #448062 and other spaces that appear
 *   in rdb$*_source fields when altering domains plus one unexpected null pointer.
 * 2001.08.12 Claudio Valderrama: adjust SUBSTRING's starting pos argument here
 *   and not in gen.c; this closes Bug #450301.
 * 2001.10.01 Claudio Valderrama: enable explicit GRANT...to ROLE role_name.
 * 2001.10.06 Claudio Valderrama: Honor explicit USER keyword in GRANTs and REVOKEs.
 * 2002.07.05 Mark O'Donohue: change keyword DEBUG to KW_DEBUG to avoid
 *            clashes with normal DEBUG macro.
 * 2002.07.30 Arno Brinkman:  
 * 2002.07.30 	Let IN predicate handle value_expressions
 * 2002.07.30 	tokens CASE, NULLIF, COALESCE added
 * 2002.07.30 	See block < CASE expression > what is added to value as case_expression
 * 2002.07.30 	function is split up into aggregate_function, numeric_value_function, string_value_function, generate_value_function
 * 2002.07.30 	new group_by_function and added to grp_column_elem
 * 2002.07.30 	cast removed from function and added as cast_specification to value
 * 2002.08.04 Claudio Valderrama: allow declaring and defining variables at the same time
 * 2002.08.04 Dmitry Yemanov: ALTER VIEW
 * 2002.08.06 Arno Brinkman: ordinal added to grp_column_elem for using positions in group by
 * 2002.08.07 Dmitry Yemanov: INT64/LARGEINT are replaced with BIGINT and available in dialect 3 only
 * 2002.08.31 Dmitry Yemanov: allowed user-defined index names for PK/FK/UK constraints
 * 2002.09.01 Dmitry Yemanov: RECREATE VIEW
 * 2002.09.28 Dmitry Yemanov: Reworked internal_info stuff, enhanced
 *                            exception handling in SPs/triggers,
 *                            implemented ROWS_AFFECTED system variable
 * 2002.10.21 Nickolay Samofatov: Added support for explicit pessimistic locks
 * 2002.10.29 Nickolay Samofatov: Added support for savepoints
 * 2002.12.03 Dmitry Yemanov: Implemented ORDER BY clause in subqueries.
 * 2002.12.18 Dmitry Yemanov: Added support for SQL-compliant labels and LEAVE statement
 * 2002.12.28 Dmitry Yemanov: Added support for parametrized events.
 * 2003.01.14 Dmitry Yemanov: Fixed bug with cursors in triggers.
 * 2003.01.15 Dmitry Yemanov: Added support for runtime trigger action checks.
 * 2003.02.10 Mike Nordell  : Undefined Microsoft introduced macros to get a clean compile.
 */

#if defined(DEV_BUILD) && defined(WIN_NT) && defined(SUPERSERVER)
#include <windows.h>
/*#include <wincon.h>*/
#endif

#include "firebird.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../jrd/common.h"
#include <stdarg.h>

#include "gen/iberror.h"
#include "../dsql/dsql.h"
#include "../dsql/node.h"
#include "../dsql/sym.h"
#include "../jrd/gds.h"
#include "../jrd/flags.h"
#include "../dsql/alld_proto.h"
#include "../dsql/errd_proto.h"
#include "../dsql/hsh_proto.h"
#include "../dsql/make_proto.h"
#include "../dsql/parse_proto.h"
#include "../dsql/keywords.h"
#include "../dsql/misc_func.h"
#include "../jrd/gds_proto.h"
#include "../jrd/thd_proto.h"
#include "../wal/wal.h"

/* Can't include ../jrd/err_proto.h here because it pulls jrd.h. */
#if !defined(JRD_ERR_PROTO_H)
extern "C" TEXT *DLL_EXPORT ERR_string(const TEXT*, int);
#endif

ASSERT_FILENAME

static void	yyerror (TEXT *);

/* since UNIX isn't standard, we have to define
   stuff which is in <limits.h> (which isn't available
   on all UNIXes... */

#define SHRT_POS_MAX		32767
#define SHRT_UNSIGNED_MAX	65535
#define SHRT_NEG_MAX		32768
#define LONG_POS_MAX		2147483647
#define POSITIVE		0
#define NEGATIVE		1
#define UNSIGNED		2

#define MIN_CACHE_BUFFERS       250
#define DEF_CACHE_BUFFERS       1000

// TMN: Remove Microsoft introduced defines
#ifdef DELETE
#undef DELETE
#endif
#ifdef IN
#undef IN
#endif
#ifdef SHARED /* sys/mman.h */
#undef SHARED
#endif

/* Fix 69th procedure problem - solution from Oleg Loa */
#define YYSTACKSIZE		2048
#define YYMAXDEPTH		2048

#define YYSTYPE		DSQL_NOD
#if defined(DEBUG) || defined(DEV_BUILD)
#define YYDEBUG		1
#endif

static const char INTERNAL_FIELD_NAME [] = "DSQL internal"; /* NTX: placeholder */
static const char NULL_STRING [] = "";	

#define TRIGGER_TYPE_SUFFIX(slot1, slot2, slot3) \
	((slot1 << 1) | (slot2 << 3) | (slot3 << 5))

extern "C" {

#ifndef SHLIB_DEFS
DSQL_NOD		DSQL_parse;
#else
extern DSQL_NOD	DSQL_parse;
#endif

}	// extern "C"
static DSQL_FLD	g_field;
static FIL	g_file;
static DSQL_NOD	g_field_name;
static TEXT	*beginning;
static SSHORT log_defined, cache_defined;
static void	yyerror (TEXT *);

#define YYPARSE_PARAM_TYPE
#define YYPARSE_PARAM USHORT client_dialect, USHORT db_dialect, USHORT parser_version, BOOLEAN *stmt_ambiguous

#include "../dsql/chars.h"

#define MAX_TOKEN_LEN   256
#define CHECK_BOUND(to)\
    {\
    if ((to - string) >= MAX_TOKEN_LEN)        \
	yyabandon (-104, isc_token_too_long); \
    }
#define CHECK_COPY_INCR(to,ch){CHECK_BOUND(to); *to++=ch;}

static int dsql_debug;

static TEXT	*lex_position (void);
#ifdef NOT_USED_OR_REPLACED
static BOOLEAN	long_int (DSQL_NOD, SLONG *);
#endif
static DSQL_FLD	make_field (DSQL_NOD);
static FIL	make_file (void);
static DSQL_NOD	make_list (DSQL_NOD);
static DSQL_NOD	make_node (NOD_TYPE, int, ...);
static DSQL_NOD	make_parameter (void);
static DSQL_NOD	make_flag_node (NOD_TYPE, SSHORT, int, ...);
static void	prepare_console_debug (int, int  *);
#ifdef NOT_USED_OR_REPLACED
static BOOLEAN	short_int (DSQL_NOD, SLONG *, SSHORT);
#endif
static void	stack_nodes (DSQL_NOD, DLLS *);
static int	yylex (USHORT, USHORT, USHORT, BOOLEAN *);
static void	yyabandon (SSHORT, STATUS);
static void	check_log_file_attrs (void);

static TEXT	*ptr, *end, *last_token, *line_start;
static TEXT	*last_token_bk, *line_start_bk;
static SSHORT	lines, att_charset;
static SSHORT	lines_bk;
static USHORT   param_number;

%}


/* token declarations */

/* Tokens are organized chronologically by date added.
   See dsql/keywords.cpp for a list organized alphabetically */

/* Tokens in v4.0 -- not separated into v3 and v4 tokens */

%token ACTIVE
%token ADD
%token AFTER
%token ALL
%token ALTER
%token AND
%token ANY
%token AS
%token ASC
%token AT
%token AVG
%token AUTO
%token BASENAME
%token BEFORE
%token BEGIN
%token BETWEEN
%token BLOB
%token BY
%token CACHE
%token CAST
%token CHARACTER
%token CHECK
%token CHECK_POINT_LEN
%token COLLATE
%token COMMA
%token COMMIT
%token COMMITTED
%token COMPUTED
%token CONCATENATE
%token CONDITIONAL
%token CONSTRAINT
%token CONTAINING
%token COUNT
%token CREATE
%token CSTRING
%token CURRENT
%token CURSOR
%token DATABASE
%token DATE
%token DB_KEY
%token KW_DEBUG
%token DECIMAL
%token DECLARE
%token DEFAULT
%token DELETE
%token DESC
%token DISTINCT
%token DO
%token DOMAIN
%token DROP
%token ELSE
%token END
%token ENTRY_POINT
%token EQL
%token ESCAPE
%token EXCEPTION
%token EXECUTE
%token EXISTS
%token EXIT
%token EXTERNAL
%token FILTER
%token FOR
%token FOREIGN
%token FROM
%token FULL
%token FUNCTION
%token GDSCODE
%token GEQ
%token GENERATOR
%token GEN_ID
%token GRANT
%token GROUP
%token GROUP_COMMIT_WAIT
%token GTR
%token HAVING
%token IF
%token IN
%token INACTIVE
%token INNER
%token INPUT_TYPE
%token INDEX
%token INSERT
%token INTEGER
%token INTO
%token IS
%token ISOLATION
%token JOIN
%token KEY
%token KW_CHAR
%token KW_DEC
%token KW_DOUBLE
%token KW_FILE
%token KW_FLOAT
%token KW_INT
%token KW_LONG
%token KW_NULL
%token KW_NUMERIC
%token KW_UPPER
%token KW_VALUE
%token LENGTH
%token LOGFILE
%token LPAREN
%token LEFT
%token LEQ
%token LEVEL
%token LIKE
%token LOG_BUF_SIZE
%token LSS
%token MANUAL
%token MAXIMUM
%token MAX_SEGMENT
%token MESSAGE
%token MERGE
%token MINIMUM
%token MODULE_NAME
%token NAMES
%token NATIONAL
%token NATURAL
%token NCHAR
%token NEQ
%token NO
%token NOT
%token NOT_GTR
%token NOT_LSS
%token NUM_LOG_BUFS
%token OF
%token ON
%token ONLY
%token OPTION
%token OR
%token ORDER
%token OUTER
%token OUTPUT_TYPE
%token OVERFLOW
%token PAGE
%token PAGES
%token PAGE_SIZE
%token PARAMETER
%token PASSWORD
%token PLAN
%token POSITION
%token POST_EVENT
%token PRECISION
%token PRIMARY
%token PRIVILEGES
%token PROCEDURE
%token PROTECTED
%token RAW_PARTITIONS
%token READ
%token REAL
%token REFERENCES
%token RESERVING
%token RETAIN
%token RETURNING_VALUES
%token RETURNS
%token REVOKE
%token RIGHT
%token RPAREN
%token ROLLBACK
%token SEGMENT
%token SELECT
%token SET
%token SHADOW
%token SHARED
%token SINGULAR
%token KW_SIZE
%token SMALLINT
%token SNAPSHOT
%token SOME
%token SORT
%token SQLCODE
%token STABILITY
%token STARTING
%token STATISTICS
%token SUB_TYPE
%token SUSPEND
%token SUM
%token TABLE
%token THEN
%token TO
%token TRANSACTION
%token TRIGGER
%token UNCOMMITTED
%token UNION
%token UNIQUE
%token UPDATE
%token USER
%token VALUES
%token VARCHAR
%token VARIABLE
%token VARYING
%token VERSION
%token VIEW
%token WAIT
%token WHEN
%token WHERE
%token WHILE
%token WITH
%token WORK
%token WRITE

%token FLOAT_NUMBER NUMBER NUMERIC SYMBOL STRING INTRODUCER 

/* New tokens added v5.0 */

%token ACTION
%token ADMIN
%token CASCADE
%token FREE_IT			/* ISC SQL extension */
%token RESTRICT
%token ROLE

/* New tokens added v6.0 */

%token COLUMN
%token TYPE
%token EXTRACT
%token YEAR
%token MONTH
%token DAY
%token HOUR
%token MINUTE
%token SECOND
%token WEEKDAY			/* ISC SQL extension */
%token YEARDAY			/* ISC SQL extension */
%token TIME
%token TIMESTAMP
%token CURRENT_DATE
%token CURRENT_TIME
%token CURRENT_TIMESTAMP

/* special aggregate token types returned by lex in v6.0 */

%token NUMBER64BIT SCALEDINT

/* CVC: Special Firebird additions. */

%token CURRENT_USER
%token CURRENT_ROLE
%token KW_BREAK
%token SUBSTRING
%token RECREATE
%token KW_DESCRIPTOR
%token FIRST
%token SKIP

/* tokens added for Firebird 1.5 */

%token CURRENT_CONNECTION
%token CURRENT_TRANSACTION
%token BIGINT
%token CASE
%token NULLIF
%token COALESCE
%token USING
%token NULLS
%token LAST
%token ROW_COUNT
%token LOCK
%token SAVEPOINT
%token STATEMENT
%token LEAVE
%token INSERTING
%token UPDATING
%token DELETING

/* precedence declarations for expression evaluation */

%left	OR
%left	AND
%left	NOT
%left	'=' '<' '>' LIKE EQL NEQ GTR LSS GEQ LEQ NOT_GTR NOT_LSS
%left	'+' '-'
%left	'*' '/'
%left	CONCATENATE
%left	COLLATE

/* Fix the dangling IF-THEN-ELSE problem */
%nonassoc THEN
%nonassoc ELSE

/* The same issue exists with ALTER COLUMN now that keywords can be used
   in order to change their names.  The syntax which shows the issue is:
     ALTER COLUMN where column is part of the alter statement
       or
     ALTER COLUMN where column is the name of the column in the relation
*/
%nonassoc ALTER
%nonassoc COLUMN

%%

/* list of possible statements */

top		: statement
			{ DSQL_parse = $1; }
		| statement ';'
			{ DSQL_parse = $1; }
		;

statement	: alter
		| blob
		| commit
		| create
		| declare
		| delete
		| drop
		| grant
		| insert
		| invoke_procedure
        | recreate
		| replace
		| revoke
		| rollback
		| user_savepoint
		| undo_savepoint
		| select
		| set
		| update
		| KW_DEBUG signed_short_integer
			{ prepare_console_debug ((int) $2, &yydebug);
			  $$ = make_node (nod_null, (int) 0, NULL); }
		;


/* GRANT statement */

grant	 	: GRANT privileges ON prot_table_name
			TO user_grantee_list grant_option
			{ $$ = make_node (nod_grant, (int) e_grant_count, 
					$2, $4, make_list($6), $7); }
		| GRANT proc_privileges ON PROCEDURE simple_proc_name
			TO user_grantee_list grant_option
			{ $$ = make_node (nod_grant, (int) e_grant_count, 
					$2, $5, make_list($7), $8); }
		| GRANT privileges ON prot_table_name
			TO grantee_list
			{ $$ = make_node (nod_grant, (int) e_grant_count, 
					$2, $4, make_list($6), NULL); }
		| GRANT proc_privileges ON PROCEDURE simple_proc_name
			TO grantee_list
			{ $$ = make_node (nod_grant, (int) e_grant_count, 
					$2, $5, make_list($7), NULL); }
		| GRANT role_name_list TO role_grantee_list role_admin_option
			{ $$ = make_node (nod_grant, (int) e_grant_count, 
					make_list($2), make_list($4), NULL, $5); }
                ; 

prot_table_name	: simple_table_name
		| TABLE simple_table_name
			{ $$ = $2; }
		;

privileges	: ALL
			{ $$ = make_node (nod_all, (int) 0, NULL); }
		| ALL PRIVILEGES
			{ $$ = make_node (nod_all, (int) 0, NULL); }
		| privilege_list
			{ $$ = make_list ($1); }
		;

privilege_list	: privilege
		| privilege_list ',' privilege
			{ $$ = make_node (nod_list, (int) 2, $1, $3); }
		;

proc_privileges	: EXECUTE
			{ $$ = make_list (make_node (nod_execute, (int) 0, NULL)); }
		;

privilege	: SELECT
			{ $$ = make_node (nod_select, (int) 0, NULL); }
		| INSERT
			{ $$ = make_node (nod_insert, (int) 0, NULL); }
		| DELETE
			{ $$ = make_node (nod_delete, (int) 0, NULL); }
		| UPDATE column_parens_opt
			{ $$ = make_node (nod_update, (int) 1, $2); }
		| REFERENCES column_parens_opt
			{ $$ = make_node (nod_references, (int) 1, $2); }
		;

grant_option	: WITH GRANT OPTION
			{ $$ = make_node (nod_grant, (int) 0, NULL); }
		|
			{ $$ = 0; }
		;

role_admin_option   : WITH ADMIN OPTION
            { $$ = make_node (nod_grant_admin, (int) 0, NULL); }
        |
            { $$ = 0; }
        ;

simple_proc_name: symbol_procedure_name
			{ $$ = make_node (nod_procedure_name, (int) 1, $1); }
		;


/* REVOKE statement */

revoke	 	: REVOKE rev_grant_option privileges ON prot_table_name
			FROM user_grantee_list
			{ $$ = make_node (nod_revoke, 
				(int) e_grant_count, $3, $5,
				make_list($7), $2); }
		| REVOKE rev_grant_option proc_privileges ON
			PROCEDURE simple_proc_name FROM user_grantee_list
			{ $$ = make_node (nod_revoke, 
				(int) e_grant_count, $3, $6,
				make_list($8), $2); }
		| REVOKE privileges ON prot_table_name
			FROM user_grantee_list
			{ $$ = make_node (nod_revoke, 
				(int) e_grant_count, $2, $4,
				make_list($6), NULL); }
		| REVOKE proc_privileges ON
			PROCEDURE simple_proc_name FROM user_grantee_list
			{ $$ = make_node (nod_revoke, 
				(int) e_grant_count, $2, $5,
				make_list($7), NULL); }
		| REVOKE privileges ON prot_table_name
			FROM grantee_list
			{ $$ = make_node (nod_revoke, 
				(int) e_grant_count, $2, $4,
				make_list($6), NULL); }
		| REVOKE proc_privileges ON
			PROCEDURE simple_proc_name FROM grantee_list
			{ $$ = make_node (nod_revoke, 
				(int) e_grant_count, $2, $5,
				make_list($7), NULL); }
		| REVOKE role_name_list FROM role_grantee_list
			{ $$ = make_node (nod_revoke, 
				(int) e_grant_count, make_list($2), make_list($4),
				NULL, NULL); }
                ; 

rev_grant_option : GRANT OPTION FOR
			{ $$ = make_node (nod_grant, (int) 0, NULL); }
		;

grantee_list	: grantee
		| grantee_list ',' grantee
			{ $$ = make_node (nod_list, (int) 2, $1, $3); }
		| grantee_list ',' user_grantee
			{ $$ = make_node (nod_list, (int) 2, $1, $3); }
		| user_grantee_list ',' grantee
			{ $$ = make_node (nod_list, (int) 2, $1, $3); }
		;

grantee : PROCEDURE symbol_procedure_name
		{ $$ = make_node (nod_proc_obj, (int) 1, $2); }
	| TRIGGER symbol_trigger_name
		{ $$ = make_node (nod_trig_obj, (int) 1, $2); }
	| VIEW symbol_view_name
		{ $$ = make_node (nod_view_obj, (int) 1, $2); }
	| ROLE symbol_role_name
	        { $$ = make_node (nod_role_name, (int) 1, $2); }
	;

user_grantee_list : user_grantee
		| user_grantee_list ',' user_grantee
			{ $$ = make_node (nod_list, (int) 2, $1, $3); }
		;

/* CVC: In the future we can deprecate the first implicit form since we'll support
explicit grant/revoke for both USER and ROLE keywords & object types. */

user_grantee	: symbol_user_name
		{ $$ = make_node (nod_user_name, (int) 1, $1); }
	| USER symbol_user_name
        { $$ = make_node (nod_user_name, (int) 2, $2, NULL); }
	| GROUP symbol_user_name
		{ $$ = make_node (nod_user_group, (int) 1, $2); }
	;

role_name_list  : role_name
        | role_name_list ',' role_name
            { $$ = make_node (nod_list, (int) 2, $1, $3); }
        ;

role_name   : symbol_role_name
        { $$ = make_node (nod_role_name, (int) 1, $1); }
        ;

role_grantee_list  : role_grantee
        | role_grantee_list ',' role_grantee
            { $$ = make_node (nod_list, (int) 2, $1, $3); }
        ;

role_grantee   : symbol_user_name
        { $$ = make_node (nod_user_name, (int) 1, $1); }
    | USER symbol_user_name
        { $$ = make_node (nod_user_name, (int) 1, $2); }
        ;


/* DECLARE operations */

declare		: DECLARE declare_clause
			{ $$ = $2;}
		;

declare_clause  : FILTER filter_decl_clause
			{ $$ = $2; }
		| EXTERNAL FUNCTION udf_decl_clause
			{ $$ = $3; }
		;


udf_decl_clause : symbol_UDF_name arg_desc_list1 RETURNS return_value1
			ENTRY_POINT sql_string MODULE_NAME sql_string
		    	{ $$ = make_node (nod_def_udf, (int) e_udf_count, 
				$1, $6, $8, make_list ($2), $4); }
		;

udf_data_type	: simple_type
		| BLOB
			{ g_field->fld_dtype = dtype_blob; }
		| CSTRING '(' pos_short_integer ')' charset_clause
			{ 
			g_field->fld_dtype = dtype_cstring; 
			g_field->fld_character_length = (USHORT) $3; }
		;

arg_desc_list1	: 
		 	{ $$ = (DSQL_NOD) NULL; }
		| arg_desc_list	
		| '(' arg_desc_list ')'	
		 	{ $$ = $2; }
		;

arg_desc_list	: arg_desc
		| arg_desc_list ',' arg_desc
			{ $$ = make_node (nod_list, (int) 2, $1, $3); }
		;

/*arg_desc	: init_data_type udf_data_type
  { $$ = $1; } */
arg_desc	: init_data_type udf_data_type
            { $$ = make_node (nod_udf_param, (int) e_udf_param_count,
				              $1, NULL); }
		| init_data_type udf_data_type BY KW_DESCRIPTOR
			{ $$ = make_node (nod_udf_param, (int) e_udf_param_count,
				$1, MAKE_constant ((STR) FUN_descriptor, CONSTANT_SLONG)); }
		;


return_value1	: return_value
		| '(' return_value ')'
			{ $$ = $2; }
		;
return_value	: init_data_type udf_data_type
			{ $$ = make_node (nod_udf_return_value, (int) 2, $1, 
				MAKE_constant ((STR) FUN_reference, CONSTANT_SLONG));}
		| init_data_type udf_data_type FREE_IT
			{ $$ = make_node (nod_udf_return_value, (int) 2, $1, 
				MAKE_constant ((STR) (-1 * FUN_reference), CONSTANT_SLONG));}
                                         /* FUN_refrence with FREE_IT is -ve */
		| init_data_type udf_data_type BY KW_VALUE
			{ $$ = make_node (nod_udf_return_value, (int) 2, $1, 
				MAKE_constant ((STR) FUN_value, CONSTANT_SLONG));}
/* CVC: Enable return by descriptor for the future.*/
		| init_data_type udf_data_type BY KW_DESCRIPTOR
			{ $$ = make_node (nod_udf_return_value, (int) 2, $1,
				MAKE_constant ((STR) FUN_descriptor, CONSTANT_SLONG));}
		| PARAMETER pos_short_integer
			{ $$ = make_node (nod_udf_return_value, (int) 2, 
		  		(DSQL_NOD) NULL, MAKE_constant ((STR) $2, CONSTANT_SLONG));}
		;

filter_decl_clause : symbol_filter_name INPUT_TYPE blob_subtype OUTPUT_TYPE blob_subtype 
			ENTRY_POINT sql_string MODULE_NAME sql_string
		    	{ $$ = make_node (nod_def_filter, (int) e_filter_count, 
						$1, $3, $5, $7, $9); }
		;


/* CREATE metadata operations */

create	 	: CREATE create_clause
			{ $$ = $2; }
                ; 

create_clause	: EXCEPTION symbol_exception_name sql_string
			{ $$ = make_node (nod_def_exception, (int) e_xcp_count, 
						$2, $3); }
		| unique_opt order_direction INDEX symbol_index_name ON simple_table_name index_definition
			{ $$ = make_node (nod_def_index, (int) e_idx_count, 
					$1, $2, $4, $6, $7); }
		| PROCEDURE procedure_clause
			{ $$ = $2; }
		| TABLE table_clause
			{ $$ = $2; }
		| TRIGGER def_trigger_clause
			{ $$ = $2; }
		| VIEW view_clause
			{ $$ = $2; }
		| GENERATOR generator_clause
			{ $$ = $2; }
		| DATABASE db_clause
			{ $$ = $2; }
		| DOMAIN   domain_clause
			{ $$ = $2; }             
		| SHADOW shadow_clause
			{ $$ = $2; }             
		| ROLE role_clause
			{ $$ = $2; }
		;


recreate 	: RECREATE recreate_clause
			{ $$ = $2; }
		;

recreate_clause	: PROCEDURE rprocedure_clause
			{ $$ = $2; }
		| TABLE rtable_clause
			{ $$ = $2; }
		| VIEW rview_clause
			{ $$ = $2; }
/*
		| TRIGGER def_trigger_clause
			{ $$ = $2; }
		| DOMAIN rdomain_clause
			{ $$ = $2; }             
*/
		;

replace	: CREATE OR ALTER replace_clause
			{ $$ = $4; }
		;

replace_clause	: PROCEDURE replace_procedure_clause
			{ $$ = $2; }
		| TRIGGER replace_trigger_clause
			{ $$ = $2; }
/*
		| VIEW replace_view_clause
			{ $$ = $2; }
*/
		;


/* CREATE INDEX */

unique_opt	: UNIQUE
			{ $$ = make_node (nod_unique, 0, NULL); }
		|
			{ $$ = NULL; }
		;

index_definition : column_list 
			{ $$ = make_list ($1); }
		| column_parens 
		| computed_by '(' begin_trigger value end_trigger ')'
			{ $$ = make_node (nod_def_computed, 2, $4, $5); }
		;


/* CREATE SHADOW */
shadow_clause	: pos_short_integer manual_auto conditional sql_string
			first_file_length sec_shadow_files
		 	{ $$ = make_node (nod_def_shadow, (int) e_shadow_count,
			     $1, $2, $3, $4, $5, make_list ($6)); }
		;

manual_auto	: MANUAL
			{ $$ = MAKE_constant ((STR) 1, CONSTANT_SLONG); } 
		| AUTO
			{ $$ = MAKE_constant ((STR) 0, CONSTANT_SLONG); } 
		| 
			{ $$ = MAKE_constant ((STR) 0, CONSTANT_SLONG); } 
		;

conditional	: 
			{ $$ = MAKE_constant ((STR) 0, CONSTANT_SLONG); }
		| CONDITIONAL
			{ $$ = MAKE_constant ((STR) 1, CONSTANT_SLONG); }
		;	

first_file_length : 
			{ $$ = (DSQL_NOD) 0;}
		| LENGTH equals long_integer page_noise
			{ $$ = $3; }
		;

sec_shadow_files :
		 	{ $$ = (DSQL_NOD) NULL; }
		| db_file_list
		;

db_file_list    : db_file
		| db_file_list db_file
			{ $$ = make_node (nod_list, (int) 2, $1, $2); }
		;


/* CREATE DOMAIN */

domain_clause	: column_def_name
		  as_opt
		  data_type
		  begin_trigger
		  domain_default_opt
		  end_trigger
		  domain_constraint_clause
		  collate_clause
			{ $$ = make_node (nod_def_domain, (int) e_dom_count,
                                          $1, $5, $6, make_list ($7), $8); }
                ;

/*
rdomain_clause	: DOMAIN alter_column_name alter_domain_ops
                        { $$ = make_node (nod_mod_domain, (int) e_alt_count,
                                          $2, make_list ($3)); }
*/

as_opt		: AS
                  { $$ = NULL; }
                | 
                  { $$ = NULL; }  
                ;

domain_default_opt	: DEFAULT begin_trigger default_value
				{ $$ = $3; }
			|
				{ $$ = (DSQL_NOD) NULL; }
			;

domain_constraint_clause	: 
                                  { $$ = (DSQL_NOD) NULL; }
				| domain_constraint_list
                                ; 

domain_constraint_list  	: domain_constraint_def
				| domain_constraint_list domain_constraint_def
                                  { $$ = make_node (nod_list, (int) 2, $1, $2); }
                        ;
domain_constraint_def		: domain_constraint
				  { $$ = make_node (nod_rel_constraint, (int) 2, NULL, $1);}
 
                        ;	

domain_constraint	  	: null_constraint
				| domain_check_constraint
                        	;
                                
null_constraint			: NOT KW_NULL
                                  { $$ = make_node (nod_null, (int) 0, NULL); }
                                ;

domain_check_constraint 	: begin_trigger CHECK '(' search_condition ')' end_trigger
				  { $$ = make_node (nod_def_constraint, 
				  (int) e_cnstr_count, MAKE_string(NULL_STRING, 0), NULL, 
				  NULL, NULL, $4, NULL, $6, NULL, NULL); }
                                ;


/* CREATE GENERATOR */

generator_clause : symbol_generator_name
			{ $$ = make_node (nod_def_generator, 
						(int) e_gen_count, $1); }
		 ;


/* CREATE ROLE */

role_clause : symbol_role_name
			{ $$ = make_node (nod_def_role, 
						(int) 1, $1); }



/* CREATE DATABASE */

db_clause	:  db_name db_initial_desc1 db_rem_desc1
			{ $$ = make_node (nod_def_database, (int) e_cdb_count,
				 $1, make_list($2), make_list ($3));}
		;

equals		:
		| '='
		;

db_name		: sql_string
			{ log_defined = FALSE;
			  cache_defined = FALSE;
			  $$ = (DSQL_NOD) $1; }
		;

db_initial_desc1 :  
			{$$ = (DSQL_NOD) NULL;}
		| db_initial_desc
		;

db_initial_desc : db_initial_option
		| db_initial_desc db_initial_option
			{ $$ = make_node (nod_list, 2, $1, $2); }
		; 
 
db_initial_option: PAGE_SIZE equals pos_short_integer 
			{ $$ = make_node (nod_page_size, 1, $3);}
		| LENGTH equals long_integer page_noise
			{ $$ = make_node (nod_file_length, 1, $3);}
		| USER sql_string
			{ $$ = make_node (nod_user_name, 1, $2);} 
		| PASSWORD sql_string	
			{ $$ = make_node (nod_password, 1, $2);} 
		| SET NAMES sql_string	
			{ $$ = make_node (nod_lc_ctype, 1, $3);} 
		;

db_rem_desc1	:  
			{$$ = (DSQL_NOD) NULL;} 
		| db_rem_desc
		;

db_rem_desc	: db_rem_option
		| db_rem_desc db_rem_option
			{ $$ = make_node (nod_list, 2, $1, $2); }
		;

db_rem_option   : db_file  
/*   		| db_cache */
		| db_log
		| db_log_option
		| DEFAULT CHARACTER SET symbol_character_set_name
			{ $$ = make_node (nod_dfl_charset, 1, $4);} 
		;

db_log_option   : GROUP_COMMIT_WAIT equals long_integer
			{ $$ = make_node (nod_group_commit_wait, 1, $3);}
		| CHECK_POINT_LEN equals long_integer
			{ $$ = make_node (nod_check_point_len, 1, $3);}
		| NUM_LOG_BUFS equals pos_short_integer
			{ $$ = make_node (nod_num_log_buffers, 1, $3);}
		| LOG_BUF_SIZE equals unsigned_short_integer
			{ $$ = make_node (nod_log_buffer_size, 1, $3);}
		;

db_log		: db_default_log_spec
			{ if (log_defined)
			    yyabandon (-260, isc_log_redef);  /* Log redefined */
			  log_defined = TRUE;
			  $$ = $1; }
		| db_rem_log_spec
			{ if (log_defined)
			    yyabandon (-260, isc_log_redef);
			  log_defined = TRUE;
			  $$ = $1; }
		;	

db_rem_log_spec	: LOGFILE '(' logfiles ')' OVERFLOW logfile_desc
			{ g_file->fil_flags |= LOG_serial | LOG_overflow; 
			  if (g_file->fil_partitions)
			      yyabandon (-261, isc_partition_not_supp);
			/* Partitions not supported in series of log file specification */
			 $$ = make_node (nod_list, 2, $3, $6); }  
		| LOGFILE BASENAME logfile_desc
			{ g_file->fil_flags |= LOG_serial;
			  if (g_file->fil_partitions)
			      yyabandon (-261, isc_partition_not_supp);
			  $$ = $3; }
		;

db_default_log_spec : LOGFILE 
			{ g_file = make_file(); 
			  g_file->fil_flags = LOG_serial | LOG_default;
			  $$ = make_node (nod_log_file_desc, (int) 1,
						(DSQL_NOD) g_file);}
		;

db_file		: file1 sql_string file_desc1
			{ g_file->fil_name = (STR) $2; 
			  $$ = (DSQL_NOD) make_node (nod_file_desc, (int) 1,
						(DSQL_NOD) g_file); }
		;

/*
db_cache	: CACHE sql_string cache_length 
			{ 
			  if (cache_defined)
			      yyabandon (-260, isc_cache_redef);
				  */ /* Cache redefined */ /*
			  g_file = make_file();
			  g_file->fil_length = (SLONG) $3;
			  g_file->fil_name = (STR) $2;
			  cache_defined = TRUE;
			  $$ = (DSQL_NOD) make_node (nod_cache_file_desc, (int) 1,
					 (DSQL_NOD) g_file); } 
		;
*/

/*
cache_length	: 
			{ $$ = (DSQL_NOD) (SLONG) DEF_CACHE_BUFFERS; }
		| LENGTH equals long_integer page_noise
			{ if ((SLONG) $3 < MIN_CACHE_BUFFERS)
			      yyabandon (-239, isc_cache_too_small);
				  */ /* Cache length too small */ /*
			  else 
			      $$ = (DSQL_NOD) $3; }
		;
*/

logfiles	: logfile_desc 
		| logfiles ',' logfile_desc
			{ $$ = make_node (nod_list, 2, $1, $3); }
		;
logfile_desc	: logfile_name logfile_attrs 
			{ 
		         check_log_file_attrs(); 
			 $$ = (DSQL_NOD) make_node (nod_log_file_desc, (int) 1,
                                                (DSQL_NOD) g_file); }
		;
logfile_name	: sql_string 
			{ g_file = make_file();
			  g_file->fil_name = (STR) $1; } 
		;
logfile_attrs	: 
		| logfile_attrs logfile_attr
		;

logfile_attr	: KW_SIZE equals long_integer
			{ g_file->fil_length = (SLONG) $3; }
/*
		| RAW_PARTITIONS equals pos_short_integer
			{ g_file->fil_partitions = (SSHORT) $3; 
			  g_file->fil_flags |= LOG_raw; } */
		;

file1		: KW_FILE
			{ g_file  = make_file ();}
		;

file_desc1	:
		| file_desc
		;

file_desc	: file_clause
		| file_desc file_clause
		;

file_clause	: STARTING file_clause_noise long_integer
			{ g_file->fil_start = (SLONG) $3;}
		| LENGTH equals long_integer page_noise
			{ g_file->fil_length = (SLONG) $3;}
		;

file_clause_noise :
		| AT
		| AT PAGE
		;

page_noise	:
		| PAGE
		| PAGES
		;


/* CREATE TABLE */

table_clause	: simple_table_name external_file '(' table_elements ')'
			{ $$ = make_node (nod_def_relation, 
				(int) e_drl_count, $1, make_list ($4), $2); }
		;

rtable_clause	: simple_table_name external_file '(' table_elements ')'
			{ $$ = make_node (nod_redef_relation, 
				(int) e_drl_count, $1, make_list ($4), $2); }
		;

external_file	: EXTERNAL KW_FILE sql_string
			{ $$ = $3; }
		| EXTERNAL sql_string
			{ $$ = $2; }
		|
			{ $$ = (DSQL_NOD) NULL; }
		;

table_elements	: table_element
		| table_elements ',' table_element
			{ $$ = make_node (nod_list, 2, $1, $3); }
		;

table_element	: column_def
		| table_constraint_definition
		;



/* column definition */

column_def	: column_def_name data_type_or_domain default_opt 
			end_trigger column_constraint_clause collate_clause
			{ $$ = make_node (nod_def_field, (int) e_dfl_count, 
					$1, $3, $4, make_list ($5), $6, $2, NULL); }   
		| column_def_name non_array_type def_computed
			{ $$ = make_node (nod_def_field, (int) e_dfl_count, 
				    $1, NULL, NULL, NULL, NULL, NULL, $3); }   

		| column_def_name def_computed
			{ $$ = make_node (nod_def_field, (int) e_dfl_count, 
				    $1, NULL, NULL, NULL, NULL, NULL, $2); }   
		;
                                 
/* value does allow parens around it, but there is a problem getting the
 * source text
 */

def_computed	: computed_by '(' begin_trigger value end_trigger ')'
			{ 
			g_field->fld_flags |= FLD_computed;
			$$ = make_node (nod_def_computed, 2, $4, $5); }
		;

computed_by	: COMPUTED BY
		| COMPUTED
		;

data_type_or_domain	: data_type begin_trigger
                          { $$ = NULL; }
			| simple_column_name begin_string
                          { $$ = make_node (nod_def_domain, (int) e_dom_count,
                                            $1, NULL, NULL, NULL, NULL); }
                        ;

collate_clause	: COLLATE symbol_collation_name
			{ $$ = $2; }
		|
			{ $$ = (DSQL_NOD) NULL; }
		;


column_def_name	: simple_column_name
			{ g_field_name = $1;
			  g_field = make_field ($1);
			  $$ = (DSQL_NOD) g_field; }
		;

simple_column_def_name  : simple_column_name
				{ g_field = make_field ($1);
				  $$ = (DSQL_NOD) g_field; }
			;


data_type_descriptor :	init_data_type data_type
			{ $$ = $1; }

init_data_type :
			{ g_field = make_field (NULL);
			  $$ = (DSQL_NOD) g_field; }


default_opt	: DEFAULT default_value
			{ $$ = $2; }
		|
			{ $$ = (DSQL_NOD) NULL; }
		;

default_value	: constant
/*		| USER
			{ $$ = make_node (nod_user_name, (int) 0, NULL); }
		| CURRENT_USER
			{ $$ = make_node (nod_user_name, (int) 0, NULL); }*/
		| current_user
		| current_role
		| internal_info
			{ $$ = $1; }
		| null_value
			{ $$ = $1; }
		| datetime_value_expression
			{ $$ = $1; }
		;
                   
column_constraint_clause : 
				{ $$ = (DSQL_NOD) NULL; }
			| column_constraint_list
			;

column_constraint_list	: column_constraint_def
		        | column_constraint_list column_constraint_def
			{ $$ = make_node (nod_list, (int) 2, $1, $2); }
		        ;

column_constraint_def : constraint_name_opt column_constraint
            { $$ = make_node (nod_rel_constraint, (int) 2, $1, $2);}


column_constraint : NOT KW_NULL 
                        { $$ = make_node (nod_null, (int) 1, NULL); }
                  | REFERENCES simple_table_name column_parens_opt
			referential_trigger_action constraint_index_opt
                        { $$ = make_node (nod_foreign, e_for_count,
                        make_node (nod_list, (int) 1, g_field_name), $2, $3, $4, $5); }

                  | check_constraint
                  | UNIQUE constraint_index_opt
                        { $$ = make_node (nod_unique, 2, NULL, $2); }
                  | PRIMARY KEY constraint_index_opt
                        { $$ = make_node (nod_primary, e_pri_count, NULL, $3); }
		;
                    


/* table constraints */

table_constraint_definition : constraint_name_opt table_constraint
           { $$ = make_node (nod_rel_constraint, (int) 2, $1, $2);}
                      ;

constraint_name_opt : CONSTRAINT symbol_constraint_name
                      { $$ = $2; }
                    | { $$ = NULL ;}
                    ;

table_constraint : unique_constraint
			| primary_constraint
			| referential_constraint
			| check_constraint
		;

unique_constraint	: UNIQUE column_parens constraint_index_opt
			{ $$ = make_node (nod_unique, 2, $2, $3); }
		;

primary_constraint	: PRIMARY KEY column_parens constraint_index_opt
			{ $$ = make_node (nod_primary, e_pri_count, $3, $4); }
		;

referential_constraint	: FOREIGN KEY column_parens REFERENCES 
			  simple_table_name column_parens_opt 
			  referential_trigger_action constraint_index_opt
			{ $$ = make_node (nod_foreign, e_for_count, $3, $5, 
			         $6, $7, $8); }
		;

constraint_index_opt	: USING order_direction INDEX symbol_index_name
			{ $$ = make_node (nod_def_index, (int) e_idx_count, 
					NULL, $2, $4, NULL, NULL); }
/*
		| NO INDEX
			{ $$ = NULL; }
*/
		|
			{ $$ = make_node (nod_def_index, (int) e_idx_count, 
					NULL, NULL, NULL, NULL, NULL); }
		;

check_constraint : begin_trigger CHECK '(' search_condition ')' end_trigger
			{ $$ = make_node (nod_def_constraint, 
				(int) e_cnstr_count, MAKE_string(NULL_STRING, 0), NULL, 
				NULL, NULL, $4, NULL, $6, NULL, NULL); }
		;

referential_trigger_action:	
		  update_rule
		  { $$ = make_node (nod_ref_upd_del, e_ref_upd_del_count, $1, NULL);} 
		| delete_rule
		  { $$ = make_node (nod_ref_upd_del, e_ref_upd_del_count, NULL, $1);}
		| delete_rule update_rule
		  { $$ = make_node (nod_ref_upd_del, e_ref_upd_del_count, $2, $1); }
		| update_rule delete_rule
		  { $$ = make_node (nod_ref_upd_del, e_ref_upd_del_count, $1, $2);}
		| /* empty */
		  { $$ = NULL;}
		;

update_rule	: ON UPDATE referential_action
		  { $$ = $3;}
		;
delete_rule	: ON DELETE referential_action
		  { $$ = $3;}
		;

referential_action: CASCADE
		  { $$ = make_flag_node (nod_ref_trig_action, 
			 REF_ACTION_CASCADE, e_ref_trig_action_count, NULL);}
                | SET DEFAULT
		  { $$ = make_flag_node (nod_ref_trig_action, 
			 REF_ACTION_SET_DEFAULT, e_ref_trig_action_count, NULL);}
		| SET KW_NULL
		  { $$ = make_flag_node (nod_ref_trig_action, 
			 REF_ACTION_SET_NULL, e_ref_trig_action_count, NULL);}
		| NO ACTION
		  { $$ = make_flag_node (nod_ref_trig_action, 
			 REF_ACTION_NONE, e_ref_trig_action_count, NULL);}
		;


/* PROCEDURE */


procedure_clause	: symbol_procedure_name input_parameters
		     	  output_parameters
		    	  AS begin_string
			  var_declaration_list
			  full_proc_block
			  end_trigger
				{ $$ = make_node (nod_def_procedure,
						  (int) e_prc_count, 
					          $1, $2, $3, $6, $7, $8, NULL); } 
		;        


rprocedure_clause	: symbol_procedure_name input_parameters
		     	  output_parameters
		    	  AS begin_string
			  var_declaration_list
			  full_proc_block
			  end_trigger
				{ $$ = make_node (nod_redef_procedure,
						  (int) e_prc_count, 
					          $1, $2, $3, $6, $7, $8, NULL); } 
		;        

replace_procedure_clause	: symbol_procedure_name input_parameters
		     	  output_parameters
		    	  AS begin_string
			  var_declaration_list
			  full_proc_block
			  end_trigger
				{ $$ = make_node (nod_replace_procedure,
						  (int) e_prc_count, 
					          $1, $2, $3, $6, $7, $8, NULL); } 
		;        

alter_procedure_clause	: symbol_procedure_name input_parameters
		     	  output_parameters
		    	  AS begin_string
			  var_declaration_list
			  full_proc_block
			  end_trigger
				{ $$ = make_node (nod_mod_procedure,
						  (int) e_prc_count, 
					          $1, $2, $3, $6, $7, $8, NULL); } 
		;        

input_parameters :	'(' proc_parameters ')'
				{ $$ = make_list ($2); }
			|
				{ $$ = NULL; }
			;

output_parameters :	RETURNS input_parameters
				{ $$ = $2; }
			|
				{ $$ = NULL; }
			;

proc_parameters	: proc_parameter
		| proc_parameters ',' proc_parameter
			{ $$ = make_node (nod_list, 2, $1, $3); }
		;

proc_parameter	: simple_column_def_name non_array_type
			{ $$ = make_node (nod_def_field, (int) e_dfl_count, 
				$1, NULL, NULL, NULL, NULL, NULL, NULL); }   
		;


var_declaration_list	: var_declarations
				{ $$ = make_list ($1); }
			|
				{ $$ = NULL; }
			;

var_declarations	: var_declaration
			| var_declarations var_declaration
			{ $$ = make_node (nod_list, 2, $1, $2); }
		;

var_declaration : DECLARE var_decl_opt column_def_name non_array_type var_init_opt ';'
			{ $$ = make_node (nod_def_field, (int) e_dfl_count, 
				$3, $5, NULL, NULL, NULL, NULL, NULL); }   
		;

var_decl_opt	: VARIABLE
			{ $$ = NULL; }
		|
			{ $$ = NULL; }
		;

var_init_opt	: '=' default_value
			{ $$ = $2; }
		| default_opt
			{ $$ = $1; }
		;

proc_block	: proc_statement
		| full_proc_block
		;

full_proc_block	: BEGIN full_proc_block_body END
			{ $$ = $2; }
		;

full_proc_block_body	: proc_statements
			{ $$ = make_node (nod_block, e_blk_count, make_list ($1), NULL); }
		| proc_statements excp_hndl_statements
			{ $$ = make_node (nod_block, e_blk_count, make_list ($1), make_list ($2)); }
		|
			{ $$ = make_node (nod_block, e_blk_count, NULL, NULL);}
		;							

proc_statements	: proc_block
		| proc_statements proc_block
			{ $$ = make_node (nod_list, 2, $1, $2); }
		;

proc_statement	: assignment ';'
		| delete ';'
		| excp_statement
		| raise_statement
		| exec_procedure
		| exec_sql
		| for_select
		| if_then_else
		| insert ';'
		| post_event
		| singleton_select
		| update ';'
		| while
		| for_exec_into
		| exec_into
		| SUSPEND ';'
			{ $$ = make_node (nod_return, e_rtn_count, NULL); }
		| EXIT ';'
			{ $$ = make_node (nod_exit, 0, NULL); }
/* dimitr: commented out until pass1.cpp is ready to assign label numbers properly
		| label
*/
		| breakleave
		;

excp_statement	: EXCEPTION symbol_exception_name ';'
			{ $$ = make_node (nod_exception_stmt, e_xcp_count, $2, NULL); }
		| EXCEPTION symbol_exception_name value ';'
			{ $$ = make_node (nod_exception_stmt, e_xcp_count, $2, $3); }
		;

raise_statement	: EXCEPTION ';'
			{ $$ = make_node (nod_exception_stmt, e_xcp_count, NULL, NULL); }
    	;

exec_procedure	: EXECUTE PROCEDURE symbol_procedure_name proc_inputs proc_outputs ';'
			{ $$ = make_node (nod_exec_procedure, e_exe_count, $3,
					  $4, $5); }
		;

exec_sql	: EXECUTE varstate value ';'
			{ $$ = make_node (nod_exec_sql, e_exec_sql_count, $3); }
		;

varstate	: VARCHAR | STATEMENT ;

for_select	: FOR select INTO variable_list cursor_def DO proc_block
			{ $$ = make_node (nod_for_select, e_flp_count, $2,
					  make_list ($4), $5, $7, NULL); }
		;

for_exec_into	: FOR EXECUTE varstate value INTO variable_list DO proc_block 
			{ 
				$$ = make_node (nod_exec_into, e_exec_into_count, $4, $8, make_list($6)); }
		;

exec_into	: EXECUTE varstate value INTO variable_list ';'
			{ 
				$$ = make_node (nod_exec_into, e_exec_into_count, $3, 0, make_list($5)); }
		;

if_then_else	: IF '(' search_condition ')' THEN proc_block ELSE proc_block
			{ $$ = make_node (nod_if, e_if_count, $3, $6, $8); }
		| IF '(' search_condition ')' THEN proc_block 
			{ $$ = make_node (nod_if, e_if_count, $3, $6, NULL); }
		;

post_event	: POST_EVENT value event_argument_opt ';'
			{ $$ = make_node (nod_post, e_pst_count, $2, $3); }
		;

event_argument_opt	: ',' value
			{ $$ = $2; }
		|
			{ $$ = NULL; }
		;

singleton_select	: select INTO variable_list ';'
			{ $$ = make_node (nod_for_select, e_flp_count, $1,
					  make_list ($3), NULL, NULL); }
		;

variable	: ':' symbol_variable_name
			{ $$ = make_node (nod_var_name, (int) e_vrn_count, 
							$2); }
		;

proc_inputs	: null_or_value_list
			{ $$ = make_list ($1); }
		| '(' null_or_value_list ')'
			{ $$ = make_list ($2); }
		|
			{ $$ = NULL; }
		;

proc_outputs	: RETURNING_VALUES variable_list
			{ $$ = make_list ($2); }
		| RETURNING_VALUES '(' variable_list  ')'
			{ $$ = make_list ($3); }
		|
			{ $$ = NULL; }
		;

variable_list	: variable
 		| column_name
		| variable_list ',' column_name
			{ $$ = make_node (nod_list, 2, $1, $3); }
		| variable_list ',' variable
			{ $$ = make_node (nod_list, 2, $1, $3); }
		;

while		: WHILE '(' search_condition ')' DO proc_block
			{ $$ = make_node (nod_while, e_while_count,
					  $3, $6, NULL); }
		;

/* dimitr: commented out until pass1.cpp is ready to assign label numbers properly
label	: symbol_label_name ':'
			{ $$ = make_node (nod_label, e_label_count, $1, NULL); }
		;
*/

breakleave	: KW_BREAK ';'
			{ $$ = make_node (nod_breakleave, e_breakleave_count, NULL, NULL); }
		| LEAVE ';'
			{ $$ = make_node (nod_breakleave, e_breakleave_count, NULL, NULL); }
/* dimitr: commented out until pass1.cpp is ready to assign label numbers properly
		| LEAVE symbol_label_name ';'
			{ $$ = make_node (nod_breakleave, e_breakleave_count, $2, NULL); }
*/
		;

cursor_def	: AS CURSOR symbol_cursor_name
			{ $$ = make_node (nod_cursor, e_cur_count, $3, NULL, NULL); }
		|
			{ $$ = NULL; }
		;
excp_hndl_statements	: excp_hndl_statement
		| excp_hndl_statements excp_hndl_statement
			{ $$ = make_node (nod_list, 2, $1, $2); }
		;

excp_hndl_statement	: WHEN errors DO proc_block
			{ $$ = make_node (nod_on_error, e_err_count,
					make_list ($2), $4); }
		;

errors	: err
	| errors ',' err
		{ $$ = make_node (nod_list, 2, $1, $3); }
	;

err	: SQLCODE signed_short_integer
		{ $$ = make_node (nod_sqlcode, 1, $2); }
	| GDSCODE symbol_gdscode_name
		{ $$ = make_node (nod_gdscode, 1, $2); }
	| EXCEPTION symbol_exception_name
		{ $$ = make_node (nod_exception, 1, $2); }
	| ANY
		{ $$ = make_node (nod_default, 1, NULL); }
	;


/* Direct EXECUTE PROCEDURE */

invoke_procedure : EXECUTE PROCEDURE symbol_procedure_name prc_inputs
			{ $$ = make_node (nod_exec_procedure, e_exe_count, $3,
				  $4, make_node (nod_all, (int) 0, NULL)); }
		;

prc_inputs	: prm_const_list
			{ $$ = make_list ($1); }
		| '(' prm_const_list ')'
			{ $$ = make_list ($2); }
		|
			{ $$ = NULL; }
		;

prm_const_list	: parameter
		| constant
		| null_value
		| prm_const_list ',' parameter
			{ $$ = make_node (nod_list, 2, $1, $3); }
		| prm_const_list ',' constant
			{ $$ = make_node (nod_list, 2, $1, $3); }
		| prm_const_list ',' null_value
			{ $$ = make_node (nod_list, 2, $1, $3); }
		;


/* CREATE VIEW */

view_clause	: symbol_view_name column_parens_opt AS begin_string union_view 
                                                            check_opt end_string
			{ $$ = make_node (nod_def_view, (int) e_view_count, 
					  $1, $2, $5, $6, $7); }   
		;        


rview_clause	: symbol_view_name column_parens_opt AS begin_string union_view 
                                                            check_opt end_string
			{ $$ = make_node (nod_redef_view, (int) e_view_count, 
					  $1, $2, $5, $6, $7); }   
		;        

/*
replace_view_clause	: symbol_view_name column_parens_opt AS begin_string union_view 
                                                            check_opt end_string
			{ $$ = make_node (nod_replace_view, (int) e_view_count, 
					  $1, $2, $5, $6, $7); }   
		;        

alter_view_clause	: symbol_view_name column_parens_opt AS begin_string union_view 
                                                            check_opt end_string
 			{ $$ = make_node (nod_mod_view, (int) e_view_count, 
					  $1, $2, $5, $6, $7); }   
 		;        
*/

union_view      : union_view_expr
			{ $$ = make_node (nod_select, (int) 2, $1, NULL); }
		;

union_view_expr	: select_view_expr
			{ $$ = make_node (nod_list, (int) 1, $1); }
		| union_view_expr UNION select_view_expr
			{ $$ = make_node (nod_list, 2, $1, $3); }
                | union_view_expr UNION ALL select_view_expr
                        { $$ = make_flag_node (nod_list, NOD_UNION_ALL, 2, $1, $4); }
		;

select_view_expr: SELECT
             distinct_clause
			 select_list 
			 from_view_clause 
			 where_clause 
			 group_clause 
			 having_clause
			{ $$ = make_node (nod_select_expr, e_sel_count, 
					NULL, $2, $3, $4, $5, $6, $7, NULL, NULL); }
		;                                               

from_view_clause : FROM from_view_list
		 	{ $$ = make_list ($2); }
		;

from_view_list	: view_table
		| from_view_list ',' view_table
			{ $$ = make_node (nod_list, 2, $1, $3); }
		;

view_table      : joined_view_table
                | table_name
                ;

joined_view_table	: view_table join_type JOIN view_table ON search_condition
				{ $$ = make_node (nod_join, (int) e_join_count,
						$1, $2, $4, $6); }
			| '(' joined_view_table ')'
				{ $$ = $2; }
			;

/* these rules will capture the input string for storage in metadata */

begin_string	: 
			{ beginning = lex_position(); }
		;

end_string	:
			{ $$ = (DSQL_NOD) MAKE_string(beginning, 
			       (lex_position() == end) ?
			       lex_position()-beginning : last_token-beginning);}
		;

begin_trigger	: 
			{ beginning = last_token; }
		;

end_trigger	:
			{ $$ = (DSQL_NOD) MAKE_string(beginning, 
					lex_position()-beginning); }
		;


check_opt	: WITH CHECK OPTION
			{ $$ = make_node (nod_def_constraint, (int) e_cnstr_count, 
					MAKE_string(NULL_STRING, 0), NULL, NULL, NULL, 
					NULL, NULL, NULL, NULL, NULL); }
		|
			{ $$ = 0; }
		;



/* CREATE TRIGGER */

def_trigger_clause : symbol_trigger_name FOR simple_table_name
		trigger_active
		trigger_type
		trigger_position
		begin_trigger
		trigger_action
		end_trigger
			{ $$ = make_node (nod_def_trigger, (int) e_trg_count,
				$1, $3, $4, $5, $6, $8, $9, NULL, NULL); }
		;

replace_trigger_clause : symbol_trigger_name FOR simple_table_name
		trigger_active
		trigger_type
		trigger_position
		begin_trigger
		trigger_action
		end_trigger
			{ $$ = make_node (nod_replace_trigger, (int) e_trg_count,
				$1, $3, $4, $5, $6, $8, $9, NULL, NULL); }
		;

trigger_active	: ACTIVE 
			{ $$ = MAKE_constant ((STR) 0, CONSTANT_SLONG); }
		| INACTIVE
			{ $$ = MAKE_constant ((STR) 1, CONSTANT_SLONG); }
		|
			{ $$ = NULL; }
		;

trigger_type	: trigger_type_prefix trigger_type_suffix
			{ $$ = MAKE_trigger_type ($1, $2); }
		;

trigger_type_prefix	: BEFORE
			{ $$ = MAKE_constant ((STR) 0, CONSTANT_SLONG); }
		| AFTER
			{ $$ = MAKE_constant ((STR) 1, CONSTANT_SLONG); }
		;

trigger_type_suffix	: INSERT
			{ $$ = MAKE_constant ((STR) TRIGGER_TYPE_SUFFIX (1, 0, 0), CONSTANT_SLONG); }
		| UPDATE
			{ $$ = MAKE_constant ((STR) TRIGGER_TYPE_SUFFIX (2, 0, 0), CONSTANT_SLONG); }
		| DELETE
			{ $$ = MAKE_constant ((STR) TRIGGER_TYPE_SUFFIX (3, 0, 0), CONSTANT_SLONG); }
		| INSERT OR UPDATE
			{ $$ = MAKE_constant ((STR) TRIGGER_TYPE_SUFFIX (1, 2, 0), CONSTANT_SLONG); }
		| INSERT OR DELETE
			{ $$ = MAKE_constant ((STR) TRIGGER_TYPE_SUFFIX (1, 3, 0), CONSTANT_SLONG); }
		| UPDATE OR INSERT
			{ $$ = MAKE_constant ((STR) TRIGGER_TYPE_SUFFIX (2, 1, 0), CONSTANT_SLONG); }
		| UPDATE OR DELETE
			{ $$ = MAKE_constant ((STR) TRIGGER_TYPE_SUFFIX (2, 3, 0), CONSTANT_SLONG); }
		| DELETE OR INSERT
			{ $$ = MAKE_constant ((STR) TRIGGER_TYPE_SUFFIX (3, 1, 0), CONSTANT_SLONG); }
		| DELETE OR UPDATE
			{ $$ = MAKE_constant ((STR) TRIGGER_TYPE_SUFFIX (3, 2, 0), CONSTANT_SLONG); }
		| INSERT OR UPDATE OR DELETE
			{ $$ = MAKE_constant ((STR) TRIGGER_TYPE_SUFFIX (1, 2, 3), CONSTANT_SLONG); }
		| INSERT OR DELETE OR UPDATE
			{ $$ = MAKE_constant ((STR) TRIGGER_TYPE_SUFFIX (1, 3, 2), CONSTANT_SLONG); }
		| UPDATE OR INSERT OR DELETE
			{ $$ = MAKE_constant ((STR) TRIGGER_TYPE_SUFFIX (2, 1, 3), CONSTANT_SLONG); }
		| UPDATE OR DELETE OR INSERT
			{ $$ = MAKE_constant ((STR) TRIGGER_TYPE_SUFFIX (2, 3, 1), CONSTANT_SLONG); }
		| DELETE OR INSERT OR UPDATE
			{ $$ = MAKE_constant ((STR) TRIGGER_TYPE_SUFFIX (3, 1, 2), CONSTANT_SLONG); }
		| DELETE OR UPDATE OR INSERT
			{ $$ = MAKE_constant ((STR) TRIGGER_TYPE_SUFFIX (3, 2, 1), CONSTANT_SLONG); }
		;

trigger_position : POSITION nonneg_short_integer
			{ $$ = MAKE_constant ((STR) $2, CONSTANT_SLONG); }
		|
			{ $$ = NULL; }
		;

trigger_action : AS begin_trigger var_declaration_list full_proc_block
			{ $$ = make_node (nod_list, 2, $3, $4); }
		;

/* ALTER statement */

alter		: ALTER alter_clause
			{ $$ = $2; }
                ; 

alter_clause	: EXCEPTION symbol_exception_name sql_string
			{ $$ = make_node (nod_mod_exception, (int) e_xcp_count, 
						$2, $3); }
		| TABLE simple_table_name alter_ops
			{ $$ = make_node (nod_mod_relation, (int) e_alt_count, 
						$2, make_list ($3)); }
/*
 		| VIEW alter_view_clause
 			{ $$ = $2; }
*/
		| TRIGGER alter_trigger_clause
			{ $$ = $2; }
		| PROCEDURE alter_procedure_clause
			{ $$ = $2; }
		| DATABASE init_alter_db alter_db
			{ $$ = make_node (nod_mod_database, (int) e_adb_count,
				make_list ($3)); }
                | DOMAIN alter_column_name alter_domain_ops
                        { $$ = make_node (nod_mod_domain, (int) e_alt_count,
                                          $2, make_list ($3)); }
		| INDEX alter_index_clause
                        { $$ = make_node (nod_mod_index, 
				     (int) e_mod_idx_count, $2); }
		;

domain_default_opt2	: DEFAULT begin_trigger default_value
				{ $$ = $3; }

domain_check_constraint2 	: CHECK begin_trigger '(' search_condition ')' end_trigger
				  { $$ = make_node (nod_def_constraint, 
				  (int) e_cnstr_count, MAKE_string(NULL_STRING, 0), NULL, 
				  NULL, NULL, $4, NULL, $6, NULL, NULL); }
                                ;

alter_domain_ops	: alter_domain_op
			| alter_domain_ops alter_domain_op
			  { $$ = make_node (nod_list, 2, $1, $2); }
			;

alter_domain_op		: SET begin_trigger domain_default_opt2 end_trigger
                          { $$ = make_node (nod_def_default, (int) e_dft_count,
					    $3, $4); }              
/*			SET begin_string default_opt end_trigger
                          { $$ = make_node (nod_def_default, (int) e_dft_count,
					    $3, $4); }
                        | begin_trigger default_opt end_trigger
                          { $$ = make_node (nod_def_default, (int) e_dft_count,
					    $2, $3); }              */
                        | ADD CONSTRAINT domain_check_constraint2
                          { $$ = $3; } 
/*                        | ADD CONSTRAINT domain_check_constraint
                          { $$ = $3; }                         */
                        | ADD domain_check_constraint
                          { $$ = $2; } 
			| DROP DEFAULT
                          {$$ = make_node (nod_del_default, (int) 0, NULL); }
			| DROP CONSTRAINT
                          { $$ = make_node (nod_delete_rel_constraint, (int) 1, NULL); }
			| TO simple_column_name
			  { $$ = $2; }
			| TYPE init_data_type non_array_type 
			  { $$ = make_node (nod_mod_domain_type, 2, $2); }
			;

alter_ops	: alter_op
		| alter_ops ',' alter_op
			{ $$ = make_node (nod_list, 2, $1, $3); }
		;

alter_op	: DROP simple_column_name drop_behaviour
			{ $$ = make_node (nod_del_field, 2, $2, $3); }
                | DROP CONSTRAINT symbol_constraint_name
                        { $$ = make_node (nod_delete_rel_constraint, (int) 1, $3);}
		| ADD column_def
			{ $$ = $2; }
        | ADD table_constraint_definition
                        { $$ = $2; }
/* CVC: From SQL, field positions start at 1, not zero. Think in ORDER BY, for example. 
		| col_opt simple_column_name POSITION nonneg_short_integer 
			{ $$ = make_node (nod_mod_field_pos, 2, $2,
            MAKE_constant ((STR) $4, CONSTANT_SLONG)); } */
		| col_opt simple_column_name POSITION pos_short_integer
			{ $$ = make_node (nod_mod_field_pos, 2, $2,
				MAKE_constant ((STR) $4, CONSTANT_SLONG)); }
		| col_opt alter_column_name TO simple_column_name
			{ $$ = make_node (nod_mod_field_name, 2, $2, $4); }
		| col_opt alter_col_name TYPE alter_data_type_or_domain end_trigger 
			{ $$ = make_node (nod_mod_field_type, 3, $2, $5, $4); } 
		;

alter_column_name  : keyword_or_column
		   { $$ = make_node (nod_field_name, (int) e_fln_count,
						NULL, $1); }
		   ;
	
keyword_or_column	: COLUMN
			| TYPE
			| EXTRACT
			| YEAR
			| MONTH
			| DAY
			| HOUR
			| MINUTE
			| SECOND
			| WEEKDAY
			| YEARDAY
			| TIME
			| TIMESTAMP
			| CURRENT_DATE
			| CURRENT_TIME
			| CURRENT_TIMESTAMP
			| CURRENT_USER /* CVC: Added the following except SYMBOL. */
			| CURRENT_ROLE
			| SYMBOL
			| KW_BREAK
			| SUBSTRING
			| KW_DESCRIPTOR
			| CURRENT_CONNECTION
			| CURRENT_TRANSACTION
			;

col_opt		: ALTER
		    { $$ = NULL; }
		| ALTER COLUMN
		    { $$ = NULL; }
		;

alter_data_type_or_domain	: non_array_type begin_trigger
                          		{ $$ = NULL; }
				| simple_column_name begin_string
                          		{ $$ = make_node (nod_def_domain, (int) e_dom_count,
                                        	    $1, NULL, NULL, NULL, NULL); }

alter_col_name	: simple_column_name
			{ g_field_name = $1;
			  g_field = make_field ($1);
			  $$ = (DSQL_NOD) g_field; }

drop_behaviour	: RESTRICT
			{ $$ = make_node (nod_restrict, 0, NULL); }
		| CASCADE
			{ $$ = make_node (nod_cascade, 0, NULL); }
		|
			{ $$ = make_node (nod_restrict, 0, NULL); }
		;

alter_index_clause	: symbol_index_name ACTIVE
				{ $$ = make_node (nod_idx_active, 1, $1); }
			| symbol_index_name INACTIVE
				{ $$ = make_node (nod_idx_inactive, 1, $1); }
			;


/* ALTER DATABASE */

init_alter_db	: 
			{ log_defined = FALSE;
			  cache_defined = FALSE;
			  $$ = (DSQL_NOD) NULL; }
		;

alter_db	: db_alter_clause
		| alter_db db_alter_clause
		    	{ $$ = make_node (nod_list, (int) 2, $1, $2); }
		;

db_alter_clause : ADD db_file_list
			{ $$ = $2; }
/*
		| ADD db_cache
			{ $$ = $2; }
                | DROP CACHE
			{ $$ = make_node (nod_drop_cache, (int) 0, NULL); }
*/
		| DROP LOGFILE
			{ $$ = make_node (nod_drop_log, (int) 0, NULL); }
		| SET  db_log_option_list
			{ $$ = $2; }
		| ADD db_log
			{ $$ = $2; }
		;

db_log_option_list : db_log_option
		   | db_log_option_list ',' db_log_option
		    	{ $$ = make_node (nod_list, (int) 2, $1, $3); }
		   ;


/* ALTER TRIGGER */

alter_trigger_clause : symbol_trigger_name trigger_active
		new_trigger_type
		trigger_position
		begin_trigger
		new_trigger_action
		end_trigger
			{ $$ = make_node (nod_mod_trigger, (int) e_trg_count,
				$1, NULL, $2, $3, $4, $6, $7, NULL, NULL); }
		;

new_trigger_type : trigger_type
		|
			{ $$ = NULL; }
		;

new_trigger_action : trigger_action
		|
			{ $$ = NULL; }
		;

/* DROP metadata operations */
                
drop		: DROP drop_clause
			{ $$ = $2; }
                ; 

drop_clause	: EXCEPTION symbol_exception_name
			{ $$ = make_node (nod_del_exception, 1, $2); }
		| INDEX symbol_index_name
			{ $$ = make_node (nod_del_index, (int) 1, $2); }
		| PROCEDURE symbol_procedure_name
			{ $$ = make_node (nod_del_procedure, (int) 1, $2); }
		| TABLE symbol_table_name
			{ $$ = make_node (nod_del_relation, (int) 1, $2); }
		| TRIGGER symbol_trigger_name
			{ $$ = make_node (nod_del_trigger, (int) 1, $2); }
		| VIEW symbol_view_name
			{ $$ = make_node (nod_del_view, (int) 1, $2); }
		| FILTER symbol_filter_name
			{ $$ = make_node (nod_del_filter, (int) 1, $2); }
		| DOMAIN symbol_domain_name
			{ $$ = make_node (nod_del_domain, (int) 1, $2); }
		| EXTERNAL FUNCTION symbol_UDF_name
			{ $$ = make_node (nod_del_udf, (int) 1, $3); }
		| SHADOW pos_short_integer
			{ $$ = make_node (nod_del_shadow, (int) 1, $2); }
		| ROLE symbol_role_name
			{ $$ = make_node (nod_del_role, (int) 1, $2); }
		| GENERATOR symbol_generator_name
			{ $$ = make_node (nod_del_generator, (int) 1, $2); }
		;


/* these are the allowable datatypes */

data_type	: non_array_type
		| array_type
		;

non_array_type	: simple_type
		| blob_type
		;

array_type	: non_charset_simple_type '[' array_spec ']'
		    { g_field->fld_ranges = make_list ($3);
		      g_field->fld_dimensions = g_field->fld_ranges->nod_count / 2;
		      g_field->fld_element_dtype = g_field->fld_dtype;
		      $$ = $1; }
		| character_type '[' array_spec ']' charset_clause
		    { g_field->fld_ranges = make_list ($3);
		      g_field->fld_dimensions = g_field->fld_ranges->nod_count / 2;
		      g_field->fld_element_dtype = g_field->fld_dtype;
		      $$ = $1; }
		;

array_spec	: array_range 
		| array_spec ',' array_range 
			{ $$ = make_node (nod_list, (int) 2, $1, $3); }
		;

array_range	: signed_long_integer
	    		{ if ((SLONG) $1 < 1)
		     		$$ = make_node (nod_list, (int) 2, 
					MAKE_constant ((STR) $1, CONSTANT_SLONG), 
					MAKE_constant ((STR) 1, CONSTANT_SLONG)); 
		          else
		     		$$ = make_node (nod_list, (int) 2, 
			   		MAKE_constant ((STR) 1, CONSTANT_SLONG), 
					MAKE_constant ((STR) $1, CONSTANT_SLONG) ); }
		| signed_long_integer ':' signed_long_integer
		    	{ $$ = make_node (nod_list, (int) 2, 
			 	MAKE_constant ((STR) $1, CONSTANT_SLONG),
				MAKE_constant ((STR) $3, CONSTANT_SLONG)); }
		;

simple_type	: non_charset_simple_type
		| character_type charset_clause
		;

non_charset_simple_type	: national_character_type
		| numeric_type
		| float_type
		| BIGINT
			{ 
			if (client_dialect < SQL_DIALECT_V6_TRANSITION)
			    ERRD_post (gds_sqlerr, gds_arg_number, (SLONG) -104, 
				    gds_arg_gds, isc_sql_dialect_datatype_unsupport,
				    gds_arg_number, client_dialect,
				    gds_arg_string, "BIGINT",
				    0);
			if (db_dialect < SQL_DIALECT_V6_TRANSITION)
			    ERRD_post (gds_sqlerr, gds_arg_number, (SLONG) -104, 
				    gds_arg_gds, isc_sql_db_dialect_dtype_unsupport,
				    gds_arg_number, db_dialect,
				    gds_arg_string, "BIGINT",
				    0);
			g_field->fld_dtype = dtype_int64; 
			g_field->fld_length = sizeof (SINT64); 
			}
		| integer_keyword
			{ 
			g_field->fld_dtype = dtype_long; 
			g_field->fld_length = sizeof (SLONG); 
			}
		| SMALLINT
			{ 
			g_field->fld_dtype = dtype_short; 
			g_field->fld_length = sizeof (SSHORT); 
			}
		| DATE
			{ 
			*stmt_ambiguous = TRUE;
			if (client_dialect <= SQL_DIALECT_V5)
			    {
			    /* Post warning saying that DATE is equivalent to TIMESTAMP */
		            ERRD_post_warning (isc_sqlwarn, gds_arg_number, (SLONG) 301, 
                                               isc_arg_warning, isc_dtype_renamed, 0);
			    g_field->fld_dtype = dtype_timestamp; 
			    g_field->fld_length = sizeof (GDS_TIMESTAMP);
			    }
			else if (client_dialect == SQL_DIALECT_V6_TRANSITION)
			    yyabandon (-104, isc_transitional_date);
			else
			    {
			    g_field->fld_dtype = dtype_sql_date; 
			    g_field->fld_length = sizeof (ULONG);
			    }
			}
		| TIME
			{ 
			if (client_dialect < SQL_DIALECT_V6_TRANSITION)
			    ERRD_post (gds_sqlerr, gds_arg_number, (SLONG) -104, 
				    gds_arg_gds, isc_sql_dialect_datatype_unsupport,
				    gds_arg_number, client_dialect,
				    gds_arg_string, "TIME",
				    0);
			if (db_dialect < SQL_DIALECT_V6_TRANSITION)
			    ERRD_post (gds_sqlerr, gds_arg_number, (SLONG) -104, 
				    gds_arg_gds, isc_sql_db_dialect_dtype_unsupport,
				    gds_arg_number, db_dialect,
				    gds_arg_string, "TIME",
				    0);
			g_field->fld_dtype = dtype_sql_time; 
			g_field->fld_length = sizeof (SLONG);
			}
		| TIMESTAMP
			{ 
			g_field->fld_dtype = dtype_timestamp; 
			g_field->fld_length = sizeof (GDS_TIMESTAMP);
			}
		;

integer_keyword	: INTEGER	
		| KW_INT
		;


/* allow a blob to be specified with any combination of 
   segment length and subtype */

blob_type	: BLOB blob_subtype blob_segsize charset_clause
			{ 
			g_field->fld_dtype = dtype_blob; 
			}
		| BLOB '(' unsigned_short_integer ')'
			{ 
			g_field->fld_dtype = dtype_blob; 
			g_field->fld_seg_length = (USHORT) $3;
			g_field->fld_sub_type = 0;
			}
		| BLOB '(' unsigned_short_integer ',' signed_short_integer ')'
			{ 
			g_field->fld_dtype = dtype_blob; 
			g_field->fld_seg_length = (USHORT) $3;
			g_field->fld_sub_type = (USHORT) $5;
			}
		| BLOB '(' ',' signed_short_integer ')'
			{ 
			g_field->fld_dtype = dtype_blob; 
			g_field->fld_seg_length = 80;
			g_field->fld_sub_type = (USHORT) $4;
			}
		;

blob_segsize	: SEGMENT KW_SIZE unsigned_short_integer
		  	{
			g_field->fld_seg_length = (USHORT) $3;
		  	}
		|
		  	{
			g_field->fld_seg_length = (USHORT) 80;
		  	}
		;

blob_subtype	: SUB_TYPE signed_short_integer
			{
			g_field->fld_sub_type = (USHORT) $2;
			}
		| SUB_TYPE symbol_blob_subtype_name
			{
			g_field->fld_sub_type_name = $2;
			}
		|
			{
			g_field->fld_sub_type = (USHORT) 0;
			}
		;

charset_clause	: CHARACTER SET symbol_character_set_name
			{
			g_field->fld_character_set = $3;
			}
		|
		;


/* character type */


national_character_type	: national_character_keyword '(' pos_short_integer ')'
			{ 
			g_field->fld_dtype = dtype_text; 
			g_field->fld_character_length = (USHORT) $3; 
			g_field->fld_flags |= FLD_national;
			}
		| national_character_keyword
			{ 
			g_field->fld_dtype = dtype_text; 
			g_field->fld_character_length = 1; 
			g_field->fld_flags |= FLD_national;
			}
		| national_character_keyword VARYING '(' pos_short_integer ')'
			{ 
			g_field->fld_dtype = dtype_varying; 
			g_field->fld_character_length = (USHORT) $4; 
			g_field->fld_flags |= FLD_national;
			}
		;

character_type	: character_keyword '(' pos_short_integer ')'
			{ 
			g_field->fld_dtype = dtype_text; 
			g_field->fld_character_length = (USHORT) $3; 
			}
		| character_keyword
			{ 
			g_field->fld_dtype = dtype_text; 
			g_field->fld_character_length = 1; 
			}
		| varying_keyword '(' pos_short_integer ')'
			{ 
			g_field->fld_dtype = dtype_varying; 
			g_field->fld_character_length = (USHORT) $3; 
			}
		;

varying_keyword 	   : VARCHAR
			   | CHARACTER VARYING
			   | KW_CHAR VARYING
			   ;

character_keyword 	   : CHARACTER
			   | KW_CHAR
			   ;

national_character_keyword : NCHAR
			   | NATIONAL CHARACTER
		           | NATIONAL KW_CHAR
		           ;



/* numeric type */

numeric_type	: KW_NUMERIC prec_scale
                        { 
			  g_field->fld_sub_type = dsc_num_type_numeric;
			}
		| decimal_keyword prec_scale
			{  
			   g_field->fld_sub_type = dsc_num_type_decimal;
			   if (g_field->fld_dtype == dtype_short)
				{
				g_field->fld_dtype = dtype_long;
				g_field->fld_length = sizeof (SLONG);
				};
			}
		;

ordinal		: pos_short_integer
			{ $$ = make_node (nod_position, 1, $1); }
		;




prec_scale	: 
			{
			g_field->fld_dtype = dtype_long; 
		    	g_field->fld_length = sizeof (SLONG); 
			g_field->fld_precision = 9;
		    	}
		| '(' signed_long_integer ')'
			{         
			if ( ((SLONG) $2 < 1) || ((SLONG) $2 > 18) )
			    yyabandon (-842, isc_precision_err);
				/* Precision most be between 1 and 18. */ 
			if ((SLONG) $2 > 9)
			    {
			    if ( ( (client_dialect <= SQL_DIALECT_V5) &&
				   (db_dialect     >  SQL_DIALECT_V5) ) ||
				 ( (client_dialect >  SQL_DIALECT_V5) &&
				   (db_dialect     <= SQL_DIALECT_V5) ) )
			        ERRD_post (gds_sqlerr,
					   gds_arg_number, (SLONG) -817,
					   gds_arg_gds,
					   isc_ddl_not_allowed_by_db_sql_dial,
					   gds_arg_number, (SLONG) db_dialect,
					   0);
			    if (client_dialect <= SQL_DIALECT_V5)
			        {
				g_field->fld_dtype = dtype_double;
				g_field->fld_length = sizeof (double);
			        }
			    else
			        {
				if (client_dialect == SQL_DIALECT_V6_TRANSITION)
				    {
				    ERRD_post_warning (
					isc_dsql_warn_precision_ambiguous,
					gds_arg_end );
				    ERRD_post_warning (
					isc_dsql_warn_precision_ambiguous1,
					gds_arg_end );
				    ERRD_post_warning (
					isc_dsql_warn_precision_ambiguous2,
					gds_arg_end );

				    }
				g_field->fld_dtype = dtype_int64;
				g_field->fld_length = sizeof (SINT64);
			        }
			    }
			else 
			    if ((SLONG) $2 < 5)
			    	{
			    	g_field->fld_dtype = dtype_short; 
			    	g_field->fld_length = sizeof (SSHORT); 
			    	}
			    else
			    	{
			    	g_field->fld_dtype = dtype_long; 
			    	g_field->fld_length = sizeof (SLONG); 
			    	}
			g_field->fld_precision = (USHORT) $2;
			}
		| '(' signed_long_integer ',' signed_long_integer ')'
			{ 
			if ( ((SLONG) $2 < 1) || ((SLONG) $2 > 18) )
			    yyabandon (-842, isc_precision_err);
				/* Precision should be between 1 and 18 */ 
			if (((SLONG) $4 > (SLONG) $2) || ((SLONG) $4 < 0))
			    yyabandon (-842, isc_scale_nogt);
				/* Scale must be between 0 and precision */
			if ((SLONG) $2 > 9)
			    {
			    if ( ( (client_dialect <= SQL_DIALECT_V5) &&
				   (db_dialect     >  SQL_DIALECT_V5) ) ||
				 ( (client_dialect >  SQL_DIALECT_V5) &&
				   (db_dialect     <= SQL_DIALECT_V5) ) )
			        ERRD_post (gds_sqlerr,
					   gds_arg_number, (SLONG) -817,
					   gds_arg_gds,
					   isc_ddl_not_allowed_by_db_sql_dial,
					   gds_arg_number, (SLONG) db_dialect,
					   0);
			    if (client_dialect <= SQL_DIALECT_V5)
			        {
				g_field->fld_dtype = dtype_double;
				g_field->fld_length = sizeof (double); 
			        }
			    else
			        {
				if (client_dialect == SQL_DIALECT_V6_TRANSITION)
				  {
				    ERRD_post_warning (
					isc_dsql_warn_precision_ambiguous,
					gds_arg_end );
				    ERRD_post_warning (
					isc_dsql_warn_precision_ambiguous1,
					gds_arg_end );
				    ERRD_post_warning (
					isc_dsql_warn_precision_ambiguous2,
					gds_arg_end );
				  }
				  /* client_dialect >= SQL_DIALECT_V6 */
				g_field->fld_dtype = dtype_int64;
				g_field->fld_length = sizeof (SINT64);
			        }
			    }
			else
			    {
			    if ((SLONG) $2 < 5)
			    	{
			    	g_field->fld_dtype = dtype_short; 
			    	g_field->fld_length = sizeof (SSHORT); 
			    	}
			    else
			    	{
			    	g_field->fld_dtype = dtype_long; 
			    	g_field->fld_length = sizeof (SLONG); 
			    	}
			    }
			g_field->fld_precision = (USHORT) $2;
			g_field->fld_scale = - (SSHORT) $4;
			}
		;

decimal_keyword	: DECIMAL	
		| KW_DEC
		;



/* floating point type */

float_type	: KW_FLOAT precision_opt
			{ 
			if ((SLONG) $2 > 7)
			    {
			    g_field->fld_dtype = dtype_double;
			    g_field->fld_length = sizeof (double); 
			    }
			else
			    {
			    g_field->fld_dtype = dtype_real; 
			    g_field->fld_length = sizeof (float);
			    }
			}
		| KW_LONG KW_FLOAT precision_opt
			{ 
			g_field->fld_dtype = dtype_double; 
			g_field->fld_length = sizeof (double); 
			}
		| REAL
			{ 
			g_field->fld_dtype = dtype_real; 
			g_field->fld_length = sizeof (float); 
			}
		| KW_DOUBLE PRECISION
			{ 
			g_field->fld_dtype = dtype_double; 
			g_field->fld_length = sizeof (double); 
			}
		;

precision_opt	: '(' nonneg_short_integer ')'
			{ $$ = $2; }
		|
			{ $$ = 0; }
		;



/* SET statements */
set		: set_transaction
		| set_generator
		| set_statistics
		;


set_generator	: SET GENERATOR symbol_generator_name TO signed_long_integer
			{ 
			  $$ = make_node (nod_set_generator2,e_gen_id_count,$3,
						MAKE_constant ((STR) $5, CONSTANT_SLONG));
			}
                | SET GENERATOR symbol_generator_name TO NUMBER64BIT
                        {
			  $$ = make_node (nod_set_generator2,e_gen_id_count,$3,
				       MAKE_constant((STR)$5, CONSTANT_SINT64));
			}
                | SET GENERATOR symbol_generator_name TO '-' NUMBER64BIT
                        {
			  $$ = make_node (nod_set_generator2, e_gen_id_count, $3,
					  make_node(nod_negate, 1,
						    MAKE_constant((STR)$6, CONSTANT_SINT64)));
			}
		;


/* transaction statements */

user_savepoint : SAVEPOINT symbol_savepoint_name
			{ $$ = make_node (nod_user_savepoint, 1, $2); }
		;
		
undo_savepoint : ROLLBACK optional_work TO optional_savepoint symbol_savepoint_name
			{ $$ = make_node (nod_undo_savepoint, 1, $5); }
		;

optional_savepoint	: SAVEPOINT
		|
		;
		
commit		: COMMIT optional_work optional_retain
			{ $$ = make_node (nod_commit, 1, $3); }
		;

rollback	: ROLLBACK optional_work
			{ $$ = make_node (nod_rollback, 0, NULL); }
		;

optional_work	: WORK
		|
		;

optional_retain	: RETAIN opt_snapshot
			{ $$ = make_node (nod_commit_retain, 0, NULL); }
		|
		 	{ $$ = (DSQL_NOD) NULL; }
		;

opt_snapshot	: SNAPSHOT
		|
		 	{ $$ = (DSQL_NOD) NULL; }
		;

set_transaction	: SET TRANSACTION tran_opt_list_m
			{$$ = make_node (nod_trans, 1, make_list ($3)); }
		;

tran_opt_list_m	: tran_opt_list	
		|
		 	{ $$ = (DSQL_NOD) NULL; }
		;

tran_opt_list	: tran_opt
		| tran_opt_list tran_opt
		    { $$ = make_node (nod_list, (int) 2, $1, $2); }
		;

tran_opt	: access_mode 
		| lock_wait 
		| isolation_mode 
		| tbl_reserve_options 
		;

access_mode	: READ ONLY
			{ $$ = make_flag_node (nod_access, NOD_READ_ONLY, (int) 0, NULL); }
		| READ WRITE
			{ $$ = make_flag_node (nod_access, NOD_READ_WRITE, (int) 0, NULL); }
		;

lock_wait	: WAIT
			{ $$ = make_flag_node (nod_wait, NOD_WAIT, (int) 0, NULL); }
		| NO WAIT
			{ $$ = make_flag_node (nod_wait, NOD_NO_WAIT, (int) 0, NULL); }
		;

isolation_mode	: ISOLATION LEVEL iso_mode
			{ $$ = $3;}
		| iso_mode
		;

iso_mode	: snap_shot
			{ $$ = $1;}
		| READ UNCOMMITTED version_mode
			{ $$ = make_flag_node (nod_isolation, NOD_READ_COMMITTED, 1, $3); }
		| READ COMMITTED version_mode
			{ $$ = make_flag_node (nod_isolation, NOD_READ_COMMITTED, 1, $3); }
		;

snap_shot	: SNAPSHOT
			{ $$ = make_flag_node (nod_isolation, NOD_CONCURRENCY, 0, NULL); }
		| SNAPSHOT TABLE 
			{ $$ = make_flag_node (nod_isolation, NOD_CONSISTENCY, 0, NULL); }
		| SNAPSHOT TABLE STABILITY
			{ $$ = make_flag_node (nod_isolation, NOD_CONSISTENCY, 0, NULL); }
		;

version_mode	: VERSION
			{ $$ = make_flag_node (nod_version, NOD_VERSION, 0, NULL); }
		| NO VERSION
			{ $$ = make_flag_node (nod_version, NOD_NO_VERSION, 0, NULL); }
		|
			{ $$ = 0; }
		;

tbl_reserve_options: RESERVING restr_list
			{ $$ = make_node (nod_reserve, 1, make_list ($2)); }
		;

lock_type	: SHARED
			{ $$ = (DSQL_NOD) NOD_SHARED; }
		| PROTECTED
			{ $$ = (DSQL_NOD) NOD_PROTECTED ; }
		|
			{ $$ = (DSQL_NOD) 0; }
		;

lock_mode	: READ
			{ $$ = (DSQL_NOD) NOD_READ; }
		| WRITE
			{ $$ = (DSQL_NOD) NOD_WRITE; }
		;

restr_list	: restr_option
		| restr_list ',' restr_option
		    { $$ = make_node (nod_list, (int) 2, $1, $3); }
		;

restr_option	: table_list table_lock
			{ $$ = make_node (nod_table_lock, (int) 2, make_list ($1), $2); }
		;

table_lock	: FOR lock_type lock_mode
			{ $$ = make_flag_node (nod_lock_mode, (SSHORT) ((SSHORT) $2 | (SSHORT) $3), (SSHORT) 0, NULL); }
		|
			{ $$ = 0; }
		;

table_list	: simple_table_name
		| table_list ',' simple_table_name
		    { $$ = make_node (nod_list, (int) 2, $1, $3); }
		;


set_statistics	: SET STATISTICS INDEX symbol_index_name
			{$$ = make_node (nod_set_statistics, 
				(int)e_stat_count, $4); }


/* SELECT statement */

select		: union_expr order_clause for_update_clause
			{ $$ = make_node (nod_select, 3, $1, $2, $3); }
		;

union_expr	: select_expr
			{ $$ = make_node (nod_list, 1, $1); }
		| union_expr UNION select_expr
			{ $$ = make_node (nod_list, 2, $1, $3); }
		| union_expr UNION ALL select_expr
			{ $$ = make_flag_node (nod_list, NOD_UNION_ALL, 2, $1, $4); }
		;

order_clause	: ORDER BY order_list
			{ $$ = make_list ($3); }
		|
			{ $$ = 0; }
		;

order_list	: order_item
		| order_list ',' order_item
			{ $$ = make_node (nod_list, 2, $1, $3); }
		;

order_item	: value order_direction nulls_placement
			{ $$ = make_node (nod_order, e_order_count, $1, $2, $3); }
		;

order_direction	: ASC
			{ $$ = 0; }
		| DESC
			{ $$ = make_node (nod_flag, 0, NULL); }
		|
			{ $$ = 0; }
		;
		
nulls_placement : NULLS FIRST
			{ $$ = make_node (nod_flag, 0, NULL); }
		| NULLS LAST
			{ $$ = 0; }
		|
			{ $$ = 0; }
		;

for_update_clause : FOR UPDATE for_update_list lock_clause
			{ $$ = make_node (nod_for_update, 2, $3, $4); }
		|
			{ $$ = 0; }
		;

for_update_list	: OF column_list
			{ $$ = $2; }
		|
			{ $$ = make_node (nod_flag, 0, NULL); }
		;

lock_clause : WITH LOCK
			{ $$ = make_node (nod_flag, 0, NULL); }
		|
			{ $$ = 0; }
		;
		

/* SELECT expression */

select_expr	: SELECT limit_clause
             distinct_clause
			 select_list 
			 from_clause 
			 where_clause 
			 group_clause 
			 having_clause
			 plan_clause
			{ $$ = make_node (nod_select_expr, e_sel_count, 
					$2, $3, $4, $5, $6, $7, $8, $9, NULL, NULL); }
		;                                               

ordered_select_expr	: SELECT limit_clause
             distinct_clause
			 select_list 
			 from_clause 
			 where_clause 
			 group_clause 
			 having_clause
			 plan_clause
			 order_clause
			{ $$ = make_node (nod_select_expr, e_sel_count, 
					$2, $3, $4, $5, $6, $7, $8, $9, $10, NULL); }
		;                                               

limit_clause	: first_clause skip_clause
			{ $$ = make_node (nod_limit, e_limit_count, $2, $1); }
		|   first_clause
			{ $$ = make_node (nod_limit, e_limit_count, NULL, $1); }
		|   skip_clause 
			{ $$ = make_node (nod_limit, e_limit_count, $1, NULL); }
		| 
			{ $$ = 0; }
		;

first_clause	: FIRST long_integer
			{ $$ = MAKE_constant ((STR) $2, CONSTANT_SLONG); }
		| FIRST '(' value ')'
			{ $$ = $3; }
		| FIRST parameter
			{ $$ = $2; }
		;

skip_clause	: SKIP long_integer
			{ $$ = MAKE_constant ((STR) $2, CONSTANT_SLONG); }
		| SKIP '(' value ')'
			{ $$ = $3; }
		| SKIP parameter
			{ $$ = $2; }
		;

distinct_clause	: DISTINCT
			{ $$ = make_node (nod_flag, 0, NULL); }
		| all_noise 
			{ $$ = 0; }
		;

select_list	: select_items
			{ $$ = make_list ($1); }
		| '*'
			{ $$ = 0; }
		;

select_items	: select_item
		| select_items ',' select_item
			{ $$ = make_node (nod_list, 2, $1, $3); }
		;

select_item	: rhs
		| rhs symbol_item_alias_name
			{ $$ = make_node (nod_alias, 2, $1, $2); }
		| rhs AS symbol_item_alias_name
			{ $$ = make_node (nod_alias, 2, $1, $3); }
		;


/* FROM clause */

from_clause	: FROM from_list
		 	{ $$ = make_list ($2); }
/*		| FROM '(' select_expr ')'
			{ $$ = make_list ($3); }*/
		;

from_list	: table_reference
		| from_list ',' table_reference
			{ $$ = make_node (nod_list, 2, $1, $3); }
		;

table_reference	: joined_table
		| table_proc
		;

joined_table	: table_reference join_type JOIN table_reference ON search_condition
			{ $$ = make_node (nod_join, (int) e_join_count, $1, $2, $4, $6); }
		| '(' joined_table ')'
			{ $$ = $2; }
		;

table_proc	: symbol_procedure_name proc_table_inputs symbol_table_alias_name
			{ $$ = make_node (nod_rel_proc_name, 
					(int) e_rpn_count, $1, $3, $2); }
		| symbol_procedure_name proc_table_inputs
			{ $$ = make_node (nod_rel_proc_name, 
					(int) e_rpn_count, $1, NULL, $2); }
		;

proc_table_inputs	: '(' null_or_value_list ')'
				{ $$ = make_list ($2); }
			|
				{ $$ = NULL; }
			;

null_or_value_list : null_or_value
		   | null_or_value_list ',' null_or_value
			{ $$ = make_node (nod_list, 2, $1, $3); }
		   ;

null_or_value : null_value 
              | value
              ;

table_name	: simple_table_name
		| symbol_table_name symbol_table_alias_name
			{ $$ = make_node (nod_relation_name, 
						(int) e_rln_count, $1, $2); }
		;                         

simple_table_name: symbol_table_name
			{ $$ = make_node (nod_relation_name, 
						(int) e_rln_count, $1, NULL); }
		;

join_type	: INNER
			{ $$ = make_node (nod_join_inner, (int) 0, NULL); }
		| LEFT
			{ $$ = make_node (nod_join_left, (int) 0, NULL); }
		| LEFT OUTER
			{ $$ = make_node (nod_join_left, (int) 0, NULL); }
		| RIGHT
			{ $$ = make_node (nod_join_right, (int) 0, NULL); }
		| RIGHT OUTER
			{ $$ = make_node (nod_join_right, (int) 0, NULL); }
		| FULL
			{ $$ = make_node (nod_join_full, (int) 0, NULL); }
		| FULL OUTER
			{ $$ = make_node (nod_join_full, (int) 0, NULL); }
		|
			{ $$ = make_node (nod_join_inner, (int) 0, NULL); }
		;


/* other clauses in the select expression */

group_clause	: GROUP BY grp_column_list
			{ $$ = make_list ($3); }
		|
			{ $$ = 0; }
		;

grp_column_list	: grp_column_elem
		| grp_column_list ',' grp_column_elem
			{ $$ = make_node (nod_list, 2, $1, $3); }
		;

grp_column_elem : column_name
		| ordinal
		| udf
		| group_by_function
		| column_name COLLATE symbol_collation_name
			{ $$ = make_node (nod_collate, e_coll_count, (DSQL_NOD) $3, $1); }
		;

group_by_function	: numeric_value_function
		| string_value_function
		| case_expression
		;

having_clause	: HAVING search_condition
			{ $$ = $2; }
		|
			{ $$ = 0; }
		;

where_clause	: WHERE search_condition
		 	{ $$ = $2; }
		| 
			{ $$ = 0; }
		;


/* PLAN clause to specify an access plan for a query */

plan_clause	: PLAN plan_expression
			{ $$ = $2; }
		|
			{ $$ = 0; }
		;

plan_expression	: plan_type '(' plan_item_list ')'
			{ $$ = make_node (nod_plan_expr, 2, $1, make_list ($3)); }
		;

plan_type	: JOIN
			{ $$ = 0; }
		| SORT MERGE
			{ $$ = make_node (nod_merge, (int) 0, NULL); }
		| MERGE
			{ $$ = make_node (nod_merge, (int) 0, NULL); }

		/* for now the SORT operator is a no-op; it does not 
		   change the place where a sort happens, but is just intended 
		   to read the output from a SET PLAN */
		| SORT
			{ $$ = 0; }
		|
			{ $$ = 0; }
		;

plan_item_list	: plan_item
		| plan_item ',' plan_item_list
			{ $$ = make_node (nod_list, 2, $1, $3); }
		;

plan_item	: table_or_alias_list access_type
			{ $$ = make_node (nod_plan_item, 2, make_list ($1), $2); }
		| plan_expression
		;

table_or_alias_list : symbol_table_name
		| symbol_table_name table_or_alias_list
			{ $$ = make_node (nod_list, 2, $1, $2); }
		;

access_type	: NATURAL
			{ $$ = make_node (nod_natural, (int) 0, NULL); }
		| INDEX '(' index_list ')'
			{ $$ = make_node (nod_index, 1, make_list ($3)); }
		| ORDER symbol_index_name
			{ $$ = make_node (nod_index_order, 1, $2); }
		;

index_list	: symbol_index_name
		| symbol_index_name ',' index_list
			{ $$ = make_node (nod_list, 2, $1, $3); }
		;



/* INSERT statement */
/* IBO hack: replace column_parens_opt by ins_column_parens_opt. */
insert		: INSERT INTO simple_table_name ins_column_parens_opt VALUES '(' insert_value_list ')'
			{ $$ = make_node (nod_insert, e_ins_count, 
			  $3, make_list ($4), make_list ($7), NULL); }
		| INSERT INTO simple_table_name ins_column_parens_opt ordered_select_expr
			{ $$ = make_node (nod_insert, e_ins_count, $3, $4, NULL, $5); }
		;

insert_value_list : rhs
		| insert_value_list ',' rhs
			{ $$ = make_node (nod_list, 2, $1, $3); }
		;


/* DELETE statement */
   
delete		: delete_searched
		| delete_positioned
		;

delete_searched	: DELETE FROM table_name where_clause
			{ $$ = make_node (nod_delete, e_del_count, $3, $4, NULL); }
		;

delete_positioned : DELETE FROM table_name cursor_clause
			{ $$ = make_node (nod_delete, e_del_count, $3, NULL, $4); }
		;

cursor_clause	: WHERE CURRENT OF symbol_cursor_name
			{ $$ = make_node (nod_cursor, e_cur_count, $4, NULL, NULL); }
		;


/* UPDATE statement */

update		: update_searched
		| update_positioned
		;

update_searched	: UPDATE table_name SET assignments where_clause
			{ $$ = make_node (nod_update, e_upd_count, 
				$2, make_list ($4), $5, NULL); }
          	;

update_positioned : UPDATE table_name SET assignments cursor_clause
			{ $$ = make_node (nod_update, e_upd_count,
			  	$2, make_list ($4), NULL, $5); }
		;

assignments	: assignment
		| assignments ',' assignment
			{ $$ = make_node (nod_list, 2, $1, $3); }
		;

assignment	: update_column_name '=' rhs
			{ $$ = make_node (nod_assign, 2, $3, $1); }
		;

rhs		: value
		| null_value
		;


/* BLOB get and put */

blob            : READ BLOB simple_column_name FROM simple_table_name filter_clause segment_clause
			{ $$ = make_node (nod_get_segment, e_blb_count, $3, $5, $6, $7); }
                | INSERT BLOB simple_column_name INTO simple_table_name filter_clause segment_clause
			{ $$ = make_node (nod_put_segment, e_blb_count, $3, $5, $6, $7); }
		;

filter_clause	: FILTER FROM blob_subtype_value TO blob_subtype_value
			{ $$ = make_node (nod_list, 2, $3, $5); }
		| FILTER TO blob_subtype_value
			{ $$ = make_node (nod_list, 2, NULL, $3); }
		|
		;

blob_subtype_value : blob_subtype
		| parameter
		;

blob_subtype	: signed_short_integer
			{ $$ = MAKE_constant ((STR) $1, CONSTANT_SLONG); }
		;

segment_clause	: MAX_SEGMENT segment_length
			{ $$ = $2; }
		|
		;

segment_length	: unsigned_short_integer
			{ $$ = MAKE_constant ((STR) $1, CONSTANT_SLONG); }
		| parameter
		;


/* column specifications */

column_parens_opt : column_parens
		|
			{ $$ = NULL; }
		;

column_parens	: '(' column_list ')'
			{ $$ = make_list ($2); }
		;

column_list	: simple_column_name
		| column_list ',' simple_column_name
			{ $$ = make_node (nod_list, 2, $1, $3); }
		;

/* begin IBO hack */
ins_column_parens_opt : ins_column_parens
		|
			{ $$ = NULL; }
		;

ins_column_parens	: '(' ins_column_list ')'
			{ $$ = make_list ($2); }
		;

ins_column_list	: update_column_name
		| ins_column_list ',' update_column_name
			{ $$ = make_node (nod_list, 2, $1, $3); }
		;
/* end IBO hack */

column_name     : simple_column_name
		| symbol_table_alias_name '.' symbol_column_name
			{ $$ = make_node (nod_field_name, (int) e_fln_count, 
							$1, $3); }
		| symbol_table_alias_name '.' '*'
			{ $$ = make_node (nod_field_name, (int) e_fln_count, 
							$1, NULL); }
		;

simple_column_name : symbol_column_name
			{ $$ = make_node (nod_field_name, (int) e_fln_count,
						NULL, $1); }
		;

update_column_name : simple_column_name
/* CVC: This option should be deprecated! The only allowed syntax should be
Update...set column = expr, without qualifier for the column. */
		| symbol_table_alias_name '.' symbol_column_name
			{ $$ = make_node (nod_field_name, (int) e_fln_count, 
							$1, $3); }
		;

/* boolean expressions */

search_condition : predicate
		| search_condition OR search_condition
			{ $$ = make_node (nod_or, 2, $1, $3); }
		| search_condition AND search_condition
			{ $$ = make_node (nod_and, 2, $1, $3); }
		| NOT search_condition
			{ $$ = make_node (nod_not, 1, $2); }
		;

predicate	: comparison_predicate
		| between_predicate
		| like_predicate
		| in_predicate
		| null_predicate
		| quantified_predicate
		| exists_predicate
		| containing_predicate
		| starting_predicate
		| unique_predicate
		| trigger_action_predicate
		| '(' search_condition ')'
			{ $$ = $2; }
		;


/* comparisons */

comparison_predicate : value '=' value
			{ $$ = make_node (nod_eql, 2, $1, $3); }
		| value '<' value
			{ $$ = make_node (nod_lss, 2, $1, $3); }
		| value '>' value
			{ $$ = make_node (nod_gtr, 2, $1, $3); }
		| value GEQ value
			{ $$ = make_node (nod_geq, 2, $1, $3); }
		| value LEQ value
			{ $$ = make_node (nod_leq, 2, $1, $3); }
		| value NOT_GTR value
			{ $$ = make_node (nod_leq, 2, $1, $3); }
		| value NOT_LSS value
			{ $$ = make_node (nod_geq, 2, $1, $3); }
		| value NEQ value
			{ $$ = make_node (nod_neq, 2, $1, $3); }
		;


/* quantified comparisons */

quantified_predicate : value '=' ALL '(' column_select ')'
		{ $$ = make_node (nod_eql_all, 2, $1, $5); }
	| value '<' ALL '(' column_select ')'
		{ $$ = make_node (nod_lss_all, 2, $1, $5); }
	| value '>' ALL '(' column_select ')'
		{ $$ = make_node (nod_gtr_all, 2, $1, $5); }
	| value GEQ ALL '(' column_select ')'
		{ $$ = make_node (nod_geq_all, 2, $1, $5); }
	| value LEQ ALL '(' column_select ')'
		{ $$ = make_node (nod_leq_all, 2, $1, $5); }
	| value NOT_GTR ALL '(' column_select ')'
		{ $$ = make_node (nod_leq_all, 2, $1, $5); }
	| value NOT_LSS ALL '(' column_select ')'
		{ $$ = make_node (nod_geq_all, 2, $1, $5); }
	| value NEQ ALL '(' column_select ')'
		{ $$ = make_node (nod_neq_all, 2, $1, $5); }
	| value '=' some '(' column_select ')'
		{ $$ = make_node (nod_eql_any, 2, $1, $5); }
	| value '<' some '(' column_select ')'
		{ $$ = make_node (nod_lss_any, 2, $1, $5); }
	| value '>' some '(' column_select ')'
		{ $$ = make_node (nod_gtr_any, 2, $1, $5); }
	| value GEQ some '(' column_select ')'
		{ $$ = make_node (nod_geq_any, 2, $1, $5); }
	| value LEQ some '(' column_select ')'
		{ $$ = make_node (nod_leq_any, 2, $1, $5); }
	| value NOT_GTR some '(' column_select ')'
		{ $$ = make_node (nod_leq_any, 2, $1, $5); }
	| value NOT_LSS some '(' column_select ')'
		{ $$ = make_node (nod_geq_any, 2, $1, $5); }
	| value NEQ some '(' column_select ')'
		{ $$ = make_node (nod_neq_any, 2, $1, $5); }
	;

some		: SOME
		| ANY
		;


/* other predicates */

between_predicate : value BETWEEN value AND value
		{ $$ = make_node (nod_between, 3, $1, $3, $5); }
	| value NOT BETWEEN value AND value
		{ $$ = make_node (nod_not, 1, make_node (nod_between, 
						3, $1, $4, $6)); }
		;

like_predicate	: value LIKE value
		{ $$ = make_node (nod_like, 2, $1, $3); }
	| value NOT LIKE value
		{ $$ = make_node (nod_not, 1, make_node (nod_like, 2, $1, $4)); }
	| value LIKE value ESCAPE value
		{ $$ = make_node (nod_like, 3, $1, $3, $5); }
	| value NOT LIKE value ESCAPE value
		{ $$ = make_node (nod_not, 1, make_node (nod_like, 
						3, $1, $4, $6)); }
		;

in_predicate	: value IN in_predicate_value
		{ $$ = make_node (nod_eql_any, 2, $1, $3); }
	| value NOT IN in_predicate_value
		{ $$ = make_node (nod_not, 1, make_node (nod_eql_any, 2, $1, $4)); }
		;

containing_predicate	: value CONTAINING value
		{ $$ = make_node (nod_containing, 2, $1, $3); }
	| value NOT CONTAINING value
		{ $$ = make_node (nod_not, 1, make_node (nod_containing, 2, $1, $4)); }
		;

starting_predicate	: value STARTING value
		{ $$ = make_node (nod_starting, 2, $1, $3); }
	| value NOT STARTING value
		{ $$ = make_node (nod_not, 1, make_node (nod_starting, 2, $1, $4)); }
	| value STARTING WITH value
		{ $$ = make_node (nod_starting, 2, $1, $4); }
	| value NOT STARTING WITH value
		{ $$ = make_node (nod_not, 1, make_node (nod_starting, 2, $1, $5)); }
		;

exists_predicate : EXISTS '(' ordered_select_expr ')'
		{ $$ = make_node (nod_exists, 1, $3); }
		;

unique_predicate : SINGULAR '(' ordered_select_expr ')'
		{ $$ = make_node (nod_singular, 1, $3); }
		;

null_predicate	: value IS KW_NULL
		{ $$ = make_node (nod_missing, 1, $1); }
	| value IS NOT KW_NULL
		{ $$ = make_node (nod_not, 1, make_node (nod_missing, 1, $1)); }
	;

trigger_action_predicate	: INSERTING
		{ $$ = make_node (nod_eql, 2,
					make_node (nod_internal_info, e_internal_info_count,
						MAKE_constant ((STR) internal_trigger_action, CONSTANT_SLONG)),
						MAKE_constant ((STR) 1, CONSTANT_SLONG)); }
	| UPDATING
		{ $$ = make_node (nod_eql, 2,
					make_node (nod_internal_info, e_internal_info_count,
						MAKE_constant ((STR) internal_trigger_action, CONSTANT_SLONG)),
						MAKE_constant ((STR) 2, CONSTANT_SLONG)); }
	| DELETING
		{ $$ = make_node (nod_eql, 2,
					make_node (nod_internal_info, e_internal_info_count,
						MAKE_constant ((STR) internal_trigger_action, CONSTANT_SLONG)),
						MAKE_constant ((STR) 3, CONSTANT_SLONG)); }
	;


/* set values */

in_predicate_value	: table_subquery
		| '(' value_list ')'
			{ $$ = make_list ($2); }
		;

table_subquery	: '(' column_select ')'
			{ $$ = $2; } 
		;

column_select	: SELECT limit_clause
            distinct_clause
			value 
			from_clause 
			where_clause 
			group_clause
			having_clause
			plan_clause
			order_clause
		    { $$ = make_node (nod_select_expr, e_sel_count, 
				$2, $3, make_list ($4), $5, $6, $7, $8, $9, $10, NULL); }
		;

column_singleton : SELECT limit_clause
            distinct_clause
			value 
			from_clause 
			where_clause 
			group_clause
			having_clause
			plan_clause
			order_clause
		    { $$ = make_node (nod_select_expr, e_sel_count, 
				$2, $3, make_list ($4), $5, $6, $7, $8, $9, $10,
				MAKE_constant ((STR) 1, CONSTANT_SLONG)); }
		;


/* value types */

value	: column_name
		| array_element
		| function
		| u_constant
		| parameter
		| variable
		| cast_specification
		| case_expression
		| udf
		| '-' value
			{ $$ = make_node (nod_negate, 1, $2); }
                | '+' value
                        { $$ = $2; }
		| value '+' value
			{ 
			  if (client_dialect >= SQL_DIALECT_V6_TRANSITION)
			      $$ = make_node (nod_add2, 2, $1, $3);
			  else
			      $$ = make_node (nod_add, 2, $1, $3);
			}
		| value CONCATENATE value
			{ $$ = make_node (nod_concatenate, 2, $1, $3); }
		| value COLLATE symbol_collation_name
			{ $$ = make_node (nod_collate, e_coll_count, (DSQL_NOD) $3, $1); }
		| value '-' value
			{ 
			  if (client_dialect >= SQL_DIALECT_V6_TRANSITION)
			      $$ = make_node (nod_subtract2, 2, $1, $3);
			  else 
			      $$ = make_node (nod_subtract, 2, $1, $3);
			}
		| value '*' value
			{ 
			  if (client_dialect >= SQL_DIALECT_V6_TRANSITION)
			       $$ = make_node (nod_multiply2, 2, $1, $3);
			  else
			       $$ = make_node (nod_multiply, 2, $1, $3);
			}
		| value '/' value
			{
			  if (client_dialect >= SQL_DIALECT_V6_TRANSITION)
			      $$ = make_node (nod_divide2, 2, $1, $3);
			  else
			      $$ = make_node (nod_divide, 2, $1, $3);
			}
		| '(' value ')'
			{ $$ = $2; }
		| '(' column_singleton ')'
			{ $$ = $2; }
		| current_user
		| current_role
		| internal_info
		| DB_KEY
			{ $$ = make_node (nod_dbkey, 1, NULL); }
		| symbol_table_alias_name '.' DB_KEY
			{ $$ = make_node (nod_dbkey, 1, $1); }
                | KW_VALUE
                        { 
			  $$ = make_node (nod_dom_value, 0, NULL);
                        }
		| datetime_value_expression
			{ $$ = $1; }
		;

datetime_value_expression : CURRENT_DATE
			{ 
			if (client_dialect < SQL_DIALECT_V6_TRANSITION)
			    ERRD_post (gds_sqlerr, gds_arg_number, (SLONG) -104, 
				    gds_arg_gds, isc_sql_dialect_datatype_unsupport,
				    gds_arg_number, client_dialect,
				    gds_arg_string, "DATE",
				    0);
			if (db_dialect < SQL_DIALECT_V6_TRANSITION)
			    ERRD_post (gds_sqlerr, gds_arg_number, (SLONG) -104, 
				    gds_arg_gds, isc_sql_db_dialect_dtype_unsupport,
				    gds_arg_number, db_dialect,
				    gds_arg_string, "DATE",
				    0);
			$$ = make_node (nod_current_date, 0, NULL);
			}
		| CURRENT_TIME
			{ 
			if (client_dialect < SQL_DIALECT_V6_TRANSITION)
			    ERRD_post (gds_sqlerr, gds_arg_number, (SLONG) -104, 
				    gds_arg_gds, isc_sql_dialect_datatype_unsupport,
				    gds_arg_number, client_dialect,
				    gds_arg_string, "TIME",
				    0);
			if (db_dialect < SQL_DIALECT_V6_TRANSITION)
			    ERRD_post (gds_sqlerr, gds_arg_number, (SLONG) -104, 
				    gds_arg_gds, isc_sql_db_dialect_dtype_unsupport,
				    gds_arg_number, db_dialect,
				    gds_arg_string, "TIME",
				    0);
			$$ = make_node (nod_current_time, 0, NULL);
			}
		| CURRENT_TIMESTAMP
			{ $$ = make_node (nod_current_timestamp, 0, NULL); }
		;

array_element   : column_name '[' value_list ']'
			{ $$ = make_node (nod_array, 2, $1, make_list ($3)); }
		;

value_list	: value
		| value_list ',' value
			{ $$ = make_node (nod_list, 2, $1, $3); }
		;

constant	: u_constant
		| '-' u_numeric_constant
			{ $$ = make_node (nod_negate, 1, $2); }
		;

u_numeric_constant : NUMERIC
			{ $$ = MAKE_constant ((STR) $1, CONSTANT_STRING); }
		| NUMBER
			{ $$ = MAKE_constant ((STR) $1, CONSTANT_SLONG); }
		| FLOAT_NUMBER
			{ $$ = MAKE_constant ((STR) $1, CONSTANT_DOUBLE); }
		| NUMBER64BIT
			{ $$ = MAKE_constant ((STR) $1, CONSTANT_SINT64); }
		| SCALEDINT
			{ $$ = MAKE_constant ((STR) $1, CONSTANT_SINT64); }
		;

u_constant	: u_numeric_constant
		| sql_string
			{ $$ = MAKE_str_constant ((STR) $1, att_charset); }
		| DATE STRING
			{ 
			if (client_dialect < SQL_DIALECT_V6_TRANSITION)
			    ERRD_post (gds_sqlerr, gds_arg_number, (SLONG) -104, 
				    gds_arg_gds, isc_sql_dialect_datatype_unsupport,
				    gds_arg_number, client_dialect,
				    gds_arg_string, "DATE",
				    0);
			if (db_dialect < SQL_DIALECT_V6_TRANSITION)
			    ERRD_post (gds_sqlerr, gds_arg_number, (SLONG) -104, 
				    gds_arg_gds, isc_sql_db_dialect_dtype_unsupport,
				    gds_arg_number, db_dialect,
				    gds_arg_string, "DATE",
				    0);
			$$ = MAKE_constant ((STR) $2, CONSTANT_DATE);
			}
		| TIME STRING
			{
			if (client_dialect < SQL_DIALECT_V6_TRANSITION)
			    ERRD_post (gds_sqlerr, gds_arg_number, (SLONG) -104, 
				    gds_arg_gds, isc_sql_dialect_datatype_unsupport,
				    gds_arg_number, client_dialect,
				    gds_arg_string, "TIME",
				    0);
			if (db_dialect < SQL_DIALECT_V6_TRANSITION)
			    ERRD_post (gds_sqlerr, gds_arg_number, (SLONG) -104, 
				    gds_arg_gds, isc_sql_db_dialect_dtype_unsupport,
				    gds_arg_number, db_dialect,
				    gds_arg_string, "TIME",
				    0);
			$$ = MAKE_constant ((STR) $2, CONSTANT_TIME); 
			}
		| TIMESTAMP STRING
			{ $$ = MAKE_constant ((STR) $2, CONSTANT_TIMESTAMP); }
		;

parameter	: '?'
			{ $$ = make_parameter (); }
		;

current_user	: USER
			{ $$ = make_node (nod_user_name, 0, NULL); }
		| CURRENT_USER
			{ $$ = make_node (nod_user_name, 0, NULL); }
		;

current_role	: CURRENT_ROLE
			{ $$ = make_node (nod_current_role, 0, NULL); }
		;

internal_info	: CURRENT_CONNECTION
			{ $$ = make_node (nod_internal_info, e_internal_info_count,
						MAKE_constant ((STR) internal_connection_id, CONSTANT_SLONG)); }
		| CURRENT_TRANSACTION
			{ $$ = make_node (nod_internal_info, e_internal_info_count,
						MAKE_constant ((STR) internal_transaction_id, CONSTANT_SLONG)); }
		| GDSCODE
			{ $$ = make_node (nod_internal_info, e_internal_info_count,
						MAKE_constant ((STR) internal_gdscode, CONSTANT_SLONG)); }
		| SQLCODE
			{ $$ = make_node (nod_internal_info, e_internal_info_count,
						MAKE_constant ((STR) internal_sqlcode, CONSTANT_SLONG)); }
		| ROW_COUNT
			{ $$ = make_node (nod_internal_info, e_internal_info_count,
						MAKE_constant ((STR) internal_rows_affected, CONSTANT_SLONG)); }
		;

sql_string	: STRING			/* string in current charset */
			{ $$ = $1; }
		| INTRODUCER STRING		/* string in specific charset */
			{ ((STR) $2)->str_charset = (TEXT *) $1;
			  $$ = $2; }
		;

signed_short_integer	:	nonneg_short_integer
		| '-' neg_short_integer
			{ $$ = (DSQL_NOD) - (SLONG) $2; }
		;

nonneg_short_integer	: NUMBER
			{ if ((SLONG) $1 > SHRT_POS_MAX)
			    yyabandon (-842, isc_expec_short);
				/* Short integer expected */
			  $$ = $1;}
		;

neg_short_integer : NUMBER
			{ if ((SLONG) $1 > SHRT_NEG_MAX)
			    yyabandon (-842, isc_expec_short);
				/* Short integer expected */
			  $$ = $1;}
		;

pos_short_integer : nonneg_short_integer
			{ if ((SLONG) $1 == 0)
			    yyabandon (-842, isc_expec_positive);
				/* Positive number expected */
			  $$ = $1;}
		;

unsigned_short_integer : NUMBER
			{ if ((SLONG) $1 > SHRT_UNSIGNED_MAX)
			    yyabandon (-842, isc_expec_ushort);
				/* Unsigned short integer expected */
			  $$ = $1;}
		;

signed_long_integer	:	long_integer
		| '-' long_integer
			{ $$ = (DSQL_NOD) - (SLONG) $2; }
		;

long_integer	: NUMBER
			{ $$ = $1;}
		;
	
/* functions */

function	: aggregate_function
	| generate_value_function
	| numeric_value_function
	| string_value_function
	;
	
aggregate_function	: COUNT '(' '*' ')'
			{ $$ = make_node (nod_agg_count, 0, NULL); }
		| COUNT '(' all_noise value ')'
			{ $$ = make_node (nod_agg_count, 1, $4); }
		| COUNT '(' DISTINCT value ')'
			{ $$ = make_flag_node (nod_agg_count,
                                       NOD_AGG_DISTINCT, 1, $4); }
		| SUM '(' all_noise value ')'
			{ 
			  if (client_dialect >= SQL_DIALECT_V6_TRANSITION)
			      $$ = make_node (nod_agg_total2, 1, $4);
			  else
			      $$ = make_node (nod_agg_total, 1, $4);
			}
		| SUM '(' DISTINCT value ')'
			{ 
			  if (client_dialect >= SQL_DIALECT_V6_TRANSITION)
			      $$ = make_flag_node (nod_agg_total2,
						   NOD_AGG_DISTINCT, 1, $4);
			  else
			      $$ = make_flag_node (nod_agg_total,
						   NOD_AGG_DISTINCT, 1, $4);
			}
		| AVG '(' all_noise value ')'
			{ 
			  if (client_dialect >= SQL_DIALECT_V6_TRANSITION)
			      $$ = make_node (nod_agg_average2, 1, $4);
			  else
			      $$ = make_node (nod_agg_average, 1, $4);
			}
		| AVG '(' DISTINCT value ')'
			{ 
			  if (client_dialect >= SQL_DIALECT_V6_TRANSITION)
			      $$ = make_flag_node (nod_agg_average2,
						   NOD_AGG_DISTINCT, 1, $4);
			  else
			      $$ = make_flag_node (nod_agg_average,
						   NOD_AGG_DISTINCT, 1, $4);
			}
		| MINIMUM '(' all_noise value ')'
			{ $$ = make_node (nod_agg_min, 1, $4); }
		| MINIMUM '(' DISTINCT value ')'
			{ $$ = make_node (nod_agg_min, 1, $4); }
		| MAXIMUM '(' all_noise value ')'
			{ $$ = make_node (nod_agg_max, 1, $4); }
		| MAXIMUM '(' DISTINCT value ')'
			{ $$ = make_node (nod_agg_max, 1, $4); }
		;

/* Firebird specific functions into 'generate_value_function' */

generate_value_function	: GEN_ID '(' symbol_generator_name ',' value ')'	
				{ 
				  if (client_dialect >= SQL_DIALECT_V6_TRANSITION)
				      $$ = make_node (nod_gen_id2, 2, $3, $5);
				  else
					  $$ = make_node (nod_gen_id, 2, $3, $5);
				}
			;

numeric_value_function	:  EXTRACT '(' timestamp_part FROM value ')'
				{ $$ = make_node (nod_extract, e_extract_count, $3, $5); }
			;

string_value_function	:  SUBSTRING '(' value FROM pos_short_integer ')'
				{ $$ = make_node (nod_substr, e_substr_count, $3,
					MAKE_constant ((STR) ((SLONG)($5) - 1), CONSTANT_SLONG),
					MAKE_constant ((STR) SHRT_POS_MAX, CONSTANT_SLONG)); }
			/* CVC: It was easier to provide a constant with maximum value if the
			third parameter -length- is ommitted than to chase and fix the functions
			that treat nod_substr as an aggregate and do not expect NULL arguments. */
			| SUBSTRING '(' value FROM pos_short_integer FOR nonneg_short_integer ')'
				{ $$ = make_node (nod_substr, e_substr_count, $3,
					MAKE_constant ((STR) ((SLONG)($5) - 1), CONSTANT_SLONG),
					MAKE_constant ((STR) ($7), CONSTANT_SLONG)); }
			| KW_UPPER '(' value ')'
				{ $$ = make_node (nod_upcase, 1, $3); }
			;

udf		: symbol_UDF_name '(' value_list ')'
			{ $$ = make_node (nod_udf, 2, $1, $3); }
		| symbol_UDF_name '(' ')'
			{ $$ = make_node (nod_udf, 1, $1); }
		;

cast_specification	: CAST '(' rhs AS data_type_descriptor ')'
			{ $$ = make_node (nod_cast, e_cast_count, $5, $3); }
		;

/* case expressions */

case_expression	: case_abbreviation
		| case_specification
		;

case_abbreviation	: NULLIF '(' value ',' value ')'
			{ $$ = make_node (nod_searched_case, 2, 
				make_node (nod_list, 2, make_node (nod_eql, 2, $3, $5), 
				make_node (nod_null, 0, NULL)), $3); }
		| COALESCE '(' null_or_value ',' null_or_value_list ')'
			{ $$ = make_node (nod_coalesce, 2, $3, $5); }
		;

case_specification	: simple_case
		| searched_case
		;

simple_case	: CASE case_operand simple_when_clause END
			{ $$ = make_node (nod_simple_case, 3, $2, make_list($3), make_node (nod_null, 0, NULL)); }
		| CASE case_operand simple_when_clause ELSE case_result END
			{ $$ = make_node (nod_simple_case, 3, $2, make_list($3), $5); }
		;

simple_when_clause	: WHEN when_operand THEN case_result
				{ $$ = make_node (nod_list, 2, $2, $4); }
			| simple_when_clause WHEN when_operand THEN case_result
				{ $$ = make_node (nod_list, 2, $1, make_node (nod_list, 2, $3, $5)); }
			;

searched_case	: CASE searched_when_clause END
			{ $$ = make_node (nod_searched_case, 2, make_list($2), make_node (nod_null, 0, NULL)); }
		| CASE searched_when_clause ELSE case_result END
			{ $$ = make_node (nod_searched_case, 2, make_list($2), $4); }
		;

searched_when_clause	: WHEN search_condition THEN case_result
			{ $$ = make_node (nod_list, 2, $2, $4); }
		| searched_when_clause WHEN search_condition THEN case_result
			{ $$ = make_node (nod_list, 2, $1, make_node (nod_list, 2, $3, $5)); }
		;

when_operand	: value
		;

case_operand	: value
		;

case_result	: null_or_value
		;

timestamp_part	: YEAR
			{ $$ = MAKE_constant ((STR)blr_extract_year, CONSTANT_SLONG); }
		| MONTH
			{ $$ = MAKE_constant ((STR)blr_extract_month, CONSTANT_SLONG); }
		| DAY
			{ $$ = MAKE_constant ((STR)blr_extract_day, CONSTANT_SLONG); }
		| HOUR
			{ $$ = MAKE_constant ((STR)blr_extract_hour, CONSTANT_SLONG); }
		| MINUTE
			{ $$ = MAKE_constant ((STR)blr_extract_minute, CONSTANT_SLONG); }
		| SECOND
			{ $$ = MAKE_constant ((STR)blr_extract_second, CONSTANT_SLONG); }
		| WEEKDAY
			{ $$ = MAKE_constant ((STR)blr_extract_weekday, CONSTANT_SLONG); }
		| YEARDAY
			{ $$ = MAKE_constant ((STR)blr_extract_yearday, CONSTANT_SLONG); }
		;

all_noise	: ALL
		|
		;

null_value	: KW_NULL
			{ $$ = make_node (nod_null, 0, NULL); }
		;



/* Performs special mapping of keywords into symbols */

symbol_UDF_name	: SYMBOL
	;

symbol_blob_subtype_name	: valid_symbol_name
	;

symbol_character_set_name	: valid_symbol_name
	;

symbol_collation_name	: valid_symbol_name
	;

symbol_column_name	: valid_symbol_name
	;

symbol_constraint_name	: valid_symbol_name
	;

symbol_cursor_name	: valid_symbol_name
	;

symbol_domain_name	: valid_symbol_name
	;

symbol_exception_name	: valid_symbol_name
	;

symbol_filter_name	: valid_symbol_name
	;

symbol_gdscode_name	: valid_symbol_name
	;

symbol_generator_name	: valid_symbol_name
	;

symbol_index_name	: valid_symbol_name
	;

symbol_item_alias_name	: valid_symbol_name
	;

/* dimitr: commented out until pass1.cpp is ready to assign label numbers properly
symbol_label_name	: valid_symbol_name
	;
*/

symbol_procedure_name	: valid_symbol_name
	;

symbol_role_name	: valid_symbol_name
	;

symbol_table_alias_name	: valid_symbol_name
	;

symbol_table_name	: valid_symbol_name
	;

symbol_trigger_name	: valid_symbol_name
	;

symbol_user_name	: valid_symbol_name
	;

symbol_variable_name	: valid_symbol_name
	;

symbol_view_name	: valid_symbol_name
	;

symbol_savepoint_name	: valid_symbol_name
	;

/* symbols */

valid_symbol_name	: SYMBOL
	| non_reserved_word
	;

/* list of non-reserved words */

non_reserved_word :
	KW_BREAK
	| COALESCE
	| KW_DESCRIPTOR
	| LAST
	| LEAVE
	| LOCK
	| NULLIF
	| NULLS
	| STATEMENT
	| SUBSTRING
	| USING
	;

%%


/*
 *	PROGRAM:	Dynamic SQL runtime support
 *	MODULE:		lex.c
 *	DESCRIPTION:	Lexical routine
 *
 */


void LEX_dsql_init (void)
{
/**************************************
 *
 *	L E X _ d s q l _ i n i t
 *
 **************************************
 *
 * Functional description
 *	Initialize LEX for processing.  This is called only once
 *	per session.
 *
 **************************************/
	for (const TOK *token = KEYWORD_getTokens(); token->tok_string; ++token)
	{
		SYM symbol = FB_NEW_RPT(*DSQL_permanent_pool, 0) sym;
		symbol->sym_string = (TEXT *) token->tok_string;
		symbol->sym_length = strlen(token->tok_string);
		symbol->sym_type = SYM_keyword;
		symbol->sym_keyword = token->tok_ident;
		symbol->sym_version = token->tok_version;
		STR str_ = FB_NEW_RPT(*DSQL_permanent_pool, symbol->sym_length) str;
		str_->str_length = symbol->sym_length;
		strncpy((char*)str_->str_data, (char*)symbol->sym_string, symbol->sym_length);
		symbol->sym_object = (void *) str_;
		HSHD_insert(symbol);
	}
}


void LEX_string (
    TEXT	*string,
    USHORT	length,
    SSHORT	character_set)
{
/**************************************
 *
 *	L E X _ s t r i n g
 *
 **************************************
 *
 * Functional description
 *	Initialize LEX to process a string.
 *
 **************************************/

    line_start = ptr = string;
    end = string + length;
    lines = 1;
    att_charset = character_set;
    line_start_bk = line_start;
    lines_bk = lines;
	param_number = 1;
#ifdef DEV_BUILD
    if (DSQL_debug & 32)
        printf("%.*s\n", (int)length, string);
#endif
}


static void check_log_file_attrs (void)
{
/**********************************************
 *
 *	c h e c k _ l o g _ f i l e _ a t t r s
 *
 **********************************************
 *
 * Functional description
 *	Check if log file attributes are valid
 *
 *********************************************/

    if (g_file->fil_partitions) {
        if (!g_file->fil_length) {
            yyabandon (-261, isc_log_length_spec);
            /* Total length of a partitioned log must be specified */
        }
        
        if (PARTITION_SIZE (OneK * g_file->fil_length, g_file->fil_partitions) <
            (OneK*MIN_LOG_LENGTH)) {
            yyabandon (-239, isc_partition_too_small);
            /* Log partition size too small */
        }
    }
    else {
        if ((g_file->fil_length) && (g_file->fil_length < MIN_LOG_LENGTH)) {
            yyabandon (-239, isc_log_too_small);   /* Log size too small */
        }
    }     
}


static TEXT *lex_position (void)
{
/**************************************
 *
 *	l e x _ p o s i t i o n
 *
 **************************************
 *
 * Functional description
 *	Return the current position of LEX 
 *	in the input string.
 *
 **************************************/

return ptr;
}


#ifdef NOT_USED_OR_REPLACED
static BOOLEAN long_int (
    DSQL_NOD		string,
    SLONG	*long_value)
{
/*************************************
 *
 *	l o n g _ i n t
 * 
 *************************************
 *
 * Functional description
 *	checks for all digits in the
 *	number and return an atol().
 *
 *************************************/

	for (const char* p = ((STR) string)->str_data; classes [*p] & CHR_DIGIT; p++)
	{
		if (!(classes [*p] & CHR_DIGIT)) {
			return FALSE;
		}
	}

	*long_value = atol ((char *)((STR) string)->str_data);

	return TRUE;
}
#endif

static DSQL_FLD make_field (
    DSQL_NOD		field_name)
{
/**************************************
 *
 *	m a k e _ f i e l d
 *
 **************************************
 *
 * Functional description
 *	Make a field block of given name.
 *
 **************************************/
DSQL_FLD	field;
STR	string;
TSQL    tdsql;

tdsql = GET_THREAD_DATA;
       
if (field_name == NULL)
   {
    field = FB_NEW_RPT(*tdsql->tsql_default, sizeof (INTERNAL_FIELD_NAME)) dsql_fld;
    strcpy (field->fld_name, (TEXT*) INTERNAL_FIELD_NAME);
    return field;
   }
string = (STR) field_name->nod_arg [1];
field = FB_NEW_RPT(*tdsql->tsql_default, strlen ((SCHAR*) string->str_data)) dsql_fld;
strcpy (field->fld_name, (TEXT*) string->str_data);

return field;
}


static FIL make_file (void)
{
/**************************************
 *
 *	m a k e _ f i l e 
 *
 **************************************
 *
 * Functional description
 *	Make a file block
 *
 **************************************/
FIL	temp_file;
TSQL    tdsql;

tdsql = GET_THREAD_DATA;
       
temp_file = FB_NEW(*tdsql->tsql_default) fil;

return temp_file;
}


static DSQL_NOD make_list (
    DSQL_NOD		node)
{
/**************************************
 *
 *	m a k e _ l i s t
 *
 **************************************
 *
 * Functional description
 *	Collapse nested list nodes into single list.
 *
 **************************************/
DSQL_NOD	*ptr;
DLLS	stack, temp;
USHORT	l;
DSQL_NOD	old;
TSQL    tdsql;

tdsql = GET_THREAD_DATA;

if (!node)
    return node;

stack = 0;
stack_nodes (node, &stack);
for (l = 0, temp = stack; temp; temp = temp->lls_next)
    l++;

old  = node;
node = FB_NEW_RPT(*tdsql->tsql_default, l) dsql_nod;
node->nod_count = l;
node->nod_type  = nod_list;
node->nod_flags = old->nod_flags;
ptr = node->nod_arg + node->nod_count;

while (stack)
    *--ptr = (DSQL_NOD) LLS_POP (&stack);

return node;
}


static DSQL_NOD make_parameter (void)
{
/**************************************
 *
 *	m a k e _ p a r a m e t e r
 *
 **************************************
 *
 * Functional description
 *	Make parameter node
 *	Any change should also be made to function below
 *
 **************************************/
DSQL_NOD	node;
TSQL    tdsql;

tdsql = GET_THREAD_DATA;

node = FB_NEW_RPT(*tdsql->tsql_default, 1) dsql_nod;
node->nod_type = nod_parameter;
node->nod_line = (USHORT) lines_bk;
node->nod_column = (USHORT) (last_token_bk - line_start_bk + 1);
node->nod_count = 1;
node->nod_arg[0] = (DSQL_NOD)param_number++;

return node;
}


static DSQL_NOD make_node (
    NOD_TYPE	type,
    int		count,
    ...)
{
/**************************************
 *
 *	m a k e _ n o d e
 *
 **************************************
 *
 * Functional description
 *	Make a node of given type.
 *	Any change should also be made to function below
 *
 **************************************/
DSQL_NOD	node, *p;
va_list	ptr;
TSQL    tdsql;

tdsql = GET_THREAD_DATA;

node = FB_NEW_RPT(*tdsql->tsql_default, count) dsql_nod;
node->nod_type = type;
node->nod_line = (USHORT) lines_bk;
node->nod_column = (USHORT) (last_token_bk - line_start_bk + 1);
node->nod_count = count;
p = node->nod_arg;
VA_START (ptr, count);

while (--count >= 0)
    *p++ = va_arg (ptr, DSQL_NOD);

return node;
}


static DSQL_NOD make_flag_node (
    NOD_TYPE	type,
    SSHORT	flag,
    int		count,
    ...)
{
/**************************************
 *
 *	m a k e _ f l a g _ n o d e
 *
 **************************************
 *
 * Functional description
 *	Make a node of given type. Set flag field
 *
 **************************************/
DSQL_NOD	node, *p;
va_list	ptr;
TSQL    tdsql;

tdsql = GET_THREAD_DATA;

node = FB_NEW_RPT(*tdsql->tsql_default, count) dsql_nod;
node->nod_type = type;
node->nod_flags = flag;
node->nod_line = (USHORT) lines_bk;
node->nod_column = (USHORT) (last_token_bk - line_start_bk + 1);
node->nod_count = count;
p = node->nod_arg;
VA_START (ptr, count);

while (--count >= 0)
    *p++ = va_arg (ptr, DSQL_NOD);

return node;
}


static void prepare_console_debug (int level, int *yydeb)
{
/*************************************
 *
 *	p r e p a r e _ c o n s o l e _ d e b u g
 * 
 *************************************
 *
 * Functional description
 *	Activate debug info. In WinNT, redirect the standard
 *	output so one can see the generated information.
 *	Feel free to add your platform specific code.
 *
 *************************************/
    DSQL_debug = level;
    if (level >> 8)
        *yydeb = level >> 8;
    /* CVC: I added this code form Mike Nordell to see the output from internal
       operations that's generated in DEV build when DEBUG <n> is typed into isql.exe.
       When n>0, the output console is activated; otherwise it's closed. */
#if defined(DEV_BUILD) && defined(WIN_NT) && defined(SUPERSERVER)
    if (level > 0) {
        /* Console debug code inside this scope */
        if (AllocConsole()) {
    		redirected_output = freopen("CONOUT$", "wt", stdout);
    		printf("DebugConsole - Yes, it's working.\n");
        }
    }
    else if (level <= 0 && redirected_output) {
        fclose (redirected_output);
        redirected_output = 0;
        FreeConsole();
    }
#endif
}

#ifdef NOT_USED_OR_REPLACED
static BOOLEAN short_int (
    DSQL_NOD		string,
    SLONG	*long_value,
    SSHORT	range)
{
/*************************************
 *
 *	s h o r t _ i n t
 * 
 *************************************
 *
 * Functional description
 *	is the string a valid representation 
 *	of a positive short int?
 *
 *************************************/

	if (((STR) string)->str_length > 5) {
		return FALSE;
	}

	for (char* p = ((STR) string)->str_data; classes [*p] & CHR_DIGIT; p++)
	{
		if (!(classes [*p] & CHR_DIGIT)) {
			return FALSE;
		}
	}

	/* there are 5 or fewer digits, it's value may still be greater
	 * than 32767... */

	SCHAR buf[10];    
	buf [0] = ((STR) string)->str_data[0];
	buf [1] = ((STR) string)->str_data[1];
	buf [2] = ((STR) string)->str_data[2];
	buf [3] = ((STR) string)->str_data[3];
	buf [4] = ((STR) string)->str_data[4];
	buf [5] = '\0';

	*long_value = atoi (buf);

	BOOLEAN return_value;

	switch (range) 
	{
		case POSITIVE:
			return_value = *long_value > SHRT_POS_MAX;		
			break;
		case NEGATIVE:
			return_value = *long_value > SHRT_NEG_MAX;
			break;
		case UNSIGNED:
			return_value = *long_value > SHRT_UNSIGNED_MAX;		
			break;
	}
	return !return_value;
}
#endif

static void stack_nodes (
    DSQL_NOD		node,
    DLLS		*stack)
{
/**************************************
 *
 *	s t a c k _ n o d e s
 *
 **************************************
 *
 * Functional description
 *	Assist in turning a tree of misc nodes into a clean list.
 *
 **************************************/
DSQL_NOD	*ptr, *end;
DSQL_NOD     curr_node, next_node, start_chain, end_chain, save_link;

if (node->nod_type != nod_list)
    {
    LLS_PUSH (node, stack);
    return;
    }

/* To take care of cases where long lists of nodes are in a chain
   of list nodes with exactly one entry, this algorithm will look
   for a pattern of repeated list nodes with two entries, the first
   being a list node and the second being a non-list node.   Such
   a list will be reverse linked, and then re-reversed, stacking the
   non-list nodes in the process.   The purpose of this is to avoid
   massive recursion of this function. */

start_chain = node;
end_chain = (DSQL_NOD) NULL;
curr_node = node;
next_node = node->nod_arg[0];
while ( curr_node->nod_count == 2 &&
        curr_node->nod_arg[0]->nod_type == nod_list &&
        curr_node->nod_arg[1]->nod_type != nod_list &&
        next_node->nod_arg[0]->nod_type == nod_list &&
        next_node->nod_arg[1]->nod_type != nod_list)
    {

    /* pattern was found so reverse the links and go to next node */

    save_link = next_node->nod_arg[0];
    next_node->nod_arg[0] = curr_node;
    curr_node = next_node;
    next_node = save_link;
    end_chain = curr_node;
    }

/* see if any chain was found */

if ( end_chain)
    {

    /* first, handle the rest of the nodes */
    /* note that next_node still points to the first non-pattern node */

    stack_nodes( next_node, stack);

    /* stack the non-list nodes and reverse the chain on the way back */
    
    curr_node = end_chain;
    while ( TRUE)
        {
        LLS_PUSH( curr_node->nod_arg[1], stack);
        if ( curr_node == start_chain)
            break;
        save_link = curr_node->nod_arg[0];
        curr_node->nod_arg[0] = next_node;
        next_node = curr_node;
        curr_node = save_link;
        }
    return;
    }

for (ptr = node->nod_arg, end = ptr + node->nod_count; ptr < end; ptr++)
    stack_nodes (*ptr, stack);
}


static int yylex (
    USHORT	client_dialect,
    USHORT	db_dialect,
    USHORT	parser_version,
    BOOLEAN	*stmt_ambiguous)
{
/**************************************
 *
 *	y y l e x
 *
 **************************************
 *
 * Functional description: lexer.
 *
 **************************************/
UCHAR	tok_class;
char  string[MAX_TOKEN_LEN];
char* p;
char* buffer;
char* buffer_end;
char* new_buffer;
SYM	sym;
SSHORT	c;
USHORT	buffer_len;

STR	delimited_id_str;

/* Find end of white space and skip comments */

for (;;)
    {
    if (ptr >= end)
	return -1;
    
    c = *ptr++;

	/* Process comments */
    
    if (c == '\n') {
	lines++;
	line_start = ptr;
	continue;
    }

    if ((c == '-') && (*ptr == '-')) {
		
		/* single-line */
		
		ptr++;
		while (ptr < end) {
			if ((c = *ptr++) == '\n') {
				lines++;
				line_start = ptr /* + 1*/; /* CVC: +1 left out. */
				break;
			}
	    }
		if (ptr >= end)
			return -1;
		continue;
	}
    else if ((c == '/') && (*ptr == '*')) {
		
		/* multi-line */
		
		ptr++;
		while (ptr < end) {
			if ((c = *ptr++) == '*') {
				if (*ptr == '/')
					break;
			}
			if (c == '\n') {
				lines++;
				line_start = ptr /* + 1*/; /* CVC: +1 left out. */

			}
	    }
		if (ptr >= end)
			return -1;
		ptr++;
		continue;
	}

    tok_class = classes [c];

    if (!(tok_class & CHR_WHITE))
	break;
    }

/* Depending on tok_class of token, parse token */

last_token = ptr - 1;

if (tok_class & CHR_INTRODUCER)
    {
    /* The Introducer (_) is skipped, all other idents are copied
     * to become the name of the character set
     */
    p = string;
    for (; ptr < end && classes [*ptr] & CHR_IDENT; ptr++)
	{
	if (ptr >= end)
	    return -1;
	CHECK_COPY_INCR(p, UPPER7(*ptr));
	}
    
    CHECK_BOUND(p);
    *p = 0;

    /* make a string value to hold the name, the name 
     * is resolved in pass1_constant */

    yylval = (DSQL_NOD) (MAKE_string(string, p - string))->str_data;

    return INTRODUCER;
    }

/* parse a quoted string, being sure to look for double quotes */

if (tok_class & CHR_QUOTE)
    {
    buffer = string;
    buffer_len = sizeof (string);
    buffer_end = buffer + buffer_len - 1;
    for (p = buffer; ; p++)
	{
	if (ptr >= end)
	    {
	    if (buffer != string)
       		gds__free (buffer);
	    return -1;
	    }
	/* *ptr is quote - if next != quote we're at the end */
	if ((*ptr == c) && ((++ptr == end) || (*ptr != c)))
	    break;
        if (p > buffer_end)
	    {
	    new_buffer = (char*)gds__alloc (2 * buffer_len);
	    /* FREE: at outer block */
	    if (!new_buffer)		/* NOMEM: */
		{
		if (buffer != string)
		    gds__free (buffer);
		return -1;
		}
            memcpy (new_buffer, buffer, buffer_len);
	    if (buffer != string)
        	gds__free (buffer);
	    buffer = new_buffer;
	    p = buffer + buffer_len;
	    buffer_len = 2 * buffer_len;
	    buffer_end = buffer + buffer_len - 1;
	    }
	*p = *ptr++;
	}
    if (c == '"')
	{
	*stmt_ambiguous = TRUE; /* string delimited by double quotes could be
				**   either a string constant or a SQL delimited
				**   identifier, therefore marks the SQL
				**   statement as ambiguous  */
	if (client_dialect == SQL_DIALECT_V6_TRANSITION)
	    {
	    if (buffer != string)
		gds__free (buffer);
	    yyabandon (-104, isc_invalid_string_constant);
	    }
	else if (client_dialect >= SQL_DIALECT_V6)
	    {
	    if ((p - buffer) >= MAX_TOKEN_LEN)
		{
		if (buffer != string)
		    gds__free (buffer);
		yyabandon (-104, isc_token_too_long);
		}
	    yylval = (DSQL_NOD) MAKE_string(buffer, p - buffer);
	    delimited_id_str = (STR) yylval;
	    delimited_id_str->str_flags |= STR_delimited_id;
	    if (buffer != string)
		gds__free (buffer);
	    return SYMBOL;
	    }
	}
    yylval = (DSQL_NOD) MAKE_string(buffer, p - buffer);
    if (buffer != string)
	gds__free (buffer);
    return STRING;
    }
                                                 
/* 
 * Check for a numeric constant, which starts either with a digit or with
 * a decimal point followed by a digit.
 * 
 * This code recognizes the following token types:
 * 
 * NUMBER: string of digits which fits into a 32-bit integer
 * 
 * NUMBER64BIT: string of digits whose value might fit into an SINT64,
 *   depending on whether or not there is a preceding '-', which is to
 *   say that "9223372036854775808" is accepted here.
 *
 * SCALEDINT: string of digits and a single '.', where the digits
 *   represent a value which might fit into an SINT64, depending on
 *   whether or not there is a preceding '-'.
 *
 * FLOAT: string of digits with an optional '.', and followed by an "e"
 *   or "E" and an optionally-signed exponent.
 *
 * NOTE: we swallow leading or trailing blanks, but we do NOT accept
 *   embedded blanks:
 *
 * Another note: c is the first character which need to be considered,
 *   ptr points to the next character.
 */

assert(ptr <= end);

if ((tok_class & CHR_DIGIT) ||
    ((c == '.') && (ptr < end) && (classes [*ptr] & CHR_DIGIT)))
    {
    /* The following variables are used to recognize kinds of numbers. */

    BOOLEAN have_error     = FALSE;	/* syntax error or value too large */
    BOOLEAN have_digit     = FALSE;	/* we've seen a digit              */
    BOOLEAN have_decimal   = FALSE;	/* we've seen a '.'                */
    BOOLEAN have_exp       = FALSE;	/* digit ... [eE]                  */
    BOOLEAN have_exp_sign  = FALSE; /* digit ... [eE] {+-]             */
    BOOLEAN have_exp_digit = FALSE; /* digit ... [eE] ... digit        */
    UINT64	number         = 0;
    UINT64	limit_by_10    = MAX_SINT64 / 10;

    for (--ptr ; ptr < end ; ptr++)
      {
	c = *ptr;
	if (have_exp_digit && (! (classes [c]  & CHR_DIGIT)))
	  /* First non-digit after exponent and digit terminates
	     the token. */
	    break;
	else if (have_exp_sign && (! (classes [c]  & CHR_DIGIT)))
	    {
	    /* only digits can be accepted after "1E-" */
	      have_error = TRUE;
	      break;
	    }
	else if (have_exp)
	    {
	    /* We've seen e or E, but nothing beyond that. */
	    if ( ('-' == c) || ('+' == c) )
		have_exp_sign = TRUE;
	    else if ( classes [c]  & CHR_DIGIT )
		/* We have a digit: we haven't seen a sign yet,
		   but it's too late now. */
		have_exp_digit = have_exp_sign  = TRUE;
	    else
		{
		/* end of the token */
		have_error = TRUE;
		break;
		}
	    }
	else if ('.' == c)
	    {
	    if (!have_decimal)
		have_decimal = TRUE;
	    else
		{
		have_error = TRUE;
		break;
		}
	    }
	else if (classes [c] & CHR_DIGIT)
	  {
	    /* Before computing the next value, make sure there will be
	       no overflow.  */

	    have_digit = TRUE;

	    if (number >= limit_by_10)
		/* possibility of an overflow */
		if ((number > limit_by_10) || (c > '8'))
		    {
		    have_error = TRUE;
		    break;
		    }
	    number = number * 10 + (c - '0');
	  }
	else if ( (('E' == c) || ('e' == c)) && have_digit )
	    have_exp = TRUE;
	else
	    /* Unexpected character: this is the end of the number. */
	    break;
      }

    /* We're done scanning the characters: now return the right kind
       of number token, if any fits the bill. */

    if (!have_error)
	{
	assert(have_digit);

	if (have_exp_digit)
	    {
	    yylval = (DSQL_NOD) MAKE_string(last_token, ptr - last_token);
	    last_token_bk = last_token;
	    line_start_bk = line_start;
	    lines_bk = lines;

	    return FLOAT_NUMBER;
	    }
	else if (!have_exp)
	    {

	    /* We should return some kind (scaled-) integer type
	       except perhaps in dialect 1. */

	    if (!have_decimal && (number <= MAX_SLONG))
		{
		yylval = (DSQL_NOD) number;
		return NUMBER;
		}
	    else
		{
		/* We have either a decimal point with no exponent
		   or a string of digits whose value exceeds MAX_SLONG:
		   the returned type depends on the client dialect,
		   so warn of the difference if the client dialect is
		   SQL_DIALECT_V6_TRANSITION.
		*/

		if (SQL_DIALECT_V6_TRANSITION == client_dialect)
		    {
		    /* Issue a warning about the ambiguity of the numeric
		     * numeric literal.  There are multiple calls because
		     * the message text exceeds the 119-character limit
		     * of our message database.
		     */
		    ERRD_post_warning( isc_dsql_warning_number_ambiguous,
				       gds_arg_string,
				       ERR_string( last_token,
						   ptr - last_token ),
				       gds_arg_end );
		    ERRD_post_warning( isc_dsql_warning_number_ambiguous1,
				       gds_arg_end );
		    }

		yylval = (DSQL_NOD) MAKE_string(last_token, ptr - last_token);

		last_token_bk = last_token;
		line_start_bk = line_start;
		lines_bk = lines;

		if (client_dialect < SQL_DIALECT_V6_TRANSITION)
		    return FLOAT_NUMBER;
		else if (have_decimal)
		    return SCALEDINT;
		else
		    return NUMBER64BIT;
		}
	    } /* else if (!have_exp) */
	} /* if (!have_error) */

    /* we got some kind of error or overflow, so don't recognize this
     * as a number: just pass it through to the next part of the lexer.
     */
    }

/* Restore the status quo ante, before we started our unsuccessful
   attempt to recognize a number. */
ptr = last_token;
c   = *ptr++;
/* We never touched tok_class, so it doesn't need to be restored. */

/* end of number-recognition code */


if (tok_class & CHR_LETTER)
    {
    p = string;
    CHECK_COPY_INCR(p, UPPER (c));
    for (; ptr < end && classes [*ptr] & CHR_IDENT; ptr++)
        {
	if (ptr >= end)
	    return -1;
	CHECK_COPY_INCR(p, UPPER (*ptr));
        }

    CHECK_BOUND(p);
    *p = 0;
    sym = HSHD_lookup (NULL_PTR, (TEXT *) string, (SSHORT)(p - string), SYM_keyword, parser_version);
    if (sym)
	{
	yylval = (DSQL_NOD) sym->sym_object;
	return sym->sym_keyword;
	}
    yylval = (DSQL_NOD) MAKE_string(string, p - string);
    last_token_bk = last_token;
    line_start_bk = line_start;
    lines_bk = lines;
	return SYMBOL;
    }

/* Must be punctuation -- test for double character punctuation */

if (last_token + 1 < end)
    {
    sym = HSHD_lookup (NULL_PTR, last_token, (SSHORT) 2, SYM_keyword, (USHORT) parser_version);
    if (sym)
	{
	++ptr;
	return sym->sym_keyword;
	}
    }

/* Single character punctuation are simply passed on */

return c;
}


static void yyerror (
    TEXT	*error_string)
{
/**************************************
 *
 *	y y e r r o r
 *
 **************************************
 *
 * Functional description
 *	Print a syntax error.
 *
 **************************************/

if (yychar < 1)
    ERRD_post (gds_sqlerr, gds_arg_number, (SLONG) -104,
	gds_arg_gds, gds_command_end_err,    /* Unexpected end of command */
	0);
else
    {
    ERRD_post (gds_sqlerr, gds_arg_number, (SLONG) -104,
 	gds_arg_gds, gds_dsql_token_unk_err, 
	gds_arg_number, (SLONG) lines, 
	gds_arg_number, (SLONG) (last_token - line_start + 1), /*CVC: +1*/
	    /* Token unknown - line %d, char %d */
 	gds_arg_gds, gds_random, 
	gds_arg_cstring, (int) (ptr - last_token), last_token,
 	0);
    }
}


static void yyabandon (
    SSHORT      sql_code,
    STATUS      error_symbol)
{
/**************************************
 *
 *	y y a b a n d o n
 *
 **************************************
 *
 * Functional description
 *	Abandon the parsing outputting the supplied string
 *
 **************************************/

ERRD_post (gds_sqlerr, gds_arg_number, (SLONG) sql_code, 
	gds_arg_gds, error_symbol, 0);
}
