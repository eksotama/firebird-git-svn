/*
 *	PROGRAM:	Alice
 *	MODULE:		all.h
 *	DESCRIPTION:	Data allocation declarations
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
 */

#ifndef ALICE_ALL_H
#define ALICE_ALL_H

#include "../jrd/block_cache.h"
#include "../alice/lls.h"

struct blk;

class AliceMemoryPool : public MemoryPool
{
protected:
	// Dummy constructor and destructor. Should never be called
	AliceMemoryPool() : MemoryPool(NULL, default_stats_group, NULL, NULL)/*, lls_cache(*this)*/ {}
	~AliceMemoryPool() {}	
public:
	static AliceMemoryPool *createPool() {
		AliceMemoryPool *result = (AliceMemoryPool *)internal_create(sizeof(AliceMemoryPool));
		//new (&result->lls_cache) BlockCache<alice_lls> (*result);
		return result;
	}
	static void deletePool(AliceMemoryPool* pool);
//	static AliceMemoryPool *create_new_pool(MemoryPool* = 0);
//	AliceMemoryPool(MemoryPool* p = 0)
//	:	MemoryPool(0, p),
//		lls_cache(*this)
//	{}

//	static blk* ALLA_pop(alice_lls**);
//	static void ALLA_push(blk*, alice_lls**);

//private:
//	BlockCache<alice_lls> lls_cache;  // Was plb_lls
};

#endif // ALICE_ALL_H

