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
 *			clashes with normal DEBUG macro.
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
 *							exception handling in SPs/triggers,
 *							implemented ROWS_AFFECTED system variable
 * 2002.10.21 Nickolay Samofatov: Added support for explicit pessimistic locks
 * 2002.10.29 Nickolay Samofatov: Added support for savepoints
 * 2002.12.03 Dmitry Yemanov: Implemented ORDER BY clause in subqueries.
 * 2002.12.18 Dmitry Yemanov: Added support for SQL-compliant labels and LEAVE statement
 * 2002.12.28 Dmitry Yemanov: Added support for parametrized events.
 * 2003.01.14 Dmitry Yemanov: Fixed bug with cursors in triggers.
 * 2003.01.15 Dmitry Yemanov: Added support for runtime trigger action checks.
 * 2003.02.10 Mike Nordell  : Undefined Microsoft introduced macros to get a clean compile.
 * 2003.05.24 Nickolay Samofatov: Make SKIP and FIRST non-reserved keywords
 * 2003.06.13 Nickolay Samofatov: Make INSERTING/UPDATING/DELETING non-reserved keywords
 * 2003.07.01 Blas Rodriguez Somoza: Change DEBUG and IN to avoid conflicts in win32 build/bison
 * 2003.08.11 Arno Brinkman: Changed GROUP BY to support all expressions and added "AS" support
 *						   with table alias. Also removed group_by_function and ordinal.
 * 2003.08.14 Arno Brinkman: Added support for derived tables.
 * 2003.10.05 Dmitry Yemanov: Added support for explicit cursors in PSQL.
 * 2004.01.16 Vlad Horsun: added support for default parameters and
 *   EXECUTE BLOCK statement
 * Adriano dos Santos Fernandes
 */

#include "firebird.h"
#include "dyn_consts.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "gen/iberror.h"
#include "../dsql/dsql.h"
#include "../jrd/ibase.h"
#include "../jrd/flags.h"
#include "../jrd/jrd.h"
#include "../dsql/errd_proto.h"
#include "../dsql/make_proto.h"
#include "../yvalve/keywords.h"
#include "../yvalve/gds_proto.h"
#include "../jrd/err_proto.h"
#include "../common/intlobj_new.h"
#include "../jrd/Attachment.h"
#include "../common/StatusArg.h"

// since UNIX isn't standard, we have to define
// stuff which is in <limits.h> (which isn't available on all UNIXes...

const long SHRT_POS_MAX			= 32767;
const long SHRT_UNSIGNED_MAX	= 65535;
const long SHRT_NEG_MAX			= 32768;
const int POSITIVE	= 0;
const int NEGATIVE	= 1;
const int UNSIGNED	= 2;

//const int MIN_CACHE_BUFFERS	= 250;
//const int DEF_CACHE_BUFFERS	= 1000;

#define YYSTYPE YYSTYPE
#if defined(DEBUG) || defined(DEV_BUILD)
#define YYDEBUG		1
#endif

#define YYREDUCEPOSNFUNC yyReducePosn
#define YYREDUCEPOSNFUNCARG NULL

inline unsigned trigger_type_suffix(const unsigned slot1, const unsigned slot2, const unsigned slot3)
{
	return ((slot1 << 1) | (slot2 << 3) | (slot3 << 5));
}


#include "../dsql/chars.h"

const int MAX_TOKEN_LEN = 256;

using namespace Jrd;
using namespace Firebird;

static dsql_fld* make_field(FieldNode*);
static Firebird::MetaName toName(dsql_str* str);
static Firebird::PathName toPathName(dsql_str* str);
static Firebird::string toString(dsql_str* str);

static void	yyabandon (SLONG, ISC_STATUS);

inline void check_bound(const char* const to, const char* const string)
{
	if ((to - string) >= MAX_TOKEN_LEN)
		yyabandon (-104, isc_token_too_long);
}

inline void check_copy_incr(char*& to, const char ch, const char* const string)
{
	check_bound(to, string);
	*to++ = ch;
}

%}


// token declarations

// Tokens are organized chronologically by date added.
// See yvalve/keywords.cpp for a list organized alphabetically

// Tokens in v4.0 -- not separated into v3 and v4 tokens

%token <legacyStr> ACTIVE
%token <legacyStr> ADD
%token <legacyStr> AFTER
%token <legacyStr> ALL
%token <legacyStr> ALTER
%token <legacyStr> AND
%token <legacyStr> ANY
%token <legacyStr> AS
%token <legacyStr> ASC
%token <legacyStr> AT
%token <legacyStr> AVG
%token <legacyStr> AUTO
%token <legacyStr> BEFORE
%token <legacyStr> BEGIN
%token <legacyStr> BETWEEN
%token <legacyStr> BLOB
%token <legacyStr> BY
%token <legacyStr> CAST
%token <legacyStr> CHARACTER
%token <legacyStr> CHECK
%token <legacyStr> COLLATE
%token <legacyStr> COMMA
%token <legacyStr> COMMIT
%token <legacyStr> COMMITTED
%token <legacyStr> COMPUTED
%token <legacyStr> CONCATENATE
%token <legacyStr> CONDITIONAL
%token <legacyStr> CONSTRAINT
%token <legacyStr> CONTAINING
%token <legacyStr> COUNT
%token <legacyStr> CREATE
%token <legacyStr> CSTRING
%token <legacyStr> CURRENT
%token <legacyStr> CURSOR
%token <legacyStr> DATABASE
%token <legacyStr> DATE
%token <legacyStr> DB_KEY
%token <legacyStr> DECIMAL
%token <legacyStr> DECLARE
%token <legacyStr> DEFAULT
%token <legacyStr> KW_DELETE
%token <legacyStr> DESC
%token <legacyStr> DISTINCT
%token <legacyStr> DO
%token <legacyStr> DOMAIN
%token <legacyStr> DROP
%token <legacyStr> ELSE
%token <legacyStr> END
%token <legacyStr> ENTRY_POINT
%token <legacyStr> EQL
%token <legacyStr> ESCAPE
%token <legacyStr> EXCEPTION
%token <legacyStr> EXECUTE
%token <legacyStr> EXISTS
%token <legacyStr> EXIT
%token <legacyStr> EXTERNAL
%token <legacyStr> FILTER
%token <legacyStr> FOR
%token <legacyStr> FOREIGN
%token <legacyStr> FROM
%token <legacyStr> FULL
%token <legacyStr> FUNCTION
%token <legacyStr> GDSCODE
%token <legacyStr> GEQ
%token <legacyStr> GENERATOR
%token <legacyStr> GEN_ID
%token <legacyStr> GRANT
%token <legacyStr> GROUP
%token <legacyStr> GTR
%token <legacyStr> HAVING
%token <legacyStr> IF
%token <legacyStr> KW_IN
%token <legacyStr> INACTIVE
%token <legacyStr> INNER
%token <legacyStr> INPUT_TYPE
%token <legacyStr> INDEX
%token <legacyStr> INSERT
%token <legacyStr> INTEGER
%token <legacyStr> INTO
%token <legacyStr> IS
%token <legacyStr> ISOLATION
%token <legacyStr> JOIN
%token <legacyStr> KEY
%token <legacyStr> KW_CHAR
%token <legacyStr> KW_DEC
%token <legacyStr> KW_DOUBLE
%token <legacyStr> KW_FILE
%token <legacyStr> KW_FLOAT
%token <legacyStr> KW_INT
%token <legacyStr> KW_LONG
%token <legacyStr> KW_NULL
%token <legacyStr> KW_NUMERIC
%token <legacyStr> KW_UPPER
%token <legacyStr> KW_VALUE
%token <legacyStr> LENGTH
%token <legacyStr> LPAREN
%token <legacyStr> LEFT
%token <legacyStr> LEQ
%token <legacyStr> LEVEL
%token <legacyStr> LIKE
%token <legacyStr> LSS
%token <legacyStr> MANUAL
%token <legacyStr> MAXIMUM
%token <legacyStr> MERGE
%token <legacyStr> MINIMUM
%token <legacyStr> MODULE_NAME
%token <legacyStr> NAMES
%token <legacyStr> NATIONAL
%token <legacyStr> NATURAL
%token <legacyStr> NCHAR
%token <legacyStr> NEQ
%token <legacyStr> NO
%token <legacyStr> NOT
%token <legacyStr> NOT_GTR
%token <legacyStr> NOT_LSS
%token <legacyStr> OF
%token <legacyStr> ON
%token <legacyStr> ONLY
%token <legacyStr> OPTION
%token <legacyStr> OR
%token <legacyStr> ORDER
%token <legacyStr> OUTER
%token <legacyStr> OUTPUT_TYPE
%token <legacyStr> OVERFLOW
%token <legacyStr> PAGE
%token <legacyStr> PAGES
%token <legacyStr> KW_PAGE_SIZE
%token <legacyStr> PARAMETER
%token <legacyStr> PASSWORD
%token <legacyStr> PLAN
%token <legacyStr> POSITION
%token <legacyStr> POST_EVENT
%token <legacyStr> PRECISION
%token <legacyStr> PRIMARY
%token <legacyStr> PRIVILEGES
%token <legacyStr> PROCEDURE
%token <legacyStr> PROTECTED
%token <legacyStr> READ
%token <legacyStr> REAL
%token <legacyStr> REFERENCES
%token <legacyStr> RESERVING
%token <legacyStr> RETAIN
%token <legacyStr> RETURNING_VALUES
%token <legacyStr> RETURNS
%token <legacyStr> REVOKE
%token <legacyStr> RIGHT
%token <legacyStr> RPAREN
%token <legacyStr> ROLLBACK
%token <legacyStr> SEGMENT
%token <legacyStr> SELECT
%token <legacyStr> SET
%token <legacyStr> SHADOW
%token <legacyStr> KW_SHARED
%token <legacyStr> SINGULAR
%token <legacyStr> KW_SIZE
%token <legacyStr> SMALLINT
%token <legacyStr> SNAPSHOT
%token <legacyStr> SOME
%token <legacyStr> SORT
%token <legacyStr> SQLCODE
%token <legacyStr> STABILITY
%token <legacyStr> STARTING
%token <legacyStr> STATISTICS
%token <legacyStr> SUB_TYPE
%token <legacyStr> SUSPEND
%token <legacyStr> SUM
%token <legacyStr> TABLE
%token <legacyStr> THEN
%token <legacyStr> TO
%token <legacyStr> TRANSACTION
%token <legacyStr> TRIGGER
%token <legacyStr> UNCOMMITTED
%token <legacyStr> UNION
%token <legacyStr> UNIQUE
%token <legacyStr> UPDATE
%token <legacyStr> USER
%token <legacyStr> VALUES
%token <legacyStr> VARCHAR
%token <legacyStr> VARIABLE
%token <legacyStr> VARYING
%token <legacyStr> VERSION
%token <legacyStr> VIEW
%token <legacyStr> WAIT
%token <legacyStr> WHEN
%token <legacyStr> WHERE
%token <legacyStr> WHILE
%token <legacyStr> WITH
%token <legacyStr> WORK
%token <legacyStr> WRITE

%token <legacyStr> FLOAT_NUMBER NUMERIC SYMBOL
%token <int32Val> NUMBER

%token <legacyStr>	STRING
%token <textPtr>	INTRODUCER

// New tokens added v5.0

%token <legacyStr> ACTION
%token <legacyStr> ADMIN
%token <legacyStr> CASCADE
%token <legacyStr> FREE_IT			// ISC SQL extension
%token <legacyStr> RESTRICT
%token <legacyStr> ROLE

// New tokens added v6.0

%token <legacyStr> COLUMN
%token <legacyStr> KW_TYPE
%token <legacyStr> EXTRACT
%token <legacyStr> YEAR
%token <legacyStr> MONTH
%token <legacyStr> DAY
%token <legacyStr> HOUR
%token <legacyStr> MINUTE
%token <legacyStr> SECOND
%token <legacyStr> WEEKDAY			// ISC SQL extension
%token <legacyStr> YEARDAY			// ISC SQL extension
%token <legacyStr> TIME
%token <legacyStr> TIMESTAMP
%token <legacyStr> CURRENT_DATE
%token <legacyStr> CURRENT_TIME
%token <legacyStr> CURRENT_TIMESTAMP

// special aggregate token types returned by lex in v6.0

%token <legacyStr> NUMBER64BIT SCALEDINT

// CVC: Special Firebird additions.

%token <legacyStr> CURRENT_USER
%token <legacyStr> CURRENT_ROLE
%token <legacyStr> KW_BREAK
%token <legacyStr> SUBSTRING
%token <legacyStr> RECREATE
%token <legacyStr> KW_DESCRIPTOR
%token <legacyStr> FIRST
%token <legacyStr> SKIP

// tokens added for Firebird 1.5

%token <legacyStr> CURRENT_CONNECTION
%token <legacyStr> CURRENT_TRANSACTION
%token <legacyStr> BIGINT
%token <legacyStr> CASE
%token <legacyStr> NULLIF
%token <legacyStr> COALESCE
%token <legacyStr> USING
%token <legacyStr> NULLS
%token <legacyStr> LAST
%token <legacyStr> ROW_COUNT
%token <legacyStr> LOCK
%token <legacyStr> SAVEPOINT
%token <legacyStr> RELEASE
%token <legacyStr> STATEMENT
%token <legacyStr> LEAVE
%token <legacyStr> INSERTING
%token <legacyStr> UPDATING
%token <legacyStr> DELETING

// tokens added for Firebird 2.0

%token <legacyStr> BACKUP
%token <legacyStr> KW_DIFFERENCE
%token <legacyStr> OPEN
%token <legacyStr> CLOSE
%token <legacyStr> FETCH
%token <legacyStr> ROWS
%token <legacyStr> BLOCK
%token <legacyStr> IIF
%token <legacyStr> SCALAR_ARRAY
%token <legacyStr> CROSS
%token <legacyStr> NEXT
%token <legacyStr> SEQUENCE
%token <legacyStr> RESTART
%token <legacyStr> BOTH
%token <legacyStr> COLLATION
%token <legacyStr> COMMENT
%token <legacyStr> BIT_LENGTH
%token <legacyStr> CHAR_LENGTH
%token <legacyStr> CHARACTER_LENGTH
%token <legacyStr> LEADING
%token <legacyStr> KW_LOWER
%token <legacyStr> OCTET_LENGTH
%token <legacyStr> TRAILING
%token <legacyStr> TRIM
%token <legacyStr> RETURNING
%token <legacyStr> KW_IGNORE
%token <legacyStr> LIMBO
%token <legacyStr> UNDO
%token <legacyStr> REQUESTS
%token <legacyStr> TIMEOUT

// tokens added for Firebird 2.1

%token <legacyStr> ABS
%token <legacyStr> ACCENT
%token <legacyStr> ACOS
%token <legacyStr> ALWAYS
%token <legacyStr> ASCII_CHAR
%token <legacyStr> ASCII_VAL
%token <legacyStr> ASIN
%token <legacyStr> ATAN
%token <legacyStr> ATAN2
%token <legacyStr> BIN_AND
%token <legacyStr> BIN_OR
%token <legacyStr> BIN_SHL
%token <legacyStr> BIN_SHR
%token <legacyStr> BIN_XOR
%token <legacyStr> CEIL
%token <legacyStr> CONNECT
%token <legacyStr> COS
%token <legacyStr> COSH
%token <legacyStr> COT
%token <legacyStr> DATEADD
%token <legacyStr> DATEDIFF
%token <legacyStr> DECODE
%token <legacyStr> DISCONNECT
%token <legacyStr> EXP
%token <legacyStr> FLOOR
%token <legacyStr> GEN_UUID
%token <legacyStr> GENERATED
%token <legacyStr> GLOBAL
%token <legacyStr> HASH
%token <legacyStr> INSENSITIVE
%token <legacyStr> LIST
%token <legacyStr> LN
%token <legacyStr> LOG
%token <legacyStr> LOG10
%token <legacyStr> LPAD
%token <legacyStr> MATCHED
%token <legacyStr> MATCHING
%token <legacyStr> MAXVALUE
%token <legacyStr> MILLISECOND
%token <legacyStr> MINVALUE
%token <legacyStr> MOD
%token <legacyStr> OVERLAY
%token <legacyStr> PAD
%token <legacyStr> PI
%token <legacyStr> PLACING
%token <legacyStr> POWER
%token <legacyStr> PRESERVE
%token <legacyStr> RAND
%token <legacyStr> RECURSIVE
%token <legacyStr> REPLACE
%token <legacyStr> REVERSE
%token <legacyStr> ROUND
%token <legacyStr> RPAD
%token <legacyStr> SENSITIVE
%token <legacyStr> SIGN
%token <legacyStr> SIN
%token <legacyStr> SINH
%token <legacyStr> SPACE
%token <legacyStr> SQRT
%token <legacyStr> START
%token <legacyStr> TAN
%token <legacyStr> TANH
%token <legacyStr> TEMPORARY
%token <legacyStr> TRUNC
%token <legacyStr> WEEK

// tokens added for Firebird 2.5

%token <legacyStr> AUTONOMOUS
%token <legacyStr> CHAR_TO_UUID
%token <legacyStr> CHAR_TO_UUID2
%token <legacyStr> FIRSTNAME
%token <legacyStr> GRANTED
%token <legacyStr> LASTNAME
%token <legacyStr> MIDDLENAME
%token <legacyStr> MAPPING
%token <legacyStr> OS_NAME
%token <legacyStr> SIMILAR
%token <legacyStr> UUID_TO_CHAR
%token <legacyStr> UUID_TO_CHAR2
// new execute statement
%token <legacyStr> CALLER
%token <legacyStr> COMMON
%token <legacyStr> DATA
%token <legacyStr> SOURCE
%token <legacyStr> TWO_PHASE
%token <legacyStr> BIND_PARAM
%token <legacyStr> BIN_NOT

// tokens added for Firebird 3.0

%token <legacyStr> BODY
%token <legacyStr> CONTINUE
%token <legacyStr> DDL
%token <legacyStr> ENGINE
%token <legacyStr> NAME
%token <legacyStr> OVER
%token <legacyStr> PACKAGE
%token <legacyStr> PARTITION
%token <legacyStr> RDB_GET_CONTEXT
%token <legacyStr> RDB_SET_CONTEXT
%token <legacyStr> SCROLL
%token <legacyStr> PRIOR
%token <legacyStr> KW_ABSOLUTE
%token <legacyStr> KW_RELATIVE
%token <legacyStr> ACOSH
%token <legacyStr> ASINH
%token <legacyStr> ATANH
%token <legacyStr> RETURN
%token <legacyStr> DETERMINISTIC
%token <legacyStr> IDENTITY
%token <legacyStr> DENSE_RANK
%token <legacyStr> FIRST_VALUE
%token <legacyStr> NTH_VALUE
%token <legacyStr> LAST_VALUE
%token <legacyStr> LAG
%token <legacyStr> LEAD
%token <legacyStr> RANK
%token <legacyStr> ROW_NUMBER
%token <legacyStr> SQLSTATE

%token <legacyStr> KW_BOOLEAN
%token <legacyStr> KW_FALSE
%token <legacyStr> KW_TRUE
%token <legacyStr> UNKNOWN

// precedence declarations for expression evaluation

%left	OR
%left	AND
%left	NOT
%left	'=' '<' '>' LIKE CONTAINING STARTING SIMILAR KW_IN EQL NEQ GTR LSS GEQ LEQ NOT_GTR NOT_LSS
%left	'+' '-'
%left	'*' '/'
%left	UMINUS UPLUS
%left	CONCATENATE
%left	COLLATE

// Fix the dangling IF-THEN-ELSE problem
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

%union
{
	BaseNullable<int> nullableIntVal;
	BaseNullable<bool> nullableBoolVal;
	bool boolVal;
	int intVal;
	unsigned uintVal;
	SLONG int32Val;
	FB_UINT64 uint64Val;
	BaseNullable<FB_UINT64> nullableUint64Val;
	UCHAR blrOp;
	Jrd::OrderNode::NullsPlacement nullsPlacement;
	Jrd::ComparativeBoolNode::DsqlFlag cmpBoolFlag;
	Jrd::dsql_str* legacyStr;
	Jrd::dsql_fld* legacyField;
	Jrd::ReturningClause* returningClause;
	Firebird::MetaName* metaNamePtr;
	Firebird::ObjectsArray<Firebird::MetaName>* metaNameArray;
	Firebird::PathName* pathNamePtr;
	Firebird::string* stringPtr;
	Jrd::IntlString* intlStringPtr;
	TEXT* textPtr;
	Jrd::DbFileClause* dbFileClause;
	Firebird::Array<NestConst<Jrd::DbFileClause> >* dbFilesClause;
	Jrd::ExternalClause* externalClause;
	Firebird::Array<Jrd::ParameterClause>* parametersClause;
	Jrd::Node* node;
	Jrd::ExprNode* exprNode;
	Jrd::ValueExprNode* valueExprNode;
	Jrd::BoolExprNode* boolExprNode;
	Jrd::RecordSourceNode* recSourceNode;
	Jrd::RelationSourceNode* relSourceNode;
	Jrd::ValueListNode* valueListNode;
	Jrd::RecSourceListNode* recSourceListNode;
	Jrd::RseNode* rseNode;
	Jrd::PlanNode* planNode;
	Jrd::PlanNode::AccessType* accessType;
	Jrd::StmtNode* stmtNode;
	Jrd::DdlNode* ddlNode;
	Jrd::SelectExprNode* selectExprNode;
	Jrd::WithClause* withClause;
	Jrd::RowsClause* rowsClause;
	Jrd::FieldNode* fieldNode;
	Jrd::DecodeNode* decodeNode;
	Firebird::Array<Jrd::FieldNode*>* fieldArray;
	Firebird::Array<NestConst<Jrd::FieldNode> >* nestFieldArray;
	Jrd::TransactionNode* traNode;
	Firebird::Array<Jrd::PrivilegeClause>* privilegeArray;
	Jrd::GranteeClause* granteeClause;
	Firebird::Array<Jrd::GranteeClause>* granteeArray;
	Jrd::GrantRevokeNode* grantRevokeNode;
	Jrd::CreateCollationNode* createCollationNode;
	Jrd::CreateDomainNode* createDomainNode;
	Jrd::AlterDomainNode* alterDomainNode;
	Jrd::CreateAlterFunctionNode* createAlterFunctionNode;
	Jrd::CreateAlterProcedureNode* createAlterProcedureNode;
	Jrd::CreateAlterTriggerNode* createAlterTriggerNode;
	Jrd::CreateAlterPackageNode* createAlterPackageNode;
	Jrd::CreateFilterNode::NameNumber* filterNameNumber;
	Jrd::CreateSequenceNode* createSequenceNode;
	Jrd::CreateShadowNode* createShadowNode;
	Firebird::Array<Jrd::CreateAlterPackageNode::Item>* packageItems;
	Jrd::ExceptionArray* exceptionArray;
	Jrd::CreateAlterPackageNode::Item packageItem;
	Jrd::CreatePackageBodyNode* createPackageBodyNode;
	Jrd::BoolSourceClause* boolSourceClause;
	Jrd::ValueSourceClause* valueSourceClause;
	Jrd::RelationNode* relationNode;
	Jrd::RelationNode::AddColumnClause* addColumnClause;
	Jrd::RelationNode::RefActionClause* refActionClause;
	Jrd::RelationNode::IndexConstraintClause* indexConstraintClause;
	Jrd::CreateRelationNode* createRelationNode;
	Jrd::CreateAlterViewNode* createAlterViewNode;
	Jrd::CreateIndexNode* createIndexNode;
	Jrd::AlterDatabaseNode* alterDatabaseNode;
	Jrd::ExecBlockNode* execBlockNode;
	Jrd::StoreNode* storeNode;
	Jrd::UpdateOrInsertNode* updInsNode;
	Jrd::AggNode* aggNode;
	Jrd::SysFuncCallNode* sysFuncCallNode;
	Jrd::ValueIfNode* valueIfNode;
	Jrd::CompoundStmtNode* compoundStmtNode;
	Jrd::CursorStmtNode* cursorStmtNode;
	Jrd::DeclareCursorNode* declCursorNode;
	Jrd::ErrorHandlerNode* errorHandlerNode;
	Jrd::ExecStatementNode* execStatementNode;
	Jrd::MergeNode* mergeNode;
	Jrd::MergeNode::NotMatched* mergeNotMatchedClause;
	Jrd::MergeNode::Matched* mergeMatchedClause;
	Jrd::SelectNode* selectNode;
	Jrd::SetTransactionNode* setTransactionNode;
	Jrd::SetTransactionNode::RestrictionOption* setTransactionRestrictionClause;
	Jrd::DeclareSubProcNode* declareSubProcNode;
	Jrd::DeclareSubFuncNode* declareSubFuncNode;
	Jrd::dsql_req* dsqlReq;
}

/*** TYPES ***/

%%

// list of possible statements

top
	: statement			{ DSQL_parse = $1; }
	| statement ';'		{ DSQL_parse = $1; }
	;

%type <dsqlReq> statement
statement
	: dml_statement		{ $$ = newNode<DsqlDmlRequest>($1); }
	| ddl_statement		{ $$ = newNode<DsqlDdlRequest>($1); }
	| tra_statement		{ $$ = newNode<DsqlTransactionRequest>($1); }
	;

%type <stmtNode> dml_statement
dml_statement
	// ASF: ALTER SEQUENCE is defined here cause it's treated as DML.
	: ALTER SEQUENCE alter_sequence_clause		{ $$ = $3; }
	| delete									{ $$ = $1; }
	| insert									{ $$ = $1; }
	| merge										{ $$ = $1; }
	| exec_procedure							{ $$ = $1; }
	| exec_block								{ $$ = $1; }
	| savepoint									{ $$ = $1; }
	| select									{ $$ = $1; }
	| set_generator								{ $$ = $1; }
	| update									{ $$ = $1; }
	| update_or_insert							{ $$ = $1; }
	;

%type <ddlNode> ddl_statement
ddl_statement
	: alter										{ $$ = $1; }
	| comment									{ $$ = $1; }
	| create									{ $$ = $1; }
	| create_or_alter							{ $$ = $1; }
	| declare									{ $$ = $1; }
	| drop										{ $$ = $1; }
	| grant										{ $$ = $1; }
	| recreate									{ $$ = $1; }
	| revoke									{ $$ = $1; }
	| set_statistics							{ $$ = $1; }
	;

%type <traNode> tra_statement
tra_statement
	: set_transaction							{ $$ = $1; }
	| commit									{ $$ = $1; }
	| rollback									{ $$ = $1; }
	;


// GRANT statement

%type <grantRevokeNode> grant
grant
	: GRANT
			{ $$ = newNode<GrantRevokeNode>(true); }
		grant0($2)
			{ $$ = $2; }
	;

%type grant0(<grantRevokeNode>)
grant0($node)
	: privileges(&$node->privileges) ON table_noise symbol_table_name
			TO non_role_grantee_list(&$node->users) grant_option granted_by
		{
			$node->table = newNode<GranteeClause>(obj_relation, toName($4));
			$node->grantAdminOption = $7;
			$node->grantor = $8;
		}
	| execute_privilege(&$node->privileges) ON PROCEDURE simple_proc_name
			TO non_role_grantee_list(&$node->users) grant_option granted_by
		{
			$node->table = $4;
			$node->grantAdminOption = $7;
			$node->grantor = $8;
		}
	| execute_privilege(&$node->privileges) ON FUNCTION simple_UDF_name
			TO non_role_grantee_list(&$node->users) grant_option granted_by
		{
			$node->table = $4;
			$node->grantAdminOption = $7;
			$node->grantor = $8;
		}
	| execute_privilege(&$node->privileges) ON PACKAGE simple_package_name
			TO non_role_grantee_list(&$node->users) grant_option granted_by
		{
			$node->table = $4;
			$node->grantAdminOption = $7;
			$node->grantor = $8;
		}
	| role_name_list(&$node->roles) TO role_grantee_list(&$node->users) role_admin_option granted_by
		{
			$node->grantAdminOption = $4;
			$node->grantor = $5;
		}
	;

table_noise
	: // nothing
	| TABLE
	;

%type privileges(<privilegeArray>)
privileges($privilegeArray)
	: ALL				{ $privilegeArray->add(PrivilegeClause('A', NULL)); }
	| ALL PRIVILEGES	{ $privilegeArray->add(PrivilegeClause('A', NULL)); }
	| privilege_list($privilegeArray)
	;

%type privilege_list(<privilegeArray>)
privilege_list($privilegeArray)
	: privilege($privilegeArray)
	| privilege_list ',' privilege($privilegeArray)
	;

%type execute_privilege(<privilegeArray>)
execute_privilege($privilegeArray)
	: EXECUTE						{ $privilegeArray->add(PrivilegeClause('X', NULL)); }
	;

%type privilege(<privilegeArray>)
privilege($privilegeArray)
	: SELECT						{ $privilegeArray->add(PrivilegeClause('S', NULL)); }
	| INSERT						{ $privilegeArray->add(PrivilegeClause('I', NULL)); }
	| KW_DELETE						{ $privilegeArray->add(PrivilegeClause('D', NULL)); }
	| UPDATE column_parens_opt		{ $privilegeArray->add(PrivilegeClause('U', $2)); }
	| REFERENCES column_parens_opt	{ $privilegeArray->add(PrivilegeClause('R', $2)); }
	;

%type <boolVal> grant_option
grant_option
	: /* nothing */			{ $$ = false; }
	| WITH GRANT OPTION		{ $$ = true; }
	;

%type <boolVal> role_admin_option
role_admin_option
	: /* nothing */			{ $$ = false; }
	| WITH ADMIN OPTION		{ $$ = true; }
	;

%type <metaNamePtr> granted_by
granted_by
	: /* nothing */				{ $$ = NULL; }
	| granted_by_text grantor	{ $$ = $2; }
	;

granted_by_text
	: GRANTED BY
	| AS
	;

%type <metaNamePtr> grantor
grantor
	: symbol_user_name			{ $$ = newNode<MetaName>(toName($1)); }
	| USER symbol_user_name		{ $$ = newNode<MetaName>(toName($2)); }
	;

%type <granteeClause> simple_package_name
simple_package_name
	: symbol_package_name	{ $$ = newNode<GranteeClause>(obj_package_header, toName($1)); }
	;

%type <granteeClause> simple_proc_name
simple_proc_name
	: symbol_procedure_name	{ $$ = newNode<GranteeClause>(obj_procedure, toName($1)); }
	;

%type <granteeClause> simple_UDF_name
simple_UDF_name
	: symbol_UDF_name		{ $$ = newNode<GranteeClause>(obj_udf, toName($1)); }
	;


// REVOKE statement

