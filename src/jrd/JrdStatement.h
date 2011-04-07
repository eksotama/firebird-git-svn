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
 * Adriano dos Santos Fernandes
 */

#ifndef JRD_STATEMENT_H
#define JRD_STATEMENT_H

#include "../include/fb_blk.h"
#include "../jrd/exe.h"

namespace Jrd {


// Compiled statement.
class JrdStatement : public Firebird::StdIface<Firebird::IRequest, FB_I_REQUEST_VERSION, pool_alloc<type_req> >
{
public:
	static const unsigned FLAG_SYS_TRIGGER	= 0x01;
	static const unsigned FLAG_INTERNAL		= 0x02;
	static const unsigned FLAG_IGNORE_PERM	= 0x04;
	static const unsigned FLAG_VERSION4		= 0x08;

	static const unsigned MAP_LENGTH = 256;
	static const unsigned MAX_CLONES = 1000;
	static const unsigned MAX_REQUEST_SIZE = 10485760;	// 10 MB - just to be safe

private:
	JrdStatement(thread_db* tdbb, MemoryPool* p, CompilerScratch* csb);

public:
	static JrdStatement* makeStatement(thread_db* tdbb, CompilerScratch* csb, bool internalFlag);
	static jrd_req* makeRequest(thread_db* tdbb, CompilerScratch* csb, bool internalFlag);

	const Routine* getRoutine() const;
	bool isActive() const;

	jrd_req* findRequest(thread_db* tdbb);
	jrd_req* getRequest(thread_db* tdbb, USHORT level);
	void verifyAccess(thread_db* tdbb);
	void release(thread_db* tdbb);

private:
	static void verifyTriggerAccess(thread_db* tdbb, jrd_rel* ownerRelation, trig_vec* triggers,
		jrd_rel* view);
	static void triggersExternalAccess(thread_db* tdbb, ExternalAccessList& list, trig_vec* tvec);

	void buildExternalAccess(thread_db* tdbb, ExternalAccessList& list);

public:
	MemoryPool* pool;
	unsigned flags;						// statement flags
	ULONG impureSize;					// Size of impure area
	Firebird::Array<record_param> rpbsSetup;
	Firebird::Array<jrd_req*> requests;	// vector of requests
	ExternalAccessList externalList;	// Access to procedures/triggers to be checked
	AccessItemList accessList;			// Access items to be checked
	ResourceList resources;				// Resources (relations and indices)
	const jrd_prc* procedure;			// procedure, if any
	const Function* function;			// function, if any
	Firebird::MetaName triggerName;		// name of request (trigger), if any
	const DmlNode* topNode;				// top of execution tree
	Firebird::Array<const RecordSource*> fors;	// record sources
	Firebird::Array<ULONG*> invariants;	// pointer to nodes invariant offsets
	Firebird::RefStrPtr sqlText;		// SQL text (encoded in the metadata charset)
	Firebird::Array<UCHAR> blr;			// BLR for non-SQL query
	MapFieldInfo mapFieldInfo;			// Map field name to field info
	MapItemInfo mapItemInfo;			// Map item to item info

public:
	virtual int FB_CARG release();
	virtual void FB_CARG receive(IStatus* status, int level, unsigned int msg_type,
						 unsigned int length, unsigned char* message);
	virtual void FB_CARG send(IStatus* status, int level, unsigned int msg_type,
					  unsigned int length, const unsigned char* message);
	virtual void FB_CARG getInfo(IStatus* status, int level,
						 unsigned int itemsLength, const unsigned char* items,
						 unsigned int bufferLength, unsigned char* buffer);
	virtual void FB_CARG start(IStatus* status, Firebird::ITransaction* tra, int level);
	virtual void FB_CARG startAndSend(IStatus* status, Firebird::ITransaction* tra, int level, unsigned int msg_type,
							  unsigned int length, const unsigned char* message);
	virtual void FB_CARG unwind(IStatus* status, int level);
	virtual void FB_CARG free(IStatus* status);
};


} // namespace Jrd

#endif // JRD_STATEMENT_H
