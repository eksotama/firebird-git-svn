/*
 *	PROGRAM:	Client/Server Common Code
 *	MODULE:		class_perf.cpp
 *	DESCRIPTION:	Class library performance measurements
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
 * Created by: Nickolay Samofatov <skidder@bssys.com>
 *
 * All Rights Reserved.
 * Contributor(s): ______________________________________.
 */

#include "tree.h"
#include "alloc.h"
#include "../memory/memory_pool.h"
#include <stdio.h>
#include <time.h>
#include <set>

clock_t t;

void start() {
	t = clock();
}

#define TEST_ITEMS 1000000

void report(int scale) {
	clock_t d = clock();
	printf("Add+remove %d elements from tree of scale %d took %d milliseconds. \n", 
		TEST_ITEMS,	scale, (int)(d-t)*1000/CLOCKS_PER_SEC);
}

using namespace Firebird;

static void testTree() {
	printf("Fill array with test data (%d items)...", TEST_ITEMS);
	Vector<int, TEST_ITEMS> *v = new Vector<int, TEST_ITEMS>();
	int n = 0;
	int i;
	for (i=0;i<TEST_ITEMS;i++) {
		n = n * 45578 - 17651;
		// Fill it with quasi-random values in range 0...TEST_ITEMS-1
		v->add(((i+n) % TEST_ITEMS + TEST_ITEMS)/2);
	}
	printf(" DONE\n");
	MallocAllocator temp;
	
	start();
	BePlusTree<int, int, MallocAllocator, DefaultKeyValue<int>, 
		DefaultComparator<int>, 10, 10> tree10(NULL);
	for (i=0; i<TEST_ITEMS;i++)
		tree10.add((*v)[i]);
	for (i=0; i<TEST_ITEMS;i++) {
		if (tree10.locate((*v)[i]))
			tree10.fastRemove();
	}
	report(10);

	start();
	BePlusTree<int, int, MallocAllocator, DefaultKeyValue<int>, 
		DefaultComparator<int>, 50, 50> tree50(NULL);
	for (i=0; i<TEST_ITEMS;i++)
		tree50.add((*v)[i]);
	for (i=0; i<TEST_ITEMS;i++) {
		if (tree50.locate((*v)[i]))
			tree50.fastRemove();
	}
	report(50);
	
	start();
	BePlusTree<int, int, MallocAllocator, DefaultKeyValue<int>, 
		DefaultComparator<int>, 75, 75> tree75(NULL);
	for (i=0; i<TEST_ITEMS;i++)
		tree75.add((*v)[i]);
	for (i=0; i<TEST_ITEMS;i++) {
		if (tree75.locate((*v)[i]))
			tree75.fastRemove();
	}
	report(75);
	
	start();
	BePlusTree<int, int, MallocAllocator, DefaultKeyValue<int>, 
		DefaultComparator<int>, 100, 100> tree100(NULL);
	for (i=0; i<TEST_ITEMS;i++)
		tree100.add((*v)[i]);
	for (i=0; i<TEST_ITEMS;i++) {
		if (tree100.locate((*v)[i]))
			tree100.fastRemove();
	}
	report(100);
	
	start();
	BePlusTree<int, int, MallocAllocator, DefaultKeyValue<int>, 
		DefaultComparator<int>, 200, 200> tree200(NULL);
	for (i=0; i<TEST_ITEMS;i++)
		tree200.add((*v)[i]);
	for (i=0; i<TEST_ITEMS;i++) {
		if (tree200.locate((*v)[i]))
			tree200.fastRemove();
	}
	report(200);
	
	start();
	BePlusTree<int, int, MallocAllocator, DefaultKeyValue<int>, 
		DefaultComparator<int>, 250, 250> tree250(NULL);
	for (i=0; i<TEST_ITEMS;i++)
		tree250.add((*v)[i]);
	for (i=0; i<TEST_ITEMS;i++) {
		if (tree250.locate((*v)[i]))
			tree250.fastRemove();
	}
	report(250);
	
	start();
	BePlusTree<int, int, MallocAllocator, DefaultKeyValue<int>, 
		DefaultComparator<int>, 500, 500> tree500(NULL);
	for (i=0; i<TEST_ITEMS;i++)
		tree500.add((*v)[i]);
	for (i=0; i<TEST_ITEMS;i++) {
		if (tree500.locate((*v)[i]))
			tree500.fastRemove();
	}
	report(500);
	
	std::set<int> stlTree;
	start();
	for (i=0; i<TEST_ITEMS;i++)
		stlTree.insert((*v)[i]);
	for (i=0; i<TEST_ITEMS;i++)
		stlTree.erase((*v)[i]);
	clock_t d = clock();
	printf("Just a reference: add+remove %d elements from STL tree took %d milliseconds. \n", 
		TEST_ITEMS,	(int)(d-t)*1000/CLOCKS_PER_SEC);
}