%type <grantRevokeNode> revoke
revoke
	: REVOKE
			{ $$ = newNode<GrantRevokeNode>(false); }
		revoke0($2)
			{ $$ = $2; }
	;

%type revoke0(<grantRevokeNode>)
revoke0($node)
	: rev_grant_option privileges(&$node->privileges) ON table_noise symbol_table_name
			FROM non_role_grantee_list(&$node->users) granted_by
		{
			$node->table = newNode<GranteeClause>(obj_relation, toName($5));
			$node->grantAdminOption = $1;
			$node->grantor = $8;
		}

	| rev_grant_option execute_privilege(&$node->privileges) ON PROCEDURE simple_proc_name
			FROM non_role_grantee_list(&$node->users) granted_by
		{
			$node->table = $5;
			$node->grantAdminOption = $1;
			$node->grantor = $8;
		}
	| rev_grant_option execute_privilege(&$node->privileges) ON FUNCTION simple_UDF_name
			FROM non_role_grantee_list(&$node->users) granted_by
		{
			$node->table = $5;
			$node->grantAdminOption = $1;
			$node->grantor = $8;
		}
	| rev_grant_option execute_privilege(&$node->privileges) ON PACKAGE simple_package_name
			FROM non_role_grantee_list(&$node->users) granted_by
		{
			$node->table = $5;
			$node->grantAdminOption = $1;
			$node->grantor = $8;
		}
	| rev_admin_option role_name_list(&$node->roles) FROM role_grantee_list(&$node->users) granted_by
		{
			$node->grantAdminOption = $1;
			$node->grantor = $5;
		}
	| ALL ON ALL FROM non_role_grantee_list(&$node->users)
	;

%type <boolVal> rev_grant_option
rev_grant_option
	: /* nothing */			{ $$ = false; }
	| GRANT OPTION FOR		{ $$ = true; }
	;

%type <boolVal> rev_admin_option
rev_admin_option
	: /* nothing */			{ $$ = false; }
	| ADMIN OPTION FOR		{ $$ = true; }
	;

%type non_role_grantee_list(<granteeArray>)
non_role_grantee_list($granteeArray)
	: grantee($granteeArray)
	| user_grantee($granteeArray)
	| non_role_grantee_list ',' grantee($granteeArray)
	| non_role_grantee_list ',' user_grantee($granteeArray)
	;

%type grantee(<granteeArray>)
grantee($granteeArray)
	: PROCEDURE symbol_procedure_name
		{ $granteeArray->add(GranteeClause(obj_procedure, toName($2))); }
	| FUNCTION symbol_UDF_name
		{ $granteeArray->add(GranteeClause(obj_udf, toName($2))); }
	| PACKAGE symbol_package_name
		{ $granteeArray->add(GranteeClause(obj_package_header, toName($2))); }
	| TRIGGER symbol_trigger_name
		{ $granteeArray->add(GranteeClause(obj_trigger, toName($2))); }
	| VIEW symbol_view_name
		{ $granteeArray->add(GranteeClause(obj_view, toName($2))); }
	| ROLE symbol_role_name
		{ $granteeArray->add(GranteeClause(obj_sql_role, toName($2))); }
	;

// CVC: In the future we can deprecate the first implicit form since we'll support
// explicit grant/revoke for both USER and ROLE keywords & object types.

%type user_grantee(<granteeArray>)
user_grantee($granteeArray)
	: symbol_user_name
		{ $granteeArray->add(GranteeClause(obj_user_or_role, toName($1))); }
	| USER symbol_user_name
		{ $granteeArray->add(GranteeClause(obj_user, toName($2))); }
	| GROUP symbol_user_name
		{ $granteeArray->add(GranteeClause(obj_user_group, toName($2))); }
	;

%type role_name_list(<granteeArray>)
role_name_list($granteeArray)
	: role_name($granteeArray)
	| role_name_list ',' role_name($granteeArray)
	;

%type role_name(<granteeArray>)
role_name($granteeArray)
	: symbol_role_name
		{ $granteeArray->add(GranteeClause(obj_sql_role, toName($1))); }
	;

%type role_grantee_list(<granteeArray>)
role_grantee_list($granteeArray)
	: role_grantee($granteeArray)
	| role_grantee_list ',' role_grantee($granteeArray)
	;

%type role_grantee(<granteeArray>)
role_grantee($granteeArray)
	: grantor	{ $granteeArray->add(GranteeClause(obj_user, *$1)); }
	;


// DECLARE operations

%type <ddlNode> declare
declare
	: DECLARE declare_clause	{ $$ = $2;}
	;

%type <ddlNode> declare_clause
declare_clause
	: FILTER filter_decl_clause				{ $$ = $2; }
	| EXTERNAL FUNCTION udf_decl_clause		{ $$ = $3; }
	;

%type <createAlterFunctionNode> udf_decl_clause
udf_decl_clause
	: symbol_UDF_name
			{ $$ = newNode<CreateAlterFunctionNode>(toName($1)); }
		arg_desc_list1(&$2->parameters)
		RETURNS return_value1($2)
		ENTRY_POINT sql_string MODULE_NAME sql_string
			{
				$$ = $2;
				$$->external = newNode<ExternalClause>();
				$$->external->name = toString($7);
				$$->external->udfModule = toString($9);
			}
	;

udf_data_type
	: simple_type
	| BLOB
		{ lex.g_field->fld_dtype = dtype_blob; }
	| CSTRING '(' pos_short_integer ')' charset_clause
		{
			lex.g_field->fld_dtype = dtype_cstring;
			lex.g_field->fld_character_length = (USHORT) $3;
		}
	;

%type arg_desc_list1(<parametersClause>)
arg_desc_list1($parameters)
	:
	| arg_desc_list($parameters)
	| '(' arg_desc_list($parameters) ')'
	;

%type arg_desc_list(<parametersClause>)
arg_desc_list($parameters)
	: arg_desc($parameters)
	| arg_desc_list ',' arg_desc($parameters)
	;

%type arg_desc(<parametersClause>)
arg_desc($parameters)
	: init_data_type udf_data_type param_mechanism
		{
			$parameters->add(ParameterClause(getPool(), $1, NULL, NULL, NULL));
			$parameters->back().udfMechanism = $3;
		}
	;

%type <nullableIntVal> param_mechanism
param_mechanism
	: /* nothing */		{ $$ = Nullable<int>::empty(); }	// Beware: This means FUN_reference or FUN_blob_struct.
	| BY KW_DESCRIPTOR	{ $$ = Nullable<int>::val(FUN_descriptor); }
	| BY SCALAR_ARRAY	{ $$ = Nullable<int>::val(FUN_scalar_array); }
	| KW_NULL			{ $$ = Nullable<int>::val(FUN_ref_with_null); }
	;

%type return_value1(<createAlterFunctionNode>)
return_value1($function)
	: return_value($function)
	| '(' return_value($function) ')'
	;

%type return_value(<createAlterFunctionNode>)
return_value($function)
	: init_data_type udf_data_type return_mechanism
		{
			$function->returnType = ParameterClause(getPool(), $1, NULL, NULL, NULL);
			$function->returnType.udfMechanism = $3;
		}
	| PARAMETER pos_short_integer
		{ $function->udfReturnPos = $2; }
	;

%type <int32Val> return_mechanism
return_mechanism
	: /* nothing */				{ $$ = FUN_reference; }
	| BY KW_VALUE				{ $$ = FUN_value; }
	| BY KW_DESCRIPTOR			{ $$ = FUN_descriptor; }
	// FUN_refrence with FREE_IT is -ve
	| FREE_IT					{ $$ = -1 * FUN_reference; }
	| BY KW_DESCRIPTOR FREE_IT	{ $$ = -1 * FUN_descriptor; }
	;


%type <ddlNode> filter_decl_clause
filter_decl_clause
	: symbol_filter_name
		INPUT_TYPE blob_filter_subtype
		OUTPUT_TYPE blob_filter_subtype
		ENTRY_POINT sql_string MODULE_NAME sql_string
			{
				CreateFilterNode* node = newNode<CreateFilterNode>(toName($1));
				node->inputFilter = $3;
				node->outputFilter = $5;
				node->entryPoint = toString($7);
				node->moduleName = toString($9);
				$$ = node;
			}
	;

%type <filterNameNumber> blob_filter_subtype
blob_filter_subtype
	: symbol_blob_subtype_name
		{ $$ = newNode<CreateFilterNode::NameNumber>(toName($1)); }
	| signed_short_integer
		{ $$ = newNode<CreateFilterNode::NameNumber>($1); }
	;

// CREATE metadata operations

%type <ddlNode> create
create
	: CREATE create_clause		{ $$ = $2; }
	;

%type <ddlNode> create_clause
create_clause
	: EXCEPTION exception_clause				{ $$ = $2; }
	| unique_opt order_direction INDEX symbol_index_name ON simple_table_name
			{
				CreateIndexNode* node = newNode<CreateIndexNode>(toName($4));
				node->unique = $1;
				node->descending = $2;
				node->relation = $6;
				$$ = node;
			}
		index_definition(static_cast<CreateIndexNode*>($7))
			{
				$$ = $7;
			}
	| FUNCTION function_clause					{ $$ = $2; }
	| PROCEDURE procedure_clause				{ $$ = $2; }
	| TABLE table_clause						{ $$ = $2; }
	| GLOBAL TEMPORARY TABLE gtt_table_clause	{ $$ = $4; }
	| TRIGGER trigger_clause					{ $$ = $2; }
	| VIEW view_clause							{ $$ = $2; }
	| GENERATOR generator_clause				{ $$ = $2; }
	| SEQUENCE generator_clause					{ $$ = $2; }
	| DATABASE db_clause						{ $$ = $2; }
	| DOMAIN domain_clause						{ $$ = $2; }
	| SHADOW shadow_clause						{ $$ = $2; }
	| ROLE role_clause							{ $$ = $2; }
	| COLLATION collation_clause				{ $$ = $2; }
	| USER create_user_clause					{ $$ = $2; }
	| PACKAGE package_clause					{ $$ = $2; }
	| PACKAGE BODY package_body_clause			{ $$ = $3; }
	;


%type <ddlNode> recreate
recreate
	: RECREATE recreate_clause		{ $$ = $2; }
	;

%type <ddlNode> recreate_clause
recreate_clause
	: PROCEDURE procedure_clause
		{ $$ = newNode<RecreateProcedureNode>($2); }
	| FUNCTION function_clause
		{ $$ = newNode<RecreateFunctionNode>($2); }
	| TABLE rtable_clause
		{ $$ = $2; }
	| GLOBAL TEMPORARY TABLE gtt_recreate_clause
		{ $$ = $4; }
	| VIEW rview_clause
		{ $$ = $2; }
	| TRIGGER trigger_clause
		{ $$ = newNode<RecreateTriggerNode>($2); }
	| PACKAGE package_clause
		{ $$ = newNode<RecreatePackageNode>($2); }
	| PACKAGE BODY package_body_clause
		{ $$ = newNode<RecreatePackageBodyNode>($3); }
	| EXCEPTION rexception_clause
		{ $$ = $2; }
	| GENERATOR generator_clause
		{ $$ = newNode<RecreateSequenceNode>($2); }
	| SEQUENCE generator_clause
		{ $$ = newNode<RecreateSequenceNode>($2); }
	;

%type <ddlNode> create_or_alter
create_or_alter
	: CREATE OR ALTER replace_clause		{ $$ = $4; }
	;

%type <ddlNode> replace_clause
replace_clause
	: PROCEDURE replace_procedure_clause	{ $$ = $2; }
	| FUNCTION replace_function_clause		{ $$ = $2; }
	| TRIGGER replace_trigger_clause		{ $$ = $2; }
	| PACKAGE replace_package_clause		{ $$ = $2; }
	| VIEW replace_view_clause				{ $$ = $2; }
	| EXCEPTION replace_exception_clause	{ $$ = $2; }
	;


// CREATE EXCEPTION

%type <ddlNode>	exception_clause
exception_clause
	: symbol_exception_name sql_string
		{ $$ = newNode<CreateAlterExceptionNode>(toName($1), toString($2)); }
	;

%type <ddlNode> rexception_clause
rexception_clause
	: symbol_exception_name sql_string
		{
			CreateAlterExceptionNode* createNode = newNode<CreateAlterExceptionNode>(
				toName($1), toString($2));
			$$ = newNode<RecreateExceptionNode>(createNode);
		}
	;

%type <ddlNode> replace_exception_clause
replace_exception_clause
	: symbol_exception_name sql_string
		{
			CreateAlterExceptionNode* node = newNode<CreateAlterExceptionNode>(
				toName($1), toString($2));
			node->alter = true;
			$$ = node;
		}
	;

%type <ddlNode> alter_exception_clause
alter_exception_clause
	: symbol_exception_name sql_string
		{
			CreateAlterExceptionNode* node = newNode<CreateAlterExceptionNode>(
				toName($1), toString($2));
			node->create = false;
			node->alter = true;
			$$ = node;
		}
	;


// CREATE INDEX

%type <boolVal> unique_opt
unique_opt
	: /* nothing */		{ $$ = false; }
	| UNIQUE			{ $$ = true; }
	;

%type index_definition(<createIndexNode>)
index_definition($createIndexNode)
	: column_list
		{ $createIndexNode->columns = $1; }
	| column_parens
		{ $createIndexNode->columns = $1; }
	| computed_by '(' value ')'
		{
 			$createIndexNode->computed = newNode<ValueSourceClause>();
			$createIndexNode->computed->value = $3;
			$createIndexNode->computed->source = toString(makeParseStr(YYPOSNARG(2), YYPOSNARG(4)));
		}
	;


// CREATE SHADOW
%type <createShadowNode> shadow_clause
shadow_clause
	: pos_short_integer manual_auto conditional sql_string first_file_length
	 		{
	 			$$ = newNode<CreateShadowNode>($1);
		 		$$->manual = $2;
		 		$$->conditional = $3;
		 		$$->files.add(newNode<DbFileClause>(toPathName($4)));
		 		$$->files.front()->length = $5;
	 		}
		sec_shadow_files(&$6->files)
		 	{ $$ = $6; }
	;

%type <boolVal>	manual_auto
manual_auto
	: /* nothing */		{ $$ = false; }
	| MANUAL			{ $$ = true; }
	| AUTO				{ $$ = false; }
	;

%type <boolVal>	conditional
conditional
	: /* nothing */		{ $$ = false; }
	| CONDITIONAL		{ $$ = true; }
	;

%type <int32Val> first_file_length
first_file_length
	: /* nothing */								{ $$ = 0; }
	| LENGTH equals long_integer page_noise		{ $$ = $3; }
	;

%type sec_shadow_files(<dbFilesClause>)
sec_shadow_files($dbFilesClause)
	: // nothing
	| db_file_list($dbFilesClause)
	;

%type db_file_list(<dbFilesClause>)
db_file_list($dbFilesClause)
	: db_file				{ $dbFilesClause->add($1); }
	| db_file_list db_file	{ $dbFilesClause->add($2); }
	;


// CREATE DOMAIN

%type <createDomainNode> domain_clause
domain_clause
	: column_def_name as_opt data_type domain_default_opt
			{
				$<createDomainNode>$ = newNode<CreateDomainNode>(
					ParameterClause(getPool(), (dsql_fld*) $1, "", $4, NULL));
			}
		domain_constraints_opt($5) collate_clause
			{
				$$ = $5;
				$$->nameType.collate = toName($7);
			}
	;

%type domain_constraints_opt(<createDomainNode>)
domain_constraints_opt($createDomainNode)
	: // nothing
	| domain_constraints($createDomainNode)
	;

%type domain_constraints(<createDomainNode>)
domain_constraints($createDomainNode)
	: domain_constraint($createDomainNode)
	| domain_constraints domain_constraint($createDomainNode)
	;

%type domain_constraint(<createDomainNode>)
domain_constraint($createDomainNode)
	: null_constraint
		{ setClause($createDomainNode->notNull, "NOT NULL"); }
	| check_constraint
		{ setClause($createDomainNode->check, "DOMAIN CHECK CONSTRAINT", $1); }
	;

as_opt
	: // nothing
	| AS
	;

%type <valueSourceClause> domain_default
domain_default
	: DEFAULT default_value
		{
			ValueSourceClause* clause = newNode<ValueSourceClause>();
			clause->value = $2;
			clause->source = toString(makeParseStr(YYPOSNARG(1), YYPOSNARG(2)));
			$$ = clause;
		}
	;

%type <valueSourceClause> domain_default_opt
domain_default_opt
	: /* nothing */		{ $$ = NULL; }
	| domain_default
	;

null_constraint
	: NOT KW_NULL
	;

%type <boolSourceClause> check_constraint
check_constraint
	: CHECK '(' search_condition ')'
		{
			BoolSourceClause* clause = newNode<BoolSourceClause>();
			clause->value = $3;
			clause->source = toString(makeParseStr(YYPOSNARG(1), YYPOSNARG(4)));
			$$ = clause;
		}
	;


// CREATE SEQUENCE/GENERATOR

%type <createSequenceNode> generator_clause
generator_clause
	: symbol_generator_name		{ $$ = newNode<CreateSequenceNode>(toName($1)); }
	;


// CREATE ROLE

%type <ddlNode> role_clause
role_clause
	: symbol_role_name		{ $$ = newNode<CreateRoleNode>(toName($1)); }
	;


// CREATE COLLATION

%type <createCollationNode> collation_clause
collation_clause
	: symbol_collation_name FOR symbol_character_set_name
			{ $<createCollationNode>$ = newNode<CreateCollationNode>(toName($1), toName($3)); }
		collation_sequence_definition($4)
		collation_attribute_list_opt($4) collation_specific_attribute_opt($4)
			{ $$ = $4; }
	;

%type collation_sequence_definition(<createCollationNode>)
collation_sequence_definition($createCollation)
	: // nothing
	| FROM symbol_collation_name
		{ $createCollation->fromName = toName($2); }
	| FROM EXTERNAL '(' sql_string ')'
		{ $createCollation->fromExternal = toString($4); }
	;

%type collation_attribute_list_opt(<createCollationNode>)
collation_attribute_list_opt($createCollation)
	: // nothing
	| collation_attribute_list($createCollation)
	;

%type collation_attribute_list(<createCollationNode>)
collation_attribute_list($createCollation)
	: collation_attribute($createCollation)
	| collation_attribute_list collation_attribute($createCollation)
	;

%type collation_attribute(<createCollationNode>)
collation_attribute($createCollation)
	: collation_pad_attribute($createCollation)
	| collation_case_attribute($createCollation)
	| collation_accent_attribute($createCollation)
	;

%type collation_pad_attribute(<createCollationNode>)
collation_pad_attribute($createCollation)
	: NO PAD		{ $createCollation->unsetAttribute(TEXTTYPE_ATTR_PAD_SPACE); }
	| PAD SPACE		{ $createCollation->setAttribute(TEXTTYPE_ATTR_PAD_SPACE); }
	;

%type collation_case_attribute(<createCollationNode>)
collation_case_attribute($createCollation)
	: CASE SENSITIVE		{ $createCollation->unsetAttribute(TEXTTYPE_ATTR_CASE_INSENSITIVE); }
	| CASE INSENSITIVE		{ $createCollation->setAttribute(TEXTTYPE_ATTR_CASE_INSENSITIVE); }
	;

%type collation_accent_attribute(<createCollationNode>)
collation_accent_attribute($createCollation)
	: ACCENT SENSITIVE		{ $createCollation->unsetAttribute(TEXTTYPE_ATTR_ACCENT_INSENSITIVE); }
	| ACCENT INSENSITIVE	{ $createCollation->setAttribute(TEXTTYPE_ATTR_ACCENT_INSENSITIVE); }
	;

%type collation_specific_attribute_opt(<createCollationNode>)
collation_specific_attribute_opt($createCollation)
	: // nothing
	| sql_string
		{
			string s(toString($1));
			$createCollation->specificAttributes.clear();
			$createCollation->specificAttributes.add((const UCHAR*) s.begin(), s.length());
		}
	;

// ALTER CHARACTER SET

%type <ddlNode> alter_charset_clause
alter_charset_clause
	: symbol_character_set_name SET DEFAULT COLLATION symbol_collation_name
		{ $$ = newNode<AlterCharSetNode>(toName($1), toName($5)); }
	;

// CREATE DATABASE
// ASF: CREATE DATABASE command is divided in three pieces: name, initial options and
// remote options.
// Initial options are basic properties of a database and should be handled here and
// in preparse.cpp.
// Remote options always come after initial options, so they don't need to be parsed
// in preparse.cpp. They are interpreted only in the server, using this grammar.
// Although LENGTH is defined as an initial option, it's also used in the server.

%type <alterDatabaseNode> db_clause
db_clause
	: db_name
			{
				$$ = newNode<AlterDatabaseNode>();
				$$->create = true;
			}
		db_initial_desc1($2) db_rem_desc1($2)
			{ $$ = $2; }
	;

equals
	: // nothing
	| '='
	;

%type <legacyStr> db_name
db_name
	: sql_string
	;

%type db_initial_desc1(<alterDatabaseNode>)
db_initial_desc1($alterDatabaseNode)
	: // nothing
	| db_initial_desc($alterDatabaseNode)
	;

%type db_initial_desc(<alterDatabaseNode>)
db_initial_desc($alterDatabaseNode)
	: db_initial_option($alterDatabaseNode)
	| db_initial_desc db_initial_option($alterDatabaseNode)
	;

// With the exception of LENGTH, all clauses here are handled only at the client.
%type db_initial_option(<alterDatabaseNode>)
db_initial_option($alterDatabaseNode)
	: KW_PAGE_SIZE equals pos_short_integer
	| USER sql_string
	| PASSWORD sql_string
	| SET NAMES sql_string
	| LENGTH equals long_integer page_noise
		{ $alterDatabaseNode->createLength = $3; }
	;

%type db_rem_desc1(<alterDatabaseNode>)
db_rem_desc1($alterDatabaseNode)
	: // nothing
	| db_rem_desc($alterDatabaseNode)
	;

%type db_rem_desc(<alterDatabaseNode>)
db_rem_desc($alterDatabaseNode)
	: db_rem_option($alterDatabaseNode)
	| db_rem_desc db_rem_option($alterDatabaseNode)
	;

%type db_rem_option(<alterDatabaseNode>)
db_rem_option($alterDatabaseNode)
	: db_file
		{ $alterDatabaseNode->files.add($1); }
	| DEFAULT CHARACTER SET symbol_character_set_name
		{ $alterDatabaseNode->setDefaultCharSet = toName($4); }
	| DEFAULT CHARACTER SET symbol_character_set_name COLLATION symbol_collation_name
		{
			$alterDatabaseNode->setDefaultCharSet = toName($4);
			$alterDatabaseNode->setDefaultCollation = toName($6);
		}
	| KW_DIFFERENCE KW_FILE sql_string
		{ $alterDatabaseNode->differenceFile = toPathName($3); }
	;

%type <dbFileClause> db_file
db_file
	: KW_FILE sql_string
			{
				DbFileClause* clause = newNode<DbFileClause>(toPathName($2));
				$$ = clause;
			}
		file_desc1($3)
			{ $$ = $3; }
	;

%type file_desc1(<dbFileClause>)
file_desc1($dbFileClause)
	: // nothing
	| file_desc($dbFileClause)
	;

%type file_desc(<dbFileClause>)
file_desc($dbFileClause)
	: file_clause($dbFileClause)
	| file_desc file_clause($dbFileClause)
	;

%type file_clause(<dbFileClause>)
file_clause($dbFileClause)
	: STARTING file_clause_noise long_integer
		{ $dbFileClause->start = $3; }
	| LENGTH equals long_integer page_noise
		{ $dbFileClause->length = $3; }
	;

file_clause_noise
	: // nothing
	| AT
	| AT PAGE
	;

page_noise
	: // nothing
	| PAGE
	| PAGES
	;


// CREATE TABLE

%type <createRelationNode> table_clause
table_clause
	: simple_table_name external_file
			{ $<createRelationNode>$ = newNode<CreateRelationNode>($1, $2); }
		'(' table_elements($3) ')'
			{ $$ = $3; }
	;

%type <ddlNode> rtable_clause
rtable_clause
	: table_clause
		{ $$ = newNode<RecreateTableNode>($1); }
	;

%type <createRelationNode> gtt_table_clause
gtt_table_clause
	: simple_table_name
			{ $<createRelationNode>$ = newNode<CreateRelationNode>($1); }
		'(' table_elements($2) ')' gtt_scope
			{
				$$ = $2;
				$$->relationType = static_cast<rel_t>($6);
			}
	;

%type <ddlNode> gtt_recreate_clause
gtt_recreate_clause
	: gtt_table_clause
		{ $$ = newNode<RecreateTableNode>($1); }
	;

%type <intVal> gtt_scope
gtt_scope
	: /* nothing */				{ $$ = rel_global_temp_delete; }
	| ON COMMIT KW_DELETE ROWS	{ $$ = rel_global_temp_delete; }
	| ON COMMIT PRESERVE ROWS	{ $$ = rel_global_temp_preserve; }
	;

%type <pathNamePtr> external_file
external_file
	: // nothing
		{ $$ = NULL; }
	| EXTERNAL KW_FILE sql_string
		{ $$ = newNode<PathName>($3->str_data, $3->str_length); }
	| EXTERNAL sql_string
		{ $$ = newNode<PathName>($2->str_data, $2->str_length); }
	;

%type table_elements(<createRelationNode>)
table_elements($createRelationNode)
	: table_element($createRelationNode)
	| table_elements ',' table_element($createRelationNode)
	;

%type table_element(<createRelationNode>)
table_element($createRelationNode)
	: column_def($createRelationNode)
	| table_constraint_definition($createRelationNode)
	;


// column definition

%type column_def(<relationNode>)
column_def($relationNode)
	: column_def_name data_type_or_domain domain_default_opt
			{
				RelationNode::AddColumnClause* clause = $<addColumnClause>$ =
					newNode<RelationNode::AddColumnClause>();
				clause->field = $1;
				clause->defaultValue = $3;
				clause->domain = toName($2);
				$relationNode->clauses.add(clause);
			}
		column_constraint_clause($<addColumnClause>4) collate_clause
			{ $<addColumnClause>4->collate = toName($6); }
	| column_def_name data_type_or_domain identity_clause
			{
				RelationNode::AddColumnClause* clause = $<addColumnClause>$ =
					newNode<RelationNode::AddColumnClause>();
				clause->field = $1;
				clause->identity = true;
				$relationNode->clauses.add(clause);
			}
		column_constraint_clause($<addColumnClause>4) collate_clause
			{ $<addColumnClause>4->collate = toName($6); }
	| column_def_name non_array_type def_computed
		{
			RelationNode::AddColumnClause* clause = newNode<RelationNode::AddColumnClause>();
			clause->field = $1;
			clause->computed = $3;
			$relationNode->clauses.add(clause);
		}
	| column_def_name def_computed
		{
			RelationNode::AddColumnClause* clause = newNode<RelationNode::AddColumnClause>();
			clause->field = $1;
			clause->computed = $2;
			$relationNode->clauses.add(clause);
		}
	;

identity_clause
	: GENERATED BY DEFAULT AS IDENTITY
	;

// value does allow parens around it, but there is a problem getting the source text.

%type <valueSourceClause> def_computed
def_computed
	: computed_clause '(' value ')'
		{
			lex.g_field->fld_flags |= FLD_computed;
			ValueSourceClause* clause = newNode<ValueSourceClause>();
			clause->value = $3;
			clause->source = toString(makeParseStr(YYPOSNARG(2), YYPOSNARG(4)));
			$$ = clause;
		}
	;

computed_clause
	: computed_by
	| generated_always_clause
	;

generated_always_clause
	: GENERATED ALWAYS AS
	;

computed_by
	: COMPUTED BY
	| COMPUTED
	;

%type <legacyStr> data_type_or_domain
data_type_or_domain
	: data_type				{ $$ = NULL; }
	| symbol_column_name
	;

%type <legacyStr> collate_clause
collate_clause
	:								{ $$ = NULL; }
	| COLLATE symbol_collation_name	{ $$ = $2; }
	;


%type <legacyField> column_def_name
column_def_name
	: simple_column_name
		{
			lex.g_field_name = $1;
			lex.g_field = make_field ($1);
			$$ = lex.g_field;
		}
	;

%type <legacyField> simple_column_def_name
simple_column_def_name
	: simple_column_name
		{
			lex.g_field = make_field ($1);
			$$ = lex.g_field;
		}
	;


%type <legacyField> data_type_descriptor
data_type_descriptor
	: init_data_type data_type
		{ $$ = $1; }
	| KW_TYPE OF column_def_name
		{
			$3->fld_type_of_name = $3->fld_name;
			$$ = $3;
		}
	| KW_TYPE OF COLUMN symbol_column_name '.' symbol_column_name
		{
			lex.g_field = make_field(NULL);
			lex.g_field->fld_type_of_table = ((dsql_str*) $4)->str_data;
			lex.g_field->fld_type_of_name = ((dsql_str*) $6)->str_data;
			$$ = lex.g_field;
		}
	| column_def_name
		{
			$1->fld_type_of_name = $1->fld_name;
			$1->fld_full_domain = true;
			$$ = $1;
		}
	;

%type <legacyField> init_data_type
init_data_type
	:
		{
			lex.g_field = make_field(NULL);
			$$ = lex.g_field;
		}
	;


%type <valueExprNode> default_value
default_value
	: constant						{ $$ = $1; }
	| current_user					{ $$ = $1; }
	| current_role					{ $$ = $1; }
	| internal_info					{ $$ = $1; }
	| null_value					{ $$ = $1; }
	| datetime_value_expression		{ $$ = $1; }
	;

%type column_constraint_clause(<addColumnClause>)
column_constraint_clause($addColumnClause)
	: // nothing
	| column_constraint_list($addColumnClause)
	;

%type column_constraint_list(<addColumnClause>)
column_constraint_list($addColumnClause)
	: column_constraint_def($addColumnClause)
	| column_constraint_list column_constraint_def($addColumnClause)
	;

%type column_constraint_def(<addColumnClause>)
column_constraint_def($addColumnClause)
	: constraint_name_opt column_constraint($addColumnClause)
		{ $addColumnClause->constraints.back()->name = toName($1); }
	;

