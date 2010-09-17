/*
 *  The contents of this file are subject to the Initial
 *  Developer's Public License Version 1.0 (the "License");
 *  you may not use this file except in compliance with the
 *  License. You may obtain a copy of the License at
 *  http://www.ibphoenix.com/main.nfs?a=ibphoenix&page=ibp_idpl.
 *
 *  Software distributed under the License is distributed AS IS,
 *  WITHOUT WARRANTY OF ANY KIND, either express or implied.
 *  See the License for the specific language governing rights
 *  and limitations under the License.
 *
 *  The Original Code was created by Adriano dos Santos Fernandes
 *  for the Firebird Open Source RDBMS project.
 *
 *  Copyright (c) 2010 Adriano dos Santos Fernandes <adrianosf@gmail.com>
 *  and all contributors signed below.
 *
 *  All Rights Reserved.
 *  Contributor(s): ______________________________________.
 */

#ifndef DSQL_BOOL_NODES_H
#define DSQL_BOOL_NODES_H

#include "../jrd/common.h"
#include "../jrd/blr.h"
#include "../dsql/Nodes.h"

namespace Jrd {

class RecordSource;


class BinaryBoolNode : public TypedNode<BoolExprNode, ExprNode::TYPE_BINARY_BOOL>
{
public:
	BinaryBoolNode(MemoryPool& pool, UCHAR aBlrOp, dsql_nod* aArg1 = NULL, dsql_nod* aArg2 = NULL);

	static DmlNode* parse(thread_db* tdbb, MemoryPool& pool, CompilerScratch* csb, UCHAR blrOp);

	virtual void print(Firebird::string& text, Firebird::Array<dsql_nod*>& nodes) const;
	virtual BoolExprNode* dsqlPass(DsqlCompilerScratch* dsqlScratch);
	virtual void genBlr(DsqlCompilerScratch* dsqlScratch);

	virtual BoolExprNode* copy(thread_db* tdbb, NodeCopier& copier);
	virtual bool dsqlMatch(const ExprNode* other, bool ignoreMapCast) const;
	virtual bool execute(thread_db* tdbb, jrd_req* request) const;

private:
	virtual bool executeAnd(thread_db* tdbb, jrd_req* request) const;
	virtual bool executeOr(thread_db* tdbb, jrd_req* request) const;

public:
	UCHAR blrOp;
	dsql_nod* dsqlArg1;
	dsql_nod* dsqlArg2;
	NestConst<jrd_nod> arg1;
	NestConst<jrd_nod> arg2;
};


class ComparativeBoolNode : public TypedNode<BoolExprNode, ExprNode::TYPE_COMPARATIVE_BOOL>
{
public:
	enum DsqlFlag
	{
		DFLAG_NONE,
		DFLAG_ANSI_ALL,
		DFLAG_ANSI_ANY
	};

	ComparativeBoolNode(MemoryPool& pool, UCHAR aBlrOp, dsql_nod* aArg1 = NULL,
		dsql_nod* aArg2 = NULL, dsql_nod* aArg3 = NULL);

	static DmlNode* parse(thread_db* tdbb, MemoryPool& pool, CompilerScratch* csb, UCHAR blrOp);

	virtual void print(Firebird::string& text, Firebird::Array<dsql_nod*>& nodes) const;
	virtual BoolExprNode* dsqlPass(DsqlCompilerScratch* dsqlScratch);
	virtual void genBlr(DsqlCompilerScratch* dsqlScratch);

	virtual bool jrdPossibleUnknownFinder(PossibleUnknownFinder& visitor)
	{
		return blrOp == blr_equiv ? true : BoolExprNode::jrdPossibleUnknownFinder(visitor);
	}

	virtual BoolExprNode* copy(thread_db* tdbb, NodeCopier& copier);
	virtual bool dsqlMatch(const ExprNode* other, bool ignoreMapCast) const;
	virtual ExprNode* pass1(thread_db* tdbb, CompilerScratch* csb);
	virtual ExprNode* pass2(thread_db* tdbb, CompilerScratch* csb);
	virtual bool execute(thread_db* tdbb, jrd_req* request) const;

private:
	bool stringBoolean(thread_db* tdbb, jrd_req* request, dsc* desc1, dsc* desc2,
		bool computedInvariant) const;
	bool stringFunction(thread_db* tdbb, jrd_req* request, SLONG l1, const UCHAR* p1,
		SLONG l2, const UCHAR* p2, USHORT ttype, bool computedInvariant) const;
	bool sleuth(thread_db* tdbb, jrd_req* request, const dsc* desc1, const dsc* desc2) const;

