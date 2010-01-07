/*
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
 * Adriano dos Santos Fernandes - refactored from pass1.cpp, gen.cpp, cmp.cpp, par.cpp and exe.cpp
 */

#include "firebird.h"
#include "../jrd/common.h"
#include "../dsql/StmtNodes.h"
#include "../dsql/node.h"
#include "../jrd/jrd.h"
#include "../jrd/blr.h"
#include "../jrd/exe.h"
#include "../jrd/tra.h"
#include "../jrd/cmp_proto.h"
#include "../jrd/dfw_proto.h"
#include "../jrd/evl_proto.h"
#include "../jrd/exe_proto.h"
#include "../jrd/met_proto.h"
#include "../jrd/mov_proto.h"
#include "../jrd/par_proto.h"
#include "../jrd/tra_proto.h"
#include "../dsql/ddl_proto.h"
#include "../jrd/vio_proto.h"
#include "../dsql/errd_proto.h"
#include "../dsql/gen_proto.h"
#include "../dsql/make_proto.h"
#include "../dsql/pass1_proto.h"

using namespace Firebird;
using namespace Jrd;

#include "gen/blrtable.h"


namespace Jrd {


template <typename T>
class RegisterNode
{
public:
	explicit RegisterNode(UCHAR blr)
	{
		PAR_register(blr, T::parse);
	}
};


//--------------------


DmlNode* DmlNode::pass2(thread_db* tdbb, CompilerScratch* csb, jrd_nod* aNode)
{
	node = aNode;
	return pass2(tdbb, csb);
}


//--------------------


StmtNode* SavepointEncloseNode::make(MemoryPool& pool, DsqlCompilerScratch* dsqlScratch, StmtNode* node)
{
	if (dsqlScratch->errorHandlers)
	{
		node = FB_NEW(pool) SavepointEncloseNode(pool, node);
		node->dsqlPass(dsqlScratch);
	}

	return node;
}


void SavepointEncloseNode::print(string& text, Array<dsql_nod*>& nodes) const
{
	text = "SavepointEncloseNode\n";
	string s;
	stmt->print(s, nodes);
	text += s;
}


void SavepointEncloseNode::genBlr()
{
	DsqlCompiledStatement* const statement = dsqlScratch->getStatement();

	statement->append_uchar(blr_begin);
	statement->append_uchar(blr_start_savepoint);
	stmt->genBlr();
	statement->append_uchar(blr_end_savepoint);
	statement->append_uchar(blr_end);
}


//--------------------


static RegisterNode<IfNode> regIfNode(blr_if);


DmlNode* IfNode::parse(thread_db* tdbb, MemoryPool& pool, CompilerScratch* csb, UCHAR /*blrOp*/)
{
	IfNode* node = FB_NEW(pool) IfNode(pool);

	node->condition = PAR_parse_node(tdbb, csb, TYPE_BOOL);
	node->trueAction = PAR_parse_node(tdbb, csb, STATEMENT);
	if (csb->csb_blr_reader.peekByte() == (UCHAR) blr_end)
		csb->csb_blr_reader.getByte(); // skip blr_end
	else
		node->falseAction = PAR_parse_node(tdbb, csb, STATEMENT);

	return node;
}


IfNode* IfNode::internalDsqlPass()
{
	IfNode* node = FB_NEW(getPool()) IfNode(getPool());
	node->dsqlScratch = dsqlScratch;
	node->dsqlCondition = PASS1_node(dsqlScratch, dsqlCondition);
	node->dsqlTrueAction = PASS1_statement(dsqlScratch, dsqlTrueAction);
	node->dsqlFalseAction = PASS1_statement(dsqlScratch, dsqlFalseAction);

	return node;
}


void IfNode::print(string& text, Array<dsql_nod*>& nodes) const
{
	text = "IfNode";
	nodes.add(dsqlCondition);
	nodes.add(dsqlTrueAction);
	if (dsqlFalseAction)
		nodes.add(dsqlFalseAction);
}


void IfNode::genBlr()
{
	DsqlCompiledStatement* statement = dsqlScratch->getStatement();

	stuff(statement, blr_if);
	GEN_expr(dsqlScratch, dsqlCondition);
	GEN_statement(dsqlScratch, dsqlTrueAction);
	if (dsqlFalseAction)
		GEN_statement(dsqlScratch, dsqlFalseAction);
	else
		stuff(statement, blr_end);
}


IfNode* IfNode::pass1(thread_db* tdbb, CompilerScratch* csb)
{
	condition = CMP_pass1(tdbb, csb, condition);
	trueAction = CMP_pass1(tdbb, csb, trueAction);
	falseAction = CMP_pass1(tdbb, csb, falseAction);
	return this;
}


IfNode* IfNode::pass2(thread_db* tdbb, CompilerScratch* csb)
{
	condition = CMP_pass2(tdbb, csb, condition, node);
	trueAction = CMP_pass2(tdbb, csb, trueAction, node);
	falseAction = CMP_pass2(tdbb, csb, falseAction, node);
	return this;
}


jrd_nod* IfNode::execute(thread_db* tdbb, jrd_req* request) const
{
	if (request->req_operation == jrd_req::req_evaluate)
	{
		if (EVL_boolean(tdbb, condition))
		{
			request->req_operation = jrd_req::req_evaluate;
			return trueAction;
		}

		if (falseAction)
		{
			request->req_operation = jrd_req::req_evaluate;
			return falseAction;
		}

		request->req_operation = jrd_req::req_return;
	}

	return node->nod_parent;
}


//--------------------


static RegisterNode<InAutonomousTransactionNode> regInAutonomousTransactionNode(blr_auto_trans);


DmlNode* InAutonomousTransactionNode::parse(thread_db* tdbb, MemoryPool& pool, CompilerScratch* csb,
	UCHAR /*blrOp*/)
{
	InAutonomousTransactionNode* node = FB_NEW(pool) InAutonomousTransactionNode(pool);

	if (csb->csb_blr_reader.getByte() != 0)	// Reserved for future improvements. Should be 0 for now.
		PAR_syntax_error(csb, "0");

	node->action = PAR_parse_node(tdbb, csb, STATEMENT);

	return node;
}


InAutonomousTransactionNode* InAutonomousTransactionNode::internalDsqlPass()
{
	const bool autoTrans = dsqlScratch->flags & DsqlCompilerScratch::FLAG_IN_AUTO_TRANS_BLOCK;
	dsqlScratch->flags |= DsqlCompilerScratch::FLAG_IN_AUTO_TRANS_BLOCK;

	InAutonomousTransactionNode* node = FB_NEW(getPool()) InAutonomousTransactionNode(getPool());
	node->dsqlScratch = dsqlScratch;
	node->dsqlAction = PASS1_statement(dsqlScratch, dsqlAction);

	if (!autoTrans)
		dsqlScratch->flags &= ~DsqlCompilerScratch::FLAG_IN_AUTO_TRANS_BLOCK;

	return node;
}


void InAutonomousTransactionNode::print(string& text, Array<dsql_nod*>& nodes) const
{
	text = "InAutonomousTransactionNode";
	nodes.add(dsqlAction);
}


void InAutonomousTransactionNode::genBlr()
{
	DsqlCompiledStatement* statement = dsqlScratch->getStatement();

	stuff(statement, blr_auto_trans);
	stuff(statement, 0);	// to extend syntax in the future
	GEN_statement(dsqlScratch, dsqlAction);
}


InAutonomousTransactionNode* InAutonomousTransactionNode::pass1(thread_db* tdbb, CompilerScratch* csb)
{
	action = CMP_pass1(tdbb, csb, action);
	return this;
}


InAutonomousTransactionNode* InAutonomousTransactionNode::pass2(thread_db* tdbb, CompilerScratch* csb)
{
	savNumberOffset = CMP_impure(csb, sizeof(SLONG));
	action = CMP_pass2(tdbb, csb, action, node);
	return this;
}


jrd_nod* InAutonomousTransactionNode::execute(thread_db* tdbb, jrd_req* request) const
{
	SLONG* savNumber = (SLONG*) ((char*) request + savNumberOffset);

	if (request->req_operation == jrd_req::req_evaluate)
	{
		fb_assert(tdbb->getTransaction() == request->req_transaction);

		request->req_auto_trans.push(request->req_transaction);
		request->req_transaction = TRA_start(tdbb, request->req_transaction->tra_flags,
											 request->req_transaction->tra_lock_timeout,
											 request->req_transaction);
		tdbb->setTransaction(request->req_transaction);

		VIO_start_save_point(tdbb, request->req_transaction);
		*savNumber = request->req_transaction->tra_save_point->sav_number;

		if (!(tdbb->getAttachment()->att_flags & ATT_no_db_triggers))
		{
			// run ON TRANSACTION START triggers
			EXE_execute_db_triggers(tdbb, request->req_transaction, jrd_req::req_trigger_trans_start);
		}

		return action;
	}

	jrd_tra* transaction = request->req_transaction;
	fb_assert(transaction);
	fb_assert(transaction != tdbb->getDatabase()->dbb_sys_trans);

	switch (request->req_operation)
	{
	case jrd_req::req_return:
		if (!(tdbb->getAttachment()->att_flags & ATT_no_db_triggers))
		{
			// run ON TRANSACTION COMMIT triggers
			EXE_execute_db_triggers(tdbb, transaction, jrd_req::req_trigger_trans_commit);
		}

		if (transaction->tra_save_point &&
			!(transaction->tra_save_point->sav_flags & SAV_user) &&
			!transaction->tra_save_point->sav_verb_count)
		{
			VIO_verb_cleanup(tdbb, transaction);
		}

		{ // scope
			AutoSetRestore2<jrd_req*, thread_db> autoNullifyRequest(
				tdbb, &thread_db::getRequest, &thread_db::setRequest, NULL);
			TRA_commit(tdbb, transaction, false);
		} // end scope
		break;

	case jrd_req::req_unwind:
		if (request->req_flags & (req_leave | req_continue_loop))
		{
			try
			{
				if (!(tdbb->getAttachment()->att_flags & ATT_no_db_triggers))
				{
					// run ON TRANSACTION COMMIT triggers
					EXE_execute_db_triggers(tdbb, transaction,
											jrd_req::req_trigger_trans_commit);
				}

				if (transaction->tra_save_point &&
					!(transaction->tra_save_point->sav_flags & SAV_user) &&
					!transaction->tra_save_point->sav_verb_count)
				{
					VIO_verb_cleanup(tdbb, transaction);
				}

				AutoSetRestore2<jrd_req*, thread_db> autoNullifyRequest(
					tdbb, &thread_db::getRequest, &thread_db::setRequest, NULL);
				TRA_commit(tdbb, transaction, false);
			}
			catch (...)
			{
				request->req_flags &= ~(req_leave | req_continue_loop);
				throw;
			}
		}
		else
		{
			ThreadStatusGuard temp_status(tdbb);

			if (!(tdbb->getAttachment()->att_flags & ATT_no_db_triggers))
			{
				try
				{
					// run ON TRANSACTION ROLLBACK triggers
					EXE_execute_db_triggers(tdbb, transaction,
											jrd_req::req_trigger_trans_rollback);
				}
				catch (const Exception&)
				{
					if (tdbb->getDatabase()->dbb_flags & DBB_bugcheck)
					{
						throw;
					}
				}
			}

			try
			{
				AutoSetRestore2<jrd_req*, thread_db> autoNullifyRequest(
					tdbb, &thread_db::getRequest, &thread_db::setRequest, NULL);

				// undo all savepoints up to our one
				for (const Savepoint* save_point = transaction->tra_save_point;
					save_point && *savNumber <= save_point->sav_number;
					save_point = transaction->tra_save_point)
				{
					++transaction->tra_save_point->sav_verb_count;
					VIO_verb_cleanup(tdbb, transaction);
				}

				TRA_rollback(tdbb, transaction, false, false);
			}
			catch (const Exception&)
			{
				if (tdbb->getDatabase()->dbb_flags & DBB_bugcheck)
				{
					throw;
				}
			}
		}
		break;

	default:
		fb_assert(false);
	}

	request->req_transaction = request->req_auto_trans.pop();
	tdbb->setTransaction(request->req_transaction);

	return node->nod_parent;
}


//--------------------


ExecBlockNode* ExecBlockNode::internalDsqlPass()
{
	DsqlCompiledStatement* statement = dsqlScratch->getStatement();

	statement->setBlockNode(this);

	if (returns.hasData())
		statement->setType(DsqlCompiledStatement::TYPE_SELECT_BLOCK);
	else
		statement->setType(DsqlCompiledStatement::TYPE_EXEC_BLOCK);

	dsqlScratch->flags |= DsqlCompilerScratch::FLAG_BLOCK;

	ExecBlockNode* node = FB_NEW(getPool()) ExecBlockNode(getPool());
	node->dsqlScratch = dsqlScratch;

	node->legacyParameters = PASS1_node_psql(dsqlScratch, legacyParameters, false);
	node->returns = returns;
	node->localDeclList = localDeclList;
	node->body = body;

	const size_t count = (node->legacyParameters ? node->legacyParameters->nod_count : 0) +
		node->returns.getCount() +
		(node->localDeclList ? node->localDeclList->nod_count : 0);

	if (count)
	{
		StrArray names(*getDefaultMemoryPool(), count);

		PASS1_check_unique_fields_names(names, node->legacyParameters);

		// Hand-made PASS1_check_unique_fields_names for array of ParameterClause
		for (size_t i = 0; i < returns.getCount(); ++i)
		{
			ParameterClause& parameter = returns[i];

			size_t pos;
			if (!names.find(parameter.name.c_str(), pos))
				names.insert(pos, parameter.name.c_str());
			else
			{
				ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-637) <<
						  Arg::Gds(isc_dsql_duplicate_spec) << Arg::Str(parameter.name));
			}
		}