%type column_constraint(<addColumnClause>)
column_constraint($addColumnClause)
	: null_constraint
		{
			RelationNode::AddConstraintClause& constraint = $addColumnClause->constraints.add();
			constraint.constraintType = RelationNode::AddConstraintClause::CTYPE_NOT_NULL;
		}
	| check_constraint
		{
			RelationNode::AddConstraintClause& constraint = $addColumnClause->constraints.add();
			constraint.constraintType = RelationNode::AddConstraintClause::CTYPE_CHECK;
			constraint.check = $1;
		}
	| REFERENCES symbol_table_name column_parens_opt
			referential_trigger_action constraint_index_opt
		{
			RelationNode::AddConstraintClause& constraint = $addColumnClause->constraints.add();
			constraint.constraintType = RelationNode::AddConstraintClause::CTYPE_FK;

			constraint.columns.add(lex.g_field_name->dsqlName);
			constraint.refRelation = toName($2);
			constraint.refAction = $4;

			const ValueListNode* refColumns = $3;
			if (refColumns)
			{
				const NestConst<ValueExprNode>* ptr = refColumns->items.begin();

				for (const NestConst<ValueExprNode>* const end = refColumns->items.end(); ptr != end; ++ptr)
					constraint.refColumns.add((*ptr)->as<FieldNode>()->dsqlName);
			}

			constraint.index = $5;
		}
	| UNIQUE constraint_index_opt
		{
			RelationNode::AddConstraintClause& constraint = $addColumnClause->constraints.add();
			constraint.constraintType = RelationNode::AddConstraintClause::CTYPE_UNIQUE;
			constraint.index = $2;
		}
	| PRIMARY KEY constraint_index_opt
		{
			RelationNode::AddConstraintClause& constraint = $addColumnClause->constraints.add();
			constraint.constraintType = RelationNode::AddConstraintClause::CTYPE_PK;
			constraint.index = $3;
		}
	;


// table constraints

%type table_constraint_definition(<relationNode>)
table_constraint_definition($relationNode)
	: constraint_name_opt table_constraint($relationNode)
		{ static_cast<RelationNode::AddConstraintClause*>($relationNode->clauses.back().getObject())->name = toName($1); }
	;

%type <legacyStr> constraint_name_opt
constraint_name_opt
	: /* nothing */							{ $$ = NULL; }
	| CONSTRAINT symbol_constraint_name		{ $$ = $2; }
	;

%type table_constraint(<relationNode>)
table_constraint($relationNode)
	: UNIQUE column_parens constraint_index_opt
		{
			RelationNode::AddConstraintClause& constraint = *newNode<RelationNode::AddConstraintClause>();
			constraint.constraintType = RelationNode::AddConstraintClause::CTYPE_UNIQUE;

			const ValueListNode* columns = $2;
			const NestConst<ValueExprNode>* ptr = columns->items.begin();

			for (const NestConst<ValueExprNode>* const end = columns->items.end(); ptr != end; ++ptr)
				constraint.columns.add((*ptr)->as<FieldNode>()->dsqlName);

			constraint.index = $3;

			$relationNode->clauses.add(&constraint);
		}
	| PRIMARY KEY column_parens constraint_index_opt
		{
			RelationNode::AddConstraintClause& constraint = *newNode<RelationNode::AddConstraintClause>();
			constraint.constraintType = RelationNode::AddConstraintClause::CTYPE_PK;

			const ValueListNode* columns = $3;
			const NestConst<ValueExprNode>* ptr = columns->items.begin();

			for (const NestConst<ValueExprNode>* const end = columns->items.end(); ptr != end; ++ptr)
				constraint.columns.add((*ptr)->as<FieldNode>()->dsqlName);

			constraint.index = $4;

			$relationNode->clauses.add(&constraint);
		}
	| FOREIGN KEY column_parens REFERENCES symbol_table_name column_parens_opt
		referential_trigger_action constraint_index_opt
		{
			RelationNode::AddConstraintClause& constraint = *newNode<RelationNode::AddConstraintClause>();
			constraint.constraintType = RelationNode::AddConstraintClause::CTYPE_FK;

			const ValueListNode* columns = $3;
			const NestConst<ValueExprNode>* ptr = columns->items.begin();

			for (const NestConst<ValueExprNode>* const end = columns->items.end(); ptr != end; ++ptr)
				constraint.columns.add((*ptr)->as<FieldNode>()->dsqlName);

			constraint.refRelation = toName($5);
			constraint.refAction = $7;

			const ValueListNode* refColumns = $6;
			if (refColumns)
			{
				const NestConst<ValueExprNode>* ptr = refColumns->items.begin();

				for (const NestConst<ValueExprNode>* const end = refColumns->items.end(); ptr != end; ++ptr)
					constraint.refColumns.add((*ptr)->as<FieldNode>()->dsqlName);
			}

			constraint.index = $8;

			$relationNode->clauses.add(&constraint);
		}
	| check_constraint
		{
			RelationNode::AddConstraintClause* constraint = newNode<RelationNode::AddConstraintClause>();
			constraint->constraintType = RelationNode::AddConstraintClause::CTYPE_CHECK;
			constraint->check = $1;
			$relationNode->clauses.add(constraint);
		}
	;

%type <indexConstraintClause> constraint_index_opt
constraint_index_opt
	: // nothing
		{ $$ = newNode<RelationNode::IndexConstraintClause>(); }
	| USING order_direction INDEX symbol_index_name
		{
			RelationNode::IndexConstraintClause* clause = $$ =
				newNode<RelationNode::IndexConstraintClause>();
			clause->descending = $2;
			clause->name = toName($4);
		}
	/***
	| NO INDEX
		{ $$ = NULL; }
	***/
	;

%type <refActionClause> referential_trigger_action
referential_trigger_action
	: /* nothing */				{ $$ = NULL; }
	| update_rule				{ $$ = newNode<RelationNode::RefActionClause>($1, 0); }
	| delete_rule				{ $$ = newNode<RelationNode::RefActionClause>(0, $1); }
	| delete_rule update_rule	{ $$ = newNode<RelationNode::RefActionClause>($2, $1); }
	| update_rule delete_rule	{ $$ = newNode<RelationNode::RefActionClause>($1, $2); }
	;

%type <uintVal> update_rule
update_rule
	: ON UPDATE referential_action		{ $$ = $3;}
	;

%type <uintVal> delete_rule
delete_rule
	: ON KW_DELETE referential_action	{ $$ = $3;}
	;

%type <uintVal>	referential_action
referential_action
	: CASCADE		{ $$ = RelationNode::RefActionClause::ACTION_CASCADE; }
	| SET DEFAULT	{ $$ = RelationNode::RefActionClause::ACTION_SET_DEFAULT; }
	| SET KW_NULL	{ $$ = RelationNode::RefActionClause::ACTION_SET_NULL; }
	| NO ACTION		{ $$ = RelationNode::RefActionClause::ACTION_NONE; }
	;


// PROCEDURE


%type <createAlterProcedureNode> procedure_clause
procedure_clause
	: procedure_clause_start AS local_declaration_list full_proc_block
		{
			$$ = $1;
			$$->source = toString(makeParseStr(YYPOSNARG(3), YYPOSNARG(4)));
			$$->localDeclList = $3;
			$$->body = $4;
		}
	| procedure_clause_start external_clause external_body_clause_opt
		{
			$$ = $1;
			$$->external = $2;
			if ($3)
				$$->source = toString($3);
		}
	;

%type <createAlterProcedureNode> procedure_clause_start
procedure_clause_start
	: symbol_procedure_name
			{ $$ = newNode<CreateAlterProcedureNode>(toName($1)); }
		input_parameters(&$2->parameters) output_parameters(&$2->returns)
			{ $$ = $2; }
	;

%type <createAlterProcedureNode> alter_procedure_clause
alter_procedure_clause
	: procedure_clause
		{
			$$ = $1;
			$$->alter = true;
			$$->create = false;
		}
	;

%type <createAlterProcedureNode> replace_procedure_clause
replace_procedure_clause
	: procedure_clause
		{
			$$ = $1;
			$$->alter = true;
		}
	;

%type input_parameters(<parametersClause>)
input_parameters($parameters)
	:
	| '(' ')'
	| '(' input_proc_parameters($parameters) ')'
	;

%type output_parameters(<parametersClause>)
output_parameters($parameters)
	:
	| RETURNS '(' output_proc_parameters($parameters) ')'
	;

%type input_proc_parameters(<parametersClause>)
input_proc_parameters($parameters)
	: input_proc_parameter($parameters)
	| input_proc_parameters ',' input_proc_parameter($parameters)
	;

%type input_proc_parameter(<parametersClause>)
input_proc_parameter($parameters)
	: simple_column_def_name domain_or_non_array_type collate_clause default_par_opt
		{ $parameters->add(ParameterClause(getPool(), $1, toName($3), $4, NULL)); }
	;

%type output_proc_parameters(<parametersClause>)
output_proc_parameters($parameters)
	: output_proc_parameter
	| output_proc_parameters ',' output_proc_parameter($parameters)
	;

%type output_proc_parameter(<parametersClause>)
output_proc_parameter($parameters)
	: simple_column_def_name domain_or_non_array_type collate_clause
		{ $parameters->add(ParameterClause(getPool(), $1, toName($3), NULL, NULL)); }
	;

%type <valueSourceClause> default_par_opt
default_par_opt
	: // nothing
		{ $$ = NULL; }
	| DEFAULT default_value
		{
			ValueSourceClause* clause = newNode<ValueSourceClause>();
			clause->value = $2;
			clause->source = toString(makeParseStr(YYPOSNARG(1), YYPOSNARG(2)));
			$$ = clause;
		}
	| '=' default_value
		{
			ValueSourceClause* clause = newNode<ValueSourceClause>();
			clause->value = $2;
			clause->source = toString(makeParseStr(YYPOSNARG(1), YYPOSNARG(2)));
			$$ = clause;
		}
	;


// FUNCTION

%type <createAlterFunctionNode> function_clause
function_clause
	: function_clause_start AS local_declaration_list full_proc_block
		{
			$$ = $1;
			$$->source = toString(makeParseStr(YYPOSNARG(3), YYPOSNARG(4)));
			$$->localDeclList = $3;
			$$->body = $4;
		}
	| function_clause_start external_clause external_body_clause_opt
		{
			$$ = $1;
			$$->external = $2;
			if ($3)
				$$->source = toString($3);
		}
	;

%type <createAlterFunctionNode> function_clause_start
function_clause_start
	: symbol_UDF_name
			{ $$ = newNode<CreateAlterFunctionNode>(toName($1)); }
		input_parameters(&$2->parameters)
		RETURNS { $<legacyField>$ = lex.g_field = make_field(NULL); }
		domain_or_non_array_type collate_clause deterministic_opt
			{
				$$ = $2;
				$$->returnType = ParameterClause(getPool(), $<legacyField>5, toName($7), NULL, NULL);
				$$->deterministic = $8;
			}
	;

%type <boolVal> deterministic_opt
deterministic_opt
	:					{ $$ = false; }
	| NOT DETERMINISTIC	{ $$ = false; }
	| DETERMINISTIC		{ $$ = true; }
	;

%type <externalClause> external_clause
external_clause
	: EXTERNAL NAME sql_string ENGINE valid_symbol_name
		{
			$$ = newNode<ExternalClause>();
			$$->name = toString($3);
			$$->engine = toName($5);
		}
	| EXTERNAL ENGINE valid_symbol_name
		{
			$$ = newNode<ExternalClause>();
			$$->engine = toName($3);
		}
	;

%type <legacyStr> external_body_clause_opt
external_body_clause_opt
	: AS sql_string
		{ $$ = $2; }
	|
		{ $$ = NULL; }
	;

%type <createAlterFunctionNode> alter_function_clause
alter_function_clause
	: function_clause
		{
			$$ = $1;
			$$->alter = true;
			$$->create = false;
		}
	;

%type <createAlterFunctionNode> replace_function_clause
replace_function_clause
	: function_clause
		{
			$$ = $1;
			$$->alter = true;
		}
	;


// PACKAGE

%type <createAlterPackageNode> package_clause
package_clause
	: symbol_package_name AS BEGIN package_items_opt END
		{
			CreateAlterPackageNode* node = newNode<CreateAlterPackageNode>(toName($1));
			node->source = toString(makeParseStr(YYPOSNARG(3), YYPOSNARG(5)));
			node->items = $4;
			$$ = node;
		}
	;

%type <packageItems> package_items_opt
package_items_opt
	: package_items
	|
		{ $$ = newNode<Array<CreateAlterPackageNode::Item> >(); }
	;

%type <packageItems> package_items
package_items
	: package_item
		{
			$$ = newNode<Array<CreateAlterPackageNode::Item> >();
			$$->add($1);
		}
	| package_items package_item
		{
			$$ = $1;
			$$->add($2);
		}
	;

%type <packageItem> package_item
package_item
	: FUNCTION function_clause_start ';'
		{
			$$ = CreateAlterPackageNode::Item::create($2);
		}
	| PROCEDURE procedure_clause_start ';'
		{
			$$ = CreateAlterPackageNode::Item::create($2);
		}
	;

%type <createAlterPackageNode> alter_package_clause
alter_package_clause
	: package_clause
		{
			$$ = $1;
			$$->alter = true;
			$$->create = false;
		}
	;

%type <createAlterPackageNode> replace_package_clause
replace_package_clause
	: package_clause
		{
			$$ = $1;
			$$->alter = true;
		}
	;


// PACKAGE BODY

%type <createPackageBodyNode> package_body_clause
package_body_clause
	: symbol_package_name AS BEGIN package_items package_body_items_opt END
		{
			CreatePackageBodyNode* node = newNode<CreatePackageBodyNode>(toName($1));
			node->source = toString(makeParseStr(YYPOSNARG(3), YYPOSNARG(6)));
			node->declaredItems = $4;
			node->items = $5;
			$$ = node;
		}
	| symbol_package_name AS BEGIN package_body_items_opt END
		{
			CreatePackageBodyNode* node = newNode<CreatePackageBodyNode>(toName($1));
			node->source = toString(makeParseStr(YYPOSNARG(3), YYPOSNARG(5)));
			node->items = $4;
			$$ = node;
		}
	;

%type <packageItems> package_body_items_opt
package_body_items_opt
	:
		{ $$ = newNode<Array<CreateAlterPackageNode::Item> >(); }
	| package_body_items
	;

%type <packageItems> package_body_items
package_body_items
	: package_body_item
		{
			$$ = newNode<Array<CreateAlterPackageNode::Item> >();
			$$->add($1);
		}
	| package_body_items package_body_item
		{
			$$ = $1;
			$$->add($2);
		}
	;

%type <packageItem> package_body_item
package_body_item
	: FUNCTION function_clause ';'
		{
			$$ = CreateAlterPackageNode::Item::create($2);
		}
	| PROCEDURE procedure_clause ';'
		{
			$$ = CreateAlterPackageNode::Item::create($2);
		}
	;


%type <compoundStmtNode> local_declaration_list
local_declaration_list
	: /* nothing */			{ $$ = NULL; }
	| local_declarations
	;

%type <compoundStmtNode> local_declarations
local_declarations
	: local_declaration
		{
			$$ = newNode<CompoundStmtNode>();
			$$->statements.add($1);
		}
	| local_declarations local_declaration
		{
			$1->statements.add($2);
			$$ = $1;
		}
	;

%type <stmtNode> local_declaration
local_declaration
	: DECLARE var_decl_opt local_declaration_item ';'
		{
			$$ = $3;
			$$->line = YYPOSNARG(1).firstLine;
			$$->column = YYPOSNARG(1).firstColumn;
		}
	| DECLARE PROCEDURE symbol_procedure_name
			{ $<execBlockNode>$ = newNode<ExecBlockNode>(); }
			input_parameters(&$<execBlockNode>4->parameters)
			output_parameters(&$<execBlockNode>4->returns) AS
			local_declaration_list
			full_proc_block
		{
			DeclareSubProcNode* node = newNode<DeclareSubProcNode>(toName($3));
			node->dsqlBlock = $<execBlockNode>4;
			node->dsqlBlock->localDeclList = $8;
			node->dsqlBlock->body = $9;

			for (size_t i = 0; i < node->dsqlBlock->parameters.getCount(); ++i)
				node->dsqlBlock->parameters[i].parameterExpr = make_parameter();

			$$ = node;
		}
	| DECLARE FUNCTION symbol_UDF_name
			{ $<execBlockNode>$ = newNode<ExecBlockNode>(); }
			input_parameters(&$<execBlockNode>4->parameters)
			RETURNS { $<legacyField>$ = lex.g_field = make_field(NULL); }
			domain_or_non_array_type collate_clause deterministic_opt AS
			local_declaration_list
			full_proc_block
		{
			DeclareSubFuncNode* node = newNode<DeclareSubFuncNode>(toName($3));
			node->dsqlDeterministic = $10;
			node->dsqlBlock = $<execBlockNode>4;
			node->dsqlBlock->localDeclList = $12;
			node->dsqlBlock->body = $13;

			for (size_t i = 0; i < node->dsqlBlock->parameters.getCount(); ++i)
				node->dsqlBlock->parameters[i].parameterExpr = make_parameter();

			node->dsqlBlock->returns.add(ParameterClause(getPool(), $<legacyField>7,
				toName($9), NULL, NULL));

			$$ = node;
		}
	;

%type <stmtNode> local_declaration_item
local_declaration_item
	: var_declaration_item
	| cursor_declaration_item
	;

%type <stmtNode> var_declaration_item
var_declaration_item
	: column_def_name domain_or_non_array_type collate_clause default_par_opt
		{
			DeclareVariableNode* node = newNode<DeclareVariableNode>();
			node->dsqlDef = FB_NEW(getPool()) ParameterClause(getPool(), $1, toName($3), $4, NULL);
			$$ = node;
		}
	;

var_decl_opt
	: // nothing
	| VARIABLE
	;

%type <stmtNode> cursor_declaration_item
cursor_declaration_item
	: symbol_cursor_name scroll_opt CURSOR FOR '(' select ')'
		{
			DeclareCursorNode* node = newNode<DeclareCursorNode>(toName($1),
				DeclareCursorNode::CUR_TYPE_EXPLICIT);
			node->dsqlScroll = $2;
			node->dsqlSelect = $6;
			$$ = node;
		}
	;

%type <boolVal> scroll_opt
scroll_opt
	: /* nothing */	{ $$ = false; }
	| NO SCROLL		{ $$ = false; }
	| SCROLL		{ $$ = true; }
	;

%type <stmtNode> proc_block
proc_block
	: proc_statement
	| full_proc_block
	;

%type <stmtNode> full_proc_block
full_proc_block
	: BEGIN full_proc_block_body END
		{ $$ = newNode<LineColumnNode>(YYPOSNARG(1).firstLine, YYPOSNARG(1).firstColumn, $2); }
	;

%type <stmtNode> full_proc_block_body
full_proc_block_body
	: // nothing
		{ $$ = newNode<CompoundStmtNode>(); }
	| proc_statements
		{
			BlockNode* node = newNode<BlockNode>();
			node->action = $1;
			$$ = node;
		}
	| proc_statements excp_hndl_statements
		{
			BlockNode* node = newNode<BlockNode>();
			node->action = $1;
			node->handlers = $2;
			$$ = node;
		}
	;

%type <compoundStmtNode> proc_statements
proc_statements
	: proc_block
		{
			$$ = newNode<CompoundStmtNode>();
			$$->statements.add($1);
		}
	| proc_statements proc_block
		{
			$1->statements.add($2);
			$$ = $1;
		}
	;

%type <stmtNode> proc_statement
proc_statement
	: simple_proc_statement ';'
		{ $$ = newNode<LineColumnNode>(YYPOSNARG(1).firstLine, YYPOSNARG(1).firstColumn, $1); }
	| complex_proc_statement
		{ $$ = newNode<LineColumnNode>(YYPOSNARG(1).firstLine, YYPOSNARG(1).firstColumn, $1); }
	;

%type <stmtNode> simple_proc_statement
simple_proc_statement
	: assignment
	| insert			{ $$ = $1; }
	| merge				{ $$ = $1; }
	| update
	| update_or_insert	{ $$ = $1; }
	| delete
	| singleton_select
	| exec_procedure
	| exec_sql			{ $$ = $1; }
	| exec_into			{ $$ = $1; }
	| exec_function
	| excp_statement	{ $$ = $1; }
	| raise_statement
	| post_event
	| cursor_statement
	| breakleave
	| continue
	| SUSPEND			{ $$ = newNode<SuspendNode>(); }
	| EXIT				{ $$ = newNode<ExitNode>(); }
	| RETURN value		{ $$ = newNode<ReturnNode>($2); }
	;

%type <stmtNode> complex_proc_statement
complex_proc_statement
	: in_autonomous_transaction
	| if_then_else
	| while
	| for_select
	| for_exec_into					{ $$ = $1; }
	;

%type <stmtNode> in_autonomous_transaction
in_autonomous_transaction
	: KW_IN AUTONOMOUS TRANSACTION DO proc_block
		{
			InAutonomousTransactionNode* node = newNode<InAutonomousTransactionNode>();
			node->action = $5;
			$$ = node;
		}
	;

%type <stmtNode> excp_statement
excp_statement
	: EXCEPTION symbol_exception_name
		{ $$ = newNode<ExceptionNode>(toName($2)); }
	| EXCEPTION symbol_exception_name value
		{ $$ = newNode<ExceptionNode>(toName($2), $3); }
	| EXCEPTION symbol_exception_name USING '(' value_list ')'
		{ $$ = newNode<ExceptionNode>(toName($2), (ValueExprNode*) NULL, $5); }
	;

%type <stmtNode> raise_statement
raise_statement
	: EXCEPTION		{ $$ = newNode<ExceptionNode>(); }
	;

%type <stmtNode> for_select
for_select
	: label_opt FOR select INTO variable_list cursor_def DO proc_block
		{
			ForNode* node = newNode<ForNode>();
			node->dsqlLabelName = $1;
			node->dsqlSelect = $3;
			node->dsqlInto = $5;
			node->dsqlCursor = $6;
			node->statement = $8;
			$$ = node;
		}
	;

%type <execStatementNode> exec_sql
exec_sql
	: EXECUTE STATEMENT
			{ $<execStatementNode>$ = newNode<ExecStatementNode>(); }
			exec_stmt_inputs($<execStatementNode>3) exec_stmt_options($<execStatementNode>3)
		{
			$$ = $<execStatementNode>3;
		}
	;

%type <execStatementNode> exec_into
exec_into
	: exec_sql INTO variable_list
		{
			$$ = $<execStatementNode>1;
			$$->outputs = $3;
		}
	;

%type <execStatementNode> for_exec_into
for_exec_into
	: label_opt FOR exec_into DO proc_block
		{
			$$ = $<execStatementNode>3;
			$$->dsqlLabelName = $1;
			$$->innerStmt = $5;
		}
	;

%type exec_stmt_inputs(<execStatementNode>)
exec_stmt_inputs($execStatementNode)
	: value
		{ $execStatementNode->sql = $1; }
	| '(' value ')' '(' named_params_list($execStatementNode) ')'
		{ $execStatementNode->sql = $2; }
	| '(' value ')' '(' not_named_params_list($execStatementNode) ')'
		{ $execStatementNode->sql = $2; }
	;

%type named_params_list(<execStatementNode>)
named_params_list($execStatementNode)
	: named_param($execStatementNode)
	| named_params_list ',' named_param($execStatementNode)
	;

%type named_param(<execStatementNode>)
named_param($execStatementNode)
	: symbol_variable_name BIND_PARAM value
		{
			if (!$execStatementNode->inputNames)
				$execStatementNode->inputNames = FB_NEW(getPool()) EDS::ParamNames(getPool());

			$execStatementNode->inputNames->add(
				FB_NEW(getPool()) string(getPool(), toString((dsql_str*) $1)));

			if (!$execStatementNode->inputs)
				$execStatementNode->inputs = newNode<ValueListNode>($3);
			else
				$execStatementNode->inputs->add($3);
		}
	;

%type not_named_params_list(<execStatementNode>)
not_named_params_list($execStatementNode)
	: not_named_param($execStatementNode)
	| not_named_params_list ',' not_named_param($execStatementNode)
	;

%type not_named_param(<execStatementNode>)
not_named_param($execStatementNode)
	: value
		{
			if (!$execStatementNode->inputs)
				$execStatementNode->inputs = newNode<ValueListNode>($1);
			else
				$execStatementNode->inputs->add($1);
		}
	;

%type exec_stmt_options(<execStatementNode>)
exec_stmt_options($execStatementNode)
	:
	| exec_stmt_options_list($execStatementNode)
	;

%type exec_stmt_options_list(<execStatementNode>)
exec_stmt_options_list($execStatementNode)
	: exec_stmt_options_list exec_stmt_option($execStatementNode)
	| exec_stmt_option($execStatementNode)
	;

%type exec_stmt_option(<execStatementNode>)
exec_stmt_option($execStatementNode)
	: ON EXTERNAL DATA SOURCE value
		{ setClause($execStatementNode->dataSource, "EXTERNAL DATA SOURCE", $5); }
	| ON EXTERNAL value
		{ setClause($execStatementNode->dataSource, "EXTERNAL DATA SOURCE", $3); }
	| AS USER value
		{ setClause($execStatementNode->userName, "USER", $3); }
	| PASSWORD value
		{ setClause($execStatementNode->password, "PASSWORD", $2); }
	| ROLE value
		{ setClause($execStatementNode->role, "ROLE", $2); }
	| WITH AUTONOMOUS TRANSACTION
		{ setClause($execStatementNode->traScope, "TRANSACTION", EDS::traAutonomous); }
	| WITH COMMON TRANSACTION
		{ setClause($execStatementNode->traScope, "TRANSACTION", EDS::traCommon); }
	| WITH CALLER PRIVILEGES
		{ setClause($execStatementNode->useCallerPrivs, "CALLER PRIVILEGES"); }
	/*
	| WITH TWO_PHASE TRANSACTION
		{ setClause($execStatementNode->traScope, "TRANSACTION", EDS::traTwoPhase); }
	*/
	;

%type <stmtNode> if_then_else
if_then_else
	: IF '(' search_condition ')' THEN proc_block ELSE proc_block
		{
			IfNode* node = newNode<IfNode>();
			node->condition = $3;
			node->trueAction = $6;
			node->falseAction = $8;
			$$ = node;
		}
	| IF '(' search_condition ')' THEN proc_block
		{
			IfNode* node = newNode<IfNode>();
			node->condition = $3;
			node->trueAction = $6;
			$$ = node;
		}
	;

%type <stmtNode> post_event
post_event
	: POST_EVENT value event_argument_opt
		{
			PostEventNode* node = newNode<PostEventNode>();
			node->event = $2;
			node->argument = $3;
			$$ = node;
		}
	;

%type <valueExprNode> event_argument_opt
event_argument_opt
	: /* nothing */	{ $$ = NULL; }
	///| ',' value		{ $$ = $2; }
	;

%type <stmtNode> singleton_select
singleton_select
	: select INTO variable_list
		{
			ForNode* node = newNode<ForNode>();
			node->dsqlSelect = $1;
			node->dsqlInto = $3;
			$$ = node;
		}
	;

%type <valueExprNode> variable
variable
	: ':' symbol_variable_name
		{
			VariableNode* node = newNode<VariableNode>();
			node->dsqlName = toName($2);
			$$ = node;
		}
	;

%type <valueListNode> variable_list
variable_list
	: variable							{ $$ = newNode<ValueListNode>($1); }
	| column_name						{ $$ = newNode<ValueListNode>($1); }
	| variable_list ',' column_name		{ $$ = $1->add($3); }
	| variable_list ',' variable		{ $$ = $1->add($3); }
	;

%type <stmtNode> while
while
	: label_opt WHILE '(' search_condition ')' DO proc_block
		{
			LoopNode* node = newNode<LoopNode>();
			node->dsqlLabelName = $1;
			node->dsqlExpr = $4;
			node->statement = $7;
			$$ = node;
		}
	;

%type <metaNamePtr> label_opt
label_opt
	: /* nothing */				{ $$ = NULL; }
	| symbol_label_name ':'		{ $$ = newNode<MetaName>(toName($1)); }
	;

%type <stmtNode> breakleave
breakleave
	: KW_BREAK
		{ $$ = newNode<ContinueLeaveNode>(blr_leave); }
	| LEAVE label_opt
		{
			ContinueLeaveNode* node = newNode<ContinueLeaveNode>(blr_leave);
			node->dsqlLabelName = $2;
			$$ = node;
		}
	;

%type <stmtNode> continue
continue
	: CONTINUE label_opt
		{
			ContinueLeaveNode* node = newNode<ContinueLeaveNode>(blr_continue_loop);
			node->dsqlLabelName = $2;
			$$ = node;
		}
	;

%type <declCursorNode> cursor_def
cursor_def
	: // nothing
		{ $$ = NULL; }
	| AS CURSOR symbol_cursor_name
		{ $$ = newNode<DeclareCursorNode>(toName($3), DeclareCursorNode::CUR_TYPE_FOR); }
	;

%type <compoundStmtNode> excp_hndl_statements
excp_hndl_statements
	: excp_hndl_statement
		{
			$$ = newNode<CompoundStmtNode>();
			$$->statements.add($1);
		}
	| excp_hndl_statements excp_hndl_statement
		{
			$1->statements.add($2);
			$$ = $1;
		}
	;

%type <stmtNode> excp_hndl_statement
excp_hndl_statement
	: WHEN
			{ $<errorHandlerNode>$ = newNode<ErrorHandlerNode>(); }
		errors(&$<errorHandlerNode>2->conditions) DO proc_block
			{
				ErrorHandlerNode* node = $<errorHandlerNode>2;
				node->action = $5;
				$$ = node;
			}
	;

%type errors(<exceptionArray>)
errors($exceptionArray)
	: err($exceptionArray)
	| errors ',' err($exceptionArray)
	;

%type err(<exceptionArray>)
err($exceptionArray)
	: SQLCODE signed_short_integer
		{
			ExceptionItem& item = $exceptionArray->add();
			item.type = ExceptionItem::SQL_CODE;
			item.code = $2;
		}
	| GDSCODE symbol_gdscode_name
		{
			ExceptionItem& item = $exceptionArray->add();
			item.type = ExceptionItem::GDS_CODE;
			item.name = toName($2).c_str();
		}
	| EXCEPTION symbol_exception_name
		{
			ExceptionItem& item = $exceptionArray->add();
			item.type = ExceptionItem::XCP_CODE;
			item.name = toName($2).c_str();
		}
	| ANY
		{
			ExceptionItem& item = $exceptionArray->add();
			item.type = ExceptionItem::XCP_DEFAULT;
		}
	;

%type <stmtNode> cursor_statement
cursor_statement
	: open_cursor
	| fetch_cursor
	| close_cursor
	;

%type <stmtNode> open_cursor
open_cursor
	: OPEN symbol_cursor_name
		{ $$ = newNode<CursorStmtNode>(blr_cursor_open, toName($2)); }
	;

%type <stmtNode> close_cursor
close_cursor
	: CLOSE symbol_cursor_name
		{ $$ = newNode<CursorStmtNode>(blr_cursor_close, toName($2)); }
	;

