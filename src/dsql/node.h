/*
 *	PROGRAM:	Dynamic SQL runtime support
 *	MODULE:		node.h
 *	DESCRIPTION:	Definitions needed for accessing a parse tree
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
 * 2001.6.12 Claudio Valderrama: add break_* constants.
 * 2001.6.30 Claudio valderrama: Jim Starkey suggested to hold information
 * about source line in each node that's created.
 * 2001.07.28 John Bellardo: Added e_rse_limit to nod_rse and nod_limit.
 * 2001.08.03 John Bellardo: Reordered args to no_sel for new LIMIT syntax
 * 2002.07.30 Arno Brinkman:
 * 2002.07.30	Added nod_searched_case, nod_simple_case, nod_coalesce
 * 2002.07.30	and constants for arguments
 * 2002.08.04 Dmitry Yemanov: ALTER VIEW
 * 2002.10.21 Nickolay Samofatov: Added support for explicit pessimistic locks
 * 2002.10.29 Nickolay Samofatov: Added support for savepoints
 * 2004.01.16 Vlad Horsun: added support for default parameters and
 *   EXECUTE BLOCK statement
 * Adriano dos Santos Fernandes
 */

#ifndef DSQL_NODE_H
#define DSQL_NODE_H

#include "../dsql/dsql.h"
#include "../common/classes/Aligner.h"

// an enumeration of the possible node types in a syntax tree

namespace Dsql {

enum nod_t
{
	// NOTE: when adding an expression node, be sure to
	// test various combinations; in particular, think
	// about parameterization using a '?' - there is code
	// in PASS1_node which must be updated to find the
	// data type of the parameter based on the arguments
	// to an expression node.

	nod_unknown_type = 0,
	nod_references,
	nod_proc_obj,
	nod_trig_obj,
	nod_view_obj,
	nod_list,
	nod_select,
	nod_insert,
	nod_delete,
	nod_update,
	nod_all,	// ALL privileges
	nod_execute,	// EXECUTE privilege
	nod_procedure_name,
	nod_flag,
	nod_user_name,
	nod_user_group,
	nod_table_lock,
	nod_lock_mode,
	nod_plan_expr,
	nod_plan_item,
	nod_natural,
	nod_index,
	nod_index_order,
	nod_role_name,
	nod_label, // label support
	nod_class_exprnode,
	nod_package_name,
	nod_package_obj,
	nod_func_obj,
	nod_function_name
};

/* enumerations of the arguments to a node, offsets
   within the variable tail nod_arg */

/* Note Bene:
 *	e_<nodename>_count	== count of arguments in nod_arg
 *	This is often used as the count of sub-nodes, but there
 *	are cases when non-DSQL_NOD arguments are stuffed into nod_arg
 *	entries.  These include nod_udf.
 */
enum node_args {
	e_lock_tables = 0,
	e_lock_mode,

	e_label_name = 0,
	e_label_number,
	e_label_count
};

} // namespace

namespace Jrd {

typedef Dsql::nod_t NOD_TYPE;

// definition of a syntax node created both in parsing and in context recognition

class dsql_nod : public pool_alloc_rpt<class dsql_nod*, dsql_type_nod>
{
public:
	NOD_TYPE nod_type;			// Type of node
	DSC nod_desc;				// Descriptor
	USHORT nod_line;			// Source line of the statement.
	USHORT nod_column;			// Source column of the statement.
	USHORT nod_count;			// Number of arguments
	USHORT nod_flags;
	// In two adjacent elements (0 and 1) 64-bit value is placed sometimes
	RPT_ALIGN(dsql_nod* nod_arg[1]);

	dsql_nod() : nod_type(Dsql::nod_unknown_type), nod_count(0), nod_flags(0) {}
};

// values of flags
enum nod_flags_vals {
	NOD_SHARED				= 1, // nod_lock_mode
	NOD_PROTECTED			= 2,
	NOD_READ				= 4,
	NOD_WRITE				= 8
};

} // namespace

#endif // DSQL_NODE_H