		PASS1_check_unique_fields_names(names, node->localDeclList);
	}

	return node;
}


void ExecBlockNode::print(string& text, Array<dsql_nod*>& nodes) const
{
	text = "ExecBlockNode\n";

	text += "  Returns:\n";

	for (size_t i = 0; i < returns.getCount(); ++i)
	{
		const ParameterClause& parameter = returns[i];

		string s;
		parameter.print(s);
		text += "    " + s + "\n";
	}

	nodes.add(legacyParameters);
	nodes.add(localDeclList);
	nodes.add(body);
}


void ExecBlockNode::genBlr()
{
	DsqlCompiledStatement* statement = dsqlScratch->getStatement();

	// Update blockNode, because we have a reference to the original unprocessed node.
	statement->setBlockNode(this);

	statement->beginDebug();

	USHORT inputs = 0, outputs = 0, locals = 0;

	// now do the input parameters
	if (legacyParameters)
	{
		dsql_nod* parameters = legacyParameters;
		dsql_nod** ptr = parameters->nod_arg;
		for (const dsql_nod* const* const end = ptr + parameters->nod_count; ptr < end; ptr++)
		{
			dsql_nod* parameter = (*ptr)->nod_arg[Dsql::e_prm_val_fld];
			dsql_fld* field = (dsql_fld*) parameter->nod_arg[Dsql::e_dfl_field];
			// parameter = (*ptr)->nod_arg[Dsql::e_prm_val_val]; USELESS

			DDL_resolve_intl_type(dsqlScratch, field,
				reinterpret_cast<const dsql_str*>(parameter->nod_arg[Dsql::e_dfl_collate]));

			variables.add(MAKE_variable(field, field->fld_name.c_str(),
				VAR_input, 0, (USHORT) (2 * inputs), locals));
			// ASF: do not increment locals here - CORE-2341
			inputs++;
		}
	}

	const unsigned returnsPos = variables.getCount();

	// now do the output parameters
	for (size_t i = 0; i < returns.getCount(); ++i)
	{
		ParameterClause& parameter = returns[i];

		parameter.resolve(dsqlScratch);

		dsql_nod* var = MAKE_variable(parameter.legacyField,
			parameter.name.c_str(), VAR_output, 1, (USHORT) (2 * outputs), locals++);

		variables.add(var);
		outputVariables.add(var);

		++outputs;
	}

	statement->append_uchar(blr_begin);

	if (inputs)
	{
		revertParametersOrder(statement->getSendMsg()->msg_parameters);
		GEN_port(dsqlScratch, statement->getSendMsg());
	}
	else
		statement->setSendMsg(NULL);

	for (Array<dsql_nod*>::const_iterator i = outputVariables.begin(); i != outputVariables.end(); ++i)
	{
		dsql_par* param = MAKE_parameter(statement->getReceiveMsg(), true, true,
			(i - outputVariables.begin()) + 1, *i);
		param->par_node = *i;
		MAKE_desc(dsqlScratch, &param->par_desc, *i, NULL);
		param->par_desc.dsc_flags |= DSC_nullable;
	}

	// Set up parameter to handle EOF
	dsql_par* param = MAKE_parameter(statement->getReceiveMsg(), false, false, 0, NULL);
	statement->setEof(param);
	param->par_desc.dsc_dtype = dtype_short;
	param->par_desc.dsc_scale = 0;
	param->par_desc.dsc_length = sizeof(SSHORT);

	revertParametersOrder(statement->getReceiveMsg()->msg_parameters);
	GEN_port(dsqlScratch, statement->getReceiveMsg());

	if (inputs)
	{
		statement->append_uchar(blr_receive);
		statement->append_uchar(0);
	}

	statement->append_uchar(blr_begin);

	for (unsigned i = 0; i < returnsPos; ++i)
	{
		const dsql_nod* parameter = variables[i];
		const dsql_var* variable = (dsql_var*) parameter->nod_arg[Dsql::e_var_variable];
		const dsql_fld* field = variable->var_field;

		if (field->fld_full_domain || field->fld_not_nullable)
		{
			// ASF: Validation of execute block input parameters is different than procedure
			// parameters, because we can't generate messages using the domains due to the
			// connection charset influence. So to validate, we cast them and assign to null.
			statement->append_uchar(blr_assignment);
			statement->append_uchar(blr_cast);
			DDL_put_field_dtype(dsqlScratch, field, true);
			statement->append_uchar(blr_parameter2);
			statement->append_uchar(0);
			statement->append_ushort(variable->var_msg_item);
			statement->append_ushort(variable->var_msg_item + 1);
			statement->append_uchar(blr_null);
		}
	}

	for (Array<dsql_nod*>::const_iterator i = outputVariables.begin(); i != outputVariables.end(); ++i)
	{
		dsql_nod* parameter = *i;
		dsql_var* variable = (dsql_var*) parameter->nod_arg[Dsql::e_var_variable];
		DDL_put_local_variable(dsqlScratch, variable, 0, NULL);
	}

	dsqlScratch->setPsql(true);

	DDL_put_local_variables(dsqlScratch, localDeclList, locals, variables);

	dsqlScratch->loopLevel = 0;

	dsql_nod* stmtNode = PASS1_statement(dsqlScratch, body);
	GEN_hidden_variables(dsqlScratch, false);

	statement->append_uchar(blr_stall);
	// Put a label before body of procedure, so that
	// any exit statement can get out
	statement->append_uchar(blr_label);
	statement->append_uchar(0);
	GEN_statement(dsqlScratch, stmtNode);
	if (outputs)
		statement->setType(DsqlCompiledStatement::TYPE_SELECT_BLOCK);
	else
		statement->setType(DsqlCompiledStatement::TYPE_EXEC_BLOCK);

	statement->append_uchar(blr_end);
	GEN_return(dsqlScratch, outputVariables, true, true);
	statement->append_uchar(blr_end);

	statement->endDebug();
}