%type <stmtNode> fetch_cursor
fetch_cursor
	: FETCH
			{ $<cursorStmtNode>$ = newNode<CursorStmtNode>(blr_cursor_fetch_scroll); }
			fetch_scroll($<cursorStmtNode>2) FROM symbol_cursor_name INTO variable_list
		{
			CursorStmtNode* cursorStmt = $<cursorStmtNode>2;
			cursorStmt->dsqlName = toName($5);
			cursorStmt->dsqlIntoStmt = $7;
			$$ = cursorStmt;
		}
	| FETCH symbol_cursor_name INTO variable_list
		{ $$ = newNode<CursorStmtNode>(blr_cursor_fetch, toName($2), $4); }
	;

%type fetch_scroll(<cursorStmtNode>)
fetch_scroll($cursorStmtNode)
	: FIRST
		{ $cursorStmtNode->scrollOp = blr_scroll_bof; }
	| LAST
		{ $cursorStmtNode->scrollOp = blr_scroll_eof; }
	| PRIOR
		{ $cursorStmtNode->scrollOp = blr_scroll_backward; }
	| NEXT
		{ $cursorStmtNode->scrollOp = blr_scroll_forward; }
	| KW_ABSOLUTE value
		{
			$cursorStmtNode->scrollOp = blr_scroll_absolute;
			$cursorStmtNode->scrollExpr = $2;
		}
	| KW_RELATIVE value
		{
			$cursorStmtNode->scrollOp = blr_scroll_relative;
			$cursorStmtNode->scrollExpr = $2;
		}
	;


// EXECUTE PROCEDURE

%type <stmtNode> exec_procedure
exec_procedure
	: EXECUTE PROCEDURE symbol_procedure_name proc_inputs proc_outputs_opt
		{ $$ = newNode<ExecProcedureNode>(QualifiedName(toName($3)), $4, $5); }
	| EXECUTE PROCEDURE symbol_package_name '.' symbol_procedure_name proc_inputs proc_outputs_opt
		{ $$ = newNode<ExecProcedureNode>(QualifiedName(toName($5), toName($3)), $6, $7); }
	;

%type <valueListNode> proc_inputs
proc_inputs
	: /* nothing */			{ $$ = NULL; }
	| value_list			{ $$ = $1; }
	| '(' value_list ')'	{ $$ = $2; }
	;

%type <valueListNode> proc_outputs_opt
proc_outputs_opt
	: /* nothing */								{ $$ = NULL; }
	| RETURNING_VALUES variable_list			{ $$ = $2; }
	| RETURNING_VALUES '(' variable_list ')'	{ $$ = $3; }
	;

// EXECUTE BLOCK

%type <execBlockNode> exec_block
exec_block
	: EXECUTE BLOCK
			{ $<execBlockNode>$ = newNode<ExecBlockNode>(); }
			block_input_params(&$3->parameters)
			output_parameters(&$3->returns) AS
			local_declaration_list
			full_proc_block
		{
			ExecBlockNode* node = $3;
			node->localDeclList = $7;
			node->body = $8;
			$$ = node;
		}
	;

%type block_input_params(<parametersClause>)
block_input_params($parameters)
	:
	| '(' block_parameters($parameters) ')'
	;

%type block_parameters(<parametersClause>)
block_parameters($parameters)
	: block_parameter($parameters)
	| block_parameters ',' block_parameter($parameters)
	;

%type block_parameter(<parametersClause>)
block_parameter($parameters)
	: simple_column_def_name domain_or_non_array_type collate_clause '=' parameter
		{ $parameters->add(ParameterClause(getPool(), $1, toName($3), NULL, $5)); }
	;

// CREATE VIEW

%type <createAlterViewNode> view_clause
view_clause
	: simple_table_name column_parens_opt AS select_expr check_opt
		{
			CreateAlterViewNode* node = newNode<CreateAlterViewNode>($1, $2, $4);
			node->source = toString(makeParseStr(YYPOSNARG(4), YYPOSNARG(5)));
			node->withCheckOption = $5;
			$$ = node;
		}
	;

%type <ddlNode> rview_clause
rview_clause
	: view_clause
		{ $$ = newNode<RecreateViewNode>($1); }
	;

%type <ddlNode> replace_view_clause
replace_view_clause
	: view_clause
		{
			$1->alter = true;
			$$ = $1;
		}
	;

%type <ddlNode>	alter_view_clause
alter_view_clause
	: view_clause
		{
			$1->alter = true;
			$1->create = false;
			$$ = $1;
		}
	;

%type <boolVal>	check_opt
check_opt
	: /* nothing */			{ $$ = false; }
	| WITH CHECK OPTION		{ $$ = true; }
	;


// CREATE TRIGGER

%type <createAlterTriggerNode> trigger_clause
trigger_clause
	: symbol_trigger_name trigger_active trigger_type trigger_position
			AS local_declaration_list full_proc_block
		{
			$$ = newNode<CreateAlterTriggerNode>(toName($1));
			$$->active = $2;
			$$->type = $3;
			$$->position = $4;
			$$->source = toString(makeParseStr(YYPOSNARG(5), YYPOSNARG(7)));
			$$->localDeclList = $6;
			$$->body = $7;
		}
	| symbol_trigger_name trigger_active trigger_type trigger_position
			external_clause external_body_clause_opt
		{
			$$ = newNode<CreateAlterTriggerNode>(toName($1));
			$$->active = $2;
			$$->type = $3;
			$$->position = $4;
			$$->external = $5;
			if ($6)
				$$->source = toString($6);
		}
	| symbol_trigger_name trigger_active trigger_type trigger_position ON symbol_table_name
			AS local_declaration_list full_proc_block
		{
			$$ = newNode<CreateAlterTriggerNode>(toName($1));
			$$->active = $2;
			$$->type = $3;
			$$->position = $4;
			$$->relationName = toName($6);
			$$->source = toString(makeParseStr(YYPOSNARG(7), YYPOSNARG(9)));
			$$->localDeclList = $8;
			$$->body = $9;
		}
	| symbol_trigger_name trigger_active trigger_type trigger_position ON symbol_table_name
			external_clause external_body_clause_opt
		{
			$$ = newNode<CreateAlterTriggerNode>(toName($1));
			$$->active = $2;
			$$->type = $3;
			$$->position = $4;
			$$->relationName = toName($6);
			$$->external = $7;
			if ($8)
				$$->source = toString($8);
		}
	| symbol_trigger_name FOR symbol_table_name trigger_active trigger_type trigger_position
			AS local_declaration_list full_proc_block
		{
			$$ = newNode<CreateAlterTriggerNode>(toName($1));
			$$->active = $4;
			$$->type = $5;
			$$->position = $6;
			$$->relationName = toName($3);
			$$->source = toString(makeParseStr(YYPOSNARG(7), YYPOSNARG(9)));
			$$->localDeclList = $8;
			$$->body = $9;
		}
	| symbol_trigger_name FOR symbol_table_name trigger_active trigger_type trigger_position
			external_clause external_body_clause_opt
		{
			$$ = newNode<CreateAlterTriggerNode>(toName($1));
			$$->active = $4;
			$$->type = $5;
			$$->position = $6;
			$$->relationName = toName($3);
			$$->external = $7;
			if ($8)
				$$->source = toString($8);
		}
	;

%type <createAlterTriggerNode> replace_trigger_clause
replace_trigger_clause
	: trigger_clause
		{
			$$ = $1;
			$$->alter = true;
		}
	;

%type <nullableBoolVal> trigger_active
trigger_active
	: ACTIVE
		{ $$ = Nullable<bool>::val(true); }
	| INACTIVE
		{ $$ = Nullable<bool>::val(false); }
	|
		{ $$ = Nullable<bool>::empty(); }
	;

%type <uint64Val> trigger_type
trigger_type
	: trigger_type_prefix trigger_type_suffix	{ $$ = $1 + $2 - 1; }
	| ON trigger_db_type						{ $$ = $2; }
	| trigger_type_prefix trigger_ddl_type		{ $$ = $1 + $2; }
	;

%type <uint64Val> trigger_db_type
trigger_db_type
	: CONNECT				{ $$ = TRIGGER_TYPE_DB | DB_TRIGGER_CONNECT; }
	| DISCONNECT			{ $$ = TRIGGER_TYPE_DB | DB_TRIGGER_DISCONNECT; }
	| TRANSACTION START		{ $$ = TRIGGER_TYPE_DB | DB_TRIGGER_TRANS_START; }
	| TRANSACTION COMMIT	{ $$ = TRIGGER_TYPE_DB | DB_TRIGGER_TRANS_COMMIT; }
	| TRANSACTION ROLLBACK	{ $$ = TRIGGER_TYPE_DB | DB_TRIGGER_TRANS_ROLLBACK; }
	;

%type <uint64Val> trigger_ddl_type
trigger_ddl_type
	: trigger_ddl_type_items
	| ANY DDL STATEMENT
		{
			$$ = TRIGGER_TYPE_DDL | (0x7FFFFFFFFFFFFFFFULL & ~(FB_UINT64) TRIGGER_TYPE_MASK & ~1ULL);
		}
	;

%type <uint64Val> trigger_ddl_type_items
trigger_ddl_type_items
	: CREATE TABLE			{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_CREATE_TABLE); }
	| ALTER TABLE			{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_ALTER_TABLE); }
	| DROP TABLE			{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_DROP_TABLE); }
	| CREATE PROCEDURE		{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_CREATE_PROCEDURE); }
	| ALTER PROCEDURE		{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_ALTER_PROCEDURE); }
	| DROP PROCEDURE		{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_DROP_PROCEDURE); }
	| CREATE FUNCTION		{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_CREATE_FUNCTION); }
	| ALTER FUNCTION		{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_ALTER_FUNCTION); }
	| DROP FUNCTION			{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_DROP_FUNCTION); }
	| CREATE TRIGGER		{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_CREATE_TRIGGER); }
	| ALTER TRIGGER			{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_ALTER_TRIGGER); }
	| DROP TRIGGER			{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_DROP_TRIGGER); }
	| CREATE EXCEPTION		{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_CREATE_EXCEPTION); }
	| ALTER EXCEPTION		{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_ALTER_EXCEPTION); }
	| DROP EXCEPTION		{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_DROP_EXCEPTION); }
	| CREATE VIEW			{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_CREATE_VIEW); }
	| ALTER VIEW			{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_ALTER_VIEW); }
	| DROP VIEW				{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_DROP_VIEW); }
	| CREATE DOMAIN			{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_CREATE_DOMAIN); }
	| ALTER DOMAIN			{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_ALTER_DOMAIN); }
	| DROP DOMAIN			{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_DROP_DOMAIN); }
	| CREATE ROLE			{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_CREATE_ROLE); }
	| ALTER ROLE			{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_ALTER_ROLE); }
	| DROP ROLE				{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_DROP_ROLE); }
	| CREATE SEQUENCE		{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_CREATE_SEQUENCE); }
	| ALTER SEQUENCE		{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_ALTER_SEQUENCE); }
	| DROP SEQUENCE			{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_DROP_SEQUENCE); }
	| CREATE USER			{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_CREATE_USER); }
	| ALTER USER			{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_ALTER_USER); }
	| DROP USER				{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_DROP_USER); }
	| CREATE INDEX			{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_CREATE_INDEX); }
	| ALTER INDEX			{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_ALTER_INDEX); }
	| DROP INDEX			{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_DROP_INDEX); }
	| CREATE COLLATION		{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_CREATE_COLLATION); }
	| DROP COLLATION		{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_DROP_COLLATION); }
	| ALTER CHARACTER SET	{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_ALTER_CHARACTER_SET); }
	| CREATE PACKAGE		{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_CREATE_PACKAGE); }
	| ALTER PACKAGE			{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_ALTER_PACKAGE); }
	| DROP PACKAGE			{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_DROP_PACKAGE); }
	| CREATE PACKAGE BODY	{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_CREATE_PACKAGE_BODY); }
	| DROP PACKAGE BODY		{ $$ = TRIGGER_TYPE_DDL | (1LL << DDL_TRIGGER_DROP_PACKAGE_BODY); }
	| trigger_ddl_type OR
		trigger_ddl_type	{ $$ = $1 | $3; }
	;

%type <uint64Val> trigger_type_prefix
trigger_type_prefix
	: BEFORE	{ $$ = 0; }
	| AFTER		{ $$ = 1; }
	;

%type <uint64Val> trigger_type_suffix
trigger_type_suffix
	: INSERT							{ $$ = trigger_type_suffix(1, 0, 0); }
	| UPDATE							{ $$ = trigger_type_suffix(2, 0, 0); }
	| KW_DELETE							{ $$ = trigger_type_suffix(3, 0, 0); }
	| INSERT OR UPDATE					{ $$ = trigger_type_suffix(1, 2, 0); }
	| INSERT OR KW_DELETE				{ $$ = trigger_type_suffix(1, 3, 0); }
	| UPDATE OR INSERT					{ $$ = trigger_type_suffix(2, 1, 0); }
	| UPDATE OR KW_DELETE				{ $$ = trigger_type_suffix(2, 3, 0); }
	| KW_DELETE OR INSERT				{ $$ = trigger_type_suffix(3, 1, 0); }
	| KW_DELETE OR UPDATE				{ $$ = trigger_type_suffix(3, 2, 0); }
	| INSERT OR UPDATE OR KW_DELETE		{ $$ = trigger_type_suffix(1, 2, 3); }
	| INSERT OR KW_DELETE OR UPDATE		{ $$ = trigger_type_suffix(1, 3, 2); }
	| UPDATE OR INSERT OR KW_DELETE		{ $$ = trigger_type_suffix(2, 1, 3); }
	| UPDATE OR KW_DELETE OR INSERT		{ $$ = trigger_type_suffix(2, 3, 1); }
	| KW_DELETE OR INSERT OR UPDATE		{ $$ = trigger_type_suffix(3, 1, 2); }
	| KW_DELETE OR UPDATE OR INSERT		{ $$ = trigger_type_suffix(3, 2, 1); }
	;

%type <nullableIntVal> trigger_position
trigger_position
	: /* nothing */					{ $$ = Nullable<int>::empty(); }
	| POSITION nonneg_short_integer	{ $$ = Nullable<int>::val($2); }
	;

// ALTER statement

%type <ddlNode> alter
alter
	: ALTER alter_clause	{ $$ = $2; }
	;

%type <ddlNode> alter_clause
alter_clause
	: EXCEPTION alter_exception_clause		{ $$ = $2; }
	| TABLE simple_table_name
			{ $$ = newNode<AlterRelationNode>($2); }
		alter_ops($<relationNode>3)
			{ $$ = $<relationNode>3; }
	| VIEW alter_view_clause				{ $$ = $2; }
	| TRIGGER alter_trigger_clause			{ $$ = $2; }
	| PROCEDURE alter_procedure_clause		{ $$ = $2; }
	| PACKAGE alter_package_clause			{ $$ = $2; }
	| DATABASE
			{ $<alterDatabaseNode>$ = newNode<AlterDatabaseNode>(); }
		alter_db($<alterDatabaseNode>2)
			{ $$ = $<alterDatabaseNode>2; }
	| DOMAIN alter_domain					{ $$ = $2; }
	| INDEX alter_index_clause				{ $$ = $2; }
	| EXTERNAL FUNCTION alter_udf_clause	{ $$ = $3; }
	| FUNCTION alter_function_clause		{ $$ = $2; }
	| ROLE alter_role_clause				{ $$ = $2; }
	| USER alter_user_clause				{ $$ = $2; }
	| CHARACTER SET alter_charset_clause	{ $$ = $3; }
	;

%type <alterDomainNode> alter_domain
alter_domain
	: keyword_or_column
			{ $<alterDomainNode>$ = newNode<AlterDomainNode>(toName($1)); }
		alter_domain_ops($2)
			{ $$ = $2; }
	;

%type alter_domain_ops(<alterDomainNode>)
alter_domain_ops($alterDomainNode)
	: alter_domain_op($alterDomainNode)
	| alter_domain_ops alter_domain_op($alterDomainNode)
	;

%type alter_domain_op(<alterDomainNode>)
alter_domain_op($alterDomainNode)
	: SET domain_default
		{ setClause($alterDomainNode->setDefault, "DOMAIN DEFAULT", $2); }
	| ADD CONSTRAINT check_constraint
		{ setClause($alterDomainNode->setConstraint, "DOMAIN CONSTRAINT", $3); }
	| ADD check_constraint
		{ setClause($alterDomainNode->setConstraint, "DOMAIN CONSTRAINT", $2); }
	| DROP DEFAULT
		{ setClause($alterDomainNode->dropDefault, "DOMAIN DROP DEFAULT"); }
	| DROP CONSTRAINT
		{ setClause($alterDomainNode->dropConstraint, "DOMAIN DROP CONSTRAINT"); }
	| KW_NULL
		{ setClause($alterDomainNode->notNullFlag, "[NOT] NULL", false); }
	| NOT KW_NULL
		{ setClause($alterDomainNode->notNullFlag, "[NOT] NULL", true); }
	| TO symbol_column_name
		{ setClause($alterDomainNode->renameTo, "DOMAIN NAME", toName($2)); }
	| KW_TYPE init_data_type non_array_type
		{
			//// FIXME: ALTER DOMAIN doesn't support collations, and altered domain's
			//// collation is always lost.
			TypeClause* type = newNode<TypeClause>($2, MetaName());
			setClause($alterDomainNode->type, "DOMAIN TYPE", type);
		}
	;

%type alter_ops(<relationNode>)
alter_ops($relationNode)
	: alter_op($relationNode)
	| alter_ops ',' alter_op($relationNode)
	;

%type alter_op(<relationNode>)
alter_op($relationNode)
	: DROP symbol_column_name drop_behaviour
		{
			RelationNode::DropColumnClause* clause = newNode<RelationNode::DropColumnClause>();
			clause->name = toName($2);
			clause->cascade = $3;
			$relationNode->clauses.add(clause);
		}
	| DROP CONSTRAINT symbol_constraint_name
		{
			RelationNode::DropConstraintClause* clause = newNode<RelationNode::DropConstraintClause>();
			clause->name = toName($3);
			$relationNode->clauses.add(clause);
		}
	| ADD column_def($relationNode)
	| ADD table_constraint_definition($relationNode)
	| col_opt alter_column_name POSITION pos_short_integer
		{
			RelationNode::AlterColPosClause* clause = newNode<RelationNode::AlterColPosClause>();
			clause->name = toName($2);
			clause->newPos = $4;
			$relationNode->clauses.add(clause);
		}
	| col_opt alter_column_name TO symbol_column_name
		{
			RelationNode::AlterColNameClause* clause = newNode<RelationNode::AlterColNameClause>();
			clause->fromName = toName($2);
			clause->toName = toName($4);
			$relationNode->clauses.add(clause);
		}
	| col_opt alter_column_name KW_NULL
		{
			RelationNode::AlterColNullClause* clause = newNode<RelationNode::AlterColNullClause>();
			clause->name = toName($2);
			clause->notNullFlag = false;
			$relationNode->clauses.add(clause);
		}
	| col_opt alter_column_name NOT KW_NULL
		{
			RelationNode::AlterColNullClause* clause = newNode<RelationNode::AlterColNullClause>();
			clause->name = toName($2);
			clause->notNullFlag = true;
			$relationNode->clauses.add(clause);
		}
	| col_opt alter_col_name KW_TYPE alter_data_type_or_domain
		{
			RelationNode::AlterColTypeClause* clause = newNode<RelationNode::AlterColTypeClause>();
			clause->field = $2;
			clause->domain = toName($4);
			$relationNode->clauses.add(clause);
		}
	| col_opt alter_col_name KW_TYPE non_array_type def_computed
		{
			RelationNode::AlterColTypeClause* clause = newNode<RelationNode::AlterColTypeClause>();
			clause->field = $2;
			clause->computed = $5;
			$relationNode->clauses.add(clause);
		}
	| col_opt alter_col_name def_computed
		{
			RelationNode::AlterColTypeClause* clause = newNode<RelationNode::AlterColTypeClause>();
			clause->field = $2;
			clause->computed = $3;
			$relationNode->clauses.add(clause);
		}
	| col_opt alter_col_name SET domain_default
		{
			RelationNode::AlterColTypeClause* clause = newNode<RelationNode::AlterColTypeClause>();
			clause->field = $2;
			clause->defaultValue = $4;
			$relationNode->clauses.add(clause);
		}
	| col_opt alter_col_name DROP DEFAULT
		{
			RelationNode::AlterColTypeClause* clause = newNode<RelationNode::AlterColTypeClause>();
			clause->field = $2;
			clause->dropDefault = true;
			$relationNode->clauses.add(clause);
		}
	;

%type <legacyStr> alter_column_name
alter_column_name
	: keyword_or_column
	;

// below are reserved words that could be used as column identifiers
// in the previous versions

%type <legacyStr> keyword_or_column
keyword_or_column
	: valid_symbol_name
	| ADMIN					// added in IB 5.0
	| COLUMN				// added in IB 6.0
	| EXTRACT
	| YEAR
	| MONTH
	| DAY
	| HOUR
	| MINUTE
	| SECOND
	| TIME
	| TIMESTAMP
	| CURRENT_DATE
	| CURRENT_TIME
	| CURRENT_TIMESTAMP
	| CURRENT_USER			// added in FB 1.0
	| CURRENT_ROLE
	| RECREATE
	| CURRENT_CONNECTION	// added in FB 1.5
	| CURRENT_TRANSACTION
	| BIGINT
	| CASE
	| RELEASE
	| ROW_COUNT
	| SAVEPOINT
	| OPEN					// added in FB 2.0
	| CLOSE
	| FETCH
	| ROWS
	| USING
	| CROSS
	| BIT_LENGTH
	| BOTH
	| CHAR_LENGTH
	| CHARACTER_LENGTH
	| COMMENT
	| LEADING
	| KW_LOWER
	| OCTET_LENGTH
	| TRAILING
	| TRIM
	| CONNECT				// added in FB 2.1
	| DISCONNECT
	| GLOBAL
	| INSENSITIVE
	| RECURSIVE
	| SENSITIVE
	| START
	| SIMILAR				// added in FB 2.5
	| OVER					// added in FB 3.0
	| SCROLL
	| RETURN
	| DETERMINISTIC
	| SQLSTATE
	;

col_opt
	: ALTER
	| ALTER COLUMN
	;

%type <legacyStr> alter_data_type_or_domain
alter_data_type_or_domain
	: non_array_type		{ $$ = NULL; }
	| symbol_column_name
	;

%type <legacyField> alter_col_name
alter_col_name
	: simple_column_name
		{
			lex.g_field_name = $1;
			lex.g_field = make_field ($1);
			$$ = lex.g_field;
		}
	;

%type <boolVal> drop_behaviour
drop_behaviour
	:				{ $$ = false; }
	| RESTRICT		{ $$ = false; }
	| CASCADE		{ $$ = true; }
	;

%type <ddlNode>	alter_index_clause
alter_index_clause
	: symbol_index_name ACTIVE		{ $$ = newNode<AlterIndexNode>(toName($1), true); }
	| symbol_index_name INACTIVE	{ $$ = newNode<AlterIndexNode>(toName($1), false); }
	;

%type <stmtNode> alter_sequence_clause
alter_sequence_clause
	: symbol_generator_name RESTART WITH signed_long_integer
		{ $$ = newNode<SetGeneratorNode>(toName($1), MAKE_const_slong($4)); }
	| symbol_generator_name RESTART WITH NUMBER64BIT
		{ $$ = newNode<SetGeneratorNode>(toName($1), MAKE_constant((dsql_str*) $4, CONSTANT_SINT64)); }
	| symbol_generator_name RESTART WITH '-' NUMBER64BIT
		{
			$$ = newNode<SetGeneratorNode>(toName($1),
				newNode<NegateNode>(MAKE_constant((dsql_str*) $5, CONSTANT_SINT64)));
		}
	;

%type <ddlNode>	alter_udf_clause
alter_udf_clause
	: symbol_UDF_name entry_op module_op
		{
			AlterExternalFunctionNode* node = newNode<AlterExternalFunctionNode>(toName($1));
			if ($2)
				node->clauses.name = toString($2);
			if ($3)
				node->clauses.udfModule = toString($3);
			$$ = node;
		}
	;

%type <legacyStr> entry_op
entry_op
	: /* nothing */				{ $$ = NULL; }
	| ENTRY_POINT sql_string	{ $$ = $2; }
	;

%type <legacyStr> module_op
module_op
	: /* nothing */				{ $$ = NULL; }
	| MODULE_NAME sql_string	{ $$ = $2; }
	;

%type <ddlNode>	alter_role_clause
alter_role_clause
	: symbol_role_name alter_role_enable AUTO ADMIN MAPPING
		{ $$ = newNode<AlterRoleNode>(toName($1), $2); }
	;

%type <boolVal>	alter_role_enable
alter_role_enable
	: SET		{ $$ = true; }
	| DROP		{ $$ = false; }
	;


// ALTER DATABASE

%type alter_db(<alterDatabaseNode>)
alter_db($alterDatabaseNode)
	: db_alter_clause($alterDatabaseNode)
	| alter_db db_alter_clause($alterDatabaseNode)
	;

%type db_alter_clause(<alterDatabaseNode>)
db_alter_clause($alterDatabaseNode)
	: ADD db_file_list(&$alterDatabaseNode->files)
	| ADD KW_DIFFERENCE KW_FILE sql_string
		{ $alterDatabaseNode->differenceFile = toPathName($4); }
	| DROP KW_DIFFERENCE KW_FILE
		{ $alterDatabaseNode->clauses |= AlterDatabaseNode::CLAUSE_DROP_DIFFERENCE; }
	| BEGIN BACKUP
		{ $alterDatabaseNode->clauses |= AlterDatabaseNode::CLAUSE_BEGIN_BACKUP; }
	| END BACKUP
		{ $alterDatabaseNode->clauses |= AlterDatabaseNode::CLAUSE_END_BACKUP; }
	| SET DEFAULT CHARACTER SET symbol_character_set_name
		{ $alterDatabaseNode->setDefaultCharSet = toName($5); }
	;


// ALTER TRIGGER

%type <createAlterTriggerNode> alter_trigger_clause
alter_trigger_clause
	: symbol_trigger_name trigger_active trigger_type_opt trigger_position
			AS local_declaration_list full_proc_block
		{
			$$ = newNode<CreateAlterTriggerNode>(toName($1));
			$$->alter = true;
			$$->create = false;
			$$->active = $2;
			$$->type = $3;
			$$->position = $4;
			$$->source = toString(makeParseStr(YYPOSNARG(5), YYPOSNARG(7)));
			$$->localDeclList = $6;
			$$->body = $7;
		}
	| symbol_trigger_name trigger_active trigger_type_opt trigger_position
			external_clause external_body_clause_opt
		{
			$$ = newNode<CreateAlterTriggerNode>(toName($1));
			$$->alter = true;
			$$->create = false;
			$$->active = $2;
			$$->type = $3;
			$$->position = $4;
			$$->external = $5;
			if ($6)
				$$->source = toString($6);
		}
	| symbol_trigger_name trigger_active trigger_type_opt trigger_position
		{
			$$ = newNode<CreateAlterTriggerNode>(toName($1));
			$$->alter = true;
			$$->create = false;
			$$->active = $2;
			$$->type = $3;
			$$->position = $4;
		}
	;

%type <nullableUint64Val> trigger_type_opt
trigger_type_opt	// we do not allow alter database triggers, hence we do not use trigger_type here
	: trigger_type_prefix trigger_type_suffix
		{ $$ = Nullable<FB_UINT64>::val($1 + $2 - 1); }
	|
		{ $$ = Nullable<FB_UINT64>::empty(); }
	;


// DROP metadata operations

%type <ddlNode> drop
drop
	: DROP drop_clause	{ $$ = $2; }
	;

%type <ddlNode> drop_clause
drop_clause
	: EXCEPTION symbol_exception_name
		{ $$ = newNode<DropExceptionNode>(toName($2)); }
	| INDEX symbol_index_name
		{ $$ = newNode<DropIndexNode>(toName($2)); }
	| PROCEDURE symbol_procedure_name
		{ $$ = newNode<DropProcedureNode>(toName($2)); }
	| TABLE symbol_table_name
		{ $$ = newNode<DropRelationNode>(toName($2), false); }
	| TRIGGER symbol_trigger_name
		{ $$ = newNode<DropTriggerNode>(toName($2)); }
	| VIEW symbol_view_name
		{ $$ = newNode<DropRelationNode>(toName($2), true); }
	| FILTER symbol_filter_name
		{ $$ = newNode<DropFilterNode>(toName($2)); }
	| DOMAIN symbol_domain_name
		{ $$ = newNode<DropDomainNode>(toName($2)); }
	| EXTERNAL FUNCTION symbol_UDF_name
		{ $$ = newNode<DropFunctionNode>(toName($3)); }
	| FUNCTION symbol_UDF_name
		{ $$ = newNode<DropFunctionNode>(toName($2)); }
	| SHADOW pos_short_integer
		{ $$ = newNode<DropShadowNode>($2); }
	| ROLE symbol_role_name
		{ $$ = newNode<DropRoleNode>(toName($2)); }
	| GENERATOR symbol_generator_name
		{ $$ = newNode<DropSequenceNode>(toName($2)); }
	| SEQUENCE symbol_generator_name
		{ $$ = newNode<DropSequenceNode>(toName($2)); }
	| COLLATION symbol_collation_name
		{ $$ = newNode<DropCollationNode>(toName($2)); }
	| USER symbol_user_name
		{ $$ = newNode<DropUserNode>(toName($2)); }
	| PACKAGE symbol_package_name
		{ $$ = newNode<DropPackageNode>(toName($2)); }
	| PACKAGE BODY symbol_package_name
		{ $$ = newNode<DropPackageBodyNode>(toName($3)); }
	;


// these are the allowable datatypes

data_type
	: non_array_type
	| array_type
	;

domain_or_non_array_type
	: domain_or_non_array_type_name
	| domain_or_non_array_type_name NOT KW_NULL
		{ lex.g_field->fld_not_nullable = true; }
	;

domain_or_non_array_type_name
	: non_array_type
	| domain_type
	;

domain_type
	: KW_TYPE OF symbol_column_name
		{ lex.g_field->fld_type_of_name = ((dsql_str*) $3)->str_data; }
	| KW_TYPE OF COLUMN symbol_column_name '.' symbol_column_name
		{
			lex.g_field->fld_type_of_name = ((dsql_str*) $6)->str_data;
			lex.g_field->fld_type_of_table = ((dsql_str*) $4)->str_data;
		}
	| symbol_column_name
		{
			lex.g_field->fld_type_of_name = ((dsql_str*) $1)->str_data;
			lex.g_field->fld_full_domain = true;
		}
	;

