/*
 *	PROGRAM:	JRD Access Method
 *	MODULE:		constants.h
 *	DESCRIPTION:	Misc system constants
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
 * 2001.10.08 Claudio Valderrama: fb_sysflag enum with numbering
 *   for automatically created triggers that aren't system triggers.
 */

#ifndef JRD_CONSTANTS_H
#define JRD_CONSTANTS_H

/* BLOb Subtype definitions */

/* Subtypes < 0  are user defined 
 * Subtype  0    means "untyped" 
 * Subtypes > 0  are InterBase defined 
 */

// BRS 29-Apr-2004
// replace those constants with public defined ones isc_blob_*
//
//const int BLOB_untyped	= 0;
//
//const int BLOB_text		= 1;
//const int BLOB_blr		= 2;
//const int BLOB_acl		= 3;
//const int BLOB_ranges	= 4;
//const int BLOB_summary	= 5;
//const int BLOB_format	= 6;
//const int BLOB_tra		= 7;
//const int BLOB_extfile	= 8;
//const int BLOB_max_predefined_subtype = 9;
//

/* Column Limits */

const ULONG MAX_COLUMN_SIZE	= 32767;	/* Bytes */

/* Misc constant values */

const int USERNAME_LENGTH	= 31;	/* Characters */

const size_t MAX_SQL_IDENTIFIER_SIZE = 32;
const size_t MAX_SQL_IDENTIFIER_LEN = MAX_SQL_IDENTIFIER_SIZE - 1;
typedef TEXT SqlIdentifier[MAX_SQL_IDENTIFIER_SIZE];

const char* const NULL_ROLE = "NONE";

const char* const PRIMARY_KEY		= "PRIMARY KEY";
const char* const FOREIGN_KEY		= "FOREIGN KEY";
const char* const UNIQUE_CNSTRT		= "UNIQUE";
const char* const CHECK_CNSTRT		= "CHECK";
const char* const NOT_NULL_CNSTRT	= "NOT NULL";

const char* const REL_SCOPE_PERSISTENT		= "persistent table \"%s\"";
const char* const REL_SCOPE_GTT_PRESERVE	= "global temporary table \"%s\" of type on commit preserve rows";
const char* const REL_SCOPE_GTT_DELETE		= "global temporary table \"%s\" of type on commit delete rows";

/* literal strings in rdb$ref_constraints to be used to identify
   the cascade actions for referential constraints. Used
   by isql/show and isql/extract for now. */

const char* const RI_ACTION_CASCADE = "CASCADE";
const char* const RI_ACTION_NULL    = "SET NULL";
const char* const RI_ACTION_DEFAULT = "SET DEFAULT";
const char* const RI_ACTION_NONE    = "NO ACTION";
const char* const RI_RESTRICT       = "RESTRICT";

const char* const IMPLICIT_DOMAIN_PREFIX = "RDB$";
const int IMPLICIT_DOMAIN_PREFIX_LEN = 4;

const char* const SQL_SECCLASS_PREFIX = "SQL$";
const int SQL_SECCLASS_PREFIX_LEN = 4;


/******************************************/
/* System flag meaning - mainly Firebird. */
/******************************************/

enum fb_sysflag {
	fb_sysflag_user = 0,
	fb_sysflag_system = 1,
	fb_sysflag_qli = 2,
	fb_sysflag_check_constraint = 3,
	fb_sysflag_referential_constraint = 4,
	fb_sysflag_view_check = 5
};


/* UDF Arguments are numbered from 0 to MAX_UDF_ARGUMENTS --
   argument 0 is reserved for the return-type of the UDF */

const int MAX_UDF_ARGUMENTS	= 10;

// Maximum length of single line returned from pretty printer
const int PRETTY_BUFFER_SIZE = 1024;

const int MAX_INDEX_SEGMENTS = 16;

// Maximum index key length
// AB: If the maximum key-size will change, don't forget dyn.h and dba.epp 
// which cannot use these defines.
const int MAX_KEY			= 4096;		// Maximum page size possible divide by 4 (16384 / 4)
const int MAX_KEY_PRE_ODS11	= 255;		// Max key-size before ODS11

const USHORT SQL_MATCH_1_CHAR		= '_';	/* Not translatable */
const USHORT SQL_MATCH_ANY_CHARS	= '%';	/* Not translatable */

static const USHORT GDML_MATCH_ONE	= '?';
static const USHORT GDML_MATCH_ANY	= '*';

static const USHORT GDML_QUOTE		= '@';
static const USHORT GDML_NOT		= '~';
static const USHORT GDML_RANGE		= '-';
static const USHORT GDML_CLASS_START= '[';
static const USHORT GDML_CLASS_END	= ']';
static const USHORT GDML_SUBSTITUTE	= '=';
static const USHORT GDML_FLAG_SET	= '+';
static const USHORT GDML_FLAG_CLEAR	= '-';
static const USHORT GDML_COMMA		= ',';
static const USHORT GDML_LPAREN		= '(';
static const USHORT GDML_RPAREN		= ')';

const size_t MAX_CONTEXT_VARS	= 1000;		// Maximum number of context variables allowed for a single object

// Time precision limits and defaults for TIME/TIMESTAMP values.
// Currently they're applied to CURRENT_TIME[STAMP] expressions only.

// Should be more than 6 as per SQL spec, but we don't support more than 3 yet
const size_t MAX_TIME_PRECISION			= 3;
// Consistent with the SQL spec
const size_t DEFAULT_TIME_PRECISION		= 0;
// Should be 6 as per SQL spec
const size_t DEFAULT_TIMESTAMP_PRECISION	= 3;

// relation types

enum rel_t {
	rel_persistent = 0,
	rel_view = 1,
	rel_external = 2,
	rel_virtual = 3,
	rel_global_temp_preserve = 4,
	rel_global_temp_delete = 5
};

// procedure types

enum prc_t {
	prc_legacy = 0,
	prc_selectable = 1,
	prc_executable = 2
};

// states

enum att_state_t {
	att_s_idle = 0,
	att_s_active = 1,
	att_s_killed = 2
};

enum tra_state_t {
	tra_s_idle = 0,
	tra_s_active = 1,
};

enum stmt_state_t {
	stmt_s_idle = 0,
	stmt_s_stalled = 1,
	stmt_s_active = 2,
	stmt_s_killed = 3
};

// shutdown modes

enum shut_mode_t {
	shut_mode_online = 0,
	shut_mode_single = 1,
	shut_mode_multi = 2,
	shut_mode_full = 3
};

// transaction isolation levels

enum tra_iso_mode_t {
	iso_mode_consistency = 0,
	iso_mode_concurrency = 1,
	iso_mode_rc_version = 2,
	iso_mode_rc_no_version = 3
};

#endif // JRD_CONSTANTS_H