void ExecBlockNode::genReturn()
{
	GEN_return(dsqlScratch, outputVariables, true, false);
}


dsql_nod* ExecBlockNode::resolveVariable(const dsql_str* varName)
{
	// try to resolve variable name against input and output parameters and local variables
	return PASS1_resolve_variable_name(variables, varName);
}


// Revert parameters order for EXECUTE BLOCK statement
void ExecBlockNode::revertParametersOrder(Array<dsql_par*>& parameters)
{
	int start = 0;
	int end = int(parameters.getCount()) - 1;

	while (start < end)
	{
		dsql_par* temp = parameters[start];
		parameters[start] = parameters[end];
		parameters[end] = temp;
		++start;
		--end;
	}
}


//--------------------


static RegisterNode<ExceptionNode> regExceptionNode(blr_abort);


DmlNode* ExceptionNode::parse(thread_db* tdbb, MemoryPool& pool, CompilerScratch* csb,
	UCHAR /*blrOp*/)
{
	ExceptionNode* node = FB_NEW(pool) ExceptionNode(pool);
	const UCHAR type = csb->csb_blr_reader.peekByte();
	const USHORT codeType = csb->csb_blr_reader.getByte();

	// Don't create PsqlException if blr_raise is used.
	if (codeType != blr_raise)
	{
		node->exception = FB_NEW_RPT(pool, 1) PsqlException();
		node->exception->xcp_count = 1;
		xcp_repeat& item = node->exception->xcp_rpt[0];

		switch (codeType)
		{
			case blr_sql_code:
				item.xcp_type = xcp_sql_code;
				item.xcp_code = (SSHORT) csb->csb_blr_reader.getWord();
				break;

			case blr_gds_code:
				{
					string exName;
					item.xcp_type = xcp_gds_code;
					PAR_name(csb, exName);
					exName.lower();
					SLONG code_number = PAR_symbol_to_gdscode(exName);
					if (code_number)
						item.xcp_code = code_number;
					else
						PAR_error(csb, Arg::Gds(isc_codnotdef) << Arg::Str(exName));
				}
				break;

			case blr_exception:
			case blr_exception_msg:
			case blr_exception_params:
				{
					item.xcp_type = xcp_xcp_code;
					PAR_name(csb, node->name);
					if (!(item.xcp_code = MET_lookup_exception_number(tdbb, node->name)))
						PAR_error(csb, Arg::Gds(isc_xcpnotdef) << Arg::Str(node->name));
					jrd_nod* dep_node = PAR_make_node(tdbb, e_dep_length);
					dep_node->nod_type = nod_dependency;
					dep_node->nod_arg[e_dep_object] = (jrd_nod*)(IPTR) item.xcp_code;
					dep_node->nod_arg[e_dep_object_type] = (jrd_nod*)(IPTR) obj_exception;
					csb->csb_dependencies.push(dep_node);
				}
				break;

			default:
				fb_assert(false);
				break;
		}
	}

	if (type == blr_exception_params)
	{
		USHORT count = csb->csb_blr_reader.getWord();

		node->parameters = PAR_make_node(tdbb, count);
		node->parameters->nod_type = nod_list;
		node->parameters->nod_count = count;

		for (unsigned i = 0; i < count; ++i)
			node->parameters->nod_arg[i] = PAR_parse_node(tdbb, csb, VALUE);
	}
	else if (type == blr_exception_msg)
		node->messageExpr = PAR_parse_node(tdbb, csb, VALUE);

	return node;
}


