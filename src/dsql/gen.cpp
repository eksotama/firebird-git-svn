/*
 *	PROGRAM:	Dynamic SQL runtime support
 *	MODULE:		gen.cpp
 *	DESCRIPTION:	Routines to generate BLR.
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
 * Contributor(s): ______________________________________
 * 2001.6.21 Claudio Valderrama: BREAK and SUBSTRING.
 * 2001.07.28 John Bellardo:  Added code to generate blr_skip.
 * 2002.07.30 Arno Brinkman:  Added code, procedures to generate COALESCE, CASE
 * 2002.09.28 Dmitry Yemanov: Reworked internal_info stuff, enhanced
 *                            exception handling in SPs/triggers,
 *                            implemented ROWS_AFFECTED system variable
 * 2002.10.21 Nickolay Samofatov: Added support for explicit pessimistic locks
 * 2002.10.29 Nickolay Samofatov: Added support for savepoints
 * 2003.10.05 Dmitry Yemanov: Added support for explicit cursors in PSQL
 * 2004.01.16 Vlad Horsun: Added support for default parameters and
 *   EXECUTE BLOCK statement
 * Adriano dos Santos Fernandes
 */

#include "firebird.h"
#include <string.h>
#include <stdio.h>
#include "../dsql/dsql.h"
#include "../dsql/node.h"
#include "../dsql/DdlNodes.h"
#include "../dsql/StmtNodes.h"
#include "../jrd/ibase.h"
#include "../jrd/align.h"
#include "../jrd/constants.h"
#include "../jrd/intl.h"
#include "../jrd/jrd.h"
#include "../jrd/val.h"
#include "../dsql/ddl_proto.h"
#include "../dsql/errd_proto.h"
#include "../dsql/gen_proto.h"
#include "../dsql/make_proto.h"
#include "../dsql/metd_proto.h"
#include "../dsql/misc_func.h"
#include "../dsql/utld_proto.h"
#include "../jrd/thread_proto.h"
#include "../jrd/dsc_proto.h"
#include "../jrd/why_proto.h"
#include "gen/iberror.h"
#include "../common/StatusArg.h"

using namespace Jrd;
using namespace Dsql;
using namespace Firebird;

static void gen_aggregate(DsqlCompilerScratch*, const dsql_nod*);
static void gen_cast(DsqlCompilerScratch*, const dsql_nod*);
static void gen_coalesce(DsqlCompilerScratch*, const dsql_nod*);
static void gen_constant(DsqlCompilerScratch*, const dsc*, bool);
static void gen_constant(DsqlCompilerScratch*, dsql_nod*, bool);
static void gen_error_condition(DsqlCompilerScratch*, const dsql_nod*);
static void gen_exec_stmt(DsqlCompilerScratch* dsqlScratch, const dsql_nod* node);
static void gen_field(DsqlCompilerScratch*, const dsql_ctx*, const dsql_fld*, dsql_nod*);
static void gen_for_select(DsqlCompilerScratch*, const dsql_nod*);
static void gen_gen_id(DsqlCompilerScratch*, const dsql_nod*);
static void gen_join_rse(DsqlCompilerScratch*, const dsql_nod*);
static void gen_map(DsqlCompilerScratch*, dsql_map*);
static inline void gen_optional_expr(DsqlCompilerScratch*, const UCHAR code, dsql_nod*);
static void gen_parameter(DsqlCompilerScratch*, const dsql_par*);
static void gen_plan(DsqlCompilerScratch*, const dsql_nod*);
static void gen_relation(DsqlCompilerScratch*, dsql_ctx*);
static void gen_rse(DsqlCompilerScratch*, const dsql_nod*);
static void gen_searched_case(DsqlCompilerScratch*, const dsql_nod*);
static void gen_select(DsqlCompilerScratch*, dsql_nod*);
static void gen_simple_case(DsqlCompilerScratch*, const dsql_nod*);
static void gen_sort(DsqlCompilerScratch*, dsql_nod*);
static void gen_statement(DsqlCompilerScratch*, const dsql_nod*);
static void gen_sys_function(DsqlCompilerScratch*, const dsql_nod*);
static void gen_table_lock(DsqlCompilerScratch*, const dsql_nod*, USHORT);
static void gen_udf(DsqlCompilerScratch*, const dsql_nod*);
static void gen_union(DsqlCompilerScratch*, const dsql_nod*);
static void stuff_context(DsqlCompilerScratch*, const dsql_ctx*);
static void stuff_meta_string(DsqlCompiledStatement*, const char*);

// STUFF is defined in dsql.h for use in common with ddl.c

// The following are passed as the third argument to gen_constant
const bool NEGATE_VALUE = true;
const bool USE_VALUE    = false;


void GEN_hidden_variables(DsqlCompilerScratch* dsqlScratch, bool inExpression)
{
/**************************************
 *
 *	G E N _ h i d d e n _ v a r i a b l e s
 *
 **************************************
 *
 * Function
 *	Emit BLR for hidden variables.
 *
 **************************************/
	if (dsqlScratch->hiddenVars.isEmpty())
		return;

	if (inExpression)
	{
		stuff(dsqlScratch->getStatement(), blr_stmt_expr);
		if (dsqlScratch->hiddenVars.getCount() > 1)
			stuff(dsqlScratch->getStatement(), blr_begin);
	}

	for (DsqlNodStack::const_iterator i(dsqlScratch->hiddenVars); i.hasData(); ++i)
	{
		const dsql_nod* varNode = i.object()->nod_arg[1];
		const dsql_var* var = (dsql_var*) varNode->nod_arg[e_var_variable];
		dsqlScratch->getStatement()->append_uchar(blr_dcl_variable);
		dsqlScratch->getStatement()->append_ushort(var->var_variable_number);
		GEN_descriptor(dsqlScratch, &varNode->nod_desc, true);
	}

	if (inExpression && dsqlScratch->hiddenVars.getCount() > 1)
		stuff(dsqlScratch->getStatement(), blr_end);

	// Clear it for GEN_expr not regenerate them.
	dsqlScratch->hiddenVars.clear();
}


/**

 	GEN_expr

    @brief	Generate blr for an arbitrary expression.


    @param dsqlScratch
    @param node

 **/
