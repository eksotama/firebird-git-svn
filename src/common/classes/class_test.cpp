/*
 *	PROGRAM:	Client/Server Common Code
 *	MODULE:		class_test.cpp
 *	DESCRIPTION:	Class library integrity tests
 *
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
 *  The Original Code was created by Nickolay Samofatov
 *  for the Firebird Open Source RDBMS project.
 *
 *  Copyright (c) 2004 Nickolay Samofatov <nickolay@broadviewsoftware.com>
 *  and all contributors signed below.
 *
 *  All Rights Reserved.
 *  Contributor(s): ______________________________________.
 * 
 *
 *  $Id: class_test.cpp,v 1.18 2004-06-30 01:26:06 skidder Exp $
 *
 */

#include "../../include/firebird.h"
#include "tree.h"
#include "alloc.h"
#include <stdio.h>

using namespace Firebird;

void testVector() {
	printf("Test Firebird::Vector: ");
	Vector<int, 100> v;
	int i;
	for (i = 0; i < 100; i++)
		v.add(i);
	for (i = 0; i < 50; i++)
		v.remove(0);
	bool passed = true;
	for (i = 50; i < 100; i++)
		if (v[i - 50] != i)
			passed = false;
	printf(passed ? "PASSED\n" : "FAILED\n");
}

void testSortedVector() {
	printf("Test Firebird::SortedVector: ");
	SortedVector<int, 100> v;
	int i;
	for (i = 0; i < 100; i++)
		v.add(99 - i);
	for (i = 0; i < 50; i++)
		v.remove(0);
	bool passed = true;
	for (i = 50;i < 100; i++)
		if (v[i - 50] != i)
			passed = false;
	printf(passed ? "PASSED\n" : "FAILED\n");
}

const int TEST_ITEMS = 1000000;

struct Test {
	int value;
	int count;
	static const int& generate(const void *sender, const Test& value) {
		return value.value;
	}
};