StmtNode* ExceptionNode::internalDsqlPass()
{
	ExceptionNode* node = FB_NEW(getPool()) ExceptionNode(getPool());
	node->dsqlScratch = dsqlScratch;
	node->name = name;
	node->dsqlMessageExpr = PASS1_node(dsqlScratch, dsqlMessageExpr);
	node->dsqlParameters = PASS1_node(dsqlScratch, dsqlParameters);

	return SavepointEncloseNode::make(getPool(), dsqlScratch, node);
}


void ExceptionNode::print(string& text, Array<dsql_nod*>& nodes) const
{
	text.printf("ExceptionNode: Name: %s", name.c_str());
	if (dsqlMessageExpr)
		nodes.add(dsqlMessageExpr);
}


void ExceptionNode::genBlr()
{
	DsqlCompiledStatement* statement = dsqlScratch->getStatement();

	stuff(statement, blr_abort);

	// If exception name is undefined, it means we have re-initiate semantics here,
	// so blr_raise verb should be generated.
	if (name.isEmpty())
	{
		stuff(statement, blr_raise);
		return;
	}

	// If exception parameters or value is defined, it means we have user-defined exception message
	// here, so blr_exception_msg verb should be generated.
	if (dsqlParameters)
		stuff(statement, blr_exception_params);
	else if (dsqlMessageExpr)
		stuff(statement, blr_exception_msg);
	else	// Otherwise go usual way, i.e. generate blr_exception.
		stuff(statement, blr_exception);

	stuff_cstring(statement, name.c_str());

	// If exception parameters or value is defined, generate appropriate BLR verbs.
	if (dsqlParameters)
	{
		stuff_word(statement, dsqlParameters->nod_count);

		dsql_nod** ptr = dsqlParameters->nod_arg;
		const dsql_nod* const* end = ptr + dsqlParameters->nod_count;
		while (ptr < end)
			GEN_expr(dsqlScratch, *ptr++);
	}
	else if (dsqlMessageExpr)
		GEN_expr(dsqlScratch, dsqlMessageExpr);
}