void GEN_expr(DsqlCompilerScratch* dsqlScratch, dsql_nod* node)
{
	UCHAR blr_operator;
	const dsql_ctx* context;

	switch (node->nod_type)
	{
	case nod_alias:
		GEN_expr(dsqlScratch, node->nod_arg[e_alias_value]);
		return;

	case nod_aggregate:
		gen_aggregate(dsqlScratch, node);
		return;

	case nod_constant:
		gen_constant(dsqlScratch, node, USE_VALUE);
		return;

	case nod_derived_field:
		// ASF: If we are not referencing a field, we should evaluate the expression based on
		// a set (ORed) of contexts. If any of them are in a valid position the expression is
		// evaluated, otherwise a NULL will be returned. This is fix for CORE-1246.
		if (node->nod_arg[e_derived_field_value]->nod_type != nod_derived_field &&
			node->nod_arg[e_derived_field_value]->nod_type != nod_field &&
			node->nod_arg[e_derived_field_value]->nod_type != nod_dbkey &&
			node->nod_arg[e_derived_field_value]->nod_type != nod_map)
		{
			const dsql_ctx* ctx = (dsql_ctx*) node->nod_arg[e_derived_field_context];

			if (ctx->ctx_main_derived_contexts.hasData())
			{
				if (ctx->ctx_main_derived_contexts.getCount() > MAX_UCHAR)
				{
					ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-204) <<
							  Arg::Gds(isc_imp_exc) <<
							  Arg::Gds(isc_ctx_too_big));
				}

				stuff(dsqlScratch->getStatement(), blr_derived_expr);
				stuff(dsqlScratch->getStatement(), ctx->ctx_main_derived_contexts.getCount());

				for (DsqlContextStack::const_iterator stack(ctx->ctx_main_derived_contexts);
					 stack.hasData(); ++stack)
				{
					fb_assert(stack.object()->ctx_context <= MAX_UCHAR);
					stuff(dsqlScratch->getStatement(), stack.object()->ctx_context);
				}
			}
		}
		GEN_expr(dsqlScratch, node->nod_arg[e_derived_field_value]);
		return;

	case nod_extract:
		stuff(dsqlScratch->getStatement(), blr_extract);
		stuff(dsqlScratch->getStatement(), node->nod_arg[e_extract_part]->getSlong());
		GEN_expr(dsqlScratch, node->nod_arg[e_extract_value]);
		return;

	case nod_strlen:
		stuff(dsqlScratch->getStatement(), blr_strlen);
		stuff(dsqlScratch->getStatement(), node->nod_arg[e_strlen_type]->getSlong());
		GEN_expr(dsqlScratch, node->nod_arg[e_strlen_value]);
		return;

	case nod_dbkey:
		node = node->nod_arg[0];
		context = (dsql_ctx*) node->nod_arg[e_rel_context];
		stuff(dsqlScratch->getStatement(), blr_dbkey);
		stuff_context(dsqlScratch, context);
		return;

	case nod_rec_version:
		node = node->nod_arg[0];
		context = (dsql_ctx*) node->nod_arg[e_rel_context];
		stuff(dsqlScratch->getStatement(), blr_record_version);
		stuff_context(dsqlScratch, context);
		return;

	case nod_dom_value:
		stuff(dsqlScratch->getStatement(), blr_fid);
		stuff(dsqlScratch->getStatement(), 0);				// Context
		stuff_word(dsqlScratch->getStatement(), 0);			// Field id
		return;

	case nod_field:
		gen_field(dsqlScratch,
				  (dsql_ctx*) node->nod_arg[e_fld_context],
				  (dsql_fld*) node->nod_arg[e_fld_field],
				  node->nod_arg[e_fld_indices]);
		return;

	case nod_user_name:
		stuff(dsqlScratch->getStatement(), blr_user_name);
		return;

	case nod_current_time:
		if (node->nod_arg[0])
		{
			const dsql_nod* const_node = node->nod_arg[0];
			fb_assert(const_node->nod_type == nod_constant);
			const int precision = (int) const_node->getSlong();
			stuff(dsqlScratch->getStatement(), blr_current_time2);
			stuff(dsqlScratch->getStatement(), precision);
		}
		else {
			stuff(dsqlScratch->getStatement(), blr_current_time);
		}
		return;

	case nod_current_timestamp:
		if (node->nod_arg[0])
		{
			const dsql_nod* const_node = node->nod_arg[0];
			fb_assert(const_node->nod_type == nod_constant);
			const int precision = (int) const_node->getSlong();
			stuff(dsqlScratch->getStatement(), blr_current_timestamp2);
			stuff(dsqlScratch->getStatement(), precision);
		}
		else {
			stuff(dsqlScratch->getStatement(), blr_current_timestamp);
		}
		return;

	case nod_current_date:
		stuff(dsqlScratch->getStatement(), blr_current_date);
		return;

	case nod_current_role:
		stuff(dsqlScratch->getStatement(), blr_current_role);
		return;

	case nod_udf:
		gen_udf(dsqlScratch, node);
		return;

	case nod_sys_function:
		gen_sys_function(dsqlScratch, node);
		return;

	case nod_variable:
		{
			const dsql_var* variable = (dsql_var*) node->nod_arg[e_var_variable];
			if (variable->var_type == VAR_input)
			{
				stuff(dsqlScratch->getStatement(), blr_parameter2);
				stuff(dsqlScratch->getStatement(), variable->var_msg_number);
				stuff_word(dsqlScratch->getStatement(), variable->var_msg_item);
				stuff_word(dsqlScratch->getStatement(), variable->var_msg_item + 1);
			}
			else
			{
				stuff(dsqlScratch->getStatement(), blr_variable);
				stuff_word(dsqlScratch->getStatement(), variable->var_variable_number);
			}
		}
		return;

	case nod_join:
		gen_join_rse(dsqlScratch, node);
		return;

	case nod_map:
		{
			const dsql_map* map = (dsql_map*) node->nod_arg[e_map_map];
			stuff(dsqlScratch->getStatement(), blr_fid);
			if (map->map_partition)
				stuff(dsqlScratch->getStatement(), map->map_partition->context);
			else
			{
				context = (dsql_ctx*) node->nod_arg[e_map_context];
				stuff_context(dsqlScratch, context);
			}
			stuff_word(dsqlScratch->getStatement(), map->map_position);
		}
		return;

	case nod_parameter:
		gen_parameter(dsqlScratch, (dsql_par*) node->nod_arg[e_par_parameter]);
		return;

	case nod_relation:
		gen_relation(dsqlScratch, (dsql_ctx*) node->nod_arg[e_rel_context]);
		return;

	case nod_rse:
		gen_rse(dsqlScratch, node);
		return;

	case nod_derived_table:
		gen_rse(dsqlScratch, node->nod_arg[e_derived_table_rse]);
		return;

	case nod_exists:
		stuff(dsqlScratch->getStatement(), blr_any);
		gen_rse(dsqlScratch, node->nod_arg[0]);
		return;

	case nod_singular:
		stuff(dsqlScratch->getStatement(), blr_unique);
		gen_rse(dsqlScratch, node->nod_arg[0]);
		return;

	case nod_agg_count:
		if (node->nod_count)
			blr_operator = (node->nod_flags & NOD_AGG_DISTINCT) ?
				blr_agg_count_distinct : blr_agg_count2;
		else
			blr_operator = blr_agg_count;
		break;

	case nod_agg_min:
		blr_operator = blr_agg_min;
		break;
	case nod_agg_max:
		blr_operator = blr_agg_max;
		break;

	case nod_window:
		GEN_expr(dsqlScratch, node->nod_arg[0]);
		return;

	case nod_agg_average:
		blr_operator = (node->nod_flags & NOD_AGG_DISTINCT) ?
			blr_agg_average_distinct : blr_agg_average;
		break;

	case nod_agg_total:
		blr_operator = (node->nod_flags & NOD_AGG_DISTINCT) ? blr_agg_total_distinct : blr_agg_total;
		break;

	case nod_agg_average2:
		blr_operator = (node->nod_flags & NOD_AGG_DISTINCT) ?
			blr_agg_average_distinct : blr_agg_average;
		break;

	case nod_agg_total2:
		blr_operator = (node->nod_flags & NOD_AGG_DISTINCT) ? blr_agg_total_distinct : blr_agg_total;
		break;

	case nod_agg_list:
		blr_operator = (node->nod_flags & NOD_AGG_DISTINCT) ? blr_agg_list_distinct : blr_agg_list;
		break;

	case nod_and:
		blr_operator = blr_and;
		break;
	case nod_or:
		blr_operator = blr_or;
		break;
	case nod_not:
		blr_operator = blr_not;
		break;
	case nod_eql:
		blr_operator = blr_eql;
		break;
	case nod_equiv:
		blr_operator = blr_equiv;
		break;
	case nod_neq:
		blr_operator = blr_neq;
		break;
	case nod_gtr:
		blr_operator = blr_gtr;
		break;
	case nod_leq:
		blr_operator = blr_leq;
		break;
	case nod_geq:
		blr_operator = blr_geq;
		break;
	case nod_lss:
		blr_operator = blr_lss;
		break;
	case nod_between:
		blr_operator = blr_between;
		break;
	case nod_containing:
		blr_operator = blr_containing;
		break;
	case nod_similar:
		stuff(dsqlScratch->getStatement(), blr_similar);
		GEN_expr(dsqlScratch, node->nod_arg[e_similar_value]);
		GEN_expr(dsqlScratch, node->nod_arg[e_similar_pattern]);

		if (node->nod_arg[e_similar_escape])
		{
			stuff(dsqlScratch->getStatement(), 1);
			GEN_expr(dsqlScratch, node->nod_arg[e_similar_escape]);
		}
		else
			stuff(dsqlScratch->getStatement(), 0);

		return;

	case nod_starting:
		blr_operator = blr_starting;
		break;
	case nod_missing:
		blr_operator = blr_missing;
		break;

	case nod_like:
		blr_operator = (node->nod_count == 2) ? blr_like : blr_ansi_like;
		break;

	case nod_add:
		blr_operator = blr_add;
		break;
	case nod_subtract:
		blr_operator = blr_subtract;
		break;
	case nod_multiply:
		blr_operator = blr_multiply;
		break;

	case nod_negate:
		{
			dsql_nod* child = node->nod_arg[0];
			if (child->nod_type == nod_constant && DTYPE_IS_NUMERIC(child->nod_desc.dsc_dtype))
			{
				gen_constant(dsqlScratch, child, NEGATE_VALUE);
				return;
			}
		}
		blr_operator = blr_negate;
		break;

	case nod_divide:
		blr_operator = blr_divide;
		break;
	case nod_add2:
		blr_operator = blr_add;
		break;
	case nod_subtract2:
		blr_operator = blr_subtract;
		break;
	case nod_multiply2:
		blr_operator = blr_multiply;
		break;
	case nod_divide2:
		blr_operator = blr_divide;
		break;
	case nod_concatenate:
		blr_operator = blr_concatenate;
		break;
	case nod_null:
		blr_operator = blr_null;
		break;
	case nod_any:
		blr_operator = blr_any;
		break;
	case nod_ansi_any:
		blr_operator = blr_ansi_any;
		break;
	case nod_ansi_all:
		blr_operator = blr_ansi_all;
		break;
	case nod_via:
		blr_operator = blr_via;
		break;

	case nod_internal_info:
		blr_operator = blr_internal_info;
		break;
	case nod_upcase:
		blr_operator = blr_upcase;
		break;
	case nod_lowcase:
		blr_operator = blr_lowcase;
		break;
	case nod_substr:
        blr_operator = blr_substring;
        break;
	case nod_cast:
		gen_cast(dsqlScratch, node);
		return;
	case nod_gen_id:
	case nod_gen_id2:
		gen_gen_id(dsqlScratch, node);
		return;
    case nod_coalesce:
		gen_coalesce(dsqlScratch, node);
		return;
    case nod_simple_case:
		gen_simple_case(dsqlScratch, node);
		return;
    case nod_searched_case:
		gen_searched_case(dsqlScratch, node);
		return;

	case nod_trim:
		stuff(dsqlScratch->getStatement(), blr_trim);
		stuff(dsqlScratch->getStatement(), node->nod_arg[e_trim_specification]->getSlong());

		if (node->nod_arg[e_trim_characters])
		{
			stuff(dsqlScratch->getStatement(), blr_trim_characters);
			GEN_expr(dsqlScratch, node->nod_arg[e_trim_characters]);
		}
		else
			stuff(dsqlScratch->getStatement(), blr_trim_spaces);

		GEN_expr(dsqlScratch, node->nod_arg[e_trim_value]);
		return;

	case nod_assign:
		stuff(dsqlScratch->getStatement(), blr_assignment);
		GEN_expr(dsqlScratch, node->nod_arg[0]);
		GEN_expr(dsqlScratch, node->nod_arg[1]);
		return;

	case nod_hidden_var:
		stuff(dsqlScratch->getStatement(), blr_stmt_expr);

		// If it was not pre-declared, declare it now.
		if (dsqlScratch->hiddenVars.hasData())
		{
			const dsql_var* var = (dsql_var*) node->nod_arg[e_hidden_var_var]->nod_arg[e_var_variable];

			stuff(dsqlScratch->getStatement(), blr_begin);
			dsqlScratch->getStatement()->append_uchar(blr_dcl_variable);
			dsqlScratch->getStatement()->append_ushort(var->var_variable_number);
			GEN_descriptor(dsqlScratch, &node->nod_arg[e_hidden_var_var]->nod_desc, true);
		}

		stuff(dsqlScratch->getStatement(), blr_assignment);
		GEN_expr(dsqlScratch, node->nod_arg[e_hidden_var_expr]);
		GEN_expr(dsqlScratch, node->nod_arg[e_hidden_var_var]);

		if (dsqlScratch->hiddenVars.hasData())
			stuff(dsqlScratch->getStatement(), blr_end);

		GEN_expr(dsqlScratch, node->nod_arg[e_hidden_var_var]);
		return;

	default:
		ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-901) <<
				  Arg::Gds(isc_dsql_internal_err) <<
				  // expression evaluation not supported
				  Arg::Gds(isc_expression_eval_err) <<
				  Arg::Gds(isc_dsql_eval_unknode) << Arg::Num(node->nod_type));
	}

	stuff(dsqlScratch->getStatement(), blr_operator);

	dsql_nod* const* ptr = node->nod_arg;
	for (const dsql_nod* const* const end = ptr + node->nod_count; ptr < end; ptr++)
	{
		GEN_expr(dsqlScratch, *ptr);
	}

	// Check whether the node we just processed is for a dialect 3
	// operation which gives a different result than the corresponding
	// operation in dialect 1.  If it is, and if the client dialect is 2,
	// issue a warning about the difference.

	switch (node->nod_type)
	{
	case nod_add2:
	case nod_subtract2:
	case nod_multiply2:
	case nod_divide2:
	case nod_agg_total2:
	case nod_agg_average2:
		dsc desc;
		MAKE_desc(dsqlScratch, &desc, node, NULL);

		if ((node->nod_flags & NOD_COMP_DIALECT) &&
			(dsqlScratch->clientDialect == SQL_DIALECT_V6_TRANSITION))
		{
			const char* s = 0;
			char message_buf[8];

			switch (node->nod_type)
			{
			case nod_add2:
				s = "add";
				break;
			case nod_subtract2:
				s = "subtract";
				break;
			case nod_multiply2:
				s = "multiply";
				break;
			case nod_divide2:
				s = "divide";
				break;
			case nod_agg_total2:
				s = "sum";
				break;
			case nod_agg_average2:
				s = "avg";
				break;
			default:
				sprintf(message_buf, "blr %d", (int) blr_operator);
				s = message_buf;
			}
			ERRD_post_warning(Arg::Warning(isc_dsql_dialect_warning_expr) << Arg::Str(s));
		}
	}
}

/**

 	GEN_port

    @brief	Generate a port from a message.  Feel free to rearrange the
 	order of parameters.


    @param dsqlScratch
    @param message

 **/
void GEN_port(DsqlCompilerScratch* dsqlScratch, dsql_msg* message)
{
	thread_db* tdbb = JRD_get_thread_data();

//	if (dsqlScratch->req_blr_string) {
		stuff(dsqlScratch->getStatement(), blr_message);
		stuff(dsqlScratch->getStatement(), message->msg_number);
		stuff_word(dsqlScratch->getStatement(), message->msg_parameter);
//	}

	ULONG offset = 0;

	for (size_t i = 0; i < message->msg_parameters.getCount(); ++i)
	{
		dsql_par* parameter = message->msg_parameters[i];

		parameter->par_parameter = (USHORT) i;

		const USHORT fromCharSet = parameter->par_desc.getCharSet();
		const USHORT toCharSet = (fromCharSet == CS_NONE || fromCharSet == CS_BINARY) ?
			fromCharSet : tdbb->getCharSet();

		if (parameter->par_desc.dsc_dtype <= dtype_any_text &&
			tdbb->getCharSet() != CS_NONE && tdbb->getCharSet() != CS_BINARY)
		{
			USHORT adjust = 0;
			if (parameter->par_desc.dsc_dtype == dtype_varying)
				adjust = sizeof(USHORT);
			else if (parameter->par_desc.dsc_dtype == dtype_cstring)
				adjust = 1;

			parameter->par_desc.dsc_length -= adjust;

			const USHORT fromCharSetBPC = METD_get_charset_bpc(dsqlScratch->getTransaction(), fromCharSet);
			const USHORT toCharSetBPC = METD_get_charset_bpc(dsqlScratch->getTransaction(), toCharSet);

			parameter->par_desc.setTextType(INTL_CS_COLL_TO_TTYPE(toCharSet,
				(fromCharSet == toCharSet ? INTL_GET_COLLATE(&parameter->par_desc) : 0)));

			parameter->par_desc.dsc_length =
				UTLD_char_length_to_byte_length(parameter->par_desc.dsc_length / fromCharSetBPC, toCharSetBPC);

			parameter->par_desc.dsc_length += adjust;
		}
		else if (parameter->par_desc.dsc_dtype == dtype_blob &&
			parameter->par_desc.dsc_sub_type == isc_blob_text &&
			tdbb->getCharSet() != CS_NONE && tdbb->getCharSet() != CS_BINARY)
		{
			if (fromCharSet != toCharSet)
				parameter->par_desc.setTextType(toCharSet);
		}

		// For older clients - generate an error should they try and
		// access data types which did not exist in the older dialect
		if (dsqlScratch->clientDialect <= SQL_DIALECT_V5)
		{
			switch (parameter->par_desc.dsc_dtype)
			{
				// In V6.0 - older clients, which we distinguish by
				// their use of SQL DIALECT 0 or 1, are forbidden
				// from selecting values of new datatypes
				case dtype_sql_date:
				case dtype_sql_time:
				case dtype_int64:
					ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-804) <<
							  Arg::Gds(isc_dsql_datatype_err) <<
							  Arg::Gds(isc_sql_dialect_datatype_unsupport) <<
									Arg::Num(dsqlScratch->clientDialect) <<
									Arg::Str(DSC_dtype_tostring(parameter->par_desc.dsc_dtype)));
					break;
				default:
					// No special action for other data types
					break;
			}
		}

		const USHORT align = type_alignments[parameter->par_desc.dsc_dtype];
		if (align)
			offset = FB_ALIGN(offset, align);
		parameter->par_desc.dsc_address = (UCHAR*)(IPTR) offset;
		offset += parameter->par_desc.dsc_length;
		GEN_descriptor(dsqlScratch, &parameter->par_desc, false);
	}

	if (offset > MAX_FORMAT_SIZE)
	{
		ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-204) <<
				  Arg::Gds(isc_imp_exc) <<
				  Arg::Gds(isc_blktoobig));
	}

	message->msg_length = (USHORT) offset;

	dsqlScratch->ports.add(message);
}


