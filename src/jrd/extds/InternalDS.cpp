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
 *  The Original Code was created by Vlad Khorsun
 *  for the Firebird Open Source RDBMS project.
 *
 *  Copyright (c) 2008 Vlad Khorsun <hvlad@users.sourceforge.net>
 *  and all contributors signed below.
 *
 *  All Rights Reserved.
 *  Contributor(s): ______________________________________.
 */

#include "firebird.h"
#include "fb_types.h"
#include "../common.h"
#include "../../include/fb_blk.h"

#include "../align.h"
#include "../exe.h"
#include "../jrd.h"
#include "../dsc.h"
#include "../../dsql/dsql.h"
#include "../../dsql/sqlda_pub.h"

#include "../blb_proto.h"
#include "../evl_proto.h"
#include "../exe_proto.h"
#include "../mov_proto.h"
#include "../mov_proto.h"
#include "../PreparedStatement.h"

#include "InternalDS.h"

using namespace Jrd;
using namespace Firebird;

namespace EDS {

const char *INTERNAL_PROVIDER_NAME = "Internal";

class RegisterInternalProvider
{
public:
	RegisterInternalProvider()
	{
		InternalProvider* provider = new InternalProvider(INTERNAL_PROVIDER_NAME);
		Manager::manager->addProvider(provider);
	}
};

static RegisterInternalProvider reg;

// InternalProvider

void InternalProvider::jrdAttachmentEnd(thread_db *tdbb, Attachment* att)
{
	if (m_connections.getCount() == 0)
		return;

	Connection **ptr = m_connections.end();
	Connection **begin = m_connections.begin();
	for (ptr--; ptr >= begin; ptr--)
	{
		InternalConnection *conn = (InternalConnection*) *ptr;
		if (conn->getJrdAtt() == att)
			releaseConnection(tdbb, *conn, false);
	}
}

void InternalProvider::getRemoteError(ISC_STATUS* status, string &err)
{
	err = "";

	char buff[512];
	const ISC_STATUS* p = status, *end = status + ISC_STATUS_LENGTH;
	while (p < end)
	{
		const ISC_STATUS code = *p ? p[1] : 0;
		if (!fb_interpret(buff, sizeof(buff), &p))
			break;

		string rem_err;
		rem_err.printf("%lu : %s\n", code, buff);
		err += rem_err;
	}
}

Connection* InternalProvider::doCreateConnection()
{
	return new InternalConnection(*this);
}


// InternalConnection

InternalConnection::~InternalConnection()
{
}

void InternalConnection::attach(thread_db *tdbb, const Firebird::string &dbName, 
		const Firebird::string &user, const Firebird::string &pwd)
{
	fb_assert(!m_attachment);
	m_attachment = tdbb->getAttachment();
	m_sqlDialect = (m_attachment->att_database->dbb_flags & DBB_DB_SQL_dialect_3) ? 
					SQL_DIALECT_V6 : SQL_DIALECT_V5;
}

void InternalConnection::detach(thread_db *tdbb)
{
	clearStatements(tdbb);

	fb_assert(m_attachment);
	m_attachment = 0;
}

bool InternalConnection::isAvailable(thread_db *tdbb, TraScope traScope) const
{
	return (tdbb->getAttachment() == m_attachment);
}

bool InternalConnection::isSameDatabase(thread_db *tdbb, const Firebird::string &dbName, 
		const Firebird::string &user, const Firebird::string &pwd) const
{
	return dbName.isEmpty() && user.isEmpty() && (tdbb->getAttachment() == m_attachment);
}

Transaction* InternalConnection::doCreateTransaction()
{
	return new InternalTransaction(*this);
}

Statement* InternalConnection::doCreateStatement()
{
	return new InternalStatement(*this);
}

Blob* InternalConnection::createBlob()
{
	return new InternalBlob(*this);
}


// InternalTransaction()

void InternalTransaction::doStart(ISC_STATUS* status, thread_db *tdbb, ClumpletWriter &tpb)
{
	fb_assert(!m_transaction);

	if (m_scope == traCommon) {
		m_transaction = tdbb->getTransaction();
	}
	else
	{
		Attachment *att = m_IntConnection.getJrdAtt();

		EngineCallbackGuard guard(tdbb, *this);
		jrd8_start_transaction(status, &m_transaction, 1, &att, 
			tpb.getBufferLength(), tpb.getBuffer());
	}
}

void InternalTransaction::doPrepare(ISC_STATUS* status, thread_db *tdbb, 
		int info_len, const char* info)
{
	fb_assert(m_transaction);
	fb_assert(false);
}

void InternalTransaction::doCommit(ISC_STATUS* status, thread_db *tdbb, bool retain)
{
	fb_assert(m_transaction);
	
	if (m_scope == traCommon) {
		m_transaction = 0;
	}
	else
	{
		Attachment *att = m_IntConnection.getJrdAtt();

		EngineCallbackGuard guard(tdbb, *this);
		if (retain)
			jrd8_commit_retaining(status, &m_transaction);
		else
			jrd8_commit_transaction(status, &m_transaction);
	}
}

void InternalTransaction::doRollback(ISC_STATUS* status, thread_db *tdbb, bool retain)
{
	fb_assert(m_transaction);

	if (m_scope == traCommon) {
		m_transaction = 0;
	}
	else
	{
		Attachment *att = m_IntConnection.getJrdAtt();

		EngineCallbackGuard guard(tdbb, *this);
		if (retain)
			jrd8_rollback_retaining(status, &m_transaction);
		else
			jrd8_rollback_transaction(status, &m_transaction);
	}
}


// InternalStatement 

InternalStatement::InternalStatement(InternalConnection &conn) :
	Statement(conn),
	m_intConnection(conn),
	m_intTransaction(0),
	m_request(0),
	m_inBlr(getPool()),
	m_outBlr(getPool())
{
}

InternalStatement::~InternalStatement()
{
}

void InternalStatement::doPrepare(thread_db *tdbb, const string &sql)
{
	m_inBlr.clear();
	m_outBlr.clear();

	Attachment *att = m_intConnection.getJrdAtt();
	jrd_tra *tran = getIntTransaction()->getJrdTran();

	ISC_STATUS_ARRAY status = {0};
	if (!m_request) 
	{
		fb_assert(!m_allocated);
		EngineCallbackGuard guard(tdbb, *this);
		jrd8_allocate_statement(status, &att, &m_request);
		m_allocated = (m_request != 0);
	}
	if (status[1]) {
		raise(status, tdbb, "jrd8_allocate_statement", &sql);
	}

	{
		EngineCallbackGuard guard(tdbb, *this);
		jrd8_prepare(status, &tran, &m_request, sql.length(), sql.c_str(), 
			m_connection.getSqlDialect(), 0, NULL, 0, NULL);
	}
	if (status[1]) {
		raise(status, tdbb, "jrd8_prepare", &sql);
	}

	if (m_request->req_send) {
		try {
			PreparedStatement::parseDsqlMessage(m_request->req_send, m_inDescs, m_inBlr, m_in_buffer);
			m_inputs = m_inDescs.getCount() / 2;
		}
		catch (const Exception&) {
			raise(tdbb->tdbb_status_vector, tdbb, "parse input message", &sql);
		}
	}
	else {
		m_inputs = 0;
	}

	if (m_request->req_receive) {
		try {
			PreparedStatement::parseDsqlMessage(m_request->req_receive, m_outDescs, m_outBlr, m_out_buffer);
			m_outputs = m_outDescs.getCount() / 2;
		}
		catch (const Exception&) {
			raise(tdbb->tdbb_status_vector, tdbb, "parse output message", &sql);
		}
	}
	else {
		m_outputs = 0;
	}

	m_stmt_selectable = false;
	switch (m_request->req_type)
	{
	case REQ_SELECT: 
	case REQ_SELECT_UPD: 
	case REQ_EMBED_SELECT: 
	case REQ_SELECT_BLOCK:
		m_stmt_selectable = true;
		break;
	
	case REQ_START_TRANS: 
	case REQ_COMMIT: 
	case REQ_ROLLBACK: 
	case REQ_COMMIT_RETAIN: 
	case REQ_ROLLBACK_RETAIN: 
	case REQ_CREATE_DB: 
		// forbidden ?
		break;

	case REQ_INSERT: case REQ_DELETE: case REQ_UPDATE: 
	case REQ_UPDATE_CURSOR: case REQ_DELETE_CURSOR: case REQ_DDL: 
	case REQ_GET_SEGMENT: case REQ_PUT_SEGMENT: 
	case REQ_EXEC_PROCEDURE: case REQ_SET_GENERATOR: 
	case REQ_SAVEPOINT: case REQ_EXEC_BLOCK:
		break;
	}
}


void InternalStatement::doExecute(thread_db *tdbb)
{
	jrd_tra *transaction = getIntTransaction()->getJrdTran();
	
	ISC_STATUS_ARRAY status = {0};
	{
		EngineCallbackGuard guard(tdbb, *this);
		jrd8_execute(status, &transaction, &m_request, 
			m_inBlr.getCount(), (SCHAR*) m_inBlr.begin(), 0, m_in_buffer.getCount(), (SCHAR*) m_in_buffer.begin(), 
			m_outBlr.getCount(), (SCHAR*) m_outBlr.begin(), 0, m_out_buffer.getCount(), (SCHAR*) m_out_buffer.begin());
	}

	if (status[1]) {
		raise(status, tdbb, "jrd8_execute");
	}
}


void InternalStatement::doOpen(thread_db *tdbb)
{
	jrd_tra *transaction = getIntTransaction()->getJrdTran();
	
	ISC_STATUS_ARRAY status = {0};
	{
		EngineCallbackGuard guard(tdbb, *this);
		jrd8_execute(status, &transaction, &m_request, 
			m_inBlr.getCount(), (SCHAR*) m_inBlr.begin(), 0, 
			m_in_buffer.getCount(), (SCHAR*) m_in_buffer.begin(), 
			0, NULL, 0, 0, NULL);
	}
	if (status[1]) {
		raise(status, tdbb, "jrd8_execute");
	}
}


bool InternalStatement::doFetch(thread_db *tdbb)
{
	ISC_STATUS_ARRAY status = {0};
	ISC_STATUS res = 0;
	{
		EngineCallbackGuard guard(tdbb, *this);
		res = jrd8_fetch(status, &m_request, 
			m_outBlr.getCount(), (SCHAR*) m_outBlr.begin(), 0, 
			m_out_buffer.getCount(), (SCHAR*) m_out_buffer.begin());
	}
	if (status[1]) {
		raise(status, tdbb, "jrd8_fetch");
	}

	return (res != 100);
}


void InternalStatement::doClose(thread_db *tdbb, bool drop)
{
	ISC_STATUS_ARRAY status = {0};
	{
		EngineCallbackGuard guard(tdbb, *this);
		jrd8_free_statement(status, &m_request, drop ? DSQL_drop : DSQL_close);
		m_allocated = (m_request != 0);
	}
	if (status[1]) {
		raise(status, tdbb, "jrd8_free_statement");
	}
}

void InternalStatement::putExtBlob(thread_db *tdbb, const dsc &src, dsc &dst)
{
	if (m_transaction->getScope() == traCommon)
		MOV_move(tdbb, (dsc*)&src, &dst);
	else
		Statement::putExtBlob(tdbb, src, dst);
}

void InternalStatement::getExtBlob(thread_db *tdbb, const dsc &src, dsc &dst)
{
	fb_assert(dst.dsc_length == src.dsc_length);
	fb_assert(dst.dsc_length == sizeof(bid));

	if (m_transaction->getScope() == traCommon)
		memcpy(dst.dsc_address, src.dsc_address, sizeof(bid));
	else
		Statement::getExtBlob(tdbb, src, dst);
}



// InternalBlob 

InternalBlob::InternalBlob(InternalConnection &conn) : 
	Blob(conn),
	m_connection(conn),
	m_blob(NULL)
{
	m_blob_id.clear();
}

InternalBlob::~InternalBlob()
{
	fb_assert(!m_blob);
}

void InternalBlob::open(thread_db *tdbb, Transaction &tran, const dsc &desc, UCharBuffer *bpb)
{
	fb_assert(!m_blob);
	fb_assert(sizeof(m_blob_id) == desc.dsc_length);

	Attachment *att = m_connection.getJrdAtt();
	jrd_tra* transaction = ((InternalTransaction&) tran).getJrdTran();
	memcpy(&m_blob_id, desc.dsc_address, sizeof(m_blob_id));

	ISC_STATUS_ARRAY status = {0};
	{
		EngineCallbackGuard guard(tdbb, m_connection);

		USHORT bpb_len = bpb ? bpb->getCount() : 0;
		UCHAR *bpb_buff = bpb ? bpb->begin() : NULL;

		jrd8_open_blob2(status, &att, &transaction, &m_blob, &m_blob_id, 
			bpb_len, bpb_buff);
	}
	if (status[1]) {
		m_connection.raise(status, tdbb, "jrd8_open_blob2");
	}
	fb_assert(m_blob);
}

void InternalBlob::create(thread_db *tdbb, Transaction &tran, dsc &desc, UCharBuffer *bpb)
{
	fb_assert(!m_blob);
	fb_assert(sizeof(m_blob_id) == desc.dsc_length);

	Attachment *att = m_connection.getJrdAtt();
	jrd_tra* transaction = ((InternalTransaction&) tran).getJrdTran();
	m_blob_id.clear();

	ISC_STATUS_ARRAY status = {0};
	{
		EngineCallbackGuard guard(tdbb, m_connection);

		USHORT bpb_len = bpb ? bpb->getCount() : 0;
		UCHAR *bpb_buff = bpb ? bpb->begin() : NULL;

		jrd8_create_blob2(status, &att, &transaction, &m_blob, &m_blob_id, 
			bpb_len, bpb_buff);
		memcpy(desc.dsc_address, &m_blob_id, sizeof(m_blob_id));
	}
	if (status[1]) {
		m_connection.raise(status, tdbb, "jrd8_create_blob2");
	}
	fb_assert(m_blob);
}

USHORT InternalBlob::read(thread_db *tdbb, char *buff, USHORT len)
{
	fb_assert(m_blob);
	Attachment *att = m_connection.getJrdAtt();

	USHORT result = 0;
	ISC_STATUS_ARRAY status = {0};
	{
		EngineCallbackGuard guard(tdbb, m_connection);
		jrd8_get_segment(status, &m_blob, &result, len, (UCHAR*) buff);
	}
	if (status[1] == isc_segstr_eof) {
		fb_assert(result == 0);
	}
	else if (status[1] == isc_segment) {
	}
	else if (status[1]) {
		m_connection.raise(status, tdbb, "jrd8_get_segment");
	}

	return result;
}

void InternalBlob::write(thread_db *tdbb, char *buff, USHORT len)
{
	fb_assert(m_blob);
	Attachment *att = m_connection.getJrdAtt();

	ISC_STATUS_ARRAY status = {0};
	{
		EngineCallbackGuard guard(tdbb, m_connection);
		jrd8_put_segment(status, &m_blob, len, (UCHAR*) buff);
	}
	if (status[1]) {
		m_connection.raise(status, tdbb, "jrd8_put_segment");
	}
}

void InternalBlob::close(thread_db *tdbb)
{
	fb_assert(m_blob);
	ISC_STATUS_ARRAY status = {0};
	{
		EngineCallbackGuard guard(tdbb, m_connection);
		jrd8_close_blob(status, &m_blob);
	}
	if (status[1]) {
		m_connection.raise(status, tdbb, "jrd8_close_blob");
	}
	fb_assert(!m_blob);
}

void InternalBlob::cancel(thread_db *tdbb)
{
	if (!m_blob) {
		return;
	}

	ISC_STATUS_ARRAY status = {0};
	{
		EngineCallbackGuard guard(tdbb, m_connection);
		jrd8_cancel_blob(status, &m_blob);
	}
	if (status[1]) {
		m_connection.raise(status, tdbb, "jrd8_cancel_blob");
	}
	fb_assert(!m_blob);
}


}; // namespace EDS