ExceptionNode* ExceptionNode::pass1(thread_db* tdbb, CompilerScratch* csb)
{
	messageExpr = CMP_pass1(tdbb, csb, messageExpr);
	parameters = CMP_pass1(tdbb, csb, parameters);
	return this;
}


ExceptionNode* ExceptionNode::pass2(thread_db* tdbb, CompilerScratch* csb)
{
	messageExpr = CMP_pass2(tdbb, csb, messageExpr, node);
	parameters = CMP_pass2(tdbb, csb, parameters, node);
	return this;
}


jrd_nod* ExceptionNode::execute(thread_db* tdbb, jrd_req* request) const
{
	if (request->req_operation == jrd_req::req_evaluate)
	{
		if (exception)
		{
			// PsqlException is defined, so throw an exception.
			setError(tdbb);
		}
		else if (!request->req_last_xcp.success())
		{
			// PsqlException is undefined, but there was a known exception before,
			// so re-initiate it.
			setError(tdbb);
		}
		else
		{
			// PsqlException is undefined and there weren't any exceptions before,
			// so just do nothing.
			request->req_operation = jrd_req::req_return;
		}
	}

	return node->nod_parent;
}


// Set status vector according to specified error condition and jump to handle error accordingly.
void ExceptionNode::setError(thread_db* tdbb) const
{
	SET_TDBB(tdbb);

	jrd_req* request = tdbb->getRequest();

	if (!exception)
	{
		// Retrieve the status vector and punt.
		request->req_last_xcp.copyTo(tdbb->tdbb_status_vector);
		request->req_last_xcp.clear();
		ERR_punt();
	}

	MetaName exName;
	MetaName relationName;
	TEXT message[XCP_MESSAGE_LENGTH + 1];
	MoveBuffer temp;
	USHORT length = 0;

	if (messageExpr)
	{
		UCHAR* string = NULL;

		// Evaluate exception message and convert it to string.
		dsc* desc = EVL_expr(tdbb, messageExpr);
		if (desc && !(request->req_flags & req_null))
		{
			length = MOV_make_string2(tdbb, desc, CS_METADATA, &string, temp);
			length = MIN(length, sizeof(message) - 1);

			/* dimitr: or should we throw an error here, i.e.
					replace the above assignment with the following lines:

			 if (length > sizeof(message) - 1)
				ERR_post(Arg::Gds(isc_imp_exc) << Arg::Gds(isc_blktoobig));
			*/

			memcpy(message, string, length);
		}
		else
			length = 0;
	}

	message[length] = 0;

	SLONG xcpCode = exception->xcp_rpt[0].xcp_code;

	switch (exception->xcp_rpt[0].xcp_type)
	{
		case xcp_sql_code:
			ERR_post(Arg::Gds(isc_sqlerr) << Arg::Num(xcpCode));

		case xcp_gds_code:
			if (xcpCode == isc_check_constraint)
			{
				MET_lookup_cnstrt_for_trigger(tdbb, exName, relationName, request->req_trg_name);
				ERR_post(Arg::Gds(xcpCode) << Arg::Str(exName) << Arg::Str(relationName));
			}
			else
				ERR_post(Arg::Gds(xcpCode));

		case xcp_xcp_code:
		{
			string tempStr;
			const TEXT* s;

			// CVC: If we have the exception name, use it instead of the number.
			// Solves SF Bug #494981.
			MET_lookup_exception(tdbb, xcpCode, exName, &tempStr);

			if (message[0])
				s = message;
			else if (tempStr.hasData())
				s = tempStr.c_str();
			else
				s = NULL;

			Arg::StatusVector status;
			ISC_STATUS msgCode = parameters ? isc_formatted_exception : isc_random;

			if (s && exName.hasData())
			{
				status << Arg::Gds(isc_except) << Arg::Num(xcpCode) <<
						  Arg::Gds(isc_random) << Arg::Str(exName) <<
						  Arg::Gds(msgCode) << Arg::Str(s);
			}
			else if (s)
			{
				status << Arg::Gds(isc_except) << Arg::Num(xcpCode) <<
						  Arg::Gds(msgCode) << Arg::Str(s);
			}
			else if (exName.hasData())
			{
				ERR_post(Arg::Gds(isc_except) << Arg::Num(xcpCode) <<
						 Arg::Gds(isc_random) << Arg::Str(exName));
			}
			else
				ERR_post(Arg::Gds(isc_except) << Arg::Num(xcpCode));

			// Preallocate the strings, because Arg::StatusVector store the pointers.
			ObjectsArray<string> paramsStr;

			if (parameters)
			{
				for (unsigned i = 0; i < parameters->nod_count; ++i)
				{
					const dsc* value = EVL_expr(tdbb, parameters->nod_arg[i]);

					if (!value || (request->req_flags & req_null))
						paramsStr.push(NULL_STRING_MARK);
					else
						paramsStr.push(MOV_make_string2(tdbb, value, ttype_metadata));
				}

				// And add the values to the status vector only when they are all created and will
				// not move in paramsStr.
				for (unsigned i = 0; i < parameters->nod_count; ++i)
					status << paramsStr[i];
			}

			ERR_post(status);
		}

		default:
			fb_assert(false);
	}
}