/**

 	GEN_request

    @brief	Generate complete blr for a dsqlScratch.


    @param dsqlScratch
    @param node

 **/
void GEN_request(dsql_req* /*request*/, DsqlCompilerScratch* scratch, dsql_nod* node)
{
	thread_db* tdbb = JRD_get_thread_data();

	DsqlCompiledStatement* statement = scratch->getStatement();

	if (statement->getType() == DsqlCompiledStatement::TYPE_CREATE_DB ||
		statement->getType() == DsqlCompiledStatement::TYPE_DDL)
	{
		DDL_generate(scratch, node);
		return;
	}

	if (statement->getFlags() & DsqlCompiledStatement::FLAG_BLR_VERSION4)
		stuff(statement, blr_version4);
	else
		stuff(statement, blr_version5);

	if (statement->getType() == DsqlCompiledStatement::TYPE_SAVEPOINT)
	{
		// Do not generate BEGIN..END block around savepoint statement
		// to avoid breaking of savepoint logic
		statement->setSendMsg(NULL);
		statement->setReceiveMsg(NULL);
		GEN_statement(scratch, node);
	}
	else
	{
		stuff(statement, blr_begin);

		GEN_hidden_variables(scratch, false);

		switch (statement->getType())
		{
		case DsqlCompiledStatement::TYPE_SELECT:
		case DsqlCompiledStatement::TYPE_SELECT_UPD:
			gen_select(scratch, node);
			break;
		case DsqlCompiledStatement::TYPE_EXEC_BLOCK:
		case DsqlCompiledStatement::TYPE_SELECT_BLOCK:
			GEN_statement(scratch, node);
			break;
		default:
			{
				dsql_msg* message = statement->getSendMsg();
				if (!message->msg_parameter)
					statement->setSendMsg(NULL);
				else
				{
					GEN_port(scratch, message);
					stuff(statement, blr_receive);
					stuff(statement, message->msg_number);
				}
				message = statement->getReceiveMsg();
				if (!message->msg_parameter)
					statement->setReceiveMsg(NULL);
				else
					GEN_port(scratch, message);
				GEN_statement(scratch, node);
			}
		}
		stuff(statement, blr_end);
	}

	stuff(statement, blr_eoc);
}


/**

 	GEN_start_transaction

    @brief	Generate tpb for set transaction.  Use blr string of dsqlScratch.
 	If a value is not specified, default is not STUFF'ed, let the
 	engine handle it.
 	Do not allow an option to be specified more than once.


    @param dsqlScratch
    @param tran_node

 **/
void GEN_start_transaction( DsqlCompilerScratch* dsqlScratch, const dsql_nod* tran_node)
{
	SSHORT count = tran_node->nod_count;

	if (!count)
		return;

	const dsql_nod* node = tran_node->nod_arg[0];

	if (!node)
		return;

	// Find out isolation level - if specified. This is required for
	// specifying the correct lock level in reserving clause.

	USHORT lock_level = isc_tpb_shared;

	if (count = node->nod_count)
	{
		while (count--)
		{
			const dsql_nod* ptr = node->nod_arg[count];

			if (!ptr || ptr->nod_type != nod_isolation)
				continue;

			lock_level = (ptr->nod_flags & NOD_CONSISTENCY) ? isc_tpb_protected : isc_tpb_shared;
		}
	}


   	bool sw_access = false, sw_wait = false, sw_isolation = false,
		sw_reserve = false, sw_lock_timeout = false;
	int misc_flags = 0;

	// Stuff some version info.
	if (count = node->nod_count)
		stuff(dsqlScratch->getStatement(), isc_tpb_version1);

	while (count--)
	{
		const dsql_nod* ptr = node->nod_arg[count];

		if (!ptr)
			continue;

		switch (ptr->nod_type)
		{
		case nod_access:
			if (sw_access)
				ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-104) <<
						  Arg::Gds(isc_dsql_dup_option));

			sw_access = true;
			if (ptr->nod_flags & NOD_READ_ONLY)
				stuff(dsqlScratch->getStatement(), isc_tpb_read);
			else
				stuff(dsqlScratch->getStatement(), isc_tpb_write);
			break;

		case nod_wait:
			if (sw_wait)
				ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-104) <<
						  Arg::Gds(isc_dsql_dup_option));

			sw_wait = true;
			if (ptr->nod_flags & NOD_NO_WAIT)
				stuff(dsqlScratch->getStatement(), isc_tpb_nowait);
			else
				stuff(dsqlScratch->getStatement(), isc_tpb_wait);
			break;

		case nod_isolation:
			if (sw_isolation)
				ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-104) <<
						  Arg::Gds(isc_dsql_dup_option));

			sw_isolation = true;

			if (ptr->nod_flags & NOD_CONCURRENCY)
				stuff(dsqlScratch->getStatement(), isc_tpb_concurrency);
			else if (ptr->nod_flags & NOD_CONSISTENCY)
				stuff(dsqlScratch->getStatement(), isc_tpb_consistency);
			else
			{
				stuff(dsqlScratch->getStatement(), isc_tpb_read_committed);

				if (ptr->nod_count && ptr->nod_arg[0] && ptr->nod_arg[0]->nod_type == nod_version)
				{
					if (ptr->nod_arg[0]->nod_flags & NOD_VERSION)
						stuff(dsqlScratch->getStatement(), isc_tpb_rec_version);
					else
						stuff(dsqlScratch->getStatement(), isc_tpb_no_rec_version);
				}
				else
					stuff(dsqlScratch->getStatement(), isc_tpb_no_rec_version);
			}
			break;

		case nod_reserve:
			{
				if (sw_reserve)
					ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-104) <<
							  Arg::Gds(isc_dsql_dup_option));

				sw_reserve = true;
				const dsql_nod* reserve = ptr->nod_arg[0];

				if (reserve)
				{
					const dsql_nod* const* temp = reserve->nod_arg;
					for (const dsql_nod* const* end = temp + reserve->nod_count; temp < end; temp++)
					{
						gen_table_lock(dsqlScratch, *temp, lock_level);
					}
				}
			}
			break;

		case nod_tra_misc:
			if (misc_flags & ptr->nod_flags)
				ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-104) <<
						  Arg::Gds(isc_dsql_dup_option));

			misc_flags |= ptr->nod_flags;
			if (ptr->nod_flags & NOD_NO_AUTO_UNDO)
				stuff(dsqlScratch->getStatement(), isc_tpb_no_auto_undo);
			else if (ptr->nod_flags & NOD_IGNORE_LIMBO)
				stuff(dsqlScratch->getStatement(), isc_tpb_ignore_limbo);
			else if (ptr->nod_flags & NOD_RESTART_REQUESTS)
				stuff(dsqlScratch->getStatement(), isc_tpb_restart_requests);
			break;

		case nod_lock_timeout:
			if (sw_lock_timeout)
				ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-104) <<
						  Arg::Gds(isc_dsql_dup_option));

			sw_lock_timeout = true;
			if (ptr->nod_count == 1 && ptr->nod_arg[0]->nod_type == nod_constant)
			{
				const int lck_timeout = (int) ptr->nod_arg[0]->getSlong();
				stuff(dsqlScratch->getStatement(), isc_tpb_lock_timeout);
				stuff(dsqlScratch->getStatement(), 2);
				stuff_word(dsqlScratch->getStatement(), lck_timeout);
			}
			break;

		default:
			ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-104) <<
					  Arg::Gds(isc_dsql_tran_err));
		}
	}
}


/**

 	GEN_statement

    @brief	Generate blr for an arbitrary expression.


    @param dsqlScratch
    @param node

 **/
void GEN_statement( DsqlCompilerScratch* dsqlScratch, dsql_nod* node)
{
	dsql_nod* temp;
	dsql_nod** ptr;
	const dsql_nod* const* end;
	dsql_str* string;

	switch (node->nod_type)
	{
	case nod_assign:
		stuff(dsqlScratch->getStatement(), blr_assignment);
		GEN_expr(dsqlScratch, node->nod_arg[0]);
		GEN_expr(dsqlScratch, node->nod_arg[1]);
		return;

	case nod_block:
		stuff(dsqlScratch->getStatement(), blr_block);
		GEN_statement(dsqlScratch, node->nod_arg[e_blk_action]);
		if (node->nod_count > 1)
		{
			temp = node->nod_arg[e_blk_errs];
			for (ptr = temp->nod_arg, end = ptr + temp->nod_count; ptr < end; ptr++)
			{
				GEN_statement(dsqlScratch, *ptr);
			}
		}
		stuff(dsqlScratch->getStatement(), blr_end);
		return;

	case nod_class_node:
		{
			StmtNode* stmtNode = reinterpret_cast<StmtNode*>(node->nod_arg[0]);
			stmtNode->genBlr();
			return;
		}

	case nod_for_select:
		gen_for_select(dsqlScratch, node);
		return;

	case nod_set_generator:
	case nod_set_generator2:
		stuff(dsqlScratch->getStatement(), blr_set_generator);
		string = (dsql_str*) node->nod_arg[e_gen_id_name];
		stuff_cstring(dsqlScratch->getStatement(), string->str_data);
		GEN_expr(dsqlScratch, node->nod_arg[e_gen_id_value]);
		return;

	case nod_list:
		if (!(node->nod_flags & NOD_SIMPLE_LIST))
			stuff(dsqlScratch->getStatement(), blr_begin);
		for (ptr = node->nod_arg, end = ptr + node->nod_count; ptr < end; ptr++)
		{
			GEN_statement(dsqlScratch, *ptr);
		}
		if (!(node->nod_flags & NOD_SIMPLE_LIST))
			stuff(dsqlScratch->getStatement(), blr_end);
		return;

	case nod_erase:
	case nod_erase_current:
	case nod_modify:
	case nod_modify_current:
	case nod_store:
	case nod_exec_procedure:
		gen_statement(dsqlScratch, node);
		return;

	case nod_on_error:
		stuff(dsqlScratch->getStatement(), blr_error_handler);
		temp = node->nod_arg[e_err_errs];
		stuff_word(dsqlScratch->getStatement(), temp->nod_count);
		for (ptr = temp->nod_arg, end = ptr + temp->nod_count; ptr < end; ptr++)
		{
			gen_error_condition(dsqlScratch, *ptr);
		}
		GEN_statement(dsqlScratch, node->nod_arg[e_err_action]);
		return;

	case nod_exec_sql:
		stuff(dsqlScratch->getStatement(), blr_exec_sql);
		GEN_expr(dsqlScratch, node->nod_arg[e_exec_sql_stmnt]);
		return;

	case nod_exec_into:
		if (node->nod_arg[e_exec_into_block])
		{
			stuff(dsqlScratch->getStatement(), blr_label);
			stuff(dsqlScratch->getStatement(), (int) (IPTR) node->nod_arg[e_exec_into_label]->nod_arg[e_label_number]);
		}
		stuff(dsqlScratch->getStatement(), blr_exec_into);
		temp = node->nod_arg[e_exec_into_list];
		stuff_word(dsqlScratch->getStatement(), temp->nod_count);
		GEN_expr(dsqlScratch, node->nod_arg[e_exec_into_stmnt]);
		if (node->nod_arg[e_exec_into_block])
		{
			stuff(dsqlScratch->getStatement(), 0); // Non-singleton
			GEN_statement(dsqlScratch, node->nod_arg[e_exec_into_block]);
		}
		else
			stuff(dsqlScratch->getStatement(), 1); // Singleton

		for (ptr = temp->nod_arg, end = ptr + temp->nod_count; ptr < end; ptr++)
		{
			GEN_expr(dsqlScratch, *ptr);
		}
		return;

	case nod_exec_stmt:
		gen_exec_stmt(dsqlScratch, node);
		return;

	case nod_breakleave:
		stuff(dsqlScratch->getStatement(), blr_leave);
		stuff(dsqlScratch->getStatement(), (int)(IPTR) node->nod_arg[e_breakleave_label]->nod_arg[e_label_number]);
		return;

	case nod_continue:
		stuff(dsqlScratch->getStatement(), blr_continue_loop);
		stuff(dsqlScratch->getStatement(), (int)(IPTR) node->nod_arg[e_continue_label]->nod_arg[e_label_number]);
		return;

	case nod_start_savepoint:
		stuff(dsqlScratch->getStatement(), blr_start_savepoint);
		return;

	case nod_end_savepoint:
		stuff(dsqlScratch->getStatement(), blr_end_savepoint);
		return;

	case nod_while:
		stuff(dsqlScratch->getStatement(), blr_label);
		stuff(dsqlScratch->getStatement(), (int) (IPTR) node->nod_arg[e_while_label]->nod_arg[e_label_number]);
		stuff(dsqlScratch->getStatement(), blr_loop);
		stuff(dsqlScratch->getStatement(), blr_begin);
		stuff(dsqlScratch->getStatement(), blr_if);
		GEN_expr(dsqlScratch, node->nod_arg[e_while_cond]);
		GEN_statement(dsqlScratch, node->nod_arg[e_while_action]);
		stuff(dsqlScratch->getStatement(), blr_leave);
		stuff(dsqlScratch->getStatement(), (int) (IPTR) node->nod_arg[e_while_label]->nod_arg[e_label_number]);
		stuff(dsqlScratch->getStatement(), blr_end);
		return;

	case nod_sqlcode:
	case nod_gdscode:
		stuff(dsqlScratch->getStatement(), blr_abort);
		gen_error_condition(dsqlScratch, node);
		return;

	case nod_cursor:
		stuff(dsqlScratch->getStatement(), blr_dcl_cursor);
		stuff_word(dsqlScratch->getStatement(), (int) (IPTR) node->nod_arg[e_cur_number]);
		if (node->nod_arg[e_cur_scroll])
		{
			stuff(dsqlScratch->getStatement(), blr_scrollable);
		}
		gen_rse(dsqlScratch, node->nod_arg[e_cur_rse]);
		temp = node->nod_arg[e_cur_rse]->nod_arg[e_rse_items];
		stuff_word(dsqlScratch->getStatement(), temp->nod_count);
		ptr = temp->nod_arg;
		end = ptr + temp->nod_count;
		while (ptr < end) {
			GEN_expr(dsqlScratch, *ptr++);
		}
		return;

	case nod_cursor_open:
	case nod_cursor_close:
	case nod_cursor_fetch:
		{
			const dsql_nod* scroll = node->nod_arg[e_cur_stmt_scroll];
			// op-code
			stuff(dsqlScratch->getStatement(), blr_cursor_stmt);
			if (node->nod_type == nod_cursor_open)
				stuff(dsqlScratch->getStatement(), blr_cursor_open);
			else if (node->nod_type == nod_cursor_close)
				stuff(dsqlScratch->getStatement(), blr_cursor_close);
			else if (scroll)
				stuff(dsqlScratch->getStatement(), blr_cursor_fetch_scroll);
			else
				stuff(dsqlScratch->getStatement(), blr_cursor_fetch);
			// cursor reference
			dsql_nod* cursor = node->nod_arg[e_cur_stmt_id];
			stuff_word(dsqlScratch->getStatement(), (int) (IPTR) cursor->nod_arg[e_cur_number]);
			// scrolling
			if (scroll)
			{
				stuff(dsqlScratch->getStatement(), scroll->nod_arg[0]->getSlong());
				if (scroll->nod_arg[1])
				{
					GEN_expr(dsqlScratch, scroll->nod_arg[1]);
				}
				else
				{
					stuff(dsqlScratch->getStatement(), blr_null);
				}
			}
			// assignment
			dsql_nod* list_into = node->nod_arg[e_cur_stmt_into];
			if (list_into)
			{
				dsql_nod* list = cursor->nod_arg[e_cur_rse]->nod_arg[e_rse_items];
				if (list->nod_count != list_into->nod_count)
				{
					ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-313) <<
							  Arg::Gds(isc_dsql_count_mismatch));
				}
				stuff(dsqlScratch->getStatement(), blr_begin);
				ptr = list->nod_arg;
				end = ptr + list->nod_count;
				dsql_nod** ptr_to = list_into->nod_arg;
				while (ptr < end)
				{
					stuff(dsqlScratch->getStatement(), blr_assignment);
					GEN_expr(dsqlScratch, *ptr++);
					GEN_expr(dsqlScratch, *ptr_to++);
				}
				stuff(dsqlScratch->getStatement(), blr_end);
			}
		}
		return;

	case nod_src_info:
		dsqlScratch->getStatement()->put_debug_src_info(node->nod_line, node->nod_column);
		GEN_statement(dsqlScratch, node->nod_arg[e_src_info_stmt]);
		return;

	default:
		ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-901) <<
				  Arg::Gds(isc_dsql_internal_err) <<
				  // gen.c: node not supported
				  Arg::Gds(isc_node_err));
	}
}