void report() {
	clock_t d = clock();
	printf("Operation took %d milliseconds.\n", (int)(d-t)*1000/CLOCKS_PER_SEC);
}

#define ALLOC_ITEMS 1000000
#define MAX_ITEM_SIZE 50
#define BIG_ITEMS (ALLOC_ITEMS/10)
#define BIG_SIZE (MAX_ITEM_SIZE*5)

struct AllocItem {
	int order;
	void *item;
	static int compare(const AllocItem &i1, const AllocItem &i2) {
		return i1.order > i2.order || (i1.order==i2.order && i1.item > i2.item);
	}
};

static void testAllocatorOverhead() {
	printf("Calculating measurement overhead...\n");
	start();
	MallocAllocator allocator;
	BePlusTree<AllocItem,AllocItem,MallocAllocator,DefaultKeyValue<AllocItem>,AllocItem> items(&allocator),
		bigItems(&allocator);
	// Allocate small items
	int n = 0;
	int i;
	for (i=0;i<ALLOC_ITEMS;i++) {
		n = n * 47163 - 57412;
		AllocItem temp = {n, (void*)i};
		items.add(temp);
	}
	// Deallocate half of small items
	n = 0;
	if (items.getFirst()) do {
		items.current();
		n++;
	} while (n < ALLOC_ITEMS/2 && items.getNext());	
	// Allocate big items
	for (i=0;i<BIG_ITEMS;i++) {
		n = n * 47163 - 57412;
		AllocItem temp = {n, (void*)i};
		bigItems.add(temp);
	}
	// Deallocate the rest of small items
	do {
		items.current();
	} while (items.getNext());
	// Deallocate big items
	if (bigItems.getFirst()) do {
		bigItems.current();
	} while (bigItems.getNext());
	report();
}
	
static void testAllocatorMemoryPool() {
	printf("Test run for Firebird::MemoryPool...\n");
	start();
	Firebird::MemoryPool* pool = Firebird::MemoryPool::createPool();	
	MallocAllocator allocator;
	BePlusTree<AllocItem,AllocItem,MallocAllocator,DefaultKeyValue<AllocItem>,AllocItem> items(&allocator),
		bigItems(&allocator);
	// Allocate small items
	int i, n = 0;	
	for (i=0;i<ALLOC_ITEMS;i++) {
		n = n * 47163 - 57412;
		AllocItem temp = {n, pool->alloc((n % MAX_ITEM_SIZE + MAX_ITEM_SIZE)/2+1)};
		items.add(temp);
	}
	// Deallocate half of small items
	n = 0;
	if (items.getFirst()) do {
		pool->free(items.current().item);
		n++;
	} while (n < ALLOC_ITEMS/2 && items.getNext());	
	// Allocate big items
	for (i=0;i<BIG_ITEMS;i++) {
		n = n * 47163 - 57412;
		AllocItem temp = {n, pool->alloc((n % BIG_SIZE + BIG_SIZE)/2+1)};
		bigItems.add(temp);
	}
	// Deallocate the rest of small items
	do {
		pool->free(items.current().item);
	} while (items.getNext());
	// Deallocate big items
	if (bigItems.getFirst()) do {
		pool->free(bigItems.current().item);
	} while (bigItems.getNext());
	Firebird::MemoryPool::deletePool(pool);
	report();
}