//--------------------


void ExitNode::print(string& text, Array<dsql_nod*>& /*nodes*/) const
{
	text = "ExitNode";
}


void ExitNode::genBlr()
{
	DsqlCompiledStatement* statement = dsqlScratch->getStatement();
	stuff(statement, blr_leave);
	stuff(statement, 0);
}


//--------------------


static RegisterNode<PostEventNode> regPostEventNode1(blr_post);
static RegisterNode<PostEventNode> regPostEventNode2(blr_post_arg);


DmlNode* PostEventNode::parse(thread_db* tdbb, MemoryPool& pool, CompilerScratch* csb, UCHAR blrOp)
{
	PostEventNode* node = FB_NEW(pool) PostEventNode(pool);

	node->event = PAR_parse_node(tdbb, csb, VALUE);
	if (blrOp == blr_post_arg)
		node->argument = PAR_parse_node(tdbb, csb, VALUE);

	return node;
}


PostEventNode* PostEventNode::internalDsqlPass()
{
	PostEventNode* node = FB_NEW(getPool()) PostEventNode(getPool());
	node->dsqlScratch = dsqlScratch;

	node->dsqlEvent = PASS1_node(dsqlScratch, dsqlEvent);
	node->dsqlArgument = PASS1_node(dsqlScratch, dsqlArgument);

	return node;
}


void PostEventNode::print(string& text, Array<dsql_nod*>& nodes) const
{
	text = "PostEventNode";
	nodes.add(dsqlEvent);
	if (dsqlArgument)
		nodes.add(dsqlArgument);
}


void PostEventNode::genBlr()
{
	DsqlCompiledStatement* statement = dsqlScratch->getStatement();

	if (dsqlArgument)
	{
		stuff(statement, blr_post_arg);
		GEN_expr(dsqlScratch, dsqlEvent);
		GEN_expr(dsqlScratch, dsqlArgument);
	}
	else
	{
		stuff(statement, blr_post);
		GEN_expr(dsqlScratch, dsqlEvent);
	}
}