/**

 	gen_aggregate

    @brief	Generate blr for a relation reference.


    @param
    @param

 **/
static void gen_aggregate( DsqlCompilerScratch* dsqlScratch, const dsql_nod* node)
{
	DsqlCompiledStatement* statement = dsqlScratch->getStatement();

	dsql_ctx* context = (dsql_ctx*) node->nod_arg[e_agg_context];
	const bool window = (node->nod_flags & NOD_AGG_WINDOW);
	stuff(statement, (window ? blr_window : blr_aggregate));

	if (!window)
	{
		stuff_context(dsqlScratch, context);
	}

	gen_rse(dsqlScratch, node->nod_arg[e_agg_rse]);

	// Handle PARTITION BY and GROUP BY clause

	if (window)
	{
		fb_assert(context->ctx_win_maps.hasData());
		stuff(statement, context->ctx_win_maps.getCount());	// number of windows

		for (Array<PartitionMap*>::iterator i = context->ctx_win_maps.begin();
			 i != context->ctx_win_maps.end();
			 ++i)
		{
			stuff(statement, blr_partition_by);
			dsql_nod* partition = (*i)->partition;
			dsql_nod* partitionRemapped = (*i)->partitionRemapped;
			dsql_nod* order = (*i)->order;

			stuff(statement, (*i)->context);

			if (partition)
			{
				stuff(statement, partition->nod_count);	// partition by expression count

				dsql_nod** ptr = partition->nod_arg;
				for (const dsql_nod* const* end = ptr + partition->nod_count; ptr < end; ++ptr)
					GEN_expr(dsqlScratch, *ptr);

				ptr = partitionRemapped->nod_arg;
				for (const dsql_nod* const* end = ptr + partitionRemapped->nod_count; ptr < end; ++ptr)
					GEN_expr(dsqlScratch, *ptr);
			}
			else
			{
				stuff(statement, 0);	// partition by expression count
			}

			if (order)
				gen_sort(dsqlScratch, order);
			else
			{
				stuff(dsqlScratch->getStatement(), blr_sort);
				stuff(dsqlScratch->getStatement(), 0);
			}

			gen_map(dsqlScratch, (*i)->map);
		}
	}
	else
	{
		stuff(dsqlScratch->getStatement(), blr_group_by);

		dsql_nod* list = node->nod_arg[e_agg_group];
		if (list != NULL)
		{
			stuff(dsqlScratch->getStatement(), list->nod_count);
			dsql_nod** ptr = list->nod_arg;
			for (const dsql_nod* const* end = ptr + list->nod_count; ptr < end; ptr++)
				GEN_expr(dsqlScratch, *ptr);
		}
		else
		{
			stuff(statement, 0);
		}

		gen_map(dsqlScratch, context->ctx_map);
	}
}


/**

 gen_cast

    @brief      Generate BLR for a data-type cast operation


    @param dsqlScratch
    @param node

 **/
static void gen_cast( DsqlCompilerScratch* dsqlScratch, const dsql_nod* node)
{
	stuff(dsqlScratch->getStatement(), blr_cast);
	const dsql_fld* field = (dsql_fld*) node->nod_arg[e_cast_target];
	DDL_put_field_dtype(dsqlScratch, field, true);
	GEN_expr(dsqlScratch, node->nod_arg[e_cast_source]);
}


/**

 gen_coalesce

    @brief      Generate BLR for coalesce function

	Generate the blr values, begin with a cast and then :

	blr_value_if
	blr_missing
	blr for expression 1
		blr_value_if
		blr_missing
		blr for expression n-1
		expression n
	blr for expression n-1

    @param dsqlScratch
    @param node

 **/
static void gen_coalesce( DsqlCompilerScratch* dsqlScratch, const dsql_nod* node)
{
	// blr_value_if is used for building the coalesce function
	dsql_nod* list = node->nod_arg[0];
	stuff(dsqlScratch->getStatement(), blr_cast);
	GEN_descriptor(dsqlScratch, &node->nod_desc, true);
	dsql_nod* const* ptr = list->nod_arg;
	for (const dsql_nod* const* const end = ptr + (list->nod_count - 1); ptr < end; ptr++)
	{
		// IF (expression IS NULL) THEN
		stuff(dsqlScratch->getStatement(), blr_value_if);
		stuff(dsqlScratch->getStatement(), blr_missing);
		GEN_expr(dsqlScratch, *ptr);
	}
	// Return values
	GEN_expr(dsqlScratch, *ptr);
	list = node->nod_arg[1];
	const dsql_nod* const* const begin = list->nod_arg;
	ptr = list->nod_arg + list->nod_count;
	// if all expressions are NULL return NULL
	for (ptr--; ptr >= begin; ptr--)
	{
		GEN_expr(dsqlScratch, *ptr);
	}
}


/**

 	gen_constant

    @brief	Generate BLR for a constant.


    @param dsqlScratch
    @param desc
    @param negate_value

 **/
static void gen_constant( DsqlCompilerScratch* dsqlScratch, const dsc* desc, bool negate_value)
{
	SLONG value;
	SINT64 i64value;

	stuff(dsqlScratch->getStatement(), blr_literal);

	const UCHAR* p = desc->dsc_address;

	switch (desc->dsc_dtype)
	{
	case dtype_short:
		GEN_descriptor(dsqlScratch, desc, true);
		value = *(SSHORT *) p;
		if (negate_value)
			value = -value;
		stuff_word(dsqlScratch->getStatement(), value);
		break;

	case dtype_long:
		GEN_descriptor(dsqlScratch, desc, true);
		value = *(SLONG *) p;
		if (negate_value)
			value = -value;
		//printf("gen.cpp = %p %d\n", *((void**)p), value);
		stuff_word(dsqlScratch->getStatement(), value);
		stuff_word(dsqlScratch->getStatement(), value >> 16);
		break;

	case dtype_sql_time:
	case dtype_sql_date:
		GEN_descriptor(dsqlScratch, desc, true);
		value = *(SLONG *) p;
		stuff_word(dsqlScratch->getStatement(), value);
		stuff_word(dsqlScratch->getStatement(), value >> 16);
		break;

	case dtype_double:
		{
			// this is used for approximate/large numeric literal
			// which is transmitted to the engine as a string.

			GEN_descriptor(dsqlScratch, desc, true);
			// Length of string literal, cast because it could be > 127 bytes.
			const USHORT l = (USHORT)(UCHAR) desc->dsc_scale;
			if (negate_value)
			{
				stuff_word(dsqlScratch->getStatement(), l + 1);
				stuff(dsqlScratch->getStatement(), '-');
			}
			else {
				stuff_word(dsqlScratch->getStatement(), l);
			}

			if (l)
				dsqlScratch->getStatement()->append_raw_string(p, l);
		}
		break;

	case dtype_int64:
		i64value = *(SINT64 *) p;

		if (negate_value)
			i64value = -i64value;
		else if (i64value == MIN_SINT64)
		{
			// UH OH!
			// yylex correctly recognized the digits as the most-negative
			// possible INT64 value, but unfortunately, there was no
			// preceding '-' (a fact which the lexer could not know).
			// The value is too big for a positive INT64 value, and it
			// didn't contain an exponent so it's not a valid DOUBLE
			// PRECISION literal either, so we have to bounce it.

			ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-104) <<
					  Arg::Gds(isc_arith_except) <<
					  Arg::Gds(isc_numeric_out_of_range));
		}

		// We and the lexer both agree that this is an SINT64 constant,
		// and if the value needed to be negated, it already has been.
		// If the value will fit into a 32-bit signed integer, generate
		// it that way, else as an INT64.

		if ((i64value >= (SINT64) MIN_SLONG) && (i64value <= (SINT64) MAX_SLONG))
		{
			stuff(dsqlScratch->getStatement(), blr_long);
			stuff(dsqlScratch->getStatement(), desc->dsc_scale);
			stuff_word(dsqlScratch->getStatement(), i64value);
			stuff_word(dsqlScratch->getStatement(), i64value >> 16);
		}
		else
		{
			stuff(dsqlScratch->getStatement(), blr_int64);
			stuff(dsqlScratch->getStatement(), desc->dsc_scale);
			stuff_word(dsqlScratch->getStatement(), i64value);
			stuff_word(dsqlScratch->getStatement(), i64value >> 16);
			stuff_word(dsqlScratch->getStatement(), i64value >> 32);
			stuff_word(dsqlScratch->getStatement(), i64value >> 48);
		}
		break;

	case dtype_quad:
	case dtype_blob:
	case dtype_array:
	case dtype_timestamp:
		GEN_descriptor(dsqlScratch, desc, true);
		value = *(SLONG *) p;
		stuff_word(dsqlScratch->getStatement(), value);
		stuff_word(dsqlScratch->getStatement(), value >> 16);
		value = *(SLONG *) (p + 4);
		stuff_word(dsqlScratch->getStatement(), value);
		stuff_word(dsqlScratch->getStatement(), value >> 16);
		break;

	case dtype_text:
		{
			const USHORT length = desc->dsc_length;

			GEN_descriptor(dsqlScratch, desc, true);
			if (length)
				dsqlScratch->getStatement()->append_raw_string(p, length);
		}
		break;

	default:
		// gen_constant: datatype not understood
		ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-103) <<
				  Arg::Gds(isc_dsql_constant_err));
	}
}