void testBePlusTree() {
    MallocAllocator temp;
    printf("Test Firebird::BePlusTree\n");
	
    printf("Fill array with test data (%d items)...", TEST_ITEMS);
	Vector<int, TEST_ITEMS> v;
	int n = 0;
	int i;
	for (i = 0; i < TEST_ITEMS; i++) {
		n = n * 45578 - 17651;
		// Fill it with quasi-random values in range 0...TEST_ITEMS-1
		v.add(((i + n) % TEST_ITEMS + TEST_ITEMS) / 2);
	}
	printf(" DONE\n");
	
	printf("Create two trees one with factor 2 and one with factor 13 and fill them with test data: ");
	BePlusTree<Test, int, MallocAllocator, Test, 
		DefaultComparator<int>, 2, 2> tree1(&temp);
	BePlusTree<Test, int, MallocAllocator, Test, 
		DefaultComparator<int>, 13, 13> tree2(&temp);
	int cnt1 = 0, cnt2 = 0;
	for (i = 0; i < v.getCount(); i++) {
		if (tree1.locate(locEqual, v[i]))
			tree1.current().count++;
		else {
			Test t;
			t.value = v[i];
			t.count = 1;
			if (!tree1.add(t))
				fb_assert(false);
			cnt1++;
		}
		if (tree2.locate(locEqual, v[i]))
			tree2.current().count++;
		else {
			Test t;
			t.value = v[i];
			t.count = 1;
			if (!tree2.add(t))
				fb_assert(false);
			cnt2++;
		}	
	}
	printf(" DONE\n");
	
	bool passed = true;

	printf("Empty trees verifying fastRemove() result: ");
	for (i = 0; i < v.getCount()-1; i++) {
		if (!tree1.getLast())
			passed = false;
		tree1.current().count--;
		if (!tree1.current().count)
			if (tree1.fastRemove())
				passed = false;
		if (!tree2.getLast())
			passed = false;
		tree2.current().count--;
		if (!tree2.current().count)
			if (tree2.fastRemove())
				passed = false;
	}
	if (!tree1.getLast())
		passed = false;
	tree1.current().count--;
	if (tree1.current().count)
		passed = false;
	else
		if (tree1.fastRemove())
			passed = false;

	if (!tree2.getLast())
		passed = false;
	tree2.current().count--;
	if (tree2.current().count)
		passed = false;
	else
		if (tree2.fastRemove())
			passed = false;
	printf(passed ? "PASSED\n" : "FAILED\n");
	passed = true;
	
	printf("Fill trees with data again: ");
	cnt1 = 0; cnt2 = 0;
	for (i = 0; i < v.getCount(); i++) {
		if (tree1.locate(locEqual, v[i]))
			tree1.current().count++;
		else {
			Test t;
			t.value = v[i];
			t.count = 1;
			if (!tree1.add(t))
				fb_assert(false);
			cnt1++;
		}
		if (tree2.locate(locEqual, v[i]))
			tree2.current().count++;
		else {
			Test t;
			t.value = v[i];
			t.count = 1;
			if (!tree2.add(t))
				fb_assert(false);
			cnt2++;
		}	
	}
	printf(" DONE\n");
	
	printf("Check that tree(2) contains test data: ");
	for (i = 0; i < v.getCount(); i++) {
		if (!tree1.locate(locEqual, v[i]))
			passed = false;
	}
	printf(passed ? "PASSED\n" : "FAILED\n");
	passed = true;
	
	printf("Check that tree(13) contains test data: ");
	for (i = 0; i < v.getCount(); i++) {
		if(!tree2.locate(locEqual, v[i]))
			passed = false;
	}
	printf(passed ? "PASSED\n" : "FAILED\n");
	
	passed = true;
	
	printf("Check that tree(2) contains data from the tree(13) and its count is correct: ");
	n = 0;
	if (tree1.getFirst()) do {
		n++;
		if (!tree2.locate(locEqual, tree1.current().value)) 
			passed = false;
	} while (tree1.getNext());
	if (n != cnt1 || cnt1 != cnt2) 
		passed = false;
	printf(passed ? "PASSED\n" : "FAILED\n");
	
	printf("Check that tree(13) contains data from the tree(2) "\
		   "and its count is correct (check in reverse order): ");
	n = 0;
	if (tree2.getLast()) do {
		n++;
		if (!tree1.locate(locEqual, tree2.current().value))
			passed = false;
	} while (tree2.getPrev());
	if (n != cnt2)
		passed = false;
	printf(passed ? "PASSED\n" : "FAILED\n");
	
	printf("Remove half of data from the trees: ");
	passed = true;
	while (v.getCount() > TEST_ITEMS / 2) {
		if (!tree1.locate(locEqual, v[v.getCount() - 1]))
			fb_assert(false);
		if (tree1.current().count > 1) 
			tree1.current().count--;
		else {
			int nextValue = -1;
			if (tree1.getNext()) {
				nextValue = tree1.current().value;
				tree1.getPrev();
			}
			if (tree1.fastRemove()) {
				if (tree1.current().value != nextValue)
					passed = false;
			} else {
				if (nextValue >= 0)
					passed = false;
			}
			cnt1--;
		}
		if (!tree2.locate(locEqual, v[v.getCount() - 1]))
			fb_assert(false);
		if (tree2.current().count > 1) 
			tree2.current().count--;
		else {
			int nextValue = -1;
			if (tree2.getNext()) {
				nextValue = tree2.current().value;
				tree2.getPrev();
			}
			if (tree2.fastRemove()) {
				if (tree2.current().value != nextValue)
					passed = false;
			} else {
				if (nextValue >= 0)
					passed = false;
			}
			cnt2--;
		}
		v.shrink(v.getCount() - 1);
	}
	printf(passed ? "PASSED\n" : "FAILED\n");
	
	passed = true;

	printf("Check that tree(2) contains test data: ");
	for (i = 0; i < v.getCount(); i++) {
		if(!tree1.locate(locEqual, v[i]))
			passed = false;
	}	
	printf(passed ? "PASSED\n" : "FAILED\n");
	passed = true;
	
	printf("Check that tree(13) contains test data: ");
	for (i = 0; i < v.getCount(); i++) {
		if(!tree2.locate(locEqual, v[i]))
			passed = false;
	}	
	printf(passed ? "PASSED\n" : "FAILED\n");
	
	passed = true;
	
	printf("Check that tree(2) contains data from the tree(13) and its count is correct: ");
	n = 0;
	if (tree1.getFirst()) do {
		n++;
		if (!tree2.locate(locEqual, tree1.current().value))
			passed = false;
	} while (tree1.getNext());
	if (n != cnt1 || cnt1 != cnt2) 
		passed = false;
	printf(passed ? "PASSED\n" : "FAILED\n");
	
	passed = true;
	
	printf("Check that tree(13) contains data from the tree(2) "\
		   "and its count is correct (check in reverse order): ");
	n = 0;
	if (tree2.getLast()) do {
		n++;
		if (!tree1.locate(locEqual, tree2.current().value))
			passed = false;
	} while (tree2.getPrev());
	if (n != cnt2) 
		passed = false;
	printf(passed ? "PASSED\n" : "FAILED\n");
	
	passed = true;
	
	printf("Remove the rest of data from the trees: ");
	for (i = 0;i < v.getCount(); i++) {
		if (!tree1.locate(locEqual, v[i]))
			fb_assert(false);
		if (tree1.current().count > 1) 
			tree1.current().count--;
		else {
			int nextValue = -1;
			if (tree1.getNext()) {
				nextValue = tree1.current().value;
				tree1.getPrev();
			}
			if (tree1.fastRemove()) {
				if (tree1.current().value != nextValue)
					passed = false;
			} else {
				if (nextValue >= 0)
					passed = false;
			}
			cnt1--;
		}
		if (!tree2.locate(locEqual, v[i]))
			fb_assert(false);
		if (tree2.current().count > 1)
			tree2.current().count--;
		else {
			int nextValue = -1;
			if (tree2.getNext()) {
				nextValue = tree2.current().value;
				tree2.getPrev();
			}
			if (tree2.fastRemove()) {
				if (tree2.current().value != nextValue)
					passed = false;
			} else {
				if (nextValue >= 0)
					passed = false;
			}
			cnt2--;
		}
	}
	printf(passed ? "PASSED\n" : "FAILED\n");
	passed = true;

	printf("Check that both trees do not contain anything: ");
	if (tree1.getFirst())
		passed = false;
	if (tree2.getLast())
		passed = false;
	printf(passed ? "PASSED\n" : "FAILED\n");
}