non_array_type
	: simple_type
	| blob_type
	;

array_type
	: non_charset_simple_type '[' array_spec ']'
		{
			lex.g_field->fld_ranges = $3;
			lex.g_field->fld_dimensions = lex.g_field->fld_ranges->items.getCount() / 2;
			lex.g_field->fld_element_dtype = lex.g_field->fld_dtype;
		}
	| character_type '[' array_spec ']' charset_clause
		{
			lex.g_field->fld_ranges = $3;
			lex.g_field->fld_dimensions = lex.g_field->fld_ranges->items.getCount() / 2;
			lex.g_field->fld_element_dtype = lex.g_field->fld_dtype;
		}
	;

%type <valueListNode> array_spec
array_spec
	: array_range
	| array_spec ',' array_range	{ $$ = $1->add($3->items[0])->add($3->items[1]); }
	;

%type <valueListNode> array_range
array_range
	: signed_long_integer
		{
			if ($1 < 1)
		 		$$ = newNode<ValueListNode>(MAKE_const_slong($1))->add(MAKE_const_slong(1));
			else
		 		$$ = newNode<ValueListNode>(MAKE_const_slong(1))->add(MAKE_const_slong($1));
		}
	| signed_long_integer ':' signed_long_integer
 		{ $$ = newNode<ValueListNode>(MAKE_const_slong($1))->add(MAKE_const_slong($3)); }
	;

simple_type
	: non_charset_simple_type
	| character_type charset_clause
	;

non_charset_simple_type
	: national_character_type
	| numeric_type
	| float_type
	| BIGINT
		{
			if (client_dialect < SQL_DIALECT_V6_TRANSITION)
			{
				ERRD_post (Arg::Gds(isc_sqlerr) << Arg::Num(-104) <<
					Arg::Gds(isc_sql_dialect_datatype_unsupport) << Arg::Num(client_dialect) <<
																	Arg::Str("BIGINT"));
			}
			if (db_dialect < SQL_DIALECT_V6_TRANSITION)
			{
				ERRD_post (Arg::Gds(isc_sqlerr) << Arg::Num(-104) <<
					Arg::Gds(isc_sql_db_dialect_dtype_unsupport) << Arg::Num(db_dialect) <<
																Arg::Str("BIGINT"));
			}
			lex.g_field->fld_dtype = dtype_int64;
			lex.g_field->fld_length = sizeof (SINT64);
		}
	| integer_keyword
		{
			lex.g_field->fld_dtype = dtype_long;
			lex.g_field->fld_length = sizeof (SLONG);
		}
	| SMALLINT
		{
			lex.g_field->fld_dtype = dtype_short;
			lex.g_field->fld_length = sizeof (SSHORT);
		}
	| DATE
		{
			stmt_ambiguous = true;
			if (client_dialect <= SQL_DIALECT_V5)
			{
				// Post warning saying that DATE is equivalent to TIMESTAMP
				ERRD_post_warning(Arg::Warning(isc_sqlwarn) << Arg::Num(301) <<
								  Arg::Warning(isc_dtype_renamed));
				lex.g_field->fld_dtype = dtype_timestamp;
				lex.g_field->fld_length = sizeof (GDS_TIMESTAMP);
			}
			else if (client_dialect == SQL_DIALECT_V6_TRANSITION)
				yyabandon (-104, isc_transitional_date);
			else
			{
				lex.g_field->fld_dtype = dtype_sql_date;
				lex.g_field->fld_length = sizeof (ULONG);
			}
		}
	| TIME
		{
			if (client_dialect < SQL_DIALECT_V6_TRANSITION)
			{
				ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-104) <<
						  Arg::Gds(isc_sql_dialect_datatype_unsupport) << Arg::Num(client_dialect) <<
																		  Arg::Str("TIME"));
			}
			if (db_dialect < SQL_DIALECT_V6_TRANSITION)
			{
				ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-104) <<
						  Arg::Gds(isc_sql_db_dialect_dtype_unsupport) << Arg::Num(db_dialect) <<
																		  Arg::Str("TIME"));
			}
			lex.g_field->fld_dtype = dtype_sql_time;
			lex.g_field->fld_length = sizeof (SLONG);
		}
	| TIMESTAMP
		{
			lex.g_field->fld_dtype = dtype_timestamp;
			lex.g_field->fld_length = sizeof (GDS_TIMESTAMP);
		}
	| KW_BOOLEAN
		{
			lex.g_field->fld_dtype = dtype_boolean;
			lex.g_field->fld_length = sizeof(UCHAR);
		}
	;

integer_keyword
	: INTEGER
	| KW_INT
	;


// allow a blob to be specified with any combination of segment length and subtype

blob_type
	: BLOB blob_subtype blob_segsize charset_clause
		{
			lex.g_field->fld_dtype = dtype_blob;
			lex.g_field->fld_length = sizeof(ISC_QUAD);
		}
	| BLOB '(' unsigned_short_integer ')'
		{
			lex.g_field->fld_dtype = dtype_blob;
			lex.g_field->fld_length = sizeof(ISC_QUAD);
			lex.g_field->fld_seg_length = (USHORT) $3;
			lex.g_field->fld_sub_type = 0;
		}
	| BLOB '(' unsigned_short_integer ',' signed_short_integer ')'
		{
			lex.g_field->fld_dtype = dtype_blob;
			lex.g_field->fld_length = sizeof(ISC_QUAD);
			lex.g_field->fld_seg_length = (USHORT) $3;
			lex.g_field->fld_sub_type = (USHORT) $5;
		}
	| BLOB '(' ',' signed_short_integer ')'
		{
			lex.g_field->fld_dtype = dtype_blob;
			lex.g_field->fld_length = sizeof(ISC_QUAD);
			lex.g_field->fld_seg_length = 80;
			lex.g_field->fld_sub_type = (USHORT) $4;
		}
	;

blob_segsize
	: // nothing
		{ lex.g_field->fld_seg_length = (USHORT) 80; }
	| SEGMENT KW_SIZE unsigned_short_integer
		{ lex.g_field->fld_seg_length = (USHORT) $3; }
	;

blob_subtype
	: // nothing
		{ lex.g_field->fld_sub_type = (USHORT) 0; }
	| SUB_TYPE signed_short_integer
		{ lex.g_field->fld_sub_type = (USHORT) $2; }
	| SUB_TYPE symbol_blob_subtype_name
		{ lex.g_field->fld_sub_type_name = (dsql_str*) $2; }
	;

charset_clause
	: // nothing
	| CHARACTER SET symbol_character_set_name	{ lex.g_field->fld_character_set = (dsql_str*) $3; }
	;


// character type


national_character_type
	: national_character_keyword '(' pos_short_integer ')'
		{
			lex.g_field->fld_dtype = dtype_text;
			lex.g_field->fld_character_length = (USHORT) $3;
			lex.g_field->fld_flags |= FLD_national;
		}
	| national_character_keyword
		{
			lex.g_field->fld_dtype = dtype_text;
			lex.g_field->fld_character_length = 1;
			lex.g_field->fld_flags |= FLD_national;
		}
	| national_character_keyword VARYING '(' pos_short_integer ')'
		{
			lex.g_field->fld_dtype = dtype_varying;
			lex.g_field->fld_character_length = (USHORT) $4;
			lex.g_field->fld_flags |= FLD_national;
		}
	;

character_type
	: character_keyword '(' pos_short_integer ')'
		{
			lex.g_field->fld_dtype = dtype_text;
			lex.g_field->fld_character_length = (USHORT) $3;
		}
	| character_keyword
		{
			lex.g_field->fld_dtype = dtype_text;
			lex.g_field->fld_character_length = 1;
		}
	| varying_keyword '(' pos_short_integer ')'
		{
			lex.g_field->fld_dtype = dtype_varying;
			lex.g_field->fld_character_length = (USHORT) $3;
		}
	;

varying_keyword
	: VARCHAR
	| CHARACTER VARYING
	| KW_CHAR VARYING
	;

character_keyword
	: CHARACTER
	| KW_CHAR
	;

national_character_keyword
	: NCHAR
	| NATIONAL CHARACTER
	| NATIONAL KW_CHAR
	;


// numeric type

numeric_type
	: KW_NUMERIC prec_scale
		{ lex.g_field->fld_sub_type = dsc_num_type_numeric; }
	| decimal_keyword prec_scale
		{
			lex.g_field->fld_sub_type = dsc_num_type_decimal;
			if (lex.g_field->fld_dtype == dtype_short)
			{
				lex.g_field->fld_dtype = dtype_long;
				lex.g_field->fld_length = sizeof (SLONG);
			}
		}
	;

prec_scale
	: // nothing
		{
			lex.g_field->fld_dtype = dtype_long;
			lex.g_field->fld_length = sizeof (SLONG);
			lex.g_field->fld_precision = 9;
		}
	| '(' signed_long_integer ')'
		{
			if ($2 < 1 || $2 > 18)
				yyabandon(-842, isc_precision_err);	// Precision must be between 1 and 18.
			if ($2 > 9)
			{
				if ( ( (client_dialect <= SQL_DIALECT_V5) && (db_dialect > SQL_DIALECT_V5) ) ||
					( (client_dialect > SQL_DIALECT_V5) && (db_dialect <= SQL_DIALECT_V5) ) )
				{
					ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-817) <<
							  Arg::Gds(isc_ddl_not_allowed_by_db_sql_dial) << Arg::Num(db_dialect));
				}
				if (client_dialect <= SQL_DIALECT_V5)
				{
					lex.g_field->fld_dtype = dtype_double;
					lex.g_field->fld_length = sizeof (double);
				}
				else
				{
					if (client_dialect == SQL_DIALECT_V6_TRANSITION)
					{
						ERRD_post_warning(Arg::Warning(isc_dsql_warn_precision_ambiguous));
						ERRD_post_warning(Arg::Warning(isc_dsql_warn_precision_ambiguous1));
						ERRD_post_warning(Arg::Warning(isc_dsql_warn_precision_ambiguous2));
					}
					lex.g_field->fld_dtype = dtype_int64;
					lex.g_field->fld_length = sizeof (SINT64);
				}
			}
			else
			{
				if ($2 < 5)
				{
					lex.g_field->fld_dtype = dtype_short;
					lex.g_field->fld_length = sizeof (SSHORT);
				}
				else
				{
					lex.g_field->fld_dtype = dtype_long;
					lex.g_field->fld_length = sizeof (SLONG);
				}
			}
			lex.g_field->fld_precision = (USHORT) $2;
		}
	| '(' signed_long_integer ',' signed_long_integer ')'
		{
			if ($2 < 1 || $2 > 18)
				yyabandon (-842, isc_precision_err);	// Precision should be between 1 and 18
			if ($4 > $2 || $4 < 0)
				yyabandon (-842, isc_scale_nogt);	// Scale must be between 0 and precision
			if ($2 > 9)
			{
				if ( ( (client_dialect <= SQL_DIALECT_V5) && (db_dialect > SQL_DIALECT_V5) ) ||
					( (client_dialect > SQL_DIALECT_V5) && (db_dialect <= SQL_DIALECT_V5) ) )
				{
					ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-817) <<
							  Arg::Gds(isc_ddl_not_allowed_by_db_sql_dial) << Arg::Num(db_dialect));
				}
				if (client_dialect <= SQL_DIALECT_V5)
				{
					lex.g_field->fld_dtype = dtype_double;
					lex.g_field->fld_length = sizeof (double);
				}
				else
				{
					if (client_dialect == SQL_DIALECT_V6_TRANSITION)
					{
						ERRD_post_warning(Arg::Warning(isc_dsql_warn_precision_ambiguous));
						ERRD_post_warning(Arg::Warning(isc_dsql_warn_precision_ambiguous1));
						ERRD_post_warning(Arg::Warning(isc_dsql_warn_precision_ambiguous2));
					}
					// client_dialect >= SQL_DIALECT_V6
					lex.g_field->fld_dtype = dtype_int64;
					lex.g_field->fld_length = sizeof (SINT64);
				}
			}
			else
			{
				if ($2 < 5)
				{
					lex.g_field->fld_dtype = dtype_short;
					lex.g_field->fld_length = sizeof (SSHORT);
				}
				else
				{
					lex.g_field->fld_dtype = dtype_long;
					lex.g_field->fld_length = sizeof (SLONG);
				}
			}
			lex.g_field->fld_precision = (USHORT) $2;
			lex.g_field->fld_scale = - (SSHORT) $4;
		}
	;

decimal_keyword
	: DECIMAL
	| KW_DEC
	;


// floating point type

float_type
	: KW_FLOAT precision_opt
		{
			if ($2 > 7)
			{
				lex.g_field->fld_dtype = dtype_double;
				lex.g_field->fld_length = sizeof(double);
			}
			else
			{
				lex.g_field->fld_dtype = dtype_real;
				lex.g_field->fld_length = sizeof(float);
			}
		}
	| KW_LONG KW_FLOAT precision_opt
		{
			lex.g_field->fld_dtype = dtype_double;
			lex.g_field->fld_length = sizeof(double);
		}
	| REAL
		{
			lex.g_field->fld_dtype = dtype_real;
			lex.g_field->fld_length = sizeof(float);
		}
	| KW_DOUBLE PRECISION
		{
			lex.g_field->fld_dtype = dtype_double;
			lex.g_field->fld_length = sizeof(double);
		}
	;

%type <int32Val> precision_opt
precision_opt
	: /* nothing */					{ $$ = 0; }
	| '(' nonneg_short_integer ')'	{ $$ = $2; }
	;


%type <stmtNode> set_generator
set_generator
	: SET GENERATOR symbol_generator_name TO signed_long_integer
		{ $$ = newNode<SetGeneratorNode>(toName($3), MAKE_const_slong($5)); }
	| SET GENERATOR symbol_generator_name TO NUMBER64BIT
		{
			$$ = newNode<SetGeneratorNode>(toName($3),
				MAKE_constant((dsql_str*) $5, CONSTANT_SINT64));
		}
	| SET GENERATOR symbol_generator_name TO '-' NUMBER64BIT
		{
			$$ = newNode<SetGeneratorNode>(toName($3),
				newNode<NegateNode>(MAKE_constant((dsql_str*) $6, CONSTANT_SINT64)));
		}
	;


// transaction statements

%type <stmtNode> savepoint
savepoint
	: set_savepoint
	| release_savepoint
	| undo_savepoint
	;

%type <stmtNode> set_savepoint
set_savepoint
	: SAVEPOINT symbol_savepoint_name
		{
			UserSavepointNode* node = newNode<UserSavepointNode>();
			node->command = UserSavepointNode::CMD_SET;
			node->name = toName($2);
			$$ = node;
		}
	;

%type <stmtNode> release_savepoint
release_savepoint
	: RELEASE SAVEPOINT symbol_savepoint_name release_only_opt
		{
			UserSavepointNode* node = newNode<UserSavepointNode>();
			node->command = ($4 ? UserSavepointNode::CMD_RELEASE_ONLY : UserSavepointNode::CMD_RELEASE);
			node->name = toName($3);
			$$ = node;
		}
	;

%type <boolVal> release_only_opt
release_only_opt
	: /* nothing */		{ $$ = false; }
	| ONLY				{ $$ = true; }
	;

%type <stmtNode> undo_savepoint
undo_savepoint
	: ROLLBACK optional_work TO optional_savepoint symbol_savepoint_name
		{
			UserSavepointNode* node = newNode<UserSavepointNode>();
			node->command = UserSavepointNode::CMD_ROLLBACK;
			node->name = toName($5);
			$$ = node;
		}
	;

optional_savepoint
	: // nothing
	| SAVEPOINT
	;

%type <traNode> commit
commit
	: COMMIT optional_work optional_retain
		{ $$ = newNode<CommitRollbackNode>(CommitRollbackNode::CMD_COMMIT, $3); }
	;

%type <traNode> rollback
rollback
	: ROLLBACK optional_work optional_retain
		{ $$ = newNode<CommitRollbackNode>(CommitRollbackNode::CMD_ROLLBACK, $3); }
	;

optional_work
	: // nothing
	| WORK
	;

%type <boolVal>	optional_retain
optional_retain
	: /* nothing */		 	{ $$ = false; }
	| RETAIN opt_snapshot	{ $$ = true; }
	;

opt_snapshot
	: // nothing
	| SNAPSHOT
	;

%type <setTransactionNode> set_transaction
set_transaction
	: SET TRANSACTION
			{ $$ = newNode<SetTransactionNode>(); }
		tran_option_list_opt($3)
			{ $$ = $3; }
	;

%type tran_option_list_opt(<setTransactionNode>)
tran_option_list_opt($setTransactionNode)
	: // nothing
	| tran_option_list($setTransactionNode)
	;

%type tran_option_list(<setTransactionNode>)
tran_option_list($setTransactionNode)
	: tran_option($setTransactionNode)
	| tran_option_list tran_option($setTransactionNode)
	;

%type tran_option(<setTransactionNode>)
tran_option($setTransactionNode)
	// access mode
	: READ ONLY
		{ setClause($setTransactionNode->readOnly, "READ {ONLY | WRITE}", true); }
	| READ WRITE
		{ setClause($setTransactionNode->readOnly, "READ {ONLY | WRITE}", false); }
	// wait mode
	| WAIT
		{ setClause($setTransactionNode->wait, "[NO] WAIT", true); }
	| NO WAIT
		{ setClause($setTransactionNode->wait, "[NO] WAIT", false); }
	// isolation mode
	| isolation_mode
		{ setClause($setTransactionNode->isoLevel, "ISOLATION LEVEL", $1); }
	// misc options
	| NO AUTO UNDO
		{ setClause($setTransactionNode->noAutoUndo, "NO AUTO UNDO", true); }
	| KW_IGNORE LIMBO
		{ setClause($setTransactionNode->ignoreLimbo, "IGNORE LIMBO", true); }
	| RESTART REQUESTS
		{ setClause($setTransactionNode->restartRequests, "RESTART REQUESTS", true); }
	// timeout
	| LOCK TIMEOUT nonneg_short_integer
		{ setClause($setTransactionNode->lockTimeout, "LOCK TIMEOUT", (USHORT) $3); }
	// reserve options
	| RESERVING
			{ checkDuplicateClause($setTransactionNode->reserveList, "RESERVING"); }
		restr_list($setTransactionNode)
	;

%type <uintVal>	isolation_mode
isolation_mode
	: ISOLATION LEVEL iso_mode	{ $$ = $3;}
	| iso_mode
	;

%type <uintVal>	iso_mode
iso_mode
	: snap_shot
	| READ UNCOMMITTED version_mode		{ $$ = $3; }
	| READ COMMITTED version_mode		{ $$ = $3; }
	;

%type <uintVal>	snap_shot
snap_shot
	: SNAPSHOT					{ $$ = SetTransactionNode::ISO_LEVEL_CONCURRENCY; }
	| SNAPSHOT TABLE			{ $$ = SetTransactionNode::ISO_LEVEL_CONSISTENCY; }
	| SNAPSHOT TABLE STABILITY	{ $$ = SetTransactionNode::ISO_LEVEL_CONSISTENCY; }
	;

%type <uintVal>	version_mode
version_mode
	: /* nothing */	{ $$ = SetTransactionNode::ISO_LEVEL_READ_COMMITTED_NO_REC_VERSION; }
	| VERSION		{ $$ = SetTransactionNode::ISO_LEVEL_READ_COMMITTED_REC_VERSION; }
	| NO VERSION	{ $$ = SetTransactionNode::ISO_LEVEL_READ_COMMITTED_NO_REC_VERSION; }
	;

%type <uintVal> lock_type
lock_type
	: /* nothing */		{ $$ = 0; }
	| KW_SHARED			{ $$ = SetTransactionNode::LOCK_MODE_SHARED; }
	| PROTECTED			{ $$ = SetTransactionNode::LOCK_MODE_PROTECTED; }
	;

%type <uintVal> lock_mode
lock_mode
	: READ		{ $$ = SetTransactionNode::LOCK_MODE_READ; }
	| WRITE		{ $$ = SetTransactionNode::LOCK_MODE_WRITE; }
	;

%type restr_list(<setTransactionNode>)
restr_list($setTransactionNode)
	: restr_option
		{ $setTransactionNode->reserveList.add($1); }
	| restr_list ',' restr_option
		{ $setTransactionNode->reserveList.add($3); }
	;

%type <setTransactionRestrictionClause> restr_option
restr_option
	: table_list table_lock
		{ $$ = newNode<SetTransactionNode::RestrictionOption>($1, $2); }
	;

%type <uintVal> table_lock
table_lock
	: /* nothing */				{ $$ = 0; }
	| FOR lock_type lock_mode	{ $$ = $2 | $3; }
	;

%type <metaNameArray> table_list
table_list
	: symbol_table_name
		{
			ObjectsArray<MetaName>* node = newNode<ObjectsArray<MetaName> >();
			node->add(toName($1));
			$$ = node;
		}
	| table_list ',' symbol_table_name
		{
			ObjectsArray<MetaName>* node = $1;
			node->add(toName($3));
			$$ = node;
		}
	;


%type <ddlNode>	set_statistics
set_statistics
	: SET STATISTICS INDEX symbol_index_name
		{ $$ = newNode<SetStatisticsNode>(toName($4)); }
	;

%type <ddlNode> comment
comment
	: COMMENT ON ddl_type0 IS ddl_desc
		{
			$$ = newNode<CommentOnNode>($3,
				"", "", ($5 ? toString($5) : ""), ($5 ? $5->str_charset : NULL));
		}
	| COMMENT ON ddl_type1 symbol_ddl_name IS ddl_desc
		{
			$$ = newNode<CommentOnNode>($3,
				toName($4), "", ($6 ? toString($6) : ""), ($6 ? $6->str_charset : NULL));
		}
	| COMMENT ON ddl_type2 symbol_ddl_name ddl_subname IS ddl_desc
		{
			$$ = newNode<CommentOnNode>($3,
				toName($4), toName($5), ($7 ? toString($7) : ""), ($7 ? $7->str_charset : NULL));
		}
	;

%type <intVal> ddl_type0
ddl_type0
	: DATABASE
		{ $$ = ddl_database; }
	;

%type <intVal> ddl_type1
ddl_type1
	: DOMAIN				{ $$ = ddl_domain; }
	| TABLE					{ $$ = ddl_relation; }
	| VIEW					{ $$ = ddl_view; }
	| PROCEDURE				{ $$ = ddl_procedure; }
	| TRIGGER				{ $$ = ddl_trigger; }
	| EXTERNAL FUNCTION		{ $$ = ddl_udf; }
	| FUNCTION				{ $$ = ddl_udf; }
	| FILTER				{ $$ = ddl_blob_filter; }
	| EXCEPTION				{ $$ = ddl_exception; }
	| GENERATOR				{ $$ = ddl_generator; }
	| SEQUENCE				{ $$ = ddl_generator; }
	| INDEX					{ $$ = ddl_index; }
	| ROLE					{ $$ = ddl_role; }
	| CHARACTER SET			{ $$ = ddl_charset; }
	| COLLATION				{ $$ = ddl_collation; }
	| PACKAGE				{ $$ = ddl_package; }
	/***
	| SECURITY CLASS		{ $$ = ddl_sec_class; }
	***/
	;

%type <intVal> ddl_type2
ddl_type2
	: COLUMN					{ $$ = ddl_relation; }
	| ddl_param_opt PARAMETER	{ $$ = $1; }
	;

%type <intVal> ddl_param_opt
ddl_param_opt
	:			{ $$ = ddl_unknown; }
	| PROCEDURE	{ $$ = ddl_procedure; }
	| FUNCTION	{ $$ = ddl_udf; }
	;

%type <legacyStr> ddl_subname
ddl_subname
	: '.' symbol_ddl_name	{ $$ = $2; }
	;

%type <legacyStr> ddl_desc
ddl_desc
    : sql_string
	| KW_NULL		{ $$ = NULL; }
	;


// SELECT statement

%type <selectNode> select
select
	: select_expr for_update_clause lock_clause
		{
			SelectNode* node = newNode<SelectNode>();
			node->dsqlExpr = $1;
			node->dsqlForUpdate = $2;
			node->dsqlWithLock = $3;
			$$ = node;
		}
	;

%type <boolVal>	for_update_clause
for_update_clause
	: /* nothing */					{ $$ = false; }
	| FOR UPDATE for_update_list	{ $$ = true; /* for_update_list is ignored */ }
	;

%type <valueListNode> for_update_list
for_update_list
	: /* nothing */		{ $$ = NULL; }
	| OF column_list	{ $$ = $2; }
	;

%type <boolVal>	lock_clause
lock_clause
	: /* nothing */	{ $$ = false; }
	| WITH LOCK		{ $$ = true; }
	;


// SELECT expression

%type <selectExprNode> select_expr
select_expr
	: with_clause select_expr_body order_clause rows_clause
		{
			SelectExprNode* node = $$ = newNode<SelectExprNode>();
			node->querySpec = $2;
			node->orderClause = $3;
			node->rowsClause = $4;
			node->withClause = $1;
		}
	;

%type <withClause> with_clause
with_clause
	: // nothing
		{ $$ = NULL; }
	| WITH RECURSIVE with_list
		{
			$$ = $3;
			$$->recursive = true;
		}
	| WITH with_list
		{ $$ = $2; }
	;

%type <withClause> with_list
with_list
	: with_item
		{
			$$ = newNode<WithClause>();
			$$->add($1);
		}
	| with_item ',' with_list
		{
			$$ = $3;
			$$->add($1);
		}
	;

%type <selectExprNode> with_item
with_item
	: symbol_table_alias_name derived_column_list AS '(' select_expr ')'
		{
			$$ = $5;
			$$->dsqlFlags |= RecordSourceNode::DFLAG_DERIVED;
			$$->alias = toString((dsql_str*) $1);
			$$->columns = $2;
		}
	;

%type <selectExprNode> column_select
column_select
	: select_expr
		{
			$$ = $1;
			$$->dsqlFlags |= RecordSourceNode::DFLAG_VALUE;
		}
	;

%type <valueExprNode> column_singleton
column_singleton
	: column_select
		{
			$1->dsqlFlags |= RecordSourceNode::DFLAG_SINGLETON;
			$$ = newNode<SubQueryNode>(blr_via, $1);
		}
	;

%type <recSourceNode> select_expr_body
select_expr_body
	: query_term
		{ $$ = $1; }
	| select_expr_body UNION distinct_noise query_term
		{
			UnionSourceNode* node = newNode<UnionSourceNode>();
			node->dsqlClauses = newNode<RecSourceListNode>($1)->add($4);
			$$ = node;
		}
	| select_expr_body UNION ALL query_term
		{
			UnionSourceNode* node = newNode<UnionSourceNode>();
			node->dsqlAll = true;
			node->dsqlClauses = newNode<RecSourceListNode>($1)->add($4);
			$$ = node;
		}
	;

%type <rseNode> query_term
query_term
	: query_spec
	;

%type <rseNode> query_spec
query_spec
	: SELECT limit_clause
			 distinct_clause
			 select_list
			 from_clause
			 where_clause
			 group_clause
			 having_clause
			 plan_clause
		{
			RseNode* rse = newNode<RseNode>();
			rse->dsqlFirst = $2 ? $2->items[1] : NULL;
			rse->dsqlSkip = $2 ? $2->items[0] : NULL;
			rse->dsqlDistinct = $3;
			rse->dsqlSelectList = $4;
			rse->dsqlFrom = $5;
			rse->dsqlWhere = $6;
			rse->dsqlGroup = $7;
			rse->dsqlHaving = $8;
			rse->rse_plan = $9;
			$$ = rse;
		}
	;

%type <valueListNode> limit_clause
limit_clause
	: /* nothing */				{ $$ = NULL; }
	| first_clause skip_clause	{ $$ = newNode<ValueListNode>($2)->add($1); }
	| first_clause				{ $$ = newNode<ValueListNode>(1u)->add($1); }
	| skip_clause				{ $$ = newNode<ValueListNode>($1)->add(NULL); }
	;

%type <valueExprNode> first_clause
first_clause
	: FIRST long_integer	{ $$ = MAKE_const_slong($2); }
	| FIRST '(' value ')'	{ $$ = $3; }
	| FIRST parameter		{ $$ = $2; }
	;

%type <valueExprNode> skip_clause
skip_clause
	: SKIP long_integer		{ $$ = MAKE_const_slong($2); }
	| SKIP '(' value ')'	{ $$ = $3; }
	| SKIP parameter		{ $$ = $2; }
	;

%type <valueListNode> distinct_clause
distinct_clause
	: DISTINCT		{ $$ = newNode<ValueListNode>(0); }
	| all_noise		{ $$ = NULL; }
	;

%type <valueListNode> select_list
select_list
	: select_items	{ $$ = $1; }
	| '*'			{ $$ = NULL; }
	;

%type <valueListNode> select_items
select_items
	: select_item					{ $$ = newNode<ValueListNode>($1); }
	| select_items ',' select_item	{ $$ = $1->add($3); }
	;

%type <valueExprNode> select_item
select_item
	: value
	| value as_noise symbol_item_alias_name
		{ $$ = newNode<DsqlAliasNode>(toName($3), $1); }
	| symbol_table_alias_name '.' '*'
		{
			FieldNode* fieldNode = newNode<FieldNode>();
			fieldNode->dsqlQualifier = toName($1);
			$$ = fieldNode;
		}
	;

as_noise
	:
	| AS
	;

// FROM clause

%type <recSourceListNode> from_clause
from_clause
	: FROM from_list	{ $$ = $2; }
	;

%type <recSourceListNode> from_list
from_list
	: table_reference					{ $$ = newNode<RecSourceListNode>($1); }
	| from_list ',' table_reference		{ $$ = $1->add($3); }
	;

%type <recSourceNode> table_reference
table_reference
	: joined_table
	| table_primary
	;

%type <recSourceNode> table_primary
table_primary
	: table_proc
	| derived_table			{ $$ = $1; }
	| '(' joined_table ')'	{ $$ = $2; }
	;

%type <selectExprNode> derived_table
derived_table
	: '(' select_expr ')' as_noise correlation_name derived_column_list
		{
			$$ = $2;
			$$->dsqlFlags |= RecordSourceNode::DFLAG_DERIVED;
			$$->alias = toString((dsql_str*) $5);
			$$->columns = $6;
		}
	;

%type <legacyStr> correlation_name
correlation_name
	: /* nothing */				{ $$ = NULL; }
	| symbol_table_alias_name
	;

%type <metaNameArray> derived_column_list
derived_column_list
	: /* nothing */			{ $$ = NULL; }
	| '(' alias_list ')'	{ $$ = $2; }
	;