/**

 	gen_constant

    @brief	Generate BLR for a constant.


    @param dsqlScratch
    @param node
    @param negate_value

 **/
static void gen_constant( DsqlCompilerScratch* dsqlScratch, dsql_nod* node, bool negate_value)
{
	if (node->nod_desc.dsc_dtype == dtype_text)
		node->nod_desc.dsc_length = ((dsql_str*) node->nod_arg[0])->str_length;

	gen_constant(dsqlScratch, &node->nod_desc, negate_value);
}


/**

 	GEN_descriptor

    @brief	Generate a blr descriptor from an internal descriptor.


    @param dsqlScratch
    @param desc
    @param texttype

 **/
void GEN_descriptor( DsqlCompilerScratch* dsqlScratch, const dsc* desc, bool texttype)
{
	switch (desc->dsc_dtype)
	{
	case dtype_text:
		if (texttype || desc->dsc_ttype() == ttype_binary || desc->dsc_ttype() == ttype_none)
		{
			stuff(dsqlScratch->getStatement(), blr_text2);
			stuff_word(dsqlScratch->getStatement(), desc->dsc_ttype());
		}
		else
		{
			stuff(dsqlScratch->getStatement(), blr_text2);	// automatic transliteration
			stuff_word(dsqlScratch->getStatement(), ttype_dynamic);
		}

		stuff_word(dsqlScratch->getStatement(), desc->dsc_length);
		break;

	case dtype_varying:
		if (texttype || desc->dsc_ttype() == ttype_binary || desc->dsc_ttype() == ttype_none)
		{
			stuff(dsqlScratch->getStatement(), blr_varying2);
			stuff_word(dsqlScratch->getStatement(), desc->dsc_ttype());
		}
		else
		{
			stuff(dsqlScratch->getStatement(), blr_varying2);	// automatic transliteration
			stuff_word(dsqlScratch->getStatement(), ttype_dynamic);
		}
		stuff_word(dsqlScratch->getStatement(), desc->dsc_length - sizeof(USHORT));
		break;

	case dtype_short:
		stuff(dsqlScratch->getStatement(), blr_short);
		stuff(dsqlScratch->getStatement(), desc->dsc_scale);
		break;

	case dtype_long:
		stuff(dsqlScratch->getStatement(), blr_long);
		stuff(dsqlScratch->getStatement(), desc->dsc_scale);
		break;

	case dtype_quad:
		stuff(dsqlScratch->getStatement(), blr_quad);
		stuff(dsqlScratch->getStatement(), desc->dsc_scale);
		break;

	case dtype_int64:
		stuff(dsqlScratch->getStatement(), blr_int64);
		stuff(dsqlScratch->getStatement(), desc->dsc_scale);
		break;

	case dtype_real:
		stuff(dsqlScratch->getStatement(), blr_float);
		break;

	case dtype_double:
		stuff(dsqlScratch->getStatement(), blr_double);
		break;

	case dtype_sql_date:
		stuff(dsqlScratch->getStatement(), blr_sql_date);
		break;

	case dtype_sql_time:
		stuff(dsqlScratch->getStatement(), blr_sql_time);
		break;

	case dtype_timestamp:
		stuff(dsqlScratch->getStatement(), blr_timestamp);
		break;

	case dtype_array:
		stuff(dsqlScratch->getStatement(), blr_quad);
		stuff(dsqlScratch->getStatement(), 0);
		break;

	case dtype_blob:
		stuff(dsqlScratch->getStatement(), blr_blob2);
		stuff_word(dsqlScratch->getStatement(), desc->dsc_sub_type);
		stuff_word(dsqlScratch->getStatement(), desc->getTextType());
		break;

	default:
		// don't understand dtype
		ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-804) <<
				  Arg::Gds(isc_dsql_datatype_err));
	}
}


/**

 	gen_error_condition

    @brief	Generate blr for an error condtion


    @param dsqlScratch
    @param node

 **/
static void gen_error_condition( DsqlCompilerScratch* dsqlScratch, const dsql_nod* node)
{
	const dsql_str* string;

	switch (node->nod_type)
	{
	case nod_sqlcode:
		stuff(dsqlScratch->getStatement(), blr_sql_code);
		stuff_word(dsqlScratch->getStatement(), (USHORT)(IPTR) node->nod_arg[0]);
		return;

	case nod_gdscode:
		stuff(dsqlScratch->getStatement(), blr_gds_code);
		string = (dsql_str*) node->nod_arg[0];
		stuff_cstring(dsqlScratch->getStatement(), string->str_data);
		return;

	case nod_exception:
		stuff(dsqlScratch->getStatement(), blr_exception);
		string = (dsql_str*) node->nod_arg[0];
		stuff_cstring(dsqlScratch->getStatement(), string->str_data);
		return;

	case nod_default:
		stuff(dsqlScratch->getStatement(), blr_default_code);
		return;

	default:
		fb_assert(false);
		return;
	}
}


/**

 	gen_exec_stmt

    @brief	Generate blr for the EXECUTE STATEMENT clause


    @param dsqlScratch
    @param node

 **/
static void gen_exec_stmt(DsqlCompilerScratch* dsqlScratch, const dsql_nod* node)
{
	if (node->nod_arg[e_exec_stmt_proc_block])
	{
		stuff(dsqlScratch->getStatement(), blr_label);
		stuff(dsqlScratch->getStatement(), (int)(IPTR) node->nod_arg[e_exec_stmt_label]->nod_arg[e_label_number]);
	}

	stuff(dsqlScratch->getStatement(), blr_exec_stmt);

	// counts of input and output parameters
	const dsql_nod* temp = node->nod_arg[e_exec_stmt_inputs];
	if (temp)
	{
		stuff(dsqlScratch->getStatement(), blr_exec_stmt_inputs);
		stuff_word(dsqlScratch->getStatement(), temp->nod_count);
	}

	temp = node->nod_arg[e_exec_stmt_outputs];
	if (temp)
	{
		stuff(dsqlScratch->getStatement(), blr_exec_stmt_outputs);
		stuff_word(dsqlScratch->getStatement(), temp->nod_count);
	}

	// query expression
	stuff(dsqlScratch->getStatement(), blr_exec_stmt_sql);
	GEN_expr(dsqlScratch, node->nod_arg[e_exec_stmt_sql]);

	// proc block body
	dsql_nod* temp2 = node->nod_arg[e_exec_stmt_proc_block];
	if (temp2)
	{
		stuff(dsqlScratch->getStatement(), blr_exec_stmt_proc_block);
		GEN_statement(dsqlScratch, temp2);
	}

	// external data source, user, password and role
	gen_optional_expr(dsqlScratch, blr_exec_stmt_data_src, node->nod_arg[e_exec_stmt_data_src]);
	gen_optional_expr(dsqlScratch, blr_exec_stmt_user, node->nod_arg[e_exec_stmt_user]);
	gen_optional_expr(dsqlScratch, blr_exec_stmt_pwd, node->nod_arg[e_exec_stmt_pwd]);
	gen_optional_expr(dsqlScratch, blr_exec_stmt_role, node->nod_arg[e_exec_stmt_role]);

	// dsqlScratch's transaction behavior
	temp = node->nod_arg[e_exec_stmt_tran];
	if (temp)
	{
		stuff(dsqlScratch->getStatement(), blr_exec_stmt_tran_clone); // transaction parameters equal to current transaction
		stuff(dsqlScratch->getStatement(), (UCHAR)(IPTR) temp->nod_flags);
	}

	// inherit caller's privileges ?
	if (node->nod_arg[e_exec_stmt_privs]) {
		stuff(dsqlScratch->getStatement(), blr_exec_stmt_privs);
	}

	// inputs
	temp = node->nod_arg[e_exec_stmt_inputs];
	if (temp)
	{
		const dsql_nod* const* ptr = temp->nod_arg;
		const bool haveNames = ((*ptr)->nod_arg[e_named_param_name] != 0);
		if (haveNames)
			stuff(dsqlScratch->getStatement(), blr_exec_stmt_in_params2);
		else
			stuff(dsqlScratch->getStatement(), blr_exec_stmt_in_params);

		for (const dsql_nod* const* end = ptr + temp->nod_count; ptr < end; ptr++)
		{
			if (haveNames)
			{
				const dsql_str* name = (dsql_str*) (*ptr)->nod_arg[e_named_param_name];
				stuff_cstring(dsqlScratch->getStatement(), name->str_data);
			}
			GEN_expr(dsqlScratch, (*ptr)->nod_arg[e_named_param_expr]);
		}
	}

	// outputs
	temp = node->nod_arg[e_exec_stmt_outputs];
	if (temp)
	{
		stuff(dsqlScratch->getStatement(), blr_exec_stmt_out_params);
		for (size_t i = 0; i < temp->nod_count; ++i) {
			GEN_expr(dsqlScratch, temp->nod_arg[i]);
		}
	}
	stuff(dsqlScratch->getStatement(), blr_end);
}


/**

 	gen_field

    @brief	Generate blr for a field - field id's
 	are preferred but not for trigger or view blr.


    @param dsqlScratch
    @param context
    @param field
    @param indices

 **/
static void gen_field( DsqlCompilerScratch* dsqlScratch, const dsql_ctx* context,
	const dsql_fld* field, dsql_nod* indices)
{
	// For older clients - generate an error should they try and
	// access data types which did not exist in the older dialect
	if (dsqlScratch->clientDialect <= SQL_DIALECT_V5)
	{
		switch (field->fld_dtype)
		{
		case dtype_sql_date:
		case dtype_sql_time:
		case dtype_int64:
			ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-804) <<
					  Arg::Gds(isc_dsql_datatype_err) <<
					  Arg::Gds(isc_sql_dialect_datatype_unsupport) <<
					  			Arg::Num(dsqlScratch->clientDialect) <<
					  			Arg::Str(DSC_dtype_tostring(static_cast<UCHAR>(field->fld_dtype))));
			break;
		default:
			// No special action for other data types
			break;
		}
	}

	if (indices)
		stuff(dsqlScratch->getStatement(), blr_index);

	if (DDL_ids(dsqlScratch))
	{
		stuff(dsqlScratch->getStatement(), blr_fid);
		stuff_context(dsqlScratch, context);
		stuff_word(dsqlScratch->getStatement(), field->fld_id);
	}
	else
	{
		stuff(dsqlScratch->getStatement(), blr_field);
		stuff_context(dsqlScratch, context);
		stuff_meta_string(dsqlScratch->getStatement(), field->fld_name.c_str());
	}

	if (indices)
	{
		stuff(dsqlScratch->getStatement(), indices->nod_count);
		dsql_nod** ptr = indices->nod_arg;
		for (const dsql_nod* const* end = ptr + indices->nod_count; ptr < end; ptr++)
		{
			GEN_expr(dsqlScratch, *ptr);
		}
	}
}


/**

 	gen_for_select

    @brief	Generate BLR for a SELECT dsqlScratch.


    @param dsqlScratch
    @param for_select

 **/