const int ALLOC_ITEMS	= 1000000;
const int MAX_ITEM_SIZE	= 50;
const int BIG_ITEMS		= ALLOC_ITEMS / 10;
const int BIG_SIZE		= MAX_ITEM_SIZE * 5;

const int LARGE_ITEMS	= 10;
const size_t LARGE_ITEM_SIZE	= 300000;

struct AllocItem {
	int order;
	void *item;
	static bool greaterThan(const AllocItem &i1, const AllocItem &i2) {
		return i1.order > i2.order || (i1.order == i2.order && i1.item > i2.item);
	}
};

void testAllocator() {
	printf("Test Firebird::MemoryPool\n");
	MemoryPool* parent = getDefaultMemoryPool();
	MemoryPool* pool = MemoryPool::createPool(parent);
	
	MallocAllocator allocator;
	BePlusTree<AllocItem, AllocItem, MallocAllocator, DefaultKeyValue<AllocItem>, AllocItem> items(&allocator),
		bigItems(&allocator);
		
	Vector<void*, LARGE_ITEMS> la;
	printf("Allocate %d large items: ", LARGE_ITEMS);
	int i;
	for (i = 0; i<LARGE_ITEMS; i++)
		la.add(pool->allocate(LARGE_ITEM_SIZE));		
	pool->verify_pool();
	printf(" DONE\n");
	
	printf("Allocate %d items: ", ALLOC_ITEMS);
	int n = 0;
	pool->verify_pool();
	for (i = 0; i < ALLOC_ITEMS; i++) {
		n = n * 47163 - 57412;
		AllocItem temp = {n, pool->allocate((n % MAX_ITEM_SIZE + MAX_ITEM_SIZE) / 2 + 1)};
		items.add(temp);
	}
	printf(" DONE\n");
	pool->verify_pool();
	parent->verify_pool();
	
	printf("Deallocate half of items in quasi-random order: ");
	n = 0;
	if (items.getFirst()) do {
		pool->deallocate(items.current().item);
		n++;
	} while (n < ALLOC_ITEMS / 2 && items.getNext());
	printf(" DONE\n");
	pool->verify_pool();
	parent->verify_pool();
	
	printf("Allocate %d big items: ", BIG_ITEMS);
	n = 0;
	pool->verify_pool();
	for (i = 0; i < BIG_ITEMS; i++) {
		n = n * 47163 - 57412;
		AllocItem temp = {n, pool->allocate((n % BIG_SIZE + BIG_SIZE) / 2 + 1)};
		bigItems.add(temp);
	}
	printf(" DONE\n");
	pool->verify_pool();
	parent->verify_pool();
	
	printf("Deallocate the rest of small items in quasi-random order: ");
	while (items.getNext()) {
		pool->deallocate(items.current().item);
	}
	printf(" DONE\n");
	pool->verify_pool();
	parent->verify_pool();
	
	printf("Deallocate big items in quasi-random order: ");
	if (bigItems.getFirst()) do {
		pool->deallocate(bigItems.current().item);
	} while (bigItems.getNext());
	printf(" DONE\n");
	
	printf("Deallocate %d large items: ", LARGE_ITEMS/2);
	for (i = 0; i<LARGE_ITEMS/2; i++)
		pool->deallocate(la[i]);
	pool->verify_pool();
	printf(" DONE\n");

	
	pool->verify_pool();
	parent->verify_pool();
	pool->print_contents(stdout, false);
	parent->print_contents(stdout, false);
	MemoryPool::deletePool(pool);
	parent->verify_pool();	
//  TODO:
//	Test critically low memory conditions
//  Test that tree correctly recovers in low-memory conditions
}

int main() {
	testVector();
	testSortedVector();
	testBePlusTree();
	testAllocator();
}