%type <metaNameArray> alias_list
alias_list
	: symbol_item_alias_name
		{
			ObjectsArray<MetaName>* node = newNode<ObjectsArray<MetaName> >();
			node->add(toName($1));
			$$ = node;
		}
	| alias_list ',' symbol_item_alias_name
		{
			ObjectsArray<MetaName>* node = $1;
			node->add(toName($3));
			$$ = node;
		}
	;

%type <recSourceNode> joined_table
joined_table
	: cross_join
	| natural_join
	| qualified_join
	;

%type <recSourceNode> cross_join
cross_join
	: table_reference CROSS JOIN table_primary
		{
			RseNode* rse = newNode<RseNode>();
			rse->dsqlExplicitJoin = true;
			rse->rse_jointype = blr_inner;
			rse->dsqlFrom = newNode<RecSourceListNode>($1)->add($4);
			$$ = rse;
		}
	;

%type <recSourceNode> natural_join
natural_join
	: table_reference NATURAL join_type JOIN table_primary
		{
			RseNode* rse = newNode<RseNode>();
			rse->dsqlExplicitJoin = true;
			rse->rse_jointype = $3;
			rse->dsqlFrom = newNode<RecSourceListNode>($1)->add($5);
			rse->dsqlJoinUsing = newNode<ValueListNode>(0u);	// using list with size 0 -> natural
			$$ = rse;
		}
	;

%type <recSourceNode> qualified_join
qualified_join
	: table_reference join_type JOIN table_reference join_condition
		{
			RseNode* rse = newNode<RseNode>();
			rse->dsqlExplicitJoin = true;
			rse->rse_jointype = $2;
			rse->dsqlFrom = newNode<RecSourceListNode>($1);
			rse->dsqlFrom->add($4);
			rse->dsqlWhere = $5;
			$$ = rse;
		}
	| table_reference join_type JOIN table_reference named_columns_join
		{
			RseNode* rse = newNode<RseNode>();
			rse->dsqlExplicitJoin = true;
			rse->rse_jointype = $2;
			rse->dsqlFrom = newNode<RecSourceListNode>($1);
			rse->dsqlFrom->add($4);
			rse->dsqlJoinUsing = $5;
			$$ = rse;
		}
	;

%type <boolExprNode> join_condition
join_condition
	: ON search_condition	{ $$ = $2; }
	;

%type <valueListNode> named_columns_join
named_columns_join
	: USING '(' column_list ')'		{ $$ = $3; }
	;

%type <recSourceNode> table_proc
table_proc
	: symbol_procedure_name table_proc_inputs as_noise symbol_table_alias_name
		{
			ProcedureSourceNode* node = newNode<ProcedureSourceNode>(QualifiedName(toName($1)));
			node->sourceList = $2;
			node->alias = toString((dsql_str*) $4);
			$$ = node;
		}
	| symbol_procedure_name table_proc_inputs
		{
			ProcedureSourceNode* node = newNode<ProcedureSourceNode>(QualifiedName(toName($1)));
			node->sourceList = $2;
			$$ = node;
		}
	| symbol_package_name '.' symbol_procedure_name table_proc_inputs as_noise symbol_table_alias_name
		{
			ProcedureSourceNode* node = newNode<ProcedureSourceNode>(
				QualifiedName(toName($3), toName($1)));
			node->sourceList = $4;
			node->alias = toString((dsql_str*) $6);
			$$ = node;
		}
	| symbol_package_name '.' symbol_procedure_name table_proc_inputs
		{
			ProcedureSourceNode* node = newNode<ProcedureSourceNode>(
				QualifiedName(toName($3), toName($1)));
			node->sourceList = $4;
			$$ = node;
		}
	;

%type <valueListNode> table_proc_inputs
table_proc_inputs
	: /* nothing */			{ $$ = NULL; }
	| '(' value_list ')'	{ $$ = $2; }
	;

%type <relSourceNode> table_name
table_name
	: simple_table_name
	| symbol_table_name as_noise symbol_table_alias_name
		{
			RelationSourceNode* node = newNode<RelationSourceNode>(toName($1));
			node->alias = toString((dsql_str*) $3);
			$$ = node;
		}
	;

%type <relSourceNode> simple_table_name
simple_table_name
	: symbol_table_name
		{ $$ = newNode<RelationSourceNode>(toName($1)); }
	;

%type <blrOp> join_type
join_type
	: /* nothing */		{ $$ = blr_inner; }
	| INNER				{ $$ = blr_inner; }
	| LEFT outer_noise	{ $$ = blr_left; }
	| RIGHT outer_noise	{ $$ = blr_right; }
	| FULL outer_noise	{ $$ = blr_full; }

	;

outer_noise
	:
	| OUTER
	;


// other clauses in the select expression

%type <valueListNode> group_clause
group_clause
	: /* nothing */				{ $$ = NULL; }
	| GROUP BY group_by_list	{ $$ = $3; }
	;

%type <valueListNode> group_by_list
group_by_list
	: group_by_item						{ $$ = newNode<ValueListNode>($1); }
	| group_by_list ',' group_by_item	{ $$ = $1->add($3); }
	;

// Except aggregate-functions are all expressions supported in group_by_item,
// they are caught inside pass1.cpp
%type <valueExprNode> group_by_item
group_by_item
	: value
	;

%type <boolExprNode> having_clause
having_clause
	: /* nothing */				{ $$ = NULL; }
	| HAVING search_condition	{ $$ = $2; }
	;

%type <boolExprNode> where_clause
where_clause
	: /* nothing */				{ $$ = NULL; }
	| WHERE search_condition	{ $$ = $2; }
	;


// PLAN clause to specify an access plan for a query

%type <planNode> plan_clause
plan_clause
	: /* nothing */			{ $$ = NULL; }
	| PLAN plan_expression	{ $$ = $2; }
	;

%type <planNode> plan_expression
plan_expression
	: plan_type { $$ = newNode<PlanNode>(PlanNode::TYPE_RETRIEVE); } '(' plan_item_list($2) ')'
		{ $$ = $2; }
	;

plan_type
	: // nothing
	| JOIN
	| SORT MERGE
	| MERGE
	| HASH
	| SORT
	;

%type plan_item_list(<planNode>)
plan_item_list($planNode)
	: plan_item						{ $planNode->subNodes.add($1); }
	| plan_item_list ',' plan_item	{ $planNode->subNodes.insert(0, $3); }
	;

%type <planNode> plan_item
plan_item
	: table_or_alias_list access_type
		{
			$$ = newNode<PlanNode>(PlanNode::TYPE_RETRIEVE);
			$$->dsqlNames = $1;
			$$->accessType = $2;
		}
	| plan_expression
	;

%type <metaNameArray> table_or_alias_list
table_or_alias_list
	: symbol_table_name
		{
			ObjectsArray<MetaName>* node = newNode<ObjectsArray<MetaName> >();
			node->add(toName($1));
			$$ = node;
		}
	| table_or_alias_list symbol_table_name
		{
			ObjectsArray<MetaName>* node = $1;
			node->add(toName($2));
			$$ = node;
		}
	;

%type <accessType> access_type
access_type
	: NATURAL
		{ $$ = newNode<PlanNode::AccessType>(PlanNode::AccessType::TYPE_SEQUENTIAL); }
	| INDEX { $$ = newNode<PlanNode::AccessType>(PlanNode::AccessType::TYPE_INDICES); }
			'(' index_list($2) ')'
		{ $$ = $2; }
	| ORDER { $$ = newNode<PlanNode::AccessType>(PlanNode::AccessType::TYPE_NAVIGATIONAL); }
			symbol_index_name extra_indices_opt($2)
		{
			$$ = $2;
			$$->items.insert(0).indexName = toName($3);
		}
	;

%type index_list(<accessType>)
index_list($accessType)
	: symbol_index_name
		{
			PlanNode::AccessItem& item = $accessType->items.add();
			item.indexName = toName($1);
		}
	| index_list ', ' symbol_index_name
		{
			PlanNode::AccessItem& item = $accessType->items.insert(0);
			item.indexName = toName($3);
		}
	;

%type extra_indices_opt(<accessType>)
extra_indices_opt($accessType)
	: // nothing
	| INDEX '(' index_list($accessType) ')'
	;

// ORDER BY clause

%type <valueListNode> order_clause
order_clause
	: /* nothing */			{ $$ = NULL; }
	| ORDER BY order_list	{ $$ = $3; }
	;

%type <valueListNode> order_list
order_list
	: order_item					{ $$ = newNode<ValueListNode>($1); }
	| order_list ',' order_item		{ $$ = $1->add($3); }
	;

%type <valueExprNode> order_item
order_item
	: value order_direction nulls_clause
		{
			OrderNode* node = newNode<OrderNode>($1);
			node->descending = $2;
			node->nullsPlacement = $3;
			$$ = node;
		}
	;

%type <boolVal>	order_direction
order_direction
	: /* nothing */		{ $$ = false; }
	| ASC				{ $$ = false; }
	| DESC				{ $$ = true; }
	;

%type <nullsPlacement> nulls_clause
nulls_clause
	: /* nothing */				{ $$ = OrderNode::NULLS_DEFAULT; }
	| NULLS nulls_placement		{ $$ = $2; }
	;

%type <nullsPlacement> nulls_placement
nulls_placement
	: FIRST	{ $$ = OrderNode::NULLS_FIRST; }
	| LAST	{ $$ = OrderNode::NULLS_LAST; }
	;

// ROWS clause

%type <rowsClause> rows_clause
rows_clause
	: // nothing
		{ $$ = NULL; }
	// equivalent to FIRST value
	| ROWS value
		{
			$$ = newNode<RowsClause>();
			$$->length = $2;
		}
	// equivalent to FIRST (upper_value - lower_value + 1) SKIP (lower_value - 1)
	| ROWS value TO value
		{
			$$ = newNode<RowsClause>();
			$$->skip = newNode<ArithmeticNode>(blr_subtract, true, $2, MAKE_const_slong(1));
			$$->length = newNode<ArithmeticNode>(blr_add, true,
				newNode<ArithmeticNode>(blr_subtract, true, $4, $2), MAKE_const_slong(1));
		}
	;


// INSERT statement
// IBO hack: replace column_parens_opt by ins_column_parens_opt.
%type <storeNode> insert
insert
	: insert_start ins_column_parens_opt(&$1->dsqlFields) VALUES '(' value_list ')' returning_clause
		{
			StoreNode* node = $$ = $1;
			node->dsqlValues = $5;
			node->dsqlReturning = $7;
		}
	| insert_start ins_column_parens_opt(&$1->dsqlFields) select_expr returning_clause
		{
			StoreNode* node = $$ = $1;
			node->dsqlRse = $3;
			node->dsqlReturning = $4;
			$$ = node;
		}
	| insert_start DEFAULT VALUES returning_clause
		{
			StoreNode* node = $$ = $1;
			node->dsqlReturning = $4;
			$$ = node;
		}
	;

%type <storeNode> insert_start
insert_start
	: INSERT INTO simple_table_name
		{
			StoreNode* node = newNode<StoreNode>();
			node->dsqlRelation = $3;
			$$ = node;
		}
	;


// MERGE statement
%type <mergeNode> merge
merge
	: MERGE INTO table_name USING table_reference ON search_condition
			{
				MergeNode* node = $$ = newNode<MergeNode>();
				node->relation = $3;
				node->usingClause = $5;
				node->condition = $7;
			}
		merge_when_clause($8) returning_clause
			{
				MergeNode* node = $$ = $8;
				node->returning = $10;
			}
	;

%type merge_when_clause(<mergeNode>)
merge_when_clause($mergeNode)
	: merge_when_matched_clause($mergeNode)
	| merge_when_not_matched_clause($mergeNode)
	| merge_when_clause merge_when_matched_clause($mergeNode)
	| merge_when_clause merge_when_not_matched_clause($mergeNode)
	;

%type merge_when_matched_clause(<mergeNode>)
merge_when_matched_clause($mergeNode)
	: WHEN MATCHED
			{ $<mergeMatchedClause>$ = &$mergeNode->whenMatched.add(); }
		merge_update_specification($<mergeMatchedClause>3)
	;

%type merge_when_not_matched_clause(<mergeNode>)
merge_when_not_matched_clause($mergeNode)
	: WHEN NOT MATCHED
			{ $<mergeNotMatchedClause>$ = &$mergeNode->whenNotMatched.add(); }
		merge_insert_specification($<mergeNotMatchedClause>4)
	;

%type merge_update_specification(<mergeMatchedClause>)
merge_update_specification($mergeMatchedClause)
	: THEN UPDATE SET assignments
		{ $mergeMatchedClause->assignments = $4; }
	| AND search_condition THEN UPDATE SET assignments
		{
			$mergeMatchedClause->condition = $2;
			$mergeMatchedClause->assignments = $6;
		}
	| THEN KW_DELETE
	| AND search_condition THEN KW_DELETE
		{ $mergeMatchedClause->condition = $2; }
	;

%type merge_insert_specification(<mergeNotMatchedClause>)
merge_insert_specification($mergeNotMatchedClause)
	: THEN INSERT ins_column_parens_opt(&$mergeNotMatchedClause->fields) VALUES '(' value_list ')'
		{ $mergeNotMatchedClause->values = $6; }
	| AND search_condition THEN INSERT ins_column_parens_opt(&$mergeNotMatchedClause->fields)
			VALUES '(' value_list ')'
		{
			$mergeNotMatchedClause->values = $8;
			$mergeNotMatchedClause->condition = $2;
		}
	;


// DELETE statement

%type <stmtNode> delete
delete
	: delete_searched
	| delete_positioned
	;

%type <stmtNode> delete_searched
delete_searched
	: KW_DELETE FROM table_name where_clause plan_clause order_clause rows_clause returning_clause
		{
			EraseNode* node = newNode<EraseNode>();
			node->dsqlRelation = $3;
			node->dsqlBoolean = $4;
			node->dsqlPlan = $5;
			node->dsqlOrder = $6;
			node->dsqlRows = $7;
			node->dsqlReturning = $8;
			$$ = node;
		}
	;

%type <stmtNode> delete_positioned
delete_positioned
	: KW_DELETE FROM table_name cursor_clause returning_clause
		{
			EraseNode* node = newNode<EraseNode>();
			node->dsqlRelation = $3;
			node->dsqlCursorName = *$4;
			node->dsqlReturning = $5;
			$$ = node;
		}
	;


// UPDATE statement

%type <stmtNode> update
update
	: update_searched
	| update_positioned
	;

%type <stmtNode> update_searched
update_searched
	: UPDATE table_name SET assignments where_clause plan_clause
			order_clause rows_clause returning_clause
		{
			ModifyNode* node = newNode<ModifyNode>();
			node->dsqlRelation = $2;
			node->statement = $4;
			node->dsqlBoolean = $5;
			node->dsqlPlan = $6;
			node->dsqlOrder = $7;
			node->dsqlRows = $8;
			node->dsqlReturning = $9;
			$$ = node;
		}
	;

%type <stmtNode> update_positioned
update_positioned
	: UPDATE table_name SET assignments cursor_clause returning_clause
		{
			ModifyNode* node = newNode<ModifyNode>();
			node->dsqlRelation = $2;
			node->statement = $4;
			node->dsqlCursorName = *$5;
			node->dsqlReturning = $6;
			$$ = node;
		}
	;


// UPDATE OR INSERT statement

%type <updInsNode> update_or_insert
update_or_insert
	: UPDATE OR INSERT INTO simple_table_name
			{
				UpdateOrInsertNode* node = $$ = newNode<UpdateOrInsertNode>();
				node->relation = $5;
			}
		ins_column_parens_opt(&$6->fields)
		VALUES '(' value_list ')' update_or_insert_matching_opt(&$6->matching) returning_clause
			{
				UpdateOrInsertNode* node = $$ = $6;
				node->values = $10;
				node->returning = $13;
			}
	;

%type update_or_insert_matching_opt(<nestFieldArray>)
update_or_insert_matching_opt($fieldArray)
	: // nothing
	| MATCHING ins_column_parens($fieldArray)
	;


%type <returningClause> returning_clause
returning_clause
	: /* nothing */
		{ $$ = NULL; }
	| RETURNING value_list
		{
			$$ = FB_NEW(getPool()) ReturningClause(getPool());
			$$->first = $2;
		}
	| RETURNING value_list INTO variable_list
		{
			$$ = FB_NEW(getPool()) ReturningClause(getPool());
			$$->first = $2;
			$$->second = $4;
		}
	;

%type <metaNamePtr> cursor_clause
cursor_clause
	: WHERE CURRENT OF symbol_cursor_name	{ $$ = newNode<MetaName>(toName($4)); }
	;


// Assignments

%type <compoundStmtNode> assignments
assignments
	: assignment
		{
			$$ = newNode<CompoundStmtNode>();
			$$->statements.add($1);
		}
	| assignments ',' assignment
		{
			$1->statements.add($3);
			$$ = $1;
		}
	;

%type <stmtNode> assignment
assignment
	: update_column_name '=' value
		{
			AssignmentNode* node = newNode<AssignmentNode>();
			node->asgnTo = $1;
			node->asgnFrom = $3;
			$$ = node;
		}
	;

%type <stmtNode> exec_function
exec_function
	: udf
		{
			AssignmentNode* node = newNode<AssignmentNode>();
			node->asgnTo = newNode<NullNode>();
			node->asgnFrom = $1;
			$$ = node;
		}
	| non_aggregate_function
		{
			AssignmentNode* node = newNode<AssignmentNode>();
			node->asgnTo = newNode<NullNode>();
			node->asgnFrom = $1;
			$$ = node;
		}
	;


// column specifications

%type <valueListNode> column_parens_opt
column_parens_opt
	: /* nothing */		{ $$ = NULL; }
	| column_parens
	;

%type <valueListNode> column_parens
column_parens
	: '(' column_list ')'	{ $$ = $2; }
	;

%type <valueListNode> column_list
column_list
	: simple_column_name					{ $$ = newNode<ValueListNode>($1); }
	| column_list ',' simple_column_name	{ $$ = $1->add($3); }
	;

// begin IBO hack
%type ins_column_parens_opt(<nestFieldArray>)
ins_column_parens_opt($fieldArray)
	: // nothing
	| ins_column_parens($fieldArray)
	;

%type ins_column_parens(<nestFieldArray>)
ins_column_parens($fieldArray)
	: '(' ins_column_list($fieldArray) ')'
	;

%type ins_column_list(<nestFieldArray>)
ins_column_list($fieldArray)
	: update_column_name						{ $fieldArray->add($1); }
	| ins_column_list ',' update_column_name	{ $fieldArray->add($3); }
	;
// end IBO hack

%type <fieldNode> column_name
column_name
	: simple_column_name
	| symbol_table_alias_name '.' symbol_column_name
		{
			FieldNode* fieldNode = newNode<FieldNode>();
			fieldNode->dsqlQualifier = toName($1);
			fieldNode->dsqlName = toName($3);
			$$ = fieldNode;
		}
	;

%type <fieldNode> simple_column_name
simple_column_name
	: symbol_column_name
		{
			FieldNode* fieldNode = newNode<FieldNode>();
			fieldNode->dsqlName = toName($1);
			$$ = fieldNode;
		}
	;

%type <fieldNode> update_column_name
update_column_name
	: simple_column_name
	// CVC: This option should be deprecated! The only allowed syntax should be
	// Update...set column = expr, without qualifier for the column.
	| symbol_table_alias_name '.' symbol_column_name
		{
			FieldNode* fieldNode = newNode<FieldNode>();
			fieldNode->dsqlQualifier = toName($1);
			fieldNode->dsqlName = toName($3);
			$$ = fieldNode;
		}
	;

// boolean expressions

%type <boolExprNode> search_condition
search_condition
	: value
		{
			BoolAsValueNode* node = ExprNode::as<BoolAsValueNode>($1);
			if (node)
				$$ = node->boolean;
			else
			{
				ComparativeBoolNode* cmpNode = newNode<ComparativeBoolNode>(
					blr_eql, $1, MAKE_constant(MAKE_string("1", 1), CONSTANT_BOOLEAN));
				cmpNode->dsqlWasValue = true;
				$$ = cmpNode;
			}
		}
	;

%type <boolExprNode> boolean_value_expression
boolean_value_expression
	: predicate
	| search_condition OR search_condition
		{ $$ = newNode<BinaryBoolNode>(blr_or, $1, $3); }
	| search_condition AND search_condition
		{ $$ = newNode<BinaryBoolNode>(blr_and, $1, $3); }
	| NOT search_condition
		{ $$ = newNode<NotBoolNode>($2); }
	| '(' boolean_value_expression ')'
		{ $$ = $2; }
	;

%type <boolExprNode> predicate
predicate
	: comparison_predicate
	| distinct_predicate
	| between_predicate
	| binary_pattern_predicate
	| ternary_pattern_predicate
	| in_predicate
	| null_predicate
	| quantified_predicate
	| exists_predicate
	| singular_predicate
	| common_value IS boolean_literal
		{ $$ = newNode<ComparativeBoolNode>(blr_equiv, $1, $3); }
	| common_value IS NOT boolean_literal
		{
			ComparativeBoolNode* node = newNode<ComparativeBoolNode>(blr_equiv, $1, $4);
			$$ = newNode<NotBoolNode>(node);
		}
	;


// comparisons

%type <boolExprNode> comparison_predicate
comparison_predicate
	: common_value comparison_operator common_value
		{ $$ = newNode<ComparativeBoolNode>($2, $1, $3); }
	;

%type <blrOp> comparison_operator
comparison_operator
	: '='		{ $$ = blr_eql; }
	| '<'		{ $$ = blr_lss; }
	| '>'		{ $$ = blr_gtr; }
	| GEQ		{ $$ = blr_geq; }
	| LEQ		{ $$ = blr_leq; }
	| NOT_GTR	{ $$ = blr_leq; }
	| NOT_LSS	{ $$ = blr_geq; }
	| NEQ		{ $$ = blr_neq; }

// quantified comparisons

%type <boolExprNode> quantified_predicate
quantified_predicate
	: common_value comparison_operator quantified_flag '(' column_select ')'
		{
			ComparativeBoolNode* node = newNode<ComparativeBoolNode>($2, $1);
			node->dsqlFlag = $3;
			node->dsqlSpecialArg = $5;
			$$ = node;
		}
	;

%type <cmpBoolFlag> quantified_flag
quantified_flag
	: ALL	{ $$ = ComparativeBoolNode::DFLAG_ANSI_ALL; }
	| SOME	{ $$ = ComparativeBoolNode::DFLAG_ANSI_ANY; }
	| ANY	{ $$ = ComparativeBoolNode::DFLAG_ANSI_ANY; }
	;


// other predicates

%type <boolExprNode> distinct_predicate
distinct_predicate
	: common_value IS DISTINCT FROM common_value
		{
			ComparativeBoolNode* node = newNode<ComparativeBoolNode>(blr_equiv, $1, $5);
			$$ = newNode<NotBoolNode>(node);
		}
	| common_value IS NOT DISTINCT FROM common_value
		{ $$ = newNode<ComparativeBoolNode>(blr_equiv, $1, $6); }
	;

%type <boolExprNode> between_predicate
between_predicate
	: common_value BETWEEN common_value AND common_value
		{ $$ = newNode<ComparativeBoolNode>(blr_between, $1, $3, $5); }
	| common_value NOT BETWEEN common_value AND common_value
		{
			ComparativeBoolNode* node = newNode<ComparativeBoolNode>(blr_between, $1, $4, $6);
			$$ = newNode<NotBoolNode>(node);
		}
	;

%type <boolExprNode> binary_pattern_predicate
binary_pattern_predicate
	: common_value binary_pattern_operator common_value
		{ $$ = newNode<ComparativeBoolNode>($2, $1, $3); }
	| common_value NOT binary_pattern_operator common_value
		{
			ComparativeBoolNode* cmpNode = newNode<ComparativeBoolNode>($3, $1, $4);
			$$ = newNode<NotBoolNode>(cmpNode);
		}
	;

%type <blrOp> binary_pattern_operator
binary_pattern_operator
	: CONTAINING	{ $$ = blr_containing; }
	| STARTING		{ $$ = blr_starting; }
	| STARTING WITH	{ $$ = blr_starting; }
	;

%type <boolExprNode> ternary_pattern_predicate
ternary_pattern_predicate
	: common_value ternary_pattern_operator common_value escape_opt
		{ $$ = newNode<ComparativeBoolNode>($2, $1, $3, $4); }
	| common_value NOT ternary_pattern_operator common_value escape_opt
		{
			ComparativeBoolNode* likeNode = newNode<ComparativeBoolNode>($3, $1, $4, $5);
			$$ = newNode<NotBoolNode>(likeNode);
		}
	;

%type <blrOp> ternary_pattern_operator
ternary_pattern_operator
	: LIKE			{ $$ = blr_like; }
	| SIMILAR TO	{ $$ = blr_similar; }
	;

%type <valueExprNode> escape_opt
escape_opt
	: /* nothing */			{ $$ = NULL; }
	| ESCAPE common_value	{ $$ = $2; }
	;

%type <boolExprNode> in_predicate
in_predicate
	: common_value KW_IN in_predicate_value
		{
			ComparativeBoolNode* node = newNode<ComparativeBoolNode>(blr_eql, $1);
			node->dsqlFlag = ComparativeBoolNode::DFLAG_ANSI_ANY;
			node->dsqlSpecialArg = $3;
			$$ = node;
		}
	| common_value NOT KW_IN in_predicate_value
		{
			ComparativeBoolNode* node = newNode<ComparativeBoolNode>(blr_eql, $1);
			node->dsqlFlag = ComparativeBoolNode::DFLAG_ANSI_ANY;
			node->dsqlSpecialArg = $4;
			$$ = newNode<NotBoolNode>(node);
		}
	;

%type <boolExprNode> exists_predicate
exists_predicate
	: EXISTS '(' select_expr ')'
		{ $$ = newNode<RseBoolNode>(blr_any, $3); }
	;

%type <boolExprNode> singular_predicate
singular_predicate
	: SINGULAR '(' select_expr ')'
		{ $$ = newNode<RseBoolNode>(blr_unique, $3); }
	;

%type <boolExprNode> null_predicate
null_predicate
	: common_value IS KW_NULL
		{ $$ = newNode<MissingBoolNode>($1); }
	| common_value IS UNKNOWN
		{ $$ = newNode<MissingBoolNode>($1, true); }
	| common_value IS NOT KW_NULL
		{ $$ = newNode<NotBoolNode>(newNode<MissingBoolNode>($1)); }
	| common_value IS NOT UNKNOWN
		{ $$ = newNode<NotBoolNode>(newNode<MissingBoolNode>($1, true)); }
	;


// set values

%type <exprNode> in_predicate_value
in_predicate_value
	: table_subquery		{ $$ = $1; }
	| '(' value_list ')'	{ $$ = $2; }
	;

%type <selectExprNode> table_subquery
table_subquery
	: '(' column_select ')'		{ $$ = $2; }
	;

// USER control SQL interface

%type <ddlNode> create_user_clause
create_user_clause
	: symbol_user_name passwd_clause firstname_opt middlename_opt lastname_opt grant_admin_opt
		{
			CreateAlterUserNode* node = newNode<CreateAlterUserNode>(true, toName($1));
			node->password = $2;
			node->firstName = $3;
			node->middleName = $4;
			node->lastName = $5;
			node->adminRole = $6;
			$$ = node;
		}
	;

%type <ddlNode> alter_user_clause
alter_user_clause
	: symbol_user_name set_noise passwd_opt firstname_opt middlename_opt lastname_opt admin_opt
		{
			CreateAlterUserNode* node = newNode<CreateAlterUserNode>(false, toName($1));
			node->password = $3;
			node->firstName = $4;
			node->middleName = $5;
			node->lastName = $6;
			node->adminRole = $7;
			$$ = node;
		}
	;

%type <intlStringPtr> passwd_opt
passwd_opt
	: /* nothing */			{ $$ = NULL; }
	| passwd_clause
	;

%type <intlStringPtr> passwd_clause
passwd_clause
	: PASSWORD sql_string	{ $$ = newNode<IntlString>($2); }
	;

set_noise
	: // nothing
	| SET
	;

%type <stringPtr> firstname_opt
firstname_opt
	: /* nothing */			{ $$ = NULL; }
	| FIRSTNAME sql_string	{ $$ = newNode<string>($2->str_data, $2->str_length); }
	;

%type <stringPtr> middlename_opt
middlename_opt
	: /* nothing */			{ $$ = NULL; }
	| MIDDLENAME sql_string	{ $$ = newNode<string>($2->str_data, $2->str_length); }
	;

%type <stringPtr> lastname_opt
lastname_opt
	: /* nothing */			{ $$ = NULL; }
	| LASTNAME sql_string	{ $$ = newNode<string>($2->str_data, $2->str_length); }
	;

%type <nullableIntVal> admin_opt
admin_opt
	: /* nothing */			{ $$ = Nullable<int>::empty(); }
	| revoke_admin
	| grant_admin
	;

%type <nullableIntVal> grant_admin_opt
grant_admin_opt
	: /* nothing */			{ $$ = Nullable<int>::empty(); }
	| grant_admin
	;

%type <nullableIntVal> grant_admin
grant_admin
	: GRANT ADMIN ROLE		{ $$ = Nullable<int>::val(1); }
	;

%type <nullableIntVal> revoke_admin
revoke_admin
	: REVOKE ADMIN ROLE		{ $$ = Nullable<int>::val(0); }
	;

// value types

%type <valueExprNode> value
value
	: common_value
	| boolean_value_expression
		{ $$ = newNode<BoolAsValueNode>($1); }
	;

%type <valueExprNode> common_value
common_value
	: column_name
		{ $$ = $1; }
	| array_element
	| function
		{ $$ = $1; }
	| u_constant
	| boolean_literal
	| parameter
	| variable
	| cast_specification
	| case_expression
	| next_value_expression
		{ $$ = $1; }
	| udf
		{ $$ = $1; }
	| '-' common_value %prec UMINUS
		{ $$ = newNode<NegateNode>($2); }
	| '+' common_value %prec UPLUS
		{ $$ = $2; }
	| common_value '+' common_value
		{ $$ = newNode<ArithmeticNode>(blr_add, (client_dialect < SQL_DIALECT_V6_TRANSITION), $1, $3); }
	| common_value CONCATENATE common_value
		{ $$ = newNode<ConcatenateNode>($1, $3); }
	| common_value COLLATE symbol_collation_name
		{ $$ = newNode<CollateNode>($1, toName($3)); }
	| common_value '-' common_value
		{ $$ = newNode<ArithmeticNode>(blr_subtract, (client_dialect < SQL_DIALECT_V6_TRANSITION), $1, $3); }
	| common_value '*' common_value
		{ $$ = newNode<ArithmeticNode>(blr_multiply, (client_dialect < SQL_DIALECT_V6_TRANSITION), $1, $3); }
	| common_value '/' common_value
		{ $$ = newNode<ArithmeticNode>(blr_divide, (client_dialect < SQL_DIALECT_V6_TRANSITION), $1, $3); }
	| '(' common_value ')'
		{ $$ = $2; }
	| '(' column_singleton ')'
		{ $$ = $2; }
	| current_user
		{ $$ = $1; }
	| current_role
		{ $$ = $1; }
	| internal_info
		{ $$ = $1; }
	| DB_KEY
		{ $$ = newNode<RecordKeyNode>(blr_dbkey); }
	| symbol_table_alias_name '.' DB_KEY
		{ $$ = newNode<RecordKeyNode>(blr_dbkey, toName($1)); }
	| KW_VALUE
		{ $$ = newNode<DomainValidationNode>(); }
	| datetime_value_expression
		{ $$ = $1; }
	| null_value
		{ $$ = $1; }
	;