	BoolExprNode* createRseNode(DsqlCompilerScratch* dsqlScratch, UCHAR rseBlrOp);

public:
	UCHAR blrOp;
	dsql_nod* dsqlArg1;
	dsql_nod* dsqlArg2;
	dsql_nod* dsqlArg3;
	DsqlFlag dsqlFlag;
	NestConst<jrd_nod> arg1;
	NestConst<jrd_nod> arg2;
	NestConst<jrd_nod> arg3;

};


class MissingBoolNode : public TypedNode<BoolExprNode, ExprNode::TYPE_MISSING_BOOL>
{
public:
	MissingBoolNode(MemoryPool& pool, dsql_nod* aArg = NULL);

	static DmlNode* parse(thread_db* tdbb, MemoryPool& pool, CompilerScratch* csb, UCHAR blrOp);

	virtual void print(Firebird::string& text, Firebird::Array<dsql_nod*>& nodes) const;
	virtual BoolExprNode* dsqlPass(DsqlCompilerScratch* dsqlScratch);
	virtual void genBlr(DsqlCompilerScratch* dsqlScratch);

	virtual bool jrdPossibleUnknownFinder(PossibleUnknownFinder& /*visitor*/)
	{
		return true;
	}

	virtual BoolExprNode* copy(thread_db* tdbb, NodeCopier& copier);
	virtual ExprNode* pass1(thread_db* tdbb, CompilerScratch* csb);
	virtual ExprNode* pass2(thread_db* tdbb, CompilerScratch* csb);
	virtual bool execute(thread_db* tdbb, jrd_req* request) const;

public:
	dsql_nod* dsqlArg;
	NestConst<jrd_nod> arg;
};


class NotBoolNode : public TypedNode<BoolExprNode, ExprNode::TYPE_NOT_BOOL>
{
public:
	NotBoolNode(MemoryPool& pool, dsql_nod* aArg = NULL);

	static DmlNode* parse(thread_db* tdbb, MemoryPool& pool, CompilerScratch* csb, UCHAR blrOp);

	virtual void print(Firebird::string& text, Firebird::Array<dsql_nod*>& nodes) const;
	virtual BoolExprNode* dsqlPass(DsqlCompilerScratch* dsqlScratch);
	virtual void genBlr(DsqlCompilerScratch* dsqlScratch);

	virtual bool jrdPossibleUnknownFinder(PossibleUnknownFinder& /*visitor*/)
	{
		return true;
	}

	virtual BoolExprNode* copy(thread_db* tdbb, NodeCopier& copier);
	virtual ExprNode* pass1(thread_db* tdbb, CompilerScratch* csb);
	virtual bool execute(thread_db* tdbb, jrd_req* request) const;

private:
	BoolExprNode* process(DsqlCompilerScratch* dsqlScratch, bool invert);

public:
	dsql_nod* dsqlArg;
	NestConst<jrd_nod> arg;
};


class RseBoolNode : public TypedNode<BoolExprNode, ExprNode::TYPE_RSE_BOOL>
{
public:
	RseBoolNode(MemoryPool& pool, UCHAR aBlrOp, dsql_nod* aDsqlRse = NULL);

	static DmlNode* parse(thread_db* tdbb, MemoryPool& pool, CompilerScratch* csb, UCHAR blrOp);

	virtual void print(Firebird::string& text, Firebird::Array<dsql_nod*>& nodes) const;
	virtual BoolExprNode* dsqlPass(DsqlCompilerScratch* dsqlScratch);
	virtual void genBlr(DsqlCompilerScratch* dsqlScratch);

	virtual bool dsqlAggregateFinder(AggregateFinder& visitor)
	{
		fb_assert(blrOp != blr_any && blrOp != blr_ansi_any && blrOp != blr_ansi_all);
		return visitor.ignoreSubSelects ? false : BoolExprNode::dsqlVisit(visitor);
	}

	virtual bool jrdPossibleUnknownFinder(PossibleUnknownFinder& /*visitor*/)
	{
		return true;
	}

	virtual BoolExprNode* copy(thread_db* tdbb, NodeCopier& copier);
	virtual bool dsqlMatch(const ExprNode* other, bool ignoreMapCast) const;
	virtual ExprNode* pass1(thread_db* tdbb, CompilerScratch* csb);
	virtual ExprNode* pass2(thread_db* tdbb, CompilerScratch* csb);
	virtual bool execute(thread_db* tdbb, jrd_req* request) const;

private:
	BoolExprNode* convertNeqAllToNotAny(thread_db* tdbb, CompilerScratch* csb);

public:
	UCHAR blrOp;
	dsql_nod* dsqlRse;
	NestConst<jrd_nod> rse;
	NestConst<RecordSource> rsb;
};


} // namespace

#endif // DSQL_BOOL_NODES_H