static void testAllocatorMemoryPoolLocking() {
	printf("Test run for Firebird::MemoryPool with locking...\n");
	start();
	Firebird::MemoryPool* pool = Firebird::MemoryPool::createPool(true);	
	MallocAllocator allocator;
	BePlusTree<AllocItem,AllocItem,MallocAllocator,DefaultKeyValue<AllocItem>,AllocItem> items(&allocator),
		bigItems(&allocator);
	// Allocate small items
	int i, n = 0;	
	for (i=0;i<ALLOC_ITEMS;i++) {
		n = n * 47163 - 57412;
		AllocItem temp = {n, pool->alloc((n % MAX_ITEM_SIZE + MAX_ITEM_SIZE)/2+1)};
		items.add(temp);
	}
	// Deallocate half of small items
	n = 0;
	if (items.getFirst()) do {
		pool->free(items.current().item);
		n++;
	} while (n < ALLOC_ITEMS/2 && items.getNext());	
	// Allocate big items
	for (i=0;i<BIG_ITEMS;i++) {
		n = n * 47163 - 57412;
		AllocItem temp = {n, pool->alloc((n % BIG_SIZE + BIG_SIZE)/2+1)};
		bigItems.add(temp);
	}
	// Deallocate the rest of small items
	do {
		pool->free(items.current().item);
	} while (items.getNext());
	// Deallocate big items
	if (bigItems.getFirst()) do {
		pool->free(bigItems.current().item);
	} while (bigItems.getNext());
	Firebird::MemoryPool::deletePool(pool);
	report();
}

static void testAllocatorMalloc() {
	printf("Test reference run for ::malloc...\n");
	start();
	MallocAllocator allocator;
	BePlusTree<AllocItem,AllocItem,MallocAllocator,DefaultKeyValue<AllocItem>,AllocItem> items(&allocator),
		bigItems(&allocator);
	// Allocate small items
	int i, n = 0;
	for (i=0;i<ALLOC_ITEMS;i++) {
		n = n * 47163 - 57412;
		AllocItem temp = {n, malloc((n % MAX_ITEM_SIZE + MAX_ITEM_SIZE)/2+1)};
		items.add(temp);
	}
	// Deallocate half of small items
	n = 0;
	if (items.getFirst()) do {
		free(items.current().item);
		n++;
	} while (n < ALLOC_ITEMS/2 && items.getNext());	
	// Allocate big items
	for (i=0;i<BIG_ITEMS;i++) {
		n = n * 47163 - 57412;
		AllocItem temp = {n, malloc((n % BIG_SIZE + BIG_SIZE)/2+1)};
		bigItems.add(temp);
	}
	// Deallocate the rest of small items
	do {
		free(items.current().item);
	} while (items.getNext());
	// Deallocate big items
	if (bigItems.getFirst()) do {
		free(bigItems.current().item);
	} while (bigItems.getNext());
	report();
}

static void testAllocatorOldPool() {
	printf("Test run for old MemoryPool...\n");
	start();
	::MemoryPool *pool = new ::MemoryPool(0,getDefaultMemoryPool());
	MallocAllocator allocator;
	BePlusTree<AllocItem,AllocItem,MallocAllocator,DefaultKeyValue<AllocItem>,AllocItem> items(&allocator),
		bigItems(&allocator);
	// Allocate small items
	int n = 0;
	int i;
	for (i=0;i<ALLOC_ITEMS;i++) {
		n = n * 47163 - 57412;
		AllocItem temp = {n, pool->allocate((n % MAX_ITEM_SIZE + MAX_ITEM_SIZE)/2+1,0)};
		items.add(temp);
	}
	// Deallocate half of small items
	n = 0;
	if (items.getFirst()) do {
		pool->deallocate(items.current().item);
		n++;
	} while (n < ALLOC_ITEMS/2 && items.getNext());	
	// Allocate big items
	for (i=0;i<BIG_ITEMS;i++) {
		n = n * 47163 - 57412;
		AllocItem temp = {n, pool->allocate((n % BIG_SIZE + BIG_SIZE)/2+1,0)};
		bigItems.add(temp);
	}
/*	// Deallocate the rest of small items
	do {
		pool->deallocate(items.current().item);
	} while (items.getNext());*/
	// Deallocate big items
	if (bigItems.getFirst()) do {
		pool->deallocate(bigItems.current().item);
	} while (bigItems.getNext());
	delete pool;
	report();
}

int main() {
	testTree();
	testAllocatorOverhead();
	testAllocatorMemoryPool();
	testAllocatorMemoryPoolLocking();
	testAllocatorMalloc();
	testAllocatorOldPool();
}