%type <valueExprNode> datetime_value_expression
datetime_value_expression
	: CURRENT_DATE
		{
			if (client_dialect < SQL_DIALECT_V6_TRANSITION)
			{
				ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-104) <<
						  Arg::Gds(isc_sql_dialect_datatype_unsupport) << Arg::Num(client_dialect) <<
						  												  Arg::Str("DATE"));
			}

			if (db_dialect < SQL_DIALECT_V6_TRANSITION)
			{
				ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-104) <<
						  Arg::Gds(isc_sql_db_dialect_dtype_unsupport) << Arg::Num(db_dialect) <<
						  												  Arg::Str("DATE"));
			}

			$$ = newNode<CurrentDateNode>();
		}
	| CURRENT_TIME time_precision_opt
		{
			if (client_dialect < SQL_DIALECT_V6_TRANSITION)
			{
				ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-104) <<
						  Arg::Gds(isc_sql_dialect_datatype_unsupport) << Arg::Num(client_dialect) <<
						  												  Arg::Str("TIME"));
			}

			if (db_dialect < SQL_DIALECT_V6_TRANSITION)
			{
				ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-104) <<
						  Arg::Gds(isc_sql_db_dialect_dtype_unsupport) << Arg::Num(db_dialect) <<
						  												  Arg::Str("TIME"));
			}

			$$ = newNode<CurrentTimeNode>($2);
		}
	| CURRENT_TIMESTAMP timestamp_precision_opt
		{ $$ = newNode<CurrentTimeStampNode>($2); }
	;

%type <uintVal>	time_precision_opt
time_precision_opt
	: /* nothing */						{ $$ = DEFAULT_TIME_PRECISION; }
	| '(' nonneg_short_integer ')'		{ $$ = $2; }
	;

%type <uintVal>	timestamp_precision_opt
timestamp_precision_opt
	: /* nothing */					{ $$ = DEFAULT_TIMESTAMP_PRECISION; }
	| '(' nonneg_short_integer ')'	{ $$ = $2; }
	;

%type <valueExprNode> array_element
array_element
	: column_name '[' value_list ']'
		{
			ArrayNode* node = newNode<ArrayNode>(ExprNode::as<FieldNode>($1));
			node->field->dsqlIndices = $3;
			$$ = node;
		}
	;

%type <valueListNode> common_value_list_opt
common_value_list_opt
	: /* nothing */			{ $$ = newNode<ValueListNode>(0); }
	| common_value_list
	;

%type <valueListNode> common_value_list
common_value_list
	: common_value							{ $$ = newNode<ValueListNode>($1); }
	| common_value_list ',' common_value	{ $$ = $1->add($3); }
	;

%type <valueListNode> value_list_opt
value_list_opt
	: /* nothing */		{ $$ = newNode<ValueListNode>(0); }
	| value_list		{ $$ = $1; }
	;

%type <valueListNode> value_list
value_list
	: value						{ $$ = newNode<ValueListNode>($1); }
	| value_list ',' value		{ $$ = $1->add($3); }
	;

%type <valueExprNode> constant
constant
	: u_constant
	| '-' u_numeric_constant	{ $$ = newNode<NegateNode>($2); }
	| boolean_literal
	;

%type <valueExprNode> u_numeric_constant
u_numeric_constant
	: NUMERIC			{ $$ = MAKE_constant ((dsql_str*) $1, CONSTANT_STRING); }
	| NUMBER			{ $$ = MAKE_const_slong($1); }
	| FLOAT_NUMBER		{ $$ = MAKE_constant ((dsql_str*) $1, CONSTANT_DOUBLE); }
	| NUMBER64BIT		{ $$ = MAKE_constant ((dsql_str*) $1, CONSTANT_SINT64); }
	| SCALEDINT			{ $$ = MAKE_constant ((dsql_str*) $1, CONSTANT_SINT64); }
	;

%type <valueExprNode> u_constant
u_constant
	: u_numeric_constant
	| sql_string
		{ $$ = MAKE_str_constant ($1, lex.att_charset); }
	| DATE STRING
		{
			if (client_dialect < SQL_DIALECT_V6_TRANSITION)
			{
				ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-104) <<
						  Arg::Gds(isc_sql_dialect_datatype_unsupport) << Arg::Num(client_dialect) <<
						  												  Arg::Str("DATE"));
			}
			if (db_dialect < SQL_DIALECT_V6_TRANSITION)
			{
				ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-104) <<
						  Arg::Gds(isc_sql_db_dialect_dtype_unsupport) << Arg::Num(db_dialect) <<
						  												  Arg::Str("DATE"));
			}
			$$ = MAKE_constant ($2, CONSTANT_DATE);
		}
	| TIME STRING
		{
			if (client_dialect < SQL_DIALECT_V6_TRANSITION)
			{
				ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-104) <<
						  Arg::Gds(isc_sql_dialect_datatype_unsupport) << Arg::Num(client_dialect) <<
						  												  Arg::Str("TIME"));
			}
			if (db_dialect < SQL_DIALECT_V6_TRANSITION)
			{
				ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-104) <<
						  Arg::Gds(isc_sql_db_dialect_dtype_unsupport) << Arg::Num(db_dialect) <<
						  												  Arg::Str("TIME"));
			}
			$$ = MAKE_constant ($2, CONSTANT_TIME);
		}
	| TIMESTAMP STRING
		{ $$ = MAKE_constant ($2, CONSTANT_TIMESTAMP); }
		;

%type <valueExprNode> boolean_literal
boolean_literal
	: KW_FALSE	{ $$ = MAKE_constant(MAKE_string("", 0), CONSTANT_BOOLEAN); }
	| KW_TRUE	{ $$ = MAKE_constant(MAKE_string("1", 1), CONSTANT_BOOLEAN); }
	;

%type <valueExprNode> parameter
parameter
	: '?'	{ $$ = make_parameter(); }
	;

%type <valueExprNode> current_user
current_user
	: USER			{ $$ = newNode<CurrentUserNode>(); }
	| CURRENT_USER	{ $$ = newNode<CurrentUserNode>(); }
	;

%type <valueExprNode> current_role
current_role
	: CURRENT_ROLE	{ $$ = newNode<CurrentRoleNode>(); }
	;

%type <valueExprNode> internal_info
internal_info
	: CURRENT_CONNECTION
		{ $$ = newNode<InternalInfoNode>(MAKE_const_slong(INFO_TYPE_CONNECTION_ID)); }
	| CURRENT_TRANSACTION
		{ $$ = newNode<InternalInfoNode>(MAKE_const_slong(INFO_TYPE_TRANSACTION_ID)); }
	| GDSCODE
		{ $$ = newNode<InternalInfoNode>(MAKE_const_slong(INFO_TYPE_GDSCODE)); }
	| SQLCODE
		{ $$ = newNode<InternalInfoNode>(MAKE_const_slong(INFO_TYPE_SQLCODE)); }
	| SQLSTATE
		{ $$ = newNode<InternalInfoNode>(MAKE_const_slong(INFO_TYPE_SQLSTATE)); }
	| ROW_COUNT
		{ $$ = newNode<InternalInfoNode>(MAKE_const_slong(INFO_TYPE_ROWS_AFFECTED)); }
	;

%type <legacyStr> sql_string
sql_string
	: STRING			// string in current charset
		{ $$ = $1; }
	| INTRODUCER STRING	// string in specific charset
		{
			dsql_str* str = $2;
			str->str_charset = $1;
			if (str->type == dsql_str::TYPE_SIMPLE || str->type == dsql_str::TYPE_ALTERNATE)
			{
				StrMark* mark = strMarks.get(str);
				fb_assert(mark);
				if (mark)
					mark->introduced = true;
			}
			$$ = str;
		}
	;

%type <int32Val> signed_short_integer
signed_short_integer
	: nonneg_short_integer
	| '-' neg_short_integer
		{ $$ = -$2; }
	;

%type <int32Val> nonneg_short_integer
nonneg_short_integer
	: NUMBER
		{
			if ($1 > SHRT_POS_MAX)
				yyabandon(-842, isc_expec_short);	// Short integer expected

			$$ = $1;
		}
	;

%type <int32Val> neg_short_integer
neg_short_integer
	: NUMBER
		{
			if ($1 > SHRT_NEG_MAX)
				yyabandon(-842, isc_expec_short);	// Short integer expected

			$$ = $1;
		}
	;

%type <int32Val> pos_short_integer
pos_short_integer
	: nonneg_short_integer
		{
			if ($1 == 0)
				yyabandon(-842, isc_expec_positive);	// Positive number expected

			$$ = $1;
		}
	;

%type <int32Val> unsigned_short_integer
unsigned_short_integer
	: NUMBER
		{
			if ($1 > SHRT_UNSIGNED_MAX)
				yyabandon(-842, isc_expec_ushort);	// Unsigned short integer expected

			$$ = $1;
		}
	;

%type <int32Val> signed_long_integer
signed_long_integer
	: long_integer
	| '-' long_integer		{ $$ = -$2; }
	;

%type <int32Val> long_integer
long_integer
	: NUMBER	{ $$ = $1;}
	;


// functions

%type <valueExprNode> function
function
	: aggregate_function		{ $$ = $1; }
	| non_aggregate_function
	| over_clause
	;

%type <valueExprNode> non_aggregate_function
non_aggregate_function
	: numeric_value_function
	| string_value_function
	| system_function_expression
	;

%type <aggNode> aggregate_function
aggregate_function
	: COUNT '(' '*' ')'
		{ $$ = newNode<CountAggNode>(false, (client_dialect < SQL_DIALECT_V6_TRANSITION)); }
	| COUNT '(' all_noise value ')'
		{ $$ = newNode<CountAggNode>(false, (client_dialect < SQL_DIALECT_V6_TRANSITION), $4); }
	| COUNT '(' DISTINCT value ')'
		{ $$ = newNode<CountAggNode>(true, (client_dialect < SQL_DIALECT_V6_TRANSITION), $4); }
	| SUM '(' all_noise value ')'
		{
			$$ = newNode<SumAggNode>(false,
				(client_dialect < SQL_DIALECT_V6_TRANSITION), $4);
		}
	| SUM '(' DISTINCT value ')'
		{
			$$ = newNode<SumAggNode>(true,
				(client_dialect < SQL_DIALECT_V6_TRANSITION), $4);
		}
	| AVG '(' all_noise value ')'
		{
			$$ = newNode<AvgAggNode>(false,
				(client_dialect < SQL_DIALECT_V6_TRANSITION), $4);
		}
	| AVG '(' DISTINCT value ')'
		{
			$$ = newNode<AvgAggNode>(true,
				(client_dialect < SQL_DIALECT_V6_TRANSITION), $4);
		}
	| MINIMUM '(' all_noise value ')'
		{ $$ = newNode<MaxMinAggNode>(MaxMinAggNode::TYPE_MIN, $4); }
	| MINIMUM '(' DISTINCT value ')'
		{ $$ = newNode<MaxMinAggNode>(MaxMinAggNode::TYPE_MIN, $4); }
	| MAXIMUM '(' all_noise value ')'
		{ $$ = newNode<MaxMinAggNode>(MaxMinAggNode::TYPE_MAX, $4); }
	| MAXIMUM '(' DISTINCT value ')'
		{ $$ = newNode<MaxMinAggNode>(MaxMinAggNode::TYPE_MAX, $4); }
	| LIST '(' all_noise value delimiter_opt ')'
		{ $$ = newNode<ListAggNode>(false, $4, $5); }
	| LIST '(' DISTINCT value delimiter_opt ')'
		{ $$ = newNode<ListAggNode>(true, $4, $5); }
	;

%type <aggNode> window_function
window_function
	: DENSE_RANK '(' ')'
		{ $$ = newNode<DenseRankWinNode>(); }
	| RANK '(' ')'
		{ $$ = newNode<RankWinNode>(); }
	| ROW_NUMBER '(' ')'
		{ $$ = newNode<RowNumberWinNode>(); }
	| FIRST_VALUE '(' value ')'
		{ $$ = newNode<FirstValueWinNode>($3); }
	| LAST_VALUE '(' value ')'
		{ $$ = newNode<LastValueWinNode>($3); }
	| NTH_VALUE '(' value ',' value ')'
		{ $$ = newNode<NthValueWinNode>($3, $5); }
	| LAG '(' value ',' value ',' value ')'
		{ $$ = newNode<LagWinNode>($3, $5, $7); }
	| LAG '(' value ',' value ')'
		{ $$ = newNode<LagWinNode>($3, $5, newNode<NullNode>()); }
	| LAG '(' value ')'
		{ $$ = newNode<LagWinNode>($3, MAKE_const_slong(1), newNode<NullNode>()); }
	| LEAD '(' value ',' value ',' value ')'
		{ $$ = newNode<LeadWinNode>($3, $5, $7); }
	| LEAD '(' value ',' value ')'
		{ $$ = newNode<LeadWinNode>($3, $5, newNode<NullNode>()); }
	| LEAD '(' value ')'
		{ $$ = newNode<LeadWinNode>($3, MAKE_const_slong(1), newNode<NullNode>()); }
	;

%type <aggNode> aggregate_window_function
aggregate_window_function
	: aggregate_function
	| window_function
	;

%type <valueExprNode> over_clause
over_clause
	: aggregate_window_function OVER '(' window_partition_opt order_clause ')'
		{ $$ = newNode<OverNode>($1, $4, $5); }
	;

%type <valueListNode> window_partition_opt
window_partition_opt
	: /* nothing */				{ $$ = NULL; }
	| PARTITION BY value_list	{ $$ = $3; }
	;

%type <valueExprNode> delimiter_opt
delimiter_opt
	: /* nothing */		{ $$ = MAKE_str_constant(MAKE_cstring(","), lex.att_charset); }
	| ',' value			{ $$ = $2; }
	;

%type <valueExprNode> numeric_value_function
numeric_value_function
	: extract_expression
	| length_expression
	;

%type <valueExprNode> extract_expression
extract_expression
	: EXTRACT '(' timestamp_part FROM value ')'		{ $$ = newNode<ExtractNode>($3, $5); }
	;

%type <valueExprNode> length_expression
length_expression
	: bit_length_expression
	| char_length_expression
	| octet_length_expression
	;

%type <valueExprNode> bit_length_expression
bit_length_expression
	: BIT_LENGTH '(' value ')'			{ $$ = newNode<StrLenNode>(blr_strlen_bit, $3); }
	;

%type <valueExprNode> char_length_expression
char_length_expression
	: CHAR_LENGTH '(' value ')'			{ $$ = newNode<StrLenNode>(blr_strlen_char, $3); }
	| CHARACTER_LENGTH '(' value ')'	{ $$ = newNode<StrLenNode>(blr_strlen_char, $3); }
	;

%type <valueExprNode> octet_length_expression
octet_length_expression
	: OCTET_LENGTH '(' value ')'		{ $$ = newNode<StrLenNode>(blr_strlen_octet, $3); }
	;

%type <valueExprNode> system_function_expression
system_function_expression
	: system_function_std_syntax '(' value_list_opt ')'
		{ $$ = newNode<SysFuncCallNode>(toName($1), $3); }
	| system_function_special_syntax
		{ $$ = $1; }
	;

%type <legacyStr> system_function_std_syntax
system_function_std_syntax
	: ABS
	| ACOS
	| ACOSH
	| ASCII_CHAR
	| ASCII_VAL
	| ASIN
	| ASINH
	| ATAN
	| ATAN2
	| ATANH
	| BIN_AND
	| BIN_NOT
	| BIN_OR
	| BIN_SHL
	| BIN_SHR
	| BIN_XOR
	| CEIL
	| CHAR_TO_UUID
	| CHAR_TO_UUID2
	| COS
	| COSH
	| COT
	| EXP
	| FLOOR
	| GEN_UUID
	| HASH
	| LEFT
	| LN
	| LOG
	| LOG10
	| LPAD
	| MAXVALUE
	| MINVALUE
	| MOD
	| PI
	| POWER
	| RAND
	| RDB_GET_CONTEXT
	| RDB_SET_CONTEXT
	| REPLACE
	| REVERSE
	| RIGHT
	| ROUND
	| RPAD
	| SIGN
	| SIN
	| SINH
	| SQRT
	| TAN
	| TANH
	| TRUNC
	| UUID_TO_CHAR
	| UUID_TO_CHAR2
	;

%type <sysFuncCallNode> system_function_special_syntax
system_function_special_syntax
	: DATEADD '(' value timestamp_part TO value ')'
		{
			$$ = newNode<SysFuncCallNode>(toName($1),
				newNode<ValueListNode>($3)->add(MAKE_const_slong($4))->add($6));
			$$->dsqlSpecialSyntax = true;
		}
	| DATEADD '(' timestamp_part ',' value ',' value ')'
		{
			$$ = newNode<SysFuncCallNode>(toName($1),
				newNode<ValueListNode>($5)->add(MAKE_const_slong($3))->add($7));
			$$->dsqlSpecialSyntax = true;
		}
	| DATEDIFF '(' timestamp_part FROM value TO value ')'
		{
			$$ = newNode<SysFuncCallNode>(toName($1),
				newNode<ValueListNode>(MAKE_const_slong($3))->add($5)->add($7));
			$$->dsqlSpecialSyntax = true;
		}
	| DATEDIFF '(' timestamp_part ',' value ',' value ')'
		{
			$$ = newNode<SysFuncCallNode>(toName($1),
				newNode<ValueListNode>(MAKE_const_slong($3))->add($5)->add($7));
			$$->dsqlSpecialSyntax = true;
		}
	| OVERLAY '(' value PLACING value FROM value FOR value ')'
		{
			$$ = newNode<SysFuncCallNode>(toName($1),
				newNode<ValueListNode>($3)->add($5)->add($7)->add($9));
			$$->dsqlSpecialSyntax = true;
		}
	| OVERLAY '(' value PLACING value FROM value ')'
		{
			$$ = newNode<SysFuncCallNode>(toName($1),
				newNode<ValueListNode>($3)->add($5)->add($7));
			$$->dsqlSpecialSyntax = true;
		}
	| POSITION '(' common_value KW_IN common_value ')'
		{
			$$ = newNode<SysFuncCallNode>(toName($1), newNode<ValueListNode>($3)->add($5));
			$$->dsqlSpecialSyntax = true;
		}
	| POSITION '(' common_value_list_opt  ')'
		{ $$ = newNode<SysFuncCallNode>(toName($1), $3); }
	;

%type <valueExprNode> string_value_function
string_value_function
	: substring_function
	| trim_function
	| KW_UPPER '(' value ')'
		{ $$ = newNode<StrCaseNode>(blr_upcase, $3); }
	| KW_LOWER '(' value ')'
		{ $$ = newNode<StrCaseNode>(blr_lowcase, $3); }
	;

%type <valueExprNode> substring_function
substring_function
	: SUBSTRING '(' value FROM value string_length_opt ')'
		{
			// SQL spec requires numbering to start with 1,
			// hence we decrement the first parameter to make it
			// compatible with the engine's implementation
			ArithmeticNode* subtractNode = newNode<ArithmeticNode>(
				blr_subtract, true, $5, MAKE_const_slong(1));

			$$ = newNode<SubstringNode>($3, subtractNode, $6);
		}
	| SUBSTRING '(' common_value SIMILAR common_value ESCAPE common_value ')'
		{ $$ = newNode<SubstringSimilarNode>($3, $5, $7); }
	;

%type <valueExprNode> string_length_opt
string_length_opt
	: /* nothing */		{ $$ = NULL; }
	| FOR value 		{ $$ = $2; }
	;

%type <valueExprNode> trim_function
trim_function
	: TRIM '(' trim_specification value FROM value ')'
		{ $$ = newNode<TrimNode>($3, $6, $4); }
	| TRIM '(' value FROM value ')'
		{ $$ = newNode<TrimNode>(blr_trim_both, $5, $3); }
	| TRIM '(' trim_specification FROM value ')'
		{ $$ = newNode<TrimNode>($3, $5); }
	| TRIM '(' value ')'
		{ $$ = newNode<TrimNode>(blr_trim_both, $3); }
	;

%type <blrOp> trim_specification
trim_specification
	: BOTH		{ $$ = blr_trim_both; }
	| TRAILING	{ $$ = blr_trim_trailing; }
	| LEADING	{ $$ = blr_trim_leading; }
	;

%type <valueExprNode> udf
udf
	: symbol_UDF_call_name '(' value_list ')'
		{ $$ = newNode<UdfCallNode>(QualifiedName(toName($1), ""), $3); }
	| symbol_UDF_call_name '(' ')'
		{ $$ = newNode<UdfCallNode>(QualifiedName(toName($1), ""), newNode<ValueListNode>(0)); }
	| symbol_package_name '.' symbol_UDF_name '(' value_list ')'
		{ $$ = newNode<UdfCallNode>(QualifiedName(toName($3), toName($1)), $5); }
	| symbol_package_name '.' symbol_UDF_name '(' ')'
		{ $$ = newNode<UdfCallNode>(QualifiedName(toName($3), toName($1)), newNode<ValueListNode>(0)); }
	;

%type <valueExprNode> cast_specification
cast_specification
	: CAST '(' value AS data_type_descriptor ')'
		{ $$ = newNode<CastNode>($3, $5); }
	;

// case expressions

%type <valueExprNode> case_expression
case_expression
	: case_abbreviation
	| case_specification
	;

%type <valueExprNode> case_abbreviation
case_abbreviation
	: NULLIF '(' value ',' value ')'
		{
			ComparativeBoolNode* condition = newNode<ComparativeBoolNode>(blr_eql, $3, $5);
			$$ = newNode<ValueIfNode>(condition, newNode<NullNode>(), $3);
		}
	| IIF '(' search_condition ',' value ',' value ')'
		{ $$ = newNode<ValueIfNode>($3, $5, $7); }
	| COALESCE '(' value ',' value_list ')'
		{ $$ = newNode<CoalesceNode>($5->addFront($3)); }
	| DECODE '(' value ',' decode_pairs ')'
		{
			ValueListNode* list = $5;
			ValueListNode* conditions = newNode<ValueListNode>(list->items.getCount() / 2);
			ValueListNode* values = newNode<ValueListNode>(list->items.getCount() / 2);

			for (size_t i = 0; i < list->items.getCount(); i += 2)
			{
				conditions->items[i / 2] = list->items[i];
				values->items[i / 2] = list->items[i + 1];
			}

			$$ = newNode<DecodeNode>($3, conditions, values);
		}
	| DECODE '(' value ',' decode_pairs ',' value ')'
		{
			ValueListNode* list = $5;
			ValueListNode* conditions = newNode<ValueListNode>(list->items.getCount() / 2);
			ValueListNode* values = newNode<ValueListNode>(list->items.getCount() / 2 + 1);

			for (size_t i = 0; i < list->items.getCount(); i += 2)
			{
				conditions->items[i / 2] = list->items[i];
				values->items[i / 2] = list->items[i + 1];
			}

			values->items[list->items.getCount() / 2] = $7;

			$$ = newNode<DecodeNode>($3, conditions, values);
		}
	;

%type <valueExprNode> case_specification
case_specification
	: simple_case		{ $$ = $1; }
	| searched_case		{ $$ = $1; }
	;

%type <decodeNode> simple_case
simple_case
	: CASE case_operand
			{ $$ = newNode<DecodeNode>($2, newNode<ValueListNode>(0u), newNode<ValueListNode>(0u)); }
		simple_when_clause($3->conditions, $3->values) else_case_result_opt END
			{
				DecodeNode* node = $$ = $3;
				node->label = "CASE";
				if ($5)
					node->values->add($5);
			}
	;

%type simple_when_clause(<valueListNode>, <valueListNode>)
simple_when_clause($conditions, $values)
	: WHEN when_operand THEN case_result
		{
			$conditions->add($2);
			$values->add($4);
		}
	| simple_when_clause WHEN when_operand THEN case_result
		{
			$conditions->add($3);
			$values->add($5);
		}
	;

%type <valueExprNode> else_case_result_opt
else_case_result_opt
	: /* nothing */		{ $$ = NULL; }
	| ELSE case_result	{ $$ = $2; }

%type <valueExprNode> searched_case
searched_case
	: CASE searched_when_clause END
		{ $$ = $2; }
	| CASE searched_when_clause ELSE case_result END
		{
			ValueIfNode* last = $2;
			ValueIfNode* next;

			while ((next = last->falseValue->as<ValueIfNode>()))
				last = next;

			fb_assert(last->falseValue->is<NullNode>());

			last->falseValue = $4;
			$$ = $2;
		}
	;

%type <valueIfNode> searched_when_clause
searched_when_clause
	: WHEN search_condition THEN case_result
		{ $$ = newNode<ValueIfNode>($2, $4, newNode<NullNode>()); }
	| searched_when_clause WHEN search_condition THEN case_result
		{
			ValueIfNode* cond = newNode<ValueIfNode>($3, $5, newNode<NullNode>());
			ValueIfNode* last = $1;
			ValueIfNode* next;

			while ((next = last->falseValue->as<ValueIfNode>()))
				last = next;

			fb_assert(last->falseValue->is<NullNode>());

			last->falseValue = cond;
			$$ = $1;
		}
	;

%type <valueExprNode> when_operand
when_operand
	: value
	;

%type <valueExprNode> case_operand
case_operand
	: value
	;

%type <valueExprNode> case_result
case_result
	: value
	;

%type <valueListNode> decode_pairs
decode_pairs
	: value ',' value
		{ $$ = newNode<ValueListNode>(0u)->add($1)->add($3); }
	| decode_pairs ',' value ',' value
		{ $$ = $1->add($3)->add($5); }
	;

// next value expression

%type <valueExprNode> next_value_expression
next_value_expression
	: NEXT KW_VALUE FOR symbol_generator_name
		{
			$$ = newNode<GenIdNode>((client_dialect < SQL_DIALECT_V6_TRANSITION),
				toName($4), MAKE_const_slong(1));
		}
	| GEN_ID '(' symbol_generator_name ',' value ')'
		{ $$ = newNode<GenIdNode>((client_dialect < SQL_DIALECT_V6_TRANSITION), toName($3), $5); }
	;


%type <blrOp> timestamp_part
timestamp_part
	: YEAR			{ $$ = blr_extract_year; }
	| MONTH			{ $$ = blr_extract_month; }
	| DAY			{ $$ = blr_extract_day; }
	| HOUR			{ $$ = blr_extract_hour; }
	| MINUTE		{ $$ = blr_extract_minute; }
	| SECOND		{ $$ = blr_extract_second; }
	| MILLISECOND	{ $$ = blr_extract_millisecond; }
	| WEEK			{ $$ = blr_extract_week; }
	| WEEKDAY		{ $$ = blr_extract_weekday; }
	| YEARDAY		{ $$ = blr_extract_yearday; }
	;

all_noise
	:
	| ALL
	;

distinct_noise
	:
	| DISTINCT
	;

%type <valueExprNode> null_value
null_value
	: KW_NULL
		{ $$ = newNode<NullNode>(); }
	| UNKNOWN
		{
			dsql_fld* field = make_field(NULL);
			field->fld_dtype = dtype_boolean;
			field->fld_length = sizeof(UCHAR);

			CastNode* castNode = newNode<CastNode>(newNode<NullNode>(), field);
			castNode->dsqlAlias = "CONSTANT";
			$$ = castNode;
		}
	;


// Performs special mapping of keywords into symbols

%type <legacyStr> symbol_UDF_call_name
symbol_UDF_call_name
	: SYMBOL
	;

%type <legacyStr> symbol_UDF_name
symbol_UDF_name
	: valid_symbol_name
	;

%type <legacyStr> symbol_blob_subtype_name
symbol_blob_subtype_name
	: valid_symbol_name
	;

%type <legacyStr> symbol_character_set_name
symbol_character_set_name
	: valid_symbol_name
	;

%type <legacyStr> symbol_collation_name
symbol_collation_name
	: valid_symbol_name
	;

%type <legacyStr> symbol_column_name
symbol_column_name
	: valid_symbol_name
	;

%type <legacyStr> symbol_constraint_name
symbol_constraint_name
	: valid_symbol_name
	;

%type <legacyStr> symbol_cursor_name
symbol_cursor_name
	: valid_symbol_name
	;

%type <legacyStr> symbol_domain_name
symbol_domain_name
	: valid_symbol_name
	;

%type <legacyStr> symbol_exception_name
symbol_exception_name
	: valid_symbol_name
	;

%type <legacyStr> symbol_filter_name
symbol_filter_name
	: valid_symbol_name
	;

%type <legacyStr> symbol_gdscode_name
symbol_gdscode_name
	: valid_symbol_name
	;

%type <legacyStr> symbol_generator_name
symbol_generator_name
	: valid_symbol_name
	;

%type <legacyStr> symbol_index_name
symbol_index_name
	: valid_symbol_name
	;

%type <legacyStr> symbol_item_alias_name
symbol_item_alias_name
	: valid_symbol_name
	;

%type <legacyStr> symbol_label_name
symbol_label_name
	: valid_symbol_name
	;

%type <legacyStr> symbol_ddl_name
symbol_ddl_name
	: valid_symbol_name
	;

%type <legacyStr> symbol_procedure_name
symbol_procedure_name
	: valid_symbol_name
	;

%type <legacyStr> symbol_role_name
symbol_role_name
	: valid_symbol_name
	;

%type <legacyStr> symbol_table_alias_name
symbol_table_alias_name
	: valid_symbol_name
	;

%type <legacyStr> symbol_table_name
symbol_table_name
	: valid_symbol_name
	;

%type <legacyStr> symbol_trigger_name
symbol_trigger_name
	: valid_symbol_name
	;

%type <legacyStr> symbol_user_name
symbol_user_name
	: valid_symbol_name
	;

%type <legacyStr> symbol_variable_name
symbol_variable_name
	: valid_symbol_name
	;

