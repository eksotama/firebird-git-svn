/*
 *	PROGRAM:	Client/Server Common Code
 *	MODULE:		alloc.h
 *	DESCRIPTION:	Memory Pool Manager (based on B+ tree)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * You may obtain a copy of the Licence at
 * http://www.gnu.org/licences/lgpl.html
 * 
 * As a special exception this file can also be included in modules
 * with other source code as long as that source code has been 
 * released under an Open Source Initiative certificed licence.  
 * More information about OSI certification can be found at: 
 * http://www.opensource.org 
 * 
 * This module is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public Licence for more details.
 * 
 * This module was created by members of the firebird development 
 * team.  All individual contributions remain the Copyright (C) of 
 * those individuals and all rights are reserved.  Contributors to 
 * this file are either listed below or can be obtained from a CVS 
 * history command.
 *
 *  Created by: Nickolay Samofatov <skidder@bssys.com>
 *             
 *  STL allocator is based on one by Mike Nordell and John Bellardo
 *
 *  Contributor(s):
 * 
 *
 *  $Id: alloc.h,v 1.38 2004-03-25 23:12:39 skidder Exp $
 *
 */

#ifndef CLASSES_ALLOC_H
#define CLASSES_ALLOC_H

#include <cstddef>

#include "../../include/fb_types.h"
#include "../../include/firebird.h"
#include "../jrd/ib_stdio.h"
#include "../jrd/common.h"
#include "../common/classes/fb_atomic.h"
#include "../common/classes/tree.h"
#include "../common/classes/locks.h"
#ifdef HAVE_STDLIB_H
#include <stdlib.h> /* XPG: prototypes for malloc/free have to be in
					   stdlib.h (EKU) */
#endif

#ifdef _MSC_VER
#define THROW_BAD_ALLOC
#else
#define THROW_BAD_ALLOC throw (std::bad_alloc)
#endif


namespace Firebird {

// Maximum number of B+ tree pages kept spare for tree allocation
// Tree pages are allocated only from this pool thus if level of tree gets higher
// it will cause bad (but not fatal, I hope) consequences. 100^4 free blocks in free list is a lot
const int MAX_TREE_DEPTH = 4;

// Alignment for all memory blocks. Sizes of memory blocks in headers are measured in this units
const size_t ALLOC_ALIGNMENT = ALIGNMENT;

static inline size_t MEM_ALIGN(size_t value) {
	return FB_ALIGN(value, ALLOC_ALIGNMENT);
}

// Flags for memory block
const USHORT MBK_LARGE = 1; // Block is large, allocated from OS directly
const USHORT MBK_PARENT = 2; // Block is allocated from parent pool
const USHORT MBK_USED = 4; // Block is used
const USHORT MBK_LAST = 8; // Block is last in the extent

// Block header.
// Has size of 12 bytes for 32-bit targets and 16 bytes on 64-bit ones
struct MemoryBlock {
	class MemoryPool* mbk_pool;
	USHORT mbk_flags;
	SSHORT mbk_type;
	union {
		struct {
		  // Length and offset are measured in bytes thus memory extent size is limited to 64k
		  // Larger extents are not needed now, but this may be icreased later via using allocation units 
		  USHORT mbk_length; // Actual block size: header not included, redirection list is included if applicable
		  USHORT mbk_prev_length;
		} small;
		// Measured in bytes
		ULONG mbk_large_length;
	};
#ifdef DEBUG_GDS_ALLOC
	const char* mbk_file;
	int mbk_line;
#endif
};

// This structure is appended to the end of block redirected to parent pool or operating system
// It is a doubly-linked list which we are going to use when our pool is going to be deleted
struct MemoryRedirectList {
	MemoryBlock* mrl_prev;
	MemoryBlock* mrl_next;
};

const SSHORT TYPE_POOL = -1;
const SSHORT TYPE_EXTENT = -2;
const SSHORT TYPE_LEAFPAGE = -3;
const SSHORT TYPE_TREEPAGE = -4;

// We store BlkInfo structures instead of BlkHeader pointers to get benefits from 
// processor cache-hit optimizations
struct BlockInfo {
	MemoryBlock* block;
	size_t length;
	inline static bool greaterThan(const BlockInfo& i1, const BlockInfo& i2) {
		return (i1.length > i2.length) || 
			(i1.length == i2.length && i1.block > i2.block);
	}
};

struct MemoryExtent {
	MemoryExtent *mxt_next;
	MemoryExtent *mxt_prev;
};

struct PendingFreeBlock {
	PendingFreeBlock *next;
};

class MemoryStats {
public:
	MemoryStats() : mst_usage(0), mst_mapped(0), mst_max_usage(0), mst_max_mapped(0) {}
	~MemoryStats() {}
	size_t get_current_usage() const { return mst_usage.value(); }
	size_t get_maximum_usage() const { return mst_max_usage; }
	size_t get_current_mapping() const { return mst_mapped.value(); }
	size_t get_maximum_mapping() const { return mst_max_mapped; }
private:
	// Forbid copy constructor
	MemoryStats(const MemoryStats& object) {}