static void gen_for_select( DsqlCompilerScratch* dsqlScratch, const dsql_nod* for_select)
{
	dsql_nod* rse = for_select->nod_arg[e_flp_select];

	// CVC: Only put a label if this is not singular; otherwise,
	// what loop is the user trying to abandon?
	if (for_select->nod_arg[e_flp_action])
	{
		stuff(dsqlScratch->getStatement(), blr_label);
		stuff(dsqlScratch->getStatement(), (int) (IPTR) for_select->nod_arg[e_flp_label]->nod_arg[e_label_number]);
	}

	// Generate FOR loop

	stuff(dsqlScratch->getStatement(), blr_for);

	if (!for_select->nod_arg[e_flp_action])
	{
		stuff(dsqlScratch->getStatement(), blr_singular);
	}
	gen_rse(dsqlScratch, rse);
	stuff(dsqlScratch->getStatement(), blr_begin);

	// Build body of FOR loop

	dsql_nod* list = rse->nod_arg[e_rse_items];
	dsql_nod* list_to = for_select->nod_arg[e_flp_into];

	if (list_to)
	{
		if (list->nod_count != list_to->nod_count)
		{
			ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-313) <<
					  Arg::Gds(isc_dsql_count_mismatch));
		}
		dsql_nod** ptr = list->nod_arg;
		dsql_nod** ptr_to = list_to->nod_arg;
		for (const dsql_nod* const* const end = ptr + list->nod_count; ptr < end; ptr++, ptr_to++)
		{
			stuff(dsqlScratch->getStatement(), blr_assignment);
			GEN_expr(dsqlScratch, *ptr);
			GEN_expr(dsqlScratch, *ptr_to);
		}
	}

	if (for_select->nod_arg[e_flp_action])
		GEN_statement(dsqlScratch, for_select->nod_arg[e_flp_action]);
	stuff(dsqlScratch->getStatement(), blr_end);
}


/**

 gen_gen_id

    @brief      Generate BLR for gen_id


    @param dsqlScratch
    @param node

 **/
static void gen_gen_id( DsqlCompilerScratch* dsqlScratch, const dsql_nod* node)
{
	stuff(dsqlScratch->getStatement(), blr_gen_id);
	const dsql_str* string = (dsql_str*) node->nod_arg[e_gen_id_name];
	stuff_cstring(dsqlScratch->getStatement(), string->str_data);
	GEN_expr(dsqlScratch, node->nod_arg[e_gen_id_value]);
}


/**

 	gen_join_rse

    @brief	Generate a record selection expression
 	with an explicit join type.


    @param dsqlScratch
    @param rse

 **/
static void gen_join_rse( DsqlCompilerScratch* dsqlScratch, const dsql_nod* rse)
{
	stuff(dsqlScratch->getStatement(), blr_rs_stream);
	stuff(dsqlScratch->getStatement(), 2);

	GEN_expr(dsqlScratch, rse->nod_arg[e_join_left_rel]);
	GEN_expr(dsqlScratch, rse->nod_arg[e_join_rght_rel]);

	const dsql_nod* node = rse->nod_arg[e_join_type];
	if (node->nod_type != nod_join_inner)
	{
		stuff(dsqlScratch->getStatement(), blr_join_type);
		if (node->nod_type == nod_join_left)
			stuff(dsqlScratch->getStatement(), blr_left);
		else if (node->nod_type == nod_join_right)
			stuff(dsqlScratch->getStatement(), blr_right);
		else
			stuff(dsqlScratch->getStatement(), blr_full);
	}

	if (rse->nod_arg[e_join_boolean])
	{
		stuff(dsqlScratch->getStatement(), blr_boolean);
		GEN_expr(dsqlScratch, rse->nod_arg[e_join_boolean]);
	}

	stuff(dsqlScratch->getStatement(), blr_end);
}


/**

 	gen_map

    @brief	Generate a value map for a record selection expression.


    @param dsqlScratch
    @param map

 **/
static void gen_map( DsqlCompilerScratch* dsqlScratch, dsql_map* map)
{
	USHORT count = 0;
	dsql_map* temp;
	for (temp = map; temp; temp = temp->map_next)
		++count;

	stuff(dsqlScratch->getStatement(), blr_map);
	stuff_word(dsqlScratch->getStatement(), count);

	for (temp = map; temp; temp = temp->map_next)
	{
		stuff_word(dsqlScratch->getStatement(), temp->map_position);
		GEN_expr(dsqlScratch, temp->map_node);
	}
}

static void gen_optional_expr(DsqlCompilerScratch* dsqlScratch, const UCHAR code, dsql_nod* node)
{
	if (node)
	{
		stuff(dsqlScratch->getStatement(), code);
		GEN_expr(dsqlScratch, node);
	}
}

/**

 	gen_parameter

    @brief	Generate a parameter reference.


    @param dsqlScratch
    @param parameter

 **/
static void gen_parameter( DsqlCompilerScratch* dsqlScratch, const dsql_par* parameter)
{
	const dsql_msg* message = parameter->par_message;

	const dsql_par* null = parameter->par_null;
	if (null != NULL)
	{
		stuff(dsqlScratch->getStatement(), blr_parameter2);
		stuff(dsqlScratch->getStatement(), message->msg_number);
		stuff_word(dsqlScratch->getStatement(), parameter->par_parameter);
		stuff_word(dsqlScratch->getStatement(), null->par_parameter);
		return;
	}

	stuff(dsqlScratch->getStatement(), blr_parameter);
	stuff(dsqlScratch->getStatement(), message->msg_number);
	stuff_word(dsqlScratch->getStatement(), parameter->par_parameter);
}



/**

 	gen_plan

    @brief	Generate blr for an access plan expression.


    @param dsqlScratch
    @param plan_expression

 **/
static void gen_plan( DsqlCompilerScratch* dsqlScratch, const dsql_nod* plan_expression)
{
	// stuff the join type

	const dsql_nod* list = plan_expression->nod_arg[0];
	if (list->nod_count > 1)
	{
		stuff(dsqlScratch->getStatement(), blr_join);
		stuff(dsqlScratch->getStatement(), list->nod_count);
	}

	// stuff one or more plan items

	const dsql_nod* const* ptr = list->nod_arg;
	for (const dsql_nod* const* const end = ptr + list->nod_count; ptr < end; ptr++)
	{
		const dsql_nod* node = *ptr;
		if (node->nod_type == nod_plan_expr)
		{
			gen_plan(dsqlScratch, node);
			continue;
		}

		// if we're here, it must be a nod_plan_item

		stuff(dsqlScratch->getStatement(), blr_retrieve);

		// stuff the relation--the relation id itself is redundant except
		// when there is a need to differentiate the base tables of views

		const dsql_nod* arg = node->nod_arg[0];
		gen_relation(dsqlScratch, (dsql_ctx*) arg->nod_arg[e_rel_context]);

		// now stuff the access method for this stream
		const dsql_str* index_string;

		arg = node->nod_arg[1];
		switch (arg->nod_type)
		{
		case nod_natural:
			stuff(dsqlScratch->getStatement(), blr_sequential);
			break;

		case nod_index_order:
			stuff(dsqlScratch->getStatement(), blr_navigational);
			index_string = (dsql_str*) arg->nod_arg[0];
			stuff_cstring(dsqlScratch->getStatement(), index_string->str_data);
			if (!arg->nod_arg[1])
				break;
			// dimitr: FALL INTO, if the plan item is ORDER ... INDEX (...)

		case nod_index:
			{
				stuff(dsqlScratch->getStatement(), blr_indices);
				arg = (arg->nod_type == nod_index) ? arg->nod_arg[0] : arg->nod_arg[1];
				stuff(dsqlScratch->getStatement(), arg->nod_count);
				const dsql_nod* const* ptr2 = arg->nod_arg;
				for (const dsql_nod* const* const end2 = ptr2 + arg->nod_count; ptr2 < end2; ptr2++)
				{
					index_string = (dsql_str*) * ptr2;
					stuff_cstring(dsqlScratch->getStatement(), index_string->str_data);
				}
				break;
			}

		default:
			fb_assert(false);
			break;
		}

	}
}



/**

 	gen_relation

    @brief	Generate blr for a relation reference.


    @param dsqlScratch
    @param context

 **/
static void gen_relation( DsqlCompilerScratch* dsqlScratch, dsql_ctx* context)
{
	const dsql_rel* relation = context->ctx_relation;
	const dsql_prc* procedure = context->ctx_procedure;

	// if this is a trigger or procedure, don't want relation id used
	if (relation)
	{
		if (DDL_ids(dsqlScratch))
		{
			stuff(dsqlScratch->getStatement(), context->ctx_alias ? blr_rid2 : blr_rid);
			stuff_word(dsqlScratch->getStatement(), relation->rel_id);
		}
		else
		{
			stuff(dsqlScratch->getStatement(), context->ctx_alias ? blr_relation2 : blr_relation);
			stuff_meta_string(dsqlScratch->getStatement(), relation->rel_name.c_str());
		}

		if (context->ctx_alias)
		{
			stuff_meta_string(dsqlScratch->getStatement(), context->ctx_alias);
		}

		stuff_context(dsqlScratch, context);
	}
	else if (procedure)
	{
		if (DDL_ids(dsqlScratch))
		{
			stuff(dsqlScratch->getStatement(), context->ctx_alias ? blr_pid2 : blr_pid);
			stuff_word(dsqlScratch->getStatement(), procedure->prc_id);
		}
		else
		{
			if (procedure->prc_name.qualifier.hasData())
			{
				stuff(dsqlScratch->getStatement(), context->ctx_alias ? blr_procedure4 : blr_procedure3);
				stuff_meta_string(dsqlScratch->getStatement(), procedure->prc_name.qualifier.c_str());
				stuff_meta_string(dsqlScratch->getStatement(), procedure->prc_name.identifier.c_str());
			}
			else
			{
				stuff(dsqlScratch->getStatement(), context->ctx_alias ? blr_procedure2 : blr_procedure);
				stuff_meta_string(dsqlScratch->getStatement(), procedure->prc_name.identifier.c_str());
			}
		}

		if (context->ctx_alias)
		{
			stuff_meta_string(dsqlScratch->getStatement(), context->ctx_alias);
		}

		stuff_context(dsqlScratch, context);

		dsql_nod* inputs = context->ctx_proc_inputs;
		if (inputs)
		{
			stuff_word(dsqlScratch->getStatement(), inputs->nod_count);

			dsql_nod* const* ptr = inputs->nod_arg;
			for (const dsql_nod* const* const end = ptr + inputs->nod_count; ptr < end; ptr++)
			{
 				GEN_expr(dsqlScratch, *ptr);
			}
 		}
		else
			stuff_word(dsqlScratch->getStatement(), 0);
	}
}


/**

 	gen_return

    @brief	Generate blr for a procedure return.


    @param dsqlScratch
    @param procedure
    @param eos_flag

 **/
void GEN_return(DsqlCompilerScratch* dsqlScratch, const Array<dsql_nod*>& variables,
				bool has_eos, bool eos_flag)
{
	if (has_eos && !eos_flag)
	{
		stuff(dsqlScratch->getStatement(), blr_begin);
	}

	stuff(dsqlScratch->getStatement(), blr_send);
	stuff(dsqlScratch->getStatement(), 1);
	stuff(dsqlScratch->getStatement(), blr_begin);

	for (Array<dsql_nod*>::const_iterator i = variables.begin(); i != variables.end(); ++i)
	{
		const dsql_nod* parameter = *i;
		const dsql_var* variable = (dsql_var*) parameter->nod_arg[e_var_variable];
		stuff(dsqlScratch->getStatement(), blr_assignment);
		stuff(dsqlScratch->getStatement(), blr_variable);
		stuff_word(dsqlScratch->getStatement(), variable->var_variable_number);
		stuff(dsqlScratch->getStatement(), blr_parameter2);
		stuff(dsqlScratch->getStatement(), variable->var_msg_number);
		stuff_word(dsqlScratch->getStatement(), variable->var_msg_item);
		stuff_word(dsqlScratch->getStatement(), variable->var_msg_item + 1);
	}

	if (has_eos)
	{
		stuff(dsqlScratch->getStatement(), blr_assignment);
		stuff(dsqlScratch->getStatement(), blr_literal);
		stuff(dsqlScratch->getStatement(), blr_short);
		stuff(dsqlScratch->getStatement(), 0);
		stuff_word(dsqlScratch->getStatement(), (eos_flag ? 0 : 1));
		stuff(dsqlScratch->getStatement(), blr_parameter);
		stuff(dsqlScratch->getStatement(), 1);
		stuff_word(dsqlScratch->getStatement(), USHORT(2 * variables.getCount()));
	}

	stuff(dsqlScratch->getStatement(), blr_end);

	if (has_eos && !eos_flag)
	{
		stuff(dsqlScratch->getStatement(), blr_stall);
		stuff(dsqlScratch->getStatement(), blr_end);
	}
}


/**

 	gen_rse

    @brief	Generate a record selection expression.


    @param dsqlScratch
    @param rse

 **/