%type <legacyStr> symbol_view_name
symbol_view_name
	: valid_symbol_name
	;

%type <legacyStr> symbol_savepoint_name
symbol_savepoint_name
	: valid_symbol_name
	;

%type <legacyStr> symbol_package_name
symbol_package_name
	: valid_symbol_name
	;

// symbols

%type <legacyStr> valid_symbol_name
valid_symbol_name
	: SYMBOL
	| non_reserved_word
	;

// list of non-reserved words

%type <legacyStr> non_reserved_word
non_reserved_word
	: ACTION				// added in IB 5.0/
	| CASCADE
	| FREE_IT
	| RESTRICT
	| ROLE
	| KW_TYPE				// added in IB 6.0
	| KW_BREAK				// added in FB 1.0
	| KW_DESCRIPTOR
	| SUBSTRING
	| COALESCE				// added in FB 1.5
	| LAST
	| LEAVE
	| LOCK
	| NULLIF
	| NULLS
	| STATEMENT
	| INSERTING
	| UPDATING
	| DELETING
	| FIRST
	| SKIP
	| BLOCK					// added in FB 2.0
	| BACKUP
	| KW_DIFFERENCE
	| IIF
	| SCALAR_ARRAY
	| WEEKDAY
	| YEARDAY
	| SEQUENCE
	| NEXT
	| RESTART
	| COLLATION
	| RETURNING
	| KW_IGNORE
	| LIMBO
	| UNDO
	| REQUESTS
	| TIMEOUT
	| ABS					// added in FB 2.1
	| ACCENT
	| ACOS
	| ALWAYS
	| ASCII_CHAR
	| ASCII_VAL
	| ASIN
	| ATAN
	| ATAN2
	| BIN_AND
	| BIN_OR
	| BIN_SHL
	| BIN_SHR
	| BIN_XOR
	| CEIL
	| COS
	| COSH
	| COT
	| DATEADD
	| DATEDIFF
	| DECODE
	| EXP
	| FLOOR
	| GEN_UUID
	| GENERATED
	| HASH
	| LIST
	| LN
	| LOG
	| LOG10
	| LPAD
	| MATCHED
	| MATCHING
	| MAXVALUE
	| MILLISECOND
	| MINVALUE
	| MOD
	| OVERLAY
	| PAD
	| PI
	| PLACING
	| POWER
	| PRESERVE
	| RAND
	| REPLACE
	| REVERSE
	| ROUND
	| RPAD
	| SIGN
	| SIN
	| SINH
	| SPACE
	| SQRT
	| TAN
	| TANH
	| TEMPORARY
	| TRUNC
	| WEEK
	| AUTONOMOUS			// added in FB 2.5
	| CHAR_TO_UUID
	| CHAR_TO_UUID2
	| FIRSTNAME
	| MIDDLENAME
	| LASTNAME
	| MAPPING
	| OS_NAME
	| UUID_TO_CHAR
	| UUID_TO_CHAR2
	| GRANTED
	| CALLER				// new execute statement
	| COMMON
	| DATA
	| SOURCE
	| TWO_PHASE
	| BIN_NOT
	| ACTIVE				// old keywords, that were reserved pre-Firebird.2.5
//	| ADD					// words commented it this list remain reserved due to conflicts
	| AFTER
	| ASC
	| AUTO
	| BEFORE
	| COMMITTED
	| COMPUTED
	| CONDITIONAL
	| CONTAINING
	| CSTRING
	| DATABASE
//	| DB_KEY
	| DESC
	| DO
	| DOMAIN
	| ENTRY_POINT
	| EXCEPTION
	| EXIT
	| KW_FILE
//	| GDSCODE
	| GENERATOR
	| GEN_ID
	| IF
	| INACTIVE
//	| INDEX
	| INPUT_TYPE
	| ISOLATION
	| KEY
	| LENGTH
	| LEVEL
//	| KW_LONG
	| MANUAL
	| MODULE_NAME
	| NAMES
	| OPTION
	| OUTPUT_TYPE
	| OVERFLOW
	| PAGE
	| PAGES
	| KW_PAGE_SIZE
	| PASSWORD
//	| PLAN
//	| POST_EVENT
	| PRIVILEGES
	| PROTECTED
	| READ
	| RESERVING
	| RETAIN
//	| RETURNING_VALUES
	| SEGMENT
	| SHADOW
	| KW_SHARED
	| SINGULAR
	| KW_SIZE
	| SNAPSHOT
	| SORT
//	| SQLCODE
	| STABILITY
	| STARTING
	| STATISTICS
	| SUB_TYPE
	| SUSPEND
	| TRANSACTION
	| UNCOMMITTED
//	| VARIABLE
//	| VIEW
	| WAIT
//	| WEEK
//	| WHILE
	| WORK
	| WRITE				// end of old keywords, that were reserved pre-Firebird.2.5
	| KW_ABSOLUTE		// added in FB 3.0
	| ACOSH
	| ASINH
	| ATANH
	| BODY
	| CONTINUE
	| DDL
	| ENGINE
	| IDENTITY
	| NAME
	| PACKAGE
	| PARTITION
	| PRIOR
	| RDB_GET_CONTEXT
	| RDB_SET_CONTEXT
	| KW_RELATIVE
	| DENSE_RANK
	| FIRST_VALUE
	| NTH_VALUE
	| LAST_VALUE
	| LAG
	| LEAD
	| RANK
	| ROW_NUMBER
	;

%%


/*
 *	PROGRAM:	Dynamic SQL runtime support
 *	MODULE:		lex.c
 *	DESCRIPTION:	Lexical routine
 *
 */


namespace
{
	const int HASH_SIZE = 1021;

	struct KeywordVersion
	{
		KeywordVersion(int aKeyword, dsql_str* aStr, USHORT aVersion)
			: keyword(aKeyword),
			  str(aStr),
			  version(aVersion)
		{
		}

		int keyword;
		dsql_str* str;
		USHORT version;
	};

	class KeywordsMap : public GenericMap<Pair<Left<string, KeywordVersion> > >
	{
	public:
		explicit KeywordsMap(MemoryPool& pool)
			: GenericMap<Pair<Left<string, KeywordVersion> > >(pool)
		{
			for (const TOK* token = KEYWORD_getTokens(); token->tok_string; ++token)
			{
				size_t len = strlen(token->tok_string);
				dsql_str* str = FB_NEW_RPT(pool, len) dsql_str;
				str->str_length = len;
				strncpy(str->str_data, token->tok_string, len);
				//str->str_data[str->str_length] = 0; Is it necessary?

				put(string(token->tok_string, len),
					KeywordVersion(token->tok_ident, str, token->tok_version));
			}
		}

		~KeywordsMap()
		{
			Accessor accessor(this);
			for (bool found = accessor.getFirst(); found; found = accessor.getNext())
				delete accessor.current()->second.str;
		}
	};

	GlobalPtr<KeywordsMap> keywordsMap;
}


// Make a substring from the command text being parsed.
dsql_str* Parser::makeParseStr(const Position& p1, const Position& p2)
{
	const char* start = p1.firstPos;
	const char* end = p2.lastPos;

	string str;
	transformString(start, end - start, str);

	return MAKE_string(str.c_str(), str.length());
}


static dsql_fld* make_field(FieldNode* field_name)
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
	thread_db* tdbb = JRD_get_thread_data();

	if (field_name == NULL)
	{
		dsql_fld* field = FB_NEW(*tdbb->getDefaultPool()) dsql_fld(*tdbb->getDefaultPool());
		return field;
	}
	dsql_fld* field = FB_NEW(*tdbb->getDefaultPool()) dsql_fld(*tdbb->getDefaultPool());
	field->fld_name = field_name->dsqlName.c_str();
	field->fld_explicit_collation = false;
	field->fld_not_nullable = false;
	field->fld_full_domain = false;

	return field;
}


// Make parameter node.
ParameterNode* Parser::make_parameter()
{
	thread_db* tdbb = JRD_get_thread_data();

	ParameterNode* node = FB_NEW(*tdbb->getDefaultPool()) ParameterNode(*tdbb->getDefaultPool());
	node->dsqlParameterIndex = lex.param_number++;

	return node;
}


static MetaName toName(dsql_str* str)
{
	if (!str)
		return "";

	if (str->str_length > MAX_SQL_IDENTIFIER_LEN)
		yyabandon(-104, isc_dyn_name_longer);

	return MetaName(str->str_data);
}

static PathName toPathName(dsql_str* str)
{
	return PathName(str->str_data);
}

static string toString(dsql_str* str)
{
	return string(str ? str->str_data : "");
}

// Set the position of a left-hand non-terminal based on its right-hand rules.
void Parser::yyReducePosn(YYPOSN& ret, YYPOSN* termPosns, YYSTYPE* /*termVals*/, int termNo,
	int /*stkPos*/, int /*yychar*/, YYPOSN& /*yyposn*/, void*)
{
	if (termNo == 0)
	{
		// Accessing termPosns[-1] seems to be the only way to get correct positions in this case.
		ret.firstLine = ret.lastLine = termPosns[termNo - 1].lastLine;
		ret.firstColumn = ret.lastColumn = termPosns[termNo - 1].lastColumn;
		ret.firstPos = ret.lastPos = termPosns[termNo - 1].lastPos;
	}
	else
	{
		ret.firstLine = termPosns[0].firstLine;
		ret.firstColumn = termPosns[0].firstColumn;
		ret.firstPos = termPosns[0].firstPos;
		ret.lastLine = termPosns[termNo - 1].lastLine;
		ret.lastColumn = termPosns[termNo - 1].lastColumn;
		ret.lastPos = termPosns[termNo - 1].lastPos;
	}

	/*** This allows us to see colored output representing the position reductions.
	printf("%.*s", int(ret.firstPos - lex.start), lex.start);
	printf("<<<<<");
	printf("\033[1;31m%.*s\033[1;37m", int(ret.lastPos - ret.firstPos), ret.firstPos);
	printf(">>>>>");
	printf("%s\n", ret.lastPos);
	***/
}

int Parser::yylex()
{
	UCHAR tok_class;
	SSHORT c;

	// Find end of white space and skip comments

	for (;;)
	{
		if (lex.ptr >= lex.end)
			return -1;

		c = *lex.ptr++;

		// Process comments

		if (c == '\n')
		{
			lex.lines++;
			lex.line_start = lex.ptr;
			continue;
		}

		if (c == '-' && lex.ptr < lex.end && *lex.ptr == '-')
		{
			// single-line

			lex.ptr++;
			while (lex.ptr < lex.end)
			{
				if ((c = *lex.ptr++) == '\n')
				{
					lex.lines++;
					lex.line_start = lex.ptr; // + 1; // CVC: +1 left out.
					break;
				}
			}
			if (lex.ptr >= lex.end)
				return -1;

			continue;
		}
		else if (c == '/' && lex.ptr < lex.end && *lex.ptr == '*')
		{
			// multi-line

			const TEXT& start_block = lex.ptr[-1];
			lex.ptr++;
			while (lex.ptr < lex.end)
			{
				if ((c = *lex.ptr++) == '*')
				{
					if (*lex.ptr == '/')
						break;
				}
				if (c == '\n')
				{
					lex.lines++;
					lex.line_start = lex.ptr; // + 1; // CVC: +1 left out.

				}
			}
			if (lex.ptr >= lex.end)
			{
				// I need this to report the correct beginning of the block,
				// since it's not a token really.
				lex.last_token = &start_block;
				yyerror("unterminated block comment");
				return -1;
			}
			lex.ptr++;
			continue;
		}

		tok_class = classes(c);

		if (!(tok_class & CHR_WHITE))
			break;
	}

	yyposn.firstLine = lex.lines;
	yyposn.firstColumn = lex.ptr - lex.line_start;
	yyposn.firstPos = lex.ptr - 1;

	lex.prev_keyword = yylexAux();

	yyposn.lastLine = lex.lines;
	yyposn.lastColumn = lex.ptr - lex.line_start;
	yyposn.lastPos = lex.ptr;

	return lex.prev_keyword;
}

int Parser::yylexAux()
{
	SSHORT c = lex.ptr[-1];
	UCHAR tok_class = classes(c);
	char string[MAX_TOKEN_LEN];

	// Depending on tok_class of token, parse token

	lex.last_token = lex.ptr - 1;

	if (tok_class & CHR_INTRODUCER)
	{
		// The Introducer (_) is skipped, all other idents are copied
		// to become the name of the character set.
		char* p = string;
		for (; lex.ptr < lex.end && classes(*lex.ptr) & CHR_IDENT; lex.ptr++)
		{
			if (lex.ptr >= lex.end)
				return -1;

			check_copy_incr(p, UPPER7(*lex.ptr), string);
		}

		check_bound(p, string);
		*p = 0;

		// make a string value to hold the name, the name
		// is resolved in pass1_constant

		yylval.textPtr = MAKE_string(string, p - string)->str_data;

		return INTRODUCER;
	}

	// parse a quoted string, being sure to look for double quotes

	if (tok_class & CHR_QUOTE)
	{
		StrMark mark;
		mark.pos = lex.last_token - lex.start;

		char* buffer = string;
		size_t buffer_len = sizeof (string);
		const char* buffer_end = buffer + buffer_len - 1;
		char* p;
		for (p = buffer; ; ++p)
		{
			if (lex.ptr >= lex.end)
			{
				if (buffer != string)
					gds__free (buffer);
				yyerror("unterminated string");
				return -1;
			}
			// Care about multi-line constants and identifiers
			if (*lex.ptr == '\n')
			{
				lex.lines++;
				lex.line_start = lex.ptr + 1;
			}
			// *lex.ptr is quote - if next != quote we're at the end
			if ((*lex.ptr == c) && ((++lex.ptr == lex.end) || (*lex.ptr != c)))
				break;
			if (p > buffer_end)
			{
				char* const new_buffer = (char*) gds__alloc (2 * buffer_len);
				// FREE: at outer block
				if (!new_buffer)		// NOMEM:
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
			*p = *lex.ptr++;
		}
		if (c == '"')
		{
			stmt_ambiguous = true;
			// string delimited by double quotes could be
			// either a string constant or a SQL delimited
			// identifier, therefore marks the SQL statement as ambiguous

			if (client_dialect == SQL_DIALECT_V6_TRANSITION)
			{
				if (buffer != string)
					gds__free (buffer);
				yyabandon (-104, isc_invalid_string_constant);
			}
			else if (client_dialect >= SQL_DIALECT_V6)
			{
				if (p - buffer >= MAX_TOKEN_LEN)
				{
					if (buffer != string)
						gds__free (buffer);
					yyabandon(-104, isc_token_too_long);
				}
				else if (p > &buffer[MAX_SQL_IDENTIFIER_LEN])
				{
					if (buffer != string)
						gds__free (buffer);
					yyabandon(-104, isc_dyn_name_longer);
				}
				else if (p - buffer == 0)
				{
					if (buffer != string)
						gds__free (buffer);
					yyabandon(-104, isc_dyn_zero_len_id);
				}

				thread_db* tdbb = JRD_get_thread_data();
				Attachment* attachment = tdbb->getAttachment();
				MetaName name(attachment->nameToMetaCharSet(tdbb, MetaName(buffer, p - buffer)));

				yylval.legacyStr = MAKE_string(name.c_str(), name.length());

				dsql_str* delimited_id_str = yylval.legacyStr;
				delimited_id_str->type = dsql_str::TYPE_DELIMITED;
				if (buffer != string)
					gds__free (buffer);

				return SYMBOL;
			}
		}
		yylval.legacyStr = MAKE_string(buffer, p - buffer);
		if (buffer != string)
			gds__free (buffer);

		mark.length = lex.ptr - lex.last_token;
		mark.str = yylval.legacyStr;
		strMarks.put(mark.str, mark);

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

	fb_assert(lex.ptr <= lex.end);

	// Hexadecimal string constant.  This is treated the same as a
	// string constant, but is defined as: X'bbbb'
	//
	// Where the X is a literal 'x' or 'X' character, followed
	// by a set of nibble values in single quotes.  The nibble
	// can be 0-9, a-f, or A-F, and is converted from the hex.
	// The number of nibbles should be even.
	//
	// The resulting value is stored in a string descriptor and
	// returned to the parser as a string.  This can be stored
	// in a character or binary item.
	if ((c == 'x' || c == 'X') && lex.ptr < lex.end && *lex.ptr == '\'')
	{
		bool hexerror = false;

		// Remember where we start from, to rescan later.
		// Also we'll need to know the length of the buffer.

		const char* hexstring = ++lex.ptr;
		int charlen = 0;

		// Time to scan the string. Make sure the characters are legal,
		// and find out how long the hex digit string is.

		for (;;)
		{
			if (lex.ptr >= lex.end)	// Unexpected EOS
			{
				hexerror = true;
				break;
			}

			c = *lex.ptr;

			if (c == '\'')			// Trailing quote, done
			{
				++lex.ptr;			// Skip the quote
				break;
			}

			if (!(classes(c) & CHR_HEX))	// Illegal character
			{
				hexerror = true;
				break;
			}

			++charlen;	// Okay, just count 'em
			++lex.ptr;	// and advance...
		}

		hexerror = hexerror || (charlen & 1);	// IS_ODD(charlen)

		// If we made it this far with no error, then convert the string.
		if (!hexerror)
		{
			// Figure out the length of the actual resulting hex string.
			// Allocate a second temporary buffer for it.

			Firebird::string temp;

			// Re-scan over the hex string we got earlier, converting
			// adjacent bytes into nibble values.  Every other nibble,
			// write the saved byte to the temp space.  At the end of
			// this, the temp.space area will contain the binary
			// representation of the hex constant.

			UCHAR byte = 0;
			for (int i = 0; i < charlen; i++)
			{
				c = UPPER7(hexstring[i]);

				// Now convert the character to a nibble

				if (c >= 'A')
					c = (c - 'A') + 10;
				else
					c = (c - '0');

				if (i & 1) // nibble?
				{
					byte = (byte << 4) + (UCHAR) c;
					temp.append(1, (char) byte);
				}
				else
					byte = c;
			}

			dsql_str* string = MAKE_string(temp.c_str(), temp.length());
			string->type = dsql_str::TYPE_HEXA;
			string->str_charset = "BINARY";
			yylval.legacyStr = string;

			return STRING;
		}  // if (!hexerror)...

		// If we got here, there was a parsing error.  Set the
		// position back to where it was before we messed with
		// it.  Then fall through to the next thing we might parse.

		c = *lex.last_token;
		lex.ptr = lex.last_token + 1;
	}

	if ((c == 'q' || c == 'Q') && lex.ptr + 3 < lex.end && *lex.ptr == '\'')
	{
		StrMark mark;
		mark.pos = lex.last_token - lex.start;

		char endChar = *++lex.ptr;
		switch (endChar)
		{
			case '{':
				endChar = '}';
				break;
			case '(':
				endChar = ')';
				break;
			case '[':
				endChar = ']';
				break;
			case '<':
				endChar = '>';
				break;
		}

		while (++lex.ptr + 1 < lex.end)
		{
			if (*lex.ptr == endChar && *++lex.ptr == '\'')
			{
				yylval.legacyStr = MAKE_string(lex.last_token + 3, lex.ptr - lex.last_token - 4);
				yylval.legacyStr->type = dsql_str::TYPE_ALTERNATE;
				lex.ptr++;

				mark.length = lex.ptr - lex.last_token;
				mark.str = yylval.legacyStr;
				strMarks.put(mark.str, mark);

				return STRING;
			}
		}

		// If we got here, there was a parsing error.  Set the
		// position back to where it was before we messed with
		// it.  Then fall through to the next thing we might parse.

		c = *lex.last_token;
		lex.ptr = lex.last_token + 1;
	}

	// Hexadecimal numeric constants - 0xBBBBBB
	//
	// where the '0' and the 'X' (or 'x') are literal, followed
	// by a set of nibbles, using 0-9, a-f, or A-F.  Odd numbers
	// of nibbles assume a leading '0'.  The result is converted
	// to an integer, and the result returned to the caller.  The
	// token is identified as a NUMBER if it's a 32-bit or less
	// value, or a NUMBER64INT if it requires a 64-bit number.
	if (c == '0' && lex.ptr + 1 < lex.end && (*lex.ptr == 'x' || *lex.ptr == 'X') &&
		(classes(lex.ptr[1]) & CHR_HEX))
	{
		bool hexerror = false;

		// Remember where we start from, to rescan later.
		// Also we'll need to know the length of the buffer.

		++lex.ptr;  // Skip the 'X' and point to the first digit
		const char* hexstring = lex.ptr;
		int charlen = 0;

		// Time to scan the string. Make sure the characters are legal,
		// and find out how long the hex digit string is.

		for (;;)
		{
			if (lex.ptr >= lex.end)			// Unexpected EOS
			{
				hexerror = true;
				break;
			}

			c = *lex.ptr;

			if (!(classes(c) & CHR_HEX))	// End of digit string
				break;

			++charlen;			// Okay, just count 'em
			++lex.ptr;			// and advance...

			if (charlen > 16)	// Too many digits...
			{
				hexerror = true;
				break;
			}
		}

		// we have a valid hex token. Now give it back, either as
		// an NUMBER or NUMBER64BIT.
		if (!hexerror)
		{
			// if charlen > 8 (something like FFFF FFFF 0, w/o the spaces)
			// then we have to return a NUMBER64BIT. We'll make a string
			// node here, and let make.cpp worry about converting the
			// string to a number and building the node later.
			if (charlen > 8)
			{
				char cbuff[32];
				cbuff[0] = 'X';
				strncpy(&cbuff[1], hexstring, charlen);
				cbuff[charlen + 1] = '\0';

				char* p = &cbuff[1];

				while (*p != '\0')
				{
					if ((*p >= 'a') && (*p <= 'f'))
						*p = UPPER(*p);
					p++;
				}

				yylval.legacyStr = MAKE_string(cbuff, strlen(cbuff));
				return NUMBER64BIT;
			}
			else
			{
				// we have an integer value. we'll return NUMBER.
				// but we have to make a number value to be compatible
				// with existing code.

				// See if the string length is odd.  If so,
				// we'll assume a leading zero.  Then figure out the length
				// of the actual resulting hex string.  Allocate a second
				// temporary buffer for it.

				bool nibble = (charlen & 1);  // IS_ODD(temp.length)

				// Re-scan over the hex string we got earlier, converting
				// adjacent bytes into nibble values.  Every other nibble,
				// write the saved byte to the temp space.  At the end of
				// this, the temp.space area will contain the binary
				// representation of the hex constant.

				UCHAR byte = 0;
				SINT64 value = 0;

				for (int i = 0; i < charlen; i++)
				{
					c = UPPER(hexstring[i]);

					// Now convert the character to a nibble

					if (c >= 'A')
						c = (c - 'A') + 10;
					else
						c = (c - '0');

					if (nibble)
					{
						byte = (byte << 4) + (UCHAR) c;
						nibble = false;
						value = (value << 8) + byte;
					}
					else
					{
						byte = c;
						nibble = true;
					}
				}

				yylval.int32Val = (SLONG) value;
				return NUMBER;
			} // integer value
		}  // if (!hexerror)...

		// If we got here, there was a parsing error.  Set the
		// position back to where it was before we messed with
		// it.  Then fall through to the next thing we might parse.

		c = *lex.last_token;
		lex.ptr = lex.last_token + 1;
	} // headecimal numeric constants

	if ((tok_class & CHR_DIGIT) ||
		((c == '.') && (lex.ptr < lex.end) && (classes(*lex.ptr) & CHR_DIGIT)))
	{
		// The following variables are used to recognize kinds of numbers.

		bool have_error	 = false;	// syntax error or value too large
		bool have_digit	 = false;	// we've seen a digit
		bool have_decimal   = false;	// we've seen a '.'
		bool have_exp	   = false;	// digit ... [eE]
		bool have_exp_sign  = false; // digit ... [eE] {+-]
		bool have_exp_digit = false; // digit ... [eE] ... digit
		FB_UINT64 number		= 0;
		FB_UINT64 limit_by_10	= MAX_SINT64 / 10;

		for (--lex.ptr; lex.ptr < lex.end; lex.ptr++)
		{
			c = *lex.ptr;
			if (have_exp_digit && (! (classes(c) & CHR_DIGIT)))
				// First non-digit after exponent and digit terminates the token.
				break;

			if (have_exp_sign && (! (classes(c) & CHR_DIGIT)))
			{
				// only digits can be accepted after "1E-"
				have_error = true;
				break;
			}

			if (have_exp)
			{
				// We've seen e or E, but nothing beyond that.
				if ( ('-' == c) || ('+' == c) )
					have_exp_sign = true;
				else if ( classes(c) & CHR_DIGIT )
					// We have a digit: we haven't seen a sign yet, but it's too late now.
					have_exp_digit = have_exp_sign  = true;
				else
				{
					// end of the token
					have_error = true;
					break;
				}
			}
			else if ('.' == c)
			{
				if (!have_decimal)
					have_decimal = true;
				else
				{
					have_error = true;
					break;
				}
			}
			else if (classes(c) & CHR_DIGIT)
			{
				// Before computing the next value, make sure there will be no overflow.

				have_digit = true;

				if (number >= limit_by_10)
				{
					// possibility of an overflow
					if ((number > limit_by_10) || (c > '8'))
					{
						have_error = true;
						break;
					}
				}
				number = number * 10 + (c - '0');
			}
			else if ( (('E' == c) || ('e' == c)) && have_digit )
				have_exp = true;
			else
				// Unexpected character: this is the end of the number.
				break;
		}

		// We're done scanning the characters: now return the right kind
		// of number token, if any fits the bill.

		if (!have_error)
		{
			fb_assert(have_digit);

			if (have_exp_digit)
			{
				yylval.legacyStr = MAKE_string(lex.last_token, lex.ptr - lex.last_token);
				lex.last_token_bk = lex.last_token;
				lex.line_start_bk = lex.line_start;
				lex.lines_bk = lex.lines;

				return FLOAT_NUMBER;
			}

			if (!have_exp)
			{

				// We should return some kind (scaled-) integer type
				// except perhaps in dialect 1.

				if (!have_decimal && (number <= MAX_SLONG))
				{
					yylval.int32Val = (SLONG) number;
					//printf ("parse.y %p %d\n", yylval.legacyStr, number);
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
						ERRD_post_warning(Arg::Warning(isc_dsql_warning_number_ambiguous) <<
										  Arg::Str(Firebird::string(lex.last_token, lex.ptr - lex.last_token)));
						ERRD_post_warning(Arg::Warning(isc_dsql_warning_number_ambiguous1));
					}

					yylval.legacyStr = MAKE_string(lex.last_token, lex.ptr - lex.last_token);

					lex.last_token_bk = lex.last_token;
					lex.line_start_bk = lex.line_start;
					lex.lines_bk = lex.lines;

					if (client_dialect < SQL_DIALECT_V6_TRANSITION)
						return FLOAT_NUMBER;

					if (have_decimal)
						return SCALEDINT;

					return NUMBER64BIT;
				}
			} // else if (!have_exp)
		} // if (!have_error)

		// we got some kind of error or overflow, so don't recognize this
		// as a number: just pass it through to the next part of the lexer.
	}

	// Restore the status quo ante, before we started our unsuccessful
	// attempt to recognize a number.
	lex.ptr = lex.last_token;
	c = *lex.ptr++;
	// We never touched tok_class, so it doesn't need to be restored.

	// end of number-recognition code

	if (tok_class & CHR_LETTER)
	{
		char* p = string;
		check_copy_incr(p, UPPER (c), string);
		for (; lex.ptr < lex.end && classes(*lex.ptr) & CHR_IDENT; lex.ptr++)
		{
			if (lex.ptr >= lex.end)
				return -1;
			check_copy_incr(p, UPPER (*lex.ptr), string);
		}

		check_bound(p, string);
		*p = 0;

		Firebird::string str(string, p - string);
		KeywordVersion* keyVer = keywordsMap->get(str);

		if (keyVer && parser_version >= keyVer->version &&
			(keyVer->keyword != COMMENT || lex.prev_keyword == -1))
		{
			yylval.legacyStr = keyVer->str;
			lex.last_token_bk = lex.last_token;
			lex.line_start_bk = lex.line_start;
			lex.lines_bk = lex.lines;
			return keyVer->keyword;
		}

		if (p > &string[MAX_SQL_IDENTIFIER_LEN])
			yyabandon(-104, isc_dyn_name_longer);

		yylval.legacyStr = MAKE_string(string, p - string);
		lex.last_token_bk = lex.last_token;
		lex.line_start_bk = lex.line_start;
		lex.lines_bk = lex.lines;
		return SYMBOL;
	}

	// Must be punctuation -- test for double character punctuation

	if (lex.last_token + 1 < lex.end)
	{
		Firebird::string str(lex.last_token, 2);
		KeywordVersion* keyVer = keywordsMap->get(str);

		if (keyVer && parser_version >= keyVer->version)
		{
			++lex.ptr;
			return keyVer->keyword;
		}
	}

	// Single character punctuation are simply passed on

	return (UCHAR) c;
}


void Parser::yyerror_detailed(const TEXT* /*error_string*/, int yychar, YYSTYPE&, YYPOSN&)
{
/**************************************
 *
 *	y y e r r o r _ d e t a i l e d
 *
 **************************************
 *
 * Functional description
 *	Print a syntax error.
 *
 **************************************/
	const TEXT* line_start = lex.line_start;
	SLONG lines = lex.lines;
	if (lex.last_token < lex.line_start)
	{
		line_start = lex.line_start_bk;
		lines--;
	}

	if (yychar < 1)
	{
		ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-104) <<
				  // Unexpected end of command
				  Arg::Gds(isc_command_end_err2) << Arg::Num(lines) <<
													Arg::Num(lex.last_token - line_start + 1));
	}
	else
	{
		ERRD_post (Arg::Gds(isc_sqlerr) << Arg::Num(-104) <<
				  // Token unknown - line %d, column %d
				  Arg::Gds(isc_dsql_token_unk_err) << Arg::Num(lines) <<
				  									  Arg::Num(lex.last_token - line_start + 1) << // CVC: +1
				  // Show the token
				  Arg::Gds(isc_random) << Arg::Str(string(lex.last_token, lex.ptr - lex.last_token)));
	}
}


// The argument passed to this function is ignored. Therefore, messages like
// "syntax error" and "yacc stack overflow" are never seen.
void Parser::yyerror(const TEXT* error_string)
{
	YYSTYPE errt_value;
	YYPOSN errt_posn;
	yyerror_detailed(error_string, -1, errt_value, errt_posn);
}


static void yyabandon (SLONG sql_code, ISC_STATUS error_symbol)
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

	ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(sql_code) <<
			  Arg::Gds(error_symbol));
}