PostEventNode* PostEventNode::pass1(thread_db* tdbb, CompilerScratch* csb)
{
	event = CMP_pass1(tdbb, csb, event);
	argument = CMP_pass1(tdbb, csb, argument);
	return this;
}


PostEventNode* PostEventNode::pass2(thread_db* tdbb, CompilerScratch* csb)
{
	event = CMP_pass2(tdbb, csb, event, node);
	argument = CMP_pass2(tdbb, csb, argument, node);
	return this;
}


jrd_nod* PostEventNode::execute(thread_db* tdbb, jrd_req* request) const
{
	jrd_tra* transaction = request->req_transaction;

	DeferredWork* work = DFW_post_work(transaction, dfw_post_event, EVL_expr(tdbb, event), 0);
	if (argument)
		DFW_post_work_arg(transaction, work, EVL_expr(tdbb, argument), 0);

	// For an autocommit transaction, events can be posted without any updates.

	if (transaction->tra_flags & TRA_autocommit)
		transaction->tra_flags |= TRA_perform_autocommit;

	if (request->req_operation == jrd_req::req_evaluate)
		request->req_operation = jrd_req::req_return;

	return node->nod_parent;
}


//--------------------


static RegisterNode<SavepointNode> regSavepointNode(blr_user_savepoint);


DmlNode* SavepointNode::parse(thread_db* /*tdbb*/, MemoryPool& pool, CompilerScratch* csb, UCHAR /*blrOp*/)
{
	SavepointNode* node = FB_NEW(pool) SavepointNode(pool);

	node->command = (Command) csb->csb_blr_reader.getByte();
	PAR_name(csb, node->name);

	return node;
}


SavepointNode* SavepointNode::internalDsqlPass()
{
	DsqlCompiledStatement* statement = dsqlScratch->getStatement();

	// ASF: It should never enter in this IF, because the grammar does not allow it.
	if (dsqlScratch->flags & DsqlCompilerScratch::FLAG_BLOCK) // blocks, procedures and triggers
	{
		const char* cmd = NULL;

		switch (command)
		{
		//case CMD_NOTHING:
		case CMD_SET:
			cmd = "SAVEPOINT";
			break;
		case CMD_RELEASE:
			cmd = "RELEASE";
			break;
		case CMD_RELEASE_ONLY:
			cmd = "RELEASE ONLY";
			break;
		case CMD_ROLLBACK:
			cmd = "ROLLBACK";
			break;
		default:
			cmd = "UNKNOWN";
			fb_assert(false);
		}

		ERRD_post(
			Arg::Gds(isc_sqlerr) << Arg::Num(-104) <<
			// Token unknown
			Arg::Gds(isc_token_err) <<
			Arg::Gds(isc_random) << Arg::Str(cmd));
	}

	statement->setType(DsqlCompiledStatement::TYPE_SAVEPOINT);

	return this;
}


void SavepointNode::print(string& text, Array<dsql_nod*>& /*nodes*/) const
{
	text = "SavepointNode";
}


void SavepointNode::genBlr()
{
	DsqlCompiledStatement* statement = dsqlScratch->getStatement();
	stuff(statement, blr_user_savepoint);
	stuff(statement, (UCHAR) command);
	stuff_cstring(statement, name.c_str());
}


SavepointNode* SavepointNode::pass1(thread_db* /*tdbb*/, CompilerScratch* /*csb*/)
{
	return this;
}


SavepointNode* SavepointNode::pass2(thread_db* /*tdbb*/, CompilerScratch* /*csb*/)
{
	return this;
}


jrd_nod* SavepointNode::execute(thread_db* tdbb, jrd_req* request) const
{
	Database* dbb = request->req_attachment->att_database;
	jrd_tra* transaction = request->req_transaction;

	if (request->req_operation == jrd_req::req_evaluate && transaction != dbb->dbb_sys_trans)
	{
		// Skip the savepoint created by EXE_start
		Savepoint* savepoint = transaction->tra_save_point->sav_next;
		Savepoint* previous = transaction->tra_save_point;

		// Find savepoint
		bool found = false;
		while (true)
		{
			if (!savepoint || !(savepoint->sav_flags & SAV_user))
				break;

			if (!strcmp(name.c_str(), savepoint->sav_name))
			{
				found = true;
				break;
			}

			previous = savepoint;
			savepoint = savepoint->sav_next;
		}

		if (!found && command != CMD_SET)
			ERR_post(Arg::Gds(isc_invalid_savepoint) << Arg::Str(name));

		switch (command)
		{
			case CMD_SET:
				// Release the savepoint
				if (found)
				{
					Savepoint* const current = transaction->tra_save_point;
					transaction->tra_save_point = savepoint;
					EXE_verb_cleanup(tdbb, transaction);
					previous->sav_next = transaction->tra_save_point;
					transaction->tra_save_point = current;
				}

				// Use the savepoint created by EXE_start
				transaction->tra_save_point->sav_flags |= SAV_user;
				strcpy(transaction->tra_save_point->sav_name, name.c_str());
				break;

			case CMD_RELEASE_ONLY:
			{
				// Release the savepoint
				Savepoint* const current = transaction->tra_save_point;
				transaction->tra_save_point = savepoint;
				EXE_verb_cleanup(tdbb, transaction);
				previous->sav_next = transaction->tra_save_point;
				transaction->tra_save_point = current;
				break;
			}

			case CMD_RELEASE:
			{
				const SLONG sav_number = savepoint->sav_number;

				// Release the savepoint and all subsequent ones
				while (transaction->tra_save_point &&
					transaction->tra_save_point->sav_number >= sav_number)
				{
					EXE_verb_cleanup(tdbb, transaction);
				}

				// Restore the savepoint initially created by EXE_start
				VIO_start_save_point(tdbb, transaction);
				break;
			}

			case CMD_ROLLBACK:
			{
				const SLONG sav_number = savepoint->sav_number;

				// Undo the savepoint
				while (transaction->tra_save_point &&
					transaction->tra_save_point->sav_number >= sav_number)
				{
					transaction->tra_save_point->sav_verb_count++;
					EXE_verb_cleanup(tdbb, transaction);
				}

				// Now set the savepoint again to allow to return to it later
				VIO_start_save_point(tdbb, transaction);
				transaction->tra_save_point->sav_flags |= SAV_user;
				strcpy(transaction->tra_save_point->sav_name, name.c_str());
				break;
			}

			default:
				BUGCHECK(232);
				break;
		}
	}

	request->req_operation = jrd_req::req_return;

	return node->nod_parent;
}