	// Currently allocated memory (without allocator overhead)
	// Useful for monitoring engine memory leaks
	AtomicCounter mst_usage;
	// Amount of memory mapped (including all overheads)
	// Useful for monitoring OS memory consumption
	AtomicCounter mst_mapped;
	
	// We don't particularily care about extreme precision of these max values,
	// this is why we don't synchronize them on Windows
	size_t mst_max_usage;
	size_t mst_max_mapped;
	
	friend class MemoryPool;	
};

// Memory pool based on B+ tree of free memory blocks

// We are going to have two target architectures:
// 1. Multi-process server with customizable lock manager
// 2. Multi-threaded server with single process (SUPERSERVER)
//
// MemoryPool inheritance looks weird because we cannot use
// any pointers to functions in shared memory. VMT usage in
// MemoryPool and its descendants is prohibited
class MemoryPool {
private:
	class InternalAllocator {
	public:
		void* allocate(size_t size) {
			return ((MemoryPool*)this)->tree_alloc(size);
		}
		void deallocate(void* block) {
			((MemoryPool*)this)->tree_free(block);
		}
	};
	typedef BePlusTree<BlockInfo, BlockInfo, InternalAllocator, 
		DefaultKeyValue<BlockInfo>, BlockInfo> FreeBlocksTree;
	
	// We keep most of our structures uninitialized as long we redirect 
	// our allocations to parent pool
	bool parent_redirect;

	// B+ tree ordered by (length,address). 
	FreeBlocksTree freeBlocks;

	MemoryExtent *extents; // Linked list of all memory extents

	Vector<void*, 2> spareLeafs;
	Vector<void*, MAX_TREE_DEPTH + 1> spareNodes;
	bool needSpare;
	PendingFreeBlock *pendingFree;

    // Synchronization of this object is a little bit tricky. Allocations 
	// redirected to parent pool are not protected with our mutex and not 
	// accounted locally, i.e. redirect_amount and parent_redirected linked list
	// are synchronized with parent pool mutex only. All other pool members are 
	// synchronized with this mutex.
	Mutex lock;
	
	// Current usage counters for pool. Used to move pool to different statistics group
	// Note that both counters are used only for blocks not redirected to parent.
	size_t mapped_memory;
	size_t used_memory;
	
	MemoryPool *parent; // Parent pool. Used to redirect small allocations there
	MemoryBlock *parent_redirected, *os_redirected;
	size_t redirect_amount; // Amount of memory redirected to parent
							// It is protected by parent pool mutex along with redirect list
	// Statistics group for the pool
	MemoryStats *stats;

	/* Returns NULL in case it cannot allocate requested chunk */
	static void* external_alloc(size_t &size);

	static void external_free(void* blk, size_t &size);
	
	void* tree_alloc(size_t size);

	void tree_free(void* block);

	void updateSpare();
	
	inline void addFreeBlock(MemoryBlock* blk);
		
	void removeFreeBlock(MemoryBlock* blk);
	
	void free_blk_extent(MemoryBlock* blk);
	
	// Allocates small block from this pool. Pool must be locked during call
	void* internal_alloc(size_t size, SSHORT type = 0
#ifdef DEBUG_GDS_ALLOC
		, const char* file = NULL, int line = 0
#endif
	);

	// Deallocates small block from this pool. Pool must be locked during this call
	void internal_deallocate(void* block);
	
	// Forbid copy constructor, should never be called
	MemoryPool(const MemoryPool& pool) : freeBlocks((InternalAllocator*)this) { }
	
	// Used by pools to track memory usage
	inline void increment_usage(size_t size);
	inline void decrement_usage(size_t size);
	inline void increment_mapping(size_t size);
	inline void decrement_mapping(size_t size);
	
protected:
	// Do not allow to create and destroy pool directly from outside
	MemoryPool(MemoryPool* _parent, MemoryStats &_stats, void* first_extent, void* root_page);

	// This should never be called
	~MemoryPool() {
	}
	
