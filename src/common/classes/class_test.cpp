/*
 *	PROGRAM:	Client/Server Common Code
 *	MODULE:		class_test.cpp
 *	DESCRIPTION:	Class library integrity tests
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
#include <stdio.h>

using namespace Firebird;

void testVector() {
	printf("Test Firebird::Vector: ");
	Vector<int,100> v;
	for (int i=0;i<100;i++)
		v.add(i);
	for (int i=0;i<50;i++)
		v.remove(0);
	bool passed = true;
	for (int i=50;i<100;i++)
		if (v[i-50] != i) passed = false;
	printf(passed?"PASSED\n":"FAILED\n");
}

void testSortedVector() {
	printf("Test Firebird::SortedVector: ");
	SortedVector<int,100> v;
	for (int i=0;i<100;i++)
		v.add(99-i);
	for (int i=0;i<50;i++)
		v.remove(0);
	bool passed = true;
	for (int i=50;i<100;i++)
		if (v[i-50] != i) passed = false;
	printf(passed?"PASSED\n":"FAILED\n");
}

#define TEST_ITEMS 100000

struct Test {
	int value;
	int count;
	static const int& generate(void *sender, const Test& value) {
		return value.value;
	}
};

void testBePlusTree() {
	MallocAllocator temp;
	printf("Test Firebird::BePlusTree\n");
	
	printf("Fill array with test data (%d items)...", TEST_ITEMS);
	Vector<int, TEST_ITEMS> v;
	int n = 0;
	for (int i=0;i<TEST_ITEMS;i++) {
		n = n * 45578 - 17651;
		// Fill it with quasi-random values in range 0...TEST_ITEMS-1
		v.add(((i+n) % TEST_ITEMS + TEST_ITEMS)/2);
	}
	printf(" DONE\n");
	
	printf("Create two trees one with factor 2 and one with factor 13 and fill them with test data: ");
	BePlusTree<Test, int, MallocAllocator, Test, 
		DefaultComparator<int>, 2, 2> tree1(&temp);
	BePlusTree<Test, int, MallocAllocator, Test, 
		DefaultComparator<int>, 13, 13> tree2(&temp);
	int cnt1 = 0, cnt2 = 0;
	for (int i=0; i < v.getCount(); i++) {
		if (tree1.locate(locEqual, v[i]))
			tree1.current().count++;
		else {
			Test t;
			t.value = v[i];
			t.count = 1;
			if (!tree1.add(t))
				assert(false);
			cnt1++;
		}
		if (tree2.locate(locEqual, v[i]))
			tree2.current().count++;
		else {
			Test t;
			t.value = v[i];
			t.count = 1;
			if (!tree2.add(t))
				assert(false);
			cnt2++;
		}	
	}
	printf(" DONE\n");
	
	bool passed = true;
	
	printf("Check that tree(2) contains test data: ");
	for (int i=0; i< v.getCount(); i++) {
		if(!tree1.locate(locEqual,v[i])) 
			passed = false;
	}
	printf(passed?"PASSED\n":"FAILED\n");
	passed = true;
	
	printf("Check that tree(13) contains test data: ");
	for (int i=0; i< v.getCount(); i++) {
		if(!tree2.locate(locEqual,v[i])) 
			passed = false;
	}
	printf(passed?"PASSED\n":"FAILED\n");
	
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
	printf(passed?"PASSED\n":"FAILED\n");
	
	printf("Check that tree(13) contains data from the tree(2) "\
		   "and its count is correct (check in reverse order): ");
	n = 0;
	if (tree2.getLast()) do {
		n++;
		if (!tree1.locate(locEqual, tree2.current().value)) passed = false;
	} while (tree2.getPrev());
	if (n != cnt2)
		passed = false;
	printf(passed?"PASSED\n":"FAILED\n");
	
	printf("Remove half of data from the trees: ");
	while (v.getCount() > TEST_ITEMS/2) {
		if (!tree1.locate(locEqual, v[v.getCount()-1]))
			assert(false);
		if (tree1.current().count > 1) 
			tree1.current().count--;
		else {
			tree1.fastRemove();
			cnt1--;
		}
		if (!tree2.locate(locEqual, v[v.getCount()-1]))
			assert(false);
		if (tree2.current().count > 1) 
			tree2.current().count--;
		else {
			tree2.fastRemove();
			cnt2--;
		}
		v.shrink(v.getCount()-1);
	}
	printf(" DONE\n");
	
	printf("Check that tree(2) contains test data: ");
	for (int i=0; i< v.getCount(); i++) {
		if(!tree1.locate(locEqual,v[i])) 
			passed = false;
	}	
	printf(passed?"PASSED\n":"FAILED\n");
	passed = true;
	
	printf("Check that tree(13) contains test data: ");
	for (int i=0; i < v.getCount(); i++) {
		if(!tree2.locate(locEqual,v[i])) 
			passed = false;
	}	
	printf(passed?"PASSED\n":"FAILED\n");
	
	passed = true;
	
	printf("Check that tree(2) contains data from the tree(13) and its count is correct: ");
	n = 0;
	if (tree1.getFirst()) do {
		n++;
		if (!tree2.locate(locEqual, tree1.current().value)) passed = false;
	} while (tree1.getNext());
	if (n != cnt1 || cnt1 != cnt2) 
		passed = false;
	printf(passed?"PASSED\n":"FAILED\n");
	
	passed = true;
	
	printf("Check that tree(13) contains data from the tree(2) "\
		   "and its count is correct (check in reverse order): ");
	n = 0;
	if (tree2.getLast()) do {
		n++;
		if (!tree1.locate(locEqual, tree2.current().value)) passed = false;
	} while (tree2.getPrev());
	if (n != cnt2) 
		passed = false;
	printf(passed?"PASSED\n":"FAILED\n");
	
	passed = true;
	
	printf("Remove the rest of data from the trees: ");
	for (int i=0;i < v.getCount(); i++) {
		if (!tree1.locate(locEqual, v[i]))
			assert(false);
		if (tree1.current().count > 1) 
			tree1.current().count--;
		else {
			tree1.fastRemove();
			cnt1--;
		}
		if (!tree2.locate(locEqual, v[i]))
			assert(false);
		if (tree2.current().count > 1)
			tree2.current().count--;
		else {
			tree2.fastRemove();
			cnt2--;
		}
	}
	printf(" DONE\n");

	printf("Check that both trees do not contain anything: ");
	if (tree1.getFirst())
		passed = false;
	if (tree2.getLast())
		passed = false;
	printf(passed?"PASSED\n":"FAILED\n");

}

#define ALLOC_ITEMS 100000
#define MAX_ITEM_SIZE 100

struct AllocItem {
	int order;
	void *item;
	static int compare(const AllocItem &i1, const AllocItem &i2) {
		return i1.order > i2.order || (i1.order==i2.order && i1.item > i2.item);
	}
};

void testAllocator() {
	printf("Test Firebird::MemoryPool\n");
	MemoryPool* pool = MemoryPool::createPool();
	
	MallocAllocator allocator;
	BePlusTree<AllocItem,AllocItem,MallocAllocator,DefaultKeyValue<AllocItem>,AllocItem> items(&allocator);
	printf("Allocate %d items: ", ALLOC_ITEMS);
	int n = 0;
	for (int i=0;i<ALLOC_ITEMS;i++) {
		n = n * 47163 - 57412;
		AllocItem temp = {n, pool->alloc((n % MAX_ITEM_SIZE + MAX_ITEM_SIZE)/2+1)};
		items.add(temp);
	}
	printf(" DONE\n");
	
	printf("Deallocate items in quasi-random order: ");
	if (items.getFirst()) do {
		pool->free(items.current().item);				
	} while (items.getNext());
	printf(" DONE\n");
//  TODO:
//	printf("Check that pool contains one free block per each segment and blocks have correct sizes");
//	Test critically low memory conditions
	MemoryPool::deletePool(pool);
}

int main() {
	testVector();
	testSortedVector();
	testBePlusTree();
	testAllocator();
}