static void gen_rse( DsqlCompilerScratch* dsqlScratch, const dsql_nod* rse)
{
	if (rse->nod_flags & NOD_SELECT_EXPR_SINGLETON)
	{
		stuff(dsqlScratch->getStatement(), blr_singular);
	}

	stuff(dsqlScratch->getStatement(), blr_rse);

	dsql_nod* list = rse->nod_arg[e_rse_streams];

	// Handle source streams

	if (list->nod_type == nod_union)
	{
		stuff(dsqlScratch->getStatement(), 1);
		gen_union(dsqlScratch, rse);
	}
	else if (list->nod_type == nod_list)
	{
		stuff(dsqlScratch->getStatement(), list->nod_count);
		dsql_nod* const* ptr = list->nod_arg;
		for (const dsql_nod* const* const end = ptr + list->nod_count; ptr < end; ptr++)
		{
			dsql_nod* node = *ptr;
			switch (node->nod_type)
			{
			case nod_relation:
			case nod_aggregate:
			case nod_join:
				GEN_expr(dsqlScratch, node);
				break;
			case nod_derived_table:
				GEN_expr(dsqlScratch, node->nod_arg[e_derived_table_rse]);
				break;
			}
		}
	}
	else
	{
		stuff(dsqlScratch->getStatement(), 1);
		GEN_expr(dsqlScratch, list);
	}

	if (rse->nod_arg[e_rse_lock])
		stuff(dsqlScratch->getStatement(), blr_writelock);

	dsql_nod* node;

	if ((node = rse->nod_arg[e_rse_first]) != NULL)
	{
		stuff(dsqlScratch->getStatement(), blr_first);
		GEN_expr(dsqlScratch, node);
	}

	if ((node = rse->nod_arg[e_rse_skip]) != NULL)
	{
		stuff(dsqlScratch->getStatement(), blr_skip);
		GEN_expr(dsqlScratch, node);
	}

	if ((node = rse->nod_arg[e_rse_boolean]) != NULL)
	{
		stuff(dsqlScratch->getStatement(), blr_boolean);
		GEN_expr(dsqlScratch, node);
	}

	if ((list = rse->nod_arg[e_rse_sort]) != NULL)
	{
		gen_sort(dsqlScratch, list);
	}

	if ((list = rse->nod_arg[e_rse_reduced]) != NULL)
	{
		stuff(dsqlScratch->getStatement(), blr_project);
		stuff(dsqlScratch->getStatement(), list->nod_count);
		dsql_nod** ptr = list->nod_arg;
		for (const dsql_nod* const* const end = ptr + list->nod_count; ptr < end; ptr++)
		{
			GEN_expr(dsqlScratch, *ptr);
		}
	}

	// if the user specified an access plan to use, add it here

	if ((node = rse->nod_arg[e_rse_plan]) != NULL)
	{
		stuff(dsqlScratch->getStatement(), blr_plan);
		gen_plan(dsqlScratch, node);
	}

	stuff(dsqlScratch->getStatement(), blr_end);
}


/**

 gen_searched_case

    @brief      Generate BLR for CASE function (searched)


    @param dsqlScratch
    @param node

 **/
static void gen_searched_case( DsqlCompilerScratch* dsqlScratch, const dsql_nod* node)
{
	// blr_value_if is used for building the case expression

	stuff(dsqlScratch->getStatement(), blr_cast);
	GEN_descriptor(dsqlScratch, &node->nod_desc, true);
	const SSHORT count = node->nod_arg[e_searched_case_search_conditions]->nod_count;
	dsql_nod* boolean_list = node->nod_arg[e_searched_case_search_conditions];
	dsql_nod* results_list = node->nod_arg[e_searched_case_results];
	dsql_nod* const* bptr = boolean_list->nod_arg;
	dsql_nod* const* rptr = results_list->nod_arg;
	for (const dsql_nod* const* const end = bptr + count; bptr < end; bptr++, rptr++)
	{
		stuff(dsqlScratch->getStatement(), blr_value_if);
		GEN_expr(dsqlScratch, *bptr);
		GEN_expr(dsqlScratch, *rptr);
	}
	// else_result
	GEN_expr(dsqlScratch, node->nod_arg[e_searched_case_results]->nod_arg[count]);
}


/**

 	gen_select

    @brief	Generate BLR for a SELECT dsqlScratch.


    @param dsqlScratch
    @param rse

 **/
static void gen_select(DsqlCompilerScratch* dsqlScratch, dsql_nod* rse)
{
	dsql_ctx* context;

	fb_assert(rse->nod_type == nod_rse);

	DsqlCompiledStatement* statement = dsqlScratch->getStatement();

	// Set up parameter for things in the select list
	const dsql_nod* list = rse->nod_arg[e_rse_items];
	dsql_nod* const* ptr = list->nod_arg;
	for (const dsql_nod* const* const end = ptr + list->nod_count; ptr < end; ptr++)
	{
		dsql_par* parameter = MAKE_parameter(statement->getReceiveMsg(), true, true, 0, *ptr);
		parameter->par_node = *ptr;
		MAKE_desc(dsqlScratch, &parameter->par_desc, *ptr, NULL);
	}

	// Set up parameter to handle EOF

	dsql_par* parameter_eof = MAKE_parameter(statement->getReceiveMsg(), false, false, 0, NULL);
	statement->setEof(parameter_eof);
	parameter_eof->par_desc.dsc_dtype = dtype_short;
	parameter_eof->par_desc.dsc_scale = 0;
	parameter_eof->par_desc.dsc_length = sizeof(SSHORT);

	// Save DBKEYs for possible update later

	list = rse->nod_arg[e_rse_streams];

	GenericMap<NonPooled<dsql_par*, dsql_ctx*> > paramContexts(*getDefaultMemoryPool());

	if (!rse->nod_arg[e_rse_reduced])
	{
		dsql_nod* const* ptr2 = list->nod_arg;
		for (const dsql_nod* const* const end2 = ptr2 + list->nod_count; ptr2 < end2; ptr2++)
		{
			dsql_nod* item = *ptr2;
			if (item && item->nod_type == nod_relation)
			{
				context = (dsql_ctx*) item->nod_arg[e_rel_context];
				const dsql_rel* relation = context->ctx_relation;
				if (relation)
				{
					// Set up dbkey
					dsql_par* parameter = MAKE_parameter(statement->getReceiveMsg(), false,
						false, 0, NULL);

					parameter->par_dbkey_relname = relation->rel_name;
					paramContexts.put(parameter, context);

					parameter->par_desc.dsc_dtype = dtype_text;
					parameter->par_desc.dsc_ttype() = ttype_binary;
					parameter->par_desc.dsc_length = relation->rel_dbkey_length;

					// Set up record version - for post v33 databases

					parameter = MAKE_parameter(statement->getReceiveMsg(), false, false, 0, NULL);
					parameter->par_rec_version_relname = relation->rel_name;
					paramContexts.put(parameter, context);

					parameter->par_desc.dsc_dtype = dtype_text;
					parameter->par_desc.dsc_ttype() = ttype_binary;
					parameter->par_desc.dsc_length = relation->rel_dbkey_length / 2;
				}
			}
		}
	}

	// Generate definitions for the messages

	GEN_port(dsqlScratch, statement->getReceiveMsg());
	dsql_msg* message = statement->getSendMsg();
	if (message->msg_parameter)
		GEN_port(dsqlScratch, message);
	else
		statement->setSendMsg(NULL);

	// If there is a send message, build a RECEIVE

	if ((message = statement->getSendMsg()) != NULL)
	{
		stuff(statement, blr_receive);
		stuff(statement, message->msg_number);
	}

	// Generate FOR loop

	message = statement->getReceiveMsg();

	stuff(statement, blr_for);
	stuff(statement, blr_stall);
	gen_rse(dsqlScratch, rse);

	stuff(statement, blr_send);
	stuff(statement, message->msg_number);
	stuff(statement, blr_begin);

	// Build body of FOR loop

	SSHORT constant;
	dsc constant_desc;
	constant_desc.dsc_dtype = dtype_short;
	constant_desc.dsc_scale = 0;
	constant_desc.dsc_sub_type = 0;
	constant_desc.dsc_flags = 0;
	constant_desc.dsc_length = sizeof(SSHORT);
	constant_desc.dsc_address = (UCHAR*) & constant;

	// Add invalid usage here

	stuff(statement, blr_assignment);
	constant = 1;
	gen_constant(dsqlScratch, &constant_desc, USE_VALUE);
	gen_parameter(dsqlScratch, statement->getEof());

	for (size_t i = 0; i < message->msg_parameters.getCount(); ++i)
	{
		dsql_par* parameter = message->msg_parameters[i];

		if (parameter->par_node)
		{
			stuff(statement, blr_assignment);
			GEN_expr(dsqlScratch, parameter->par_node);
			gen_parameter(dsqlScratch, parameter);
		}

		if (parameter->par_dbkey_relname.hasData() && paramContexts.get(parameter, context))
		{
			stuff(statement, blr_assignment);
			stuff(statement, blr_dbkey);
			stuff_context(dsqlScratch, context);
			gen_parameter(dsqlScratch, parameter);
		}

		if (parameter->par_rec_version_relname.hasData() && paramContexts.get(parameter, context))
		{
			stuff(statement, blr_assignment);
			stuff(statement, blr_record_version);
			stuff_context(dsqlScratch, context);
			gen_parameter(dsqlScratch, parameter);
		}
	}

	stuff(statement, blr_end);
	stuff(statement, blr_send);
	stuff(statement, message->msg_number);
	stuff(statement, blr_assignment);
	constant = 0;
	gen_constant(dsqlScratch, &constant_desc, USE_VALUE);
	gen_parameter(dsqlScratch, statement->getEof());
}


/**

 gen_simple_case

    @brief      Generate BLR for CASE function (simple)


    @param dsqlScratch
    @param node

 **/
static void gen_simple_case( DsqlCompilerScratch* dsqlScratch, const dsql_nod* node)
{
	// blr_value_if is used for building the case expression

	stuff(dsqlScratch->getStatement(), blr_cast);
	GEN_descriptor(dsqlScratch, &node->nod_desc, true);
	const SSHORT count = node->nod_arg[e_simple_case_when_operands]->nod_count;
	dsql_nod* when_list = node->nod_arg[e_simple_case_when_operands];
	dsql_nod* results_list = node->nod_arg[e_simple_case_results];

	dsql_nod* const* wptr = when_list->nod_arg;
	dsql_nod* const* rptr = results_list->nod_arg;
	for (const dsql_nod* const* const end = wptr + count; wptr < end; wptr++, rptr++)
	{
		stuff(dsqlScratch->getStatement(), blr_value_if);
		stuff(dsqlScratch->getStatement(), blr_eql);

		if (wptr == when_list->nod_arg || !node->nod_arg[e_simple_case_case_operand2])
			GEN_expr(dsqlScratch, node->nod_arg[e_simple_case_case_operand]);
		else
			GEN_expr(dsqlScratch, node->nod_arg[e_simple_case_case_operand2]);

		GEN_expr(dsqlScratch, *wptr);
		GEN_expr(dsqlScratch, *rptr);
	}
	// else_result
	GEN_expr(dsqlScratch, node->nod_arg[e_simple_case_results]->nod_arg[count]);
}


/**

 	gen_sort

    @brief	Generate a sort clause.


    @param dsqlScratch
    @param list

 **/
static void gen_sort( DsqlCompilerScratch* dsqlScratch, dsql_nod* list)
{
	stuff(dsqlScratch->getStatement(), blr_sort);
	stuff(dsqlScratch->getStatement(), list->nod_count);

	dsql_nod* const* ptr = list->nod_arg;
	for (const dsql_nod* const* const end = ptr + list->nod_count; ptr < end; ptr++)
	{
		dsql_nod* nulls_placement = (*ptr)->nod_arg[e_order_nulls];
		if (nulls_placement)
		{
			switch (nulls_placement->getSlong())
			{
				case NOD_NULLS_FIRST:
					stuff(dsqlScratch->getStatement(), blr_nullsfirst);
					break;
				case NOD_NULLS_LAST:
					stuff(dsqlScratch->getStatement(), blr_nullslast);
					break;
			}
		}
		if ((*ptr)->nod_arg[e_order_flag])
			stuff(dsqlScratch->getStatement(), blr_descending);
		else
			stuff(dsqlScratch->getStatement(), blr_ascending);
		GEN_expr(dsqlScratch, (*ptr)->nod_arg[e_order_field]);
	}
}


/**

 	gen_statement

    @brief	Generate BLR for DML statements.


    @param dsqlScratch
    @param node

 **/