//--------------------


static RegisterNode<SuspendNode> regSuspendNode(blr_send);


DmlNode* SuspendNode::parse(thread_db* tdbb, MemoryPool& pool, CompilerScratch* csb, UCHAR /*blrOp*/)
{
	SuspendNode* node = FB_NEW(pool) SuspendNode(pool);

	USHORT n = csb->csb_blr_reader.getByte();
	node->message = csb->csb_rpt[n].csb_message;
	node->statement = PAR_parse_node(tdbb, csb, STATEMENT);

	return node;
}


SuspendNode* SuspendNode::internalDsqlPass()
{
	DsqlCompiledStatement* statement = dsqlScratch->getStatement();

	if (dsqlScratch->flags & (DsqlCompilerScratch::FLAG_TRIGGER | DsqlCompilerScratch::FLAG_FUNCTION))
	{
		ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-104) <<
				  // Token unknown
				  Arg::Gds(isc_token_err) <<
				  Arg::Gds(isc_random) << Arg::Str("SUSPEND"));
	}

	if (dsqlScratch->flags & DsqlCompilerScratch::FLAG_IN_AUTO_TRANS_BLOCK)	// autonomous transaction
	{
		ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-901) <<
				  Arg::Gds(isc_dsql_unsupported_in_auto_trans) << Arg::Str("SUSPEND"));
	}

	statement->addFlags(DsqlCompiledStatement::FLAG_SELECTABLE);

	blockNode = statement->getBlockNode();

	return this;
}


void SuspendNode::print(string& text, Array<dsql_nod*>& /*nodes*/) const
{
	text = "SuspendNode";
}


void SuspendNode::genBlr()
{
	if (blockNode)
		blockNode->genReturn();
}


SuspendNode* SuspendNode::pass1(thread_db* tdbb, CompilerScratch* csb)
{
	statement = CMP_pass1(tdbb, csb, statement);
	return this;
}


SuspendNode* SuspendNode::pass2(thread_db* tdbb, CompilerScratch* csb)
{
	statement = CMP_pass2(tdbb, csb, statement, node);
	return this;
}


// Execute a SEND statement.
jrd_nod* SuspendNode::execute(thread_db* /*tdbb*/, jrd_req* request) const
{
	switch (request->req_operation)
	{
	case jrd_req::req_evaluate:
		return statement;

	case jrd_req::req_return:
		request->req_operation = jrd_req::req_send;
		request->req_message = message;
		request->req_flags |= req_stall;
		return node;

	case jrd_req::req_proceed:
		request->req_operation = jrd_req::req_return;
		return node->nod_parent;

	default:
		return node->nod_parent;
	}
}


//--------------------


ReturnNode* ReturnNode::internalDsqlPass()
{
	DsqlCompiledStatement* const statement = dsqlScratch->getStatement();

	if (!(dsqlScratch->flags & DsqlCompilerScratch::FLAG_FUNCTION))
	{
		ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-104) <<
				  // Token unknown
				  Arg::Gds(isc_token_err) <<
				  Arg::Gds(isc_random) << Arg::Str("RETURN"));
	}

	if (dsqlScratch->flags & DsqlCompilerScratch::FLAG_IN_AUTO_TRANS_BLOCK)	// autonomous transaction
	{
		ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-901) <<
				  Arg::Gds(isc_dsql_unsupported_in_auto_trans) << Arg::Str("RETURN"));
	}

	ReturnNode* node = FB_NEW(getPool()) ReturnNode(getPool());
	node->dsqlScratch = dsqlScratch;
	node->blockNode = statement->getBlockNode();
	node->value = PASS1_node(dsqlScratch, value);

	return node;
}


void ReturnNode::print(string& text, Array<dsql_nod*>& nodes) const
{
	text = "ReturnNode";
	nodes.add(value);
}


void ReturnNode::genBlr()
{
	DsqlCompiledStatement* const statement = dsqlScratch->getStatement();

	statement->append_uchar(blr_assignment);
	GEN_expr(dsqlScratch, value);
	statement->append_uchar(blr_variable);
	statement->append_ushort(0);

	blockNode->genReturn();

	statement->append_uchar(blr_leave);
	statement->append_uchar(0);
}


}	// namespace Jrd