	// Used to create MemoryPool descendants
	static MemoryPool* internal_create(size_t instance_size, 
		MemoryPool* parent = NULL, MemoryStats &stats = default_stats_group);
	
public:
	// Default statistics group for process
	static MemoryStats default_stats_group;

	// Pool created for process
	static MemoryPool* processMemoryPool;
	
	// Create memory pool instance
	static MemoryPool* createPool(MemoryPool* parent = NULL, MemoryStats &stats = default_stats_group) {
		return internal_create(sizeof(MemoryPool), parent, stats);
	}
	
	// Set context pool for current thread of execution
	static MemoryPool* setContextPool(MemoryPool *newPool);
	
	// Get context pool for current thread of execution
	static MemoryPool* getContextPool();
	
	// Set statistics group for pool. Usage counters will be decremented from 
	// previously set group and added to new
	void setStatsGroup(MemoryStats &stats);

	// Deallocate pool and all its contents
	static void deletePool(MemoryPool* pool);

	// Allocate memory block. Result is not zero-initialized.
	// It case of problems this method throws std::bad_alloc
	void* allocate(size_t size, SSHORT type = 0
#ifdef DEBUG_GDS_ALLOC
		, const char* file = NULL, int line = 0
#endif
	);

	// Allocate memory block. In case of problems this method returns NULL
	void* allocate_nothrow(size_t size, SSHORT type = 0
#ifdef DEBUG_GDS_ALLOC
		, const char* file = NULL, int line = 0
#endif
	);
	
	void deallocate(void* block);
	
	// Check pool for internal consistent. When enabled, call is very expensive
	bool verify_pool();

	// Print out pool contents. This is debugging routine
	void print_contents(IB_FILE*, bool = false);
	
	// Deallocate memory block. Pool is derived from block header
	static void globalFree(void* block) {
	    if (block)
		  ((MemoryBlock*)((char*)block - MEM_ALIGN(sizeof(MemoryBlock))))->mbk_pool->deallocate(block);
	}
	
	// Allocate zero-initialized block of memory
	void* calloc(size_t size, SSHORT type = 0
#ifdef DEBUG_GDS_ALLOC
		, const char* file = NULL, int line = 0
#endif
	) {
		void* result = allocate(size, type
#ifdef DEBUG_GDS_ALLOC
			, file, line
#endif
		);
		memset(result, 0, size);
		return result;	
	}

	/// Returns the type associated with the allocated memory.
	static SSHORT blk_type(const void* mem) {
		return ((MemoryBlock*)((char *)mem - MEM_ALIGN(sizeof(MemoryBlock))))->mbk_type;
	}
	
	/// Returns the pool the memory was allocated from.
	static MemoryPool* blk_pool(const void* mem) {
		return ((MemoryBlock*)((char *)mem - MEM_ALIGN(sizeof(MemoryBlock))))->mbk_pool;
	}
	
	friend class InternalAllocator;
};

// Class intended to manage execution context pool stack
// Declare instance of this class when you need to set new context pool and it 
// will be restored automatically as soon holder variable gets out of scope
class ContextPoolHolder {
public:
	ContextPoolHolder(MemoryPool* newPool) {
		savedPool = MemoryPool::setContextPool(newPool);
	}
	~ContextPoolHolder() {
		MemoryPool::setContextPool(savedPool);
	}
private:
	MemoryPool* savedPool;	
};

}; // namespace Firebird

using Firebird::MemoryPool;

inline static MemoryPool* getDefaultMemoryPool() { return Firebird::MemoryPool::processMemoryPool; }

MemoryPool* getContextMemoryPool();

// Global versions of operator new()
// Implemented in alloc.cpp
void* operator new(size_t) THROW_BAD_ALLOC;
void* operator new[](size_t) THROW_BAD_ALLOC;

// We cannot use inline versions because we have to replace STL delete defined in <new> header
void operator delete(void* mem) throw();
void operator delete[](void* mem) throw();

#ifdef DEBUG_GDS_ALLOC
static inline void* operator new(size_t s, Firebird::MemoryPool& pool, const char* file, int line) {
	return pool.allocate(s, 0, file, line);
}
static inline void* operator new[](size_t s, Firebird::MemoryPool& pool, const char* file, int line) {
	return pool.allocate(s, 0, file, line);
}
#define FB_NEW(pool) new(pool,__FILE__,__LINE__)
#define FB_NEW_RPT(pool,count) new(pool,count,__FILE__,__LINE__)
#else
static inline void* operator new(size_t s, Firebird::MemoryPool& pool) {
	return pool.allocate(s);
}
static inline void* operator new[](size_t s, Firebird::MemoryPool& pool) {
	return pool.allocate(s);
}
#define FB_NEW(pool) new(pool)
#define FB_NEW_RPT(pool,count) new(pool,count)
#endif