static void gen_statement(DsqlCompilerScratch* dsqlScratch, const dsql_nod* node)
{
	dsql_nod* rse = NULL;
	const dsql_msg* message = NULL;
	bool send_before_for = !(dsqlScratch->flags & DsqlCompilerScratch::FLAG_UPDATE_OR_INSERT);

	switch (node->nod_type)
	{
	case nod_store:
		rse = node->nod_arg[e_sto_rse];
		break;
	case nod_modify:
		rse = node->nod_arg[e_mod_rse];
		break;
	case nod_erase:
		rse = node->nod_arg[e_era_rse];
		break;
	default:
		send_before_for = false;
		break;
	}

	if (dsqlScratch->getStatement()->getType() == DsqlCompiledStatement::TYPE_EXEC_PROCEDURE &&
		send_before_for)
	{
		if ((message = dsqlScratch->getStatement()->getReceiveMsg()))
		{
			stuff(dsqlScratch->getStatement(), blr_send);
			stuff(dsqlScratch->getStatement(), message->msg_number);
		}
	}

	if (rse)
	{
		stuff(dsqlScratch->getStatement(), blr_for);
		GEN_expr(dsqlScratch, rse);
	}

	if (dsqlScratch->getStatement()->getType() == DsqlCompiledStatement::TYPE_EXEC_PROCEDURE)
	{
		if ((message = dsqlScratch->getStatement()->getReceiveMsg()))
		{
			stuff(dsqlScratch->getStatement(), blr_begin);

			if (!send_before_for)
			{
				stuff(dsqlScratch->getStatement(), blr_send);
				stuff(dsqlScratch->getStatement(), message->msg_number);
			}
		}
	}

	dsql_nod* temp;
	const dsql_ctx* context;
	const dsql_str* name;

	switch (node->nod_type)
	{
	case nod_store:
		stuff(dsqlScratch->getStatement(), node->nod_arg[e_sto_return] ? blr_store2 : blr_store);
		GEN_expr(dsqlScratch, node->nod_arg[e_sto_relation]);
		GEN_statement(dsqlScratch, node->nod_arg[e_sto_statement]);
		if (node->nod_arg[e_sto_return]) {
			GEN_statement(dsqlScratch, node->nod_arg[e_sto_return]);
		}
		break;

	case nod_modify:
		stuff(dsqlScratch->getStatement(), node->nod_arg[e_mod_return] ? blr_modify2 : blr_modify);
		temp = node->nod_arg[e_mod_source];
		context = (dsql_ctx*) temp->nod_arg[e_rel_context];
		stuff_context(dsqlScratch, context);
		temp = node->nod_arg[e_mod_update];
		context = (dsql_ctx*) temp->nod_arg[e_rel_context];
		stuff_context(dsqlScratch, context);
		GEN_statement(dsqlScratch, node->nod_arg[e_mod_statement]);
		if (node->nod_arg[e_mod_return]) {
			GEN_statement(dsqlScratch, node->nod_arg[e_mod_return]);
		}
		break;

	case nod_modify_current:
		stuff(dsqlScratch->getStatement(), node->nod_arg[e_mdc_return] ? blr_modify2 : blr_modify);
		context = (dsql_ctx*) node->nod_arg[e_mdc_context];
		stuff_context(dsqlScratch, context);
		temp = node->nod_arg[e_mdc_update];
		context = (dsql_ctx*) temp->nod_arg[e_rel_context];
		stuff_context(dsqlScratch, context);
		GEN_statement(dsqlScratch, node->nod_arg[e_mdc_statement]);
		if (node->nod_arg[e_mdc_return]) {
			GEN_statement(dsqlScratch, node->nod_arg[e_mdc_return]);
		}
		break;

	case nod_erase:
		temp = node->nod_arg[e_era_relation];
		context = (dsql_ctx*) temp->nod_arg[e_rel_context];
		if (node->nod_arg[e_era_return])
		{
			stuff(dsqlScratch->getStatement(), blr_begin);
			GEN_statement(dsqlScratch, node->nod_arg[e_era_return]);
			stuff(dsqlScratch->getStatement(), blr_erase);
			stuff_context(dsqlScratch, context);
			stuff(dsqlScratch->getStatement(), blr_end);
		}
		else
		{
			stuff(dsqlScratch->getStatement(), blr_erase);
			stuff_context(dsqlScratch, context);
		}
		break;

	case nod_erase_current:
		context = (dsql_ctx*) node->nod_arg[e_erc_context];
		if (node->nod_arg[e_erc_return])
		{
			stuff(dsqlScratch->getStatement(), blr_begin);
			GEN_statement(dsqlScratch, node->nod_arg[e_erc_return]);
			stuff(dsqlScratch->getStatement(), blr_erase);
			stuff_context(dsqlScratch, context);
			stuff(dsqlScratch->getStatement(), blr_end);
		}
		else
		{
			stuff(dsqlScratch->getStatement(), blr_erase);
			stuff_context(dsqlScratch, context);
		}
		break;

	case nod_exec_procedure:
		if (node->nod_arg[e_exe_package])
		{
			stuff(dsqlScratch->getStatement(), blr_exec_proc2);
			stuff_meta_string(dsqlScratch->getStatement(), ((dsql_str*) node->nod_arg[e_exe_package])->str_data);
		}
		else
			stuff(dsqlScratch->getStatement(), blr_exec_proc);

		name = (dsql_str*) node->nod_arg[e_exe_procedure];
		stuff_meta_string(dsqlScratch->getStatement(), name->str_data);

		// Input parameters
		if ( (temp = node->nod_arg[e_exe_inputs]) )
		{
			stuff_word(dsqlScratch->getStatement(), temp->nod_count);
			dsql_nod** ptr = temp->nod_arg;
			const dsql_nod* const* end = ptr + temp->nod_count;
			while (ptr < end)
			{
				GEN_expr(dsqlScratch, *ptr++);
			}
		}
		else {
			stuff_word(dsqlScratch->getStatement(), 0);
		}
		// Output parameters
		if ( ( temp = node->nod_arg[e_exe_outputs]) )
		{
			stuff_word(dsqlScratch->getStatement(), temp->nod_count);
			dsql_nod** ptr = temp->nod_arg;
			const dsql_nod* const* end = ptr + temp->nod_count;
			while (ptr < end)
			{
				GEN_expr(dsqlScratch, *ptr++);
			}
		}
		else {
			stuff_word(dsqlScratch->getStatement(), 0);
		}
		break;

	default:
		fb_assert(false);
	}

	if (message) {
		stuff(dsqlScratch->getStatement(), blr_end);
	}
}


/**

 	gen_sys_function

    @brief	Generate a system defined function.


    @param dsqlScratch
    @param node

 **/
static void gen_sys_function(DsqlCompilerScratch* dsqlScratch, const dsql_nod* node)
{
	stuff(dsqlScratch->getStatement(), blr_sys_function);
	stuff_cstring(dsqlScratch->getStatement(), ((dsql_str*) node->nod_arg[e_sysfunc_name])->str_data);

	const dsql_nod* list;
	if ((node->nod_count == e_sysfunc_args + 1) && (list = node->nod_arg[e_sysfunc_args]))
	{
		stuff(dsqlScratch->getStatement(), list->nod_count);
		dsql_nod* const* ptr = list->nod_arg;
		for (const dsql_nod* const* const end = ptr + list->nod_count; ptr < end; ptr++)
		{
			GEN_expr(dsqlScratch, *ptr);
		}
	}
	else
		stuff(dsqlScratch->getStatement(), 0);
}


/**

 	gen_table_lock

    @brief	Generate tpb for table lock.
 	If lock level is specified, it overrrides the transaction lock level.


    @param dsqlScratch
    @param tbl_lock
    @param lock_level

 **/
static void gen_table_lock( DsqlCompilerScratch* dsqlScratch, const dsql_nod* tbl_lock, USHORT lock_level)
{
	if (!tbl_lock || tbl_lock->nod_type != nod_table_lock)
		return;

	const dsql_nod* tbl_names = tbl_lock->nod_arg[e_lock_tables];
	SSHORT flags = 0;

	if (tbl_lock->nod_arg[e_lock_mode])
		flags = tbl_lock->nod_arg[e_lock_mode]->nod_flags;

	if (flags & NOD_PROTECTED)
		lock_level = isc_tpb_protected;
	else if (flags & NOD_SHARED)
		lock_level = isc_tpb_shared;

	const USHORT lock_mode = (flags & NOD_WRITE) ? isc_tpb_lock_write : isc_tpb_lock_read;

	const dsql_nod* const* ptr = tbl_names->nod_arg;
	for (const dsql_nod* const* const end = ptr + tbl_names->nod_count; ptr < end; ptr++)
	{
		if ((*ptr)->nod_type != nod_relation_name)
			continue;

		stuff(dsqlScratch->getStatement(), lock_mode);

		// stuff table name
		const dsql_str* temp = (dsql_str*) ((*ptr)->nod_arg[e_rln_name]);
		stuff_cstring(dsqlScratch->getStatement(), temp->str_data);

		stuff(dsqlScratch->getStatement(), lock_level);
	}
}


/**

 	gen_udf

    @brief	Generate a user defined function.


    @param dsqlScratch
    @param node

 **/
static void gen_udf( DsqlCompilerScratch* dsqlScratch, const dsql_nod* node)
{
	const dsql_udf* userFunc = (dsql_udf*) node->nod_arg[0];

	if (userFunc->udf_name.qualifier.isEmpty())
		stuff(dsqlScratch->getStatement(), blr_function);
	else
	{
		stuff(dsqlScratch->getStatement(), blr_function2);
		stuff_meta_string(dsqlScratch->getStatement(), userFunc->udf_name.qualifier.c_str());
	}

	stuff_meta_string(dsqlScratch->getStatement(), userFunc->udf_name.identifier.c_str());

	const dsql_nod* list;
	if ((node->nod_count == 3) && (list = node->nod_arg[2]))
	{
		stuff(dsqlScratch->getStatement(), list->nod_count);
		dsql_nod* const* ptr = list->nod_arg;
		for (const dsql_nod* const* const end = ptr + list->nod_count; ptr < end; ptr++)
		{
			GEN_expr(dsqlScratch, *ptr);
		}
	}
	else
		stuff(dsqlScratch->getStatement(), 0);
}


/**

 	gen_union

    @brief	Generate a union of substreams.


    @param dsqlScratch
    @param union_node

 **/
static void gen_union( DsqlCompilerScratch* dsqlScratch, const dsql_nod* union_node)
{
	if (union_node->nod_arg[0]->nod_flags & NOD_UNION_RECURSIVE) {
		stuff(dsqlScratch->getStatement(), blr_recurse);
	}
	else {
		stuff(dsqlScratch->getStatement(), blr_union);
	}

	// Obtain the context for UNION from the first dsql_map* node
	dsql_nod* items = union_node->nod_arg[e_rse_items];
	dsql_nod* map_item = items->nod_arg[0];
	// AB: First item could be a virtual field generated by derived table.
	if (map_item->nod_type == nod_derived_field) {
		map_item = map_item->nod_arg[e_derived_field_value];
	}
	dsql_ctx* union_context = (dsql_ctx*) map_item->nod_arg[e_map_context];
	stuff_context(dsqlScratch, union_context);
	// secondary context number must be present once in generated blr
	union_context->ctx_flags &= ~CTX_recursive;

	dsql_nod* streams = union_node->nod_arg[e_rse_streams];
	stuff(dsqlScratch->getStatement(), streams->nod_count);	// number of substreams

	dsql_nod** ptr = streams->nod_arg;
	for (const dsql_nod* const* const end = ptr + streams->nod_count; ptr < end; ptr++)
	{
		dsql_nod* sub_rse = *ptr;
		gen_rse(dsqlScratch, sub_rse);
		items = sub_rse->nod_arg[e_rse_items];
		stuff(dsqlScratch->getStatement(), blr_map);
		stuff_word(dsqlScratch->getStatement(), items->nod_count);
		USHORT count = 0;
		dsql_nod** iptr = items->nod_arg;
		for (const dsql_nod* const* const iend = iptr + items->nod_count; iptr < iend; iptr++)
		{
			stuff_word(dsqlScratch->getStatement(), count);
			GEN_expr(dsqlScratch, *iptr);
			count++;
		}
	}
}


/**

 	stuff_context

    @brief	Write a context number into the BLR buffer.
			Check for possible overflow.


    @param dsqlScratch
    @param context

 **/
static void stuff_context(DsqlCompilerScratch* dsqlScratch, const dsql_ctx* context)
{
	if (context->ctx_context > MAX_UCHAR) {
		ERRD_post(Arg::Gds(isc_too_many_contexts));
	}

	stuff(dsqlScratch->getStatement(), context->ctx_context);

	if (context->ctx_flags & CTX_recursive)
	{
		if (context->ctx_recursive > MAX_UCHAR) {
			ERRD_post(Arg::Gds(isc_too_many_contexts));
		}
		stuff(dsqlScratch->getStatement(), context->ctx_recursive);
	}
}


/**

 	stuff_meta_string

    @brief	Write out a string in metadata charset with one byte of length.


    @param dsqlScratch
    @param string

 **/
static void stuff_meta_string(DsqlCompiledStatement* dsqlScratch, const char* string)
{
	dsqlScratch->append_meta_string(string);
}