#ifndef TESTING_ONLY

/**
	This is the allocator template provided to be used with the STL.
	Since the STL is the client of this class look to its documentation
	to determine what the individual functions and typedefs do.

	In order to use the allocator class you need to instanciate the
	C++ container template with the allocator.  For example if you
	want to use a std::vector<int> the declaration would be:

	std::vector<int, MemoryPool::allocator<int> >

	The allocator, by default, allocates all memory from the process
	wide pool FB_MemoryPool.  Typically this is NOT the behavior you
	want.  Selection of the correct pool to allocate your memory from is
	important.  If you select a pool to far down in (a statement pool,
	for example) you memory may be freed before you are done with it.
	On the other hand if you always use the global pool you will
	either leak memory or have to make sure you always delete the objects
	you create.

	If you decide to allocate the memory from a pool other than the global
	pool you need to pass an allocator object to the constructor for
	the STL object.  For example:

	std::vector<int, MemoryPool::allocator<int> > vec(MemoryPool::allocator<int>(poolRef, type));
	The type is an optional parameter that defaults to 0.
**/
namespace Firebird
{
	template <class T>
	class allocator
	{
		public:
		typedef size_t		size_type;
		typedef ptrdiff_t	difference_type;
		typedef T*			pointer;
		typedef const T*	const_pointer;
		typedef T&			reference;
		typedef const T&	const_reference;
		typedef T			value_type;
	
		allocator(MemoryPool& p, SSHORT t = 0) : pool(&p), type(t) {}
		allocator(MemoryPool* p = getDefaultMemoryPool(), SSHORT t = 0) : pool(p), type(t) {}
	
		template <class DST>
		allocator(const allocator<DST> &alloc)
			: pool(alloc.getPool()), type(alloc.getType()) { }

#ifdef DEBUG_GDS_ALLOC
		pointer allocate(size_type s, const void* = 0)
			{ return (pointer) pool->allocate(sizeof(T) * s, 0, __FILE__, __LINE__); }
		char* _Charalloc(size_type n)
			{ return (char*) pool->allocate(n, 0, __FILE__, __LINE__); }
#else
		pointer allocate(size_type s, const void* = 0)
			{ return (pointer) pool->allocate(sizeof(T) * s, 0); }
		char* _Charalloc(size_type n)
			{ return (char*) pool->allocate(n, 0); }
#endif
			
		void deallocate(pointer p, size_type s)	{ pool->deallocate(p); }
		void deallocate(void* p, size_type s) { pool->deallocate(p); }
		void construct(pointer p, const T& v) { new(p) T(v); }
		void destroy(pointer p) { p->~T(); }
	
		size_type max_size() const { return (size_type) - 1 / sizeof(T); }
	
		pointer address(reference X) const { return &X; }
		const_pointer address(const_reference X) const { return &X; }
	
		template <class _Tp1> struct rebind {
			typedef Firebird::allocator<_Tp1> other;
		};

		bool operator==(const allocator<T>& rhs) const
		{
			return pool == rhs.pool && type == rhs.type;
		}

		MemoryPool* getPool() const { return pool; }
		SSHORT getType() const { return type; }

	private:
		MemoryPool* pool;
		SSHORT type;
	};

	class PermanentStorage {
	private:
		MemoryPool& pool;
	protected:
		explicit PermanentStorage(MemoryPool& p) : pool(p) { }
		MemoryPool& getPool() const { return pool; }
	};

	class AutoStorage : public PermanentStorage {
	public:
		static MemoryPool& getAutoMemoryPool() { return *getDefaultMemoryPool(); }
	protected:
		AutoStorage() : PermanentStorage(getAutoMemoryPool()) {
#ifdef DEV_BUILD
			//
			// AutoStorage can be used only for objects on the stack.
			// What is this check based on:
			//	1. One and only one stack is used for all kind of variables.
			//	2. Objects don't grow > 64K.
			//
			char ProbeVar = '\0';
			char *MyStack = &ProbeVar;
			char *ThisLocation = (char *)this;
			int distance = ThisLocation - MyStack;
			if (distance < 0) {
				distance = -distance;
			}
			// Cannot use fb_assert in this header
			fb_assert(distance < 64 * 1024);
#endif
		}
		explicit AutoStorage(MemoryPool& p) : PermanentStorage(p) { }
	};
	
};

#endif /*TESTING_ONLY*/

#endif // CLASSES_ALLOC_H

