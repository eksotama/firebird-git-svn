/*
 *	PROGRAM:	Common class definition
 *	MODULE:		object_array.h
 *	DESCRIPTION:	half-static array of any objects,
 *			having MemoryPool'ed constructor.
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
 *  The Original Code was created by Alexander Peshkoff
 *  for the Firebird Open Source RDBMS project.
 *
 *  Copyright (c) 2004 Alexander Peshkoff <peshkoff@mail.ru>
 *  and all contributors signed below.
 *
 *  All Rights Reserved.
 *  Contributor(s): ______________________________________.
 */

#ifndef CLASSES_OBJECTS_ARRAY_H
#define CLASSES_OBJECTS_ARRAY_H
 
#include "../common/classes/alloc.h"
#include "../common/classes/array.h"

namespace Firebird
{
	template <typename T, typename A = Array<T*, InlineStorage<T*, 8> > >
	class ObjectsArray : protected A
	{
	private:
		typedef A inherited;
	public:
		class iterator {
			friend class ObjectsArray<T, A>;
		private:
			ObjectsArray *lst;
			int pos;
			iterator(ObjectsArray *l, int p) : lst(l), pos(p){}
		public:
			iterator& operator++() {++pos; return (*this);}
			iterator operator++(int) {iterator tmp = *this; ++pos; 
									  return tmp;}
			iterator& operator--() {--pos; return (*this);}
			iterator operator--(int) {iterator tmp = *this; --pos; 
									  return tmp;}
			T* operator->() {
				T* pointer = lst->getPointer(pos);
				return pointer;
			}
			T& operator*() {
				T* pointer = lst->getPointer(pos);
				return *pointer;
			}
			bool operator!=(const iterator& v) const
			{
				fb_assert(lst == v.lst);
				return pos != v.pos;
			}
		};
	public:
		void insert(int index, const T& item) {
			T* data = FB_NEW(getPool()) T(getPool(), item);
			inherited::insert(index, data);
		}
		int add(const T& item) {
			T* data = FB_NEW(getPool()) T(getPool(), item);
			return inherited::add(data);
		};
		void push(const T& item) {
			add(item);
		}
		T pop() {
			T* pntr = inherited::pop();
			T rc = *pntr;
			delete pntr;
			return rc;
		}
		void remove(int index) {
			delete getPointer(index);
			inherited::remove(index);
		}
		void remove(iterator itr) {
  			fb_assert(itr.lst == this);
			remove(itr.pos);
		}
		void shrink(int newCount) {
			for (int i = newCount; i < getCount(); i++) {
				delete getPointer(i);
			}
			inherited::shrink(newCount);
		};
		iterator begin() {
			return iterator(this, 0);
		}
		iterator end() {
			return iterator(this, getCount());
		}
		const T& operator[](int index) const {
  			return *getPointer(index);
		}
		const T* getPointer(int index) const {
  			return inherited::getElement(index);
		}
		T& operator[](int index) {
  			return *getPointer(index);
		}
		T* getPointer(int index) {
  			return inherited::getElement(index);
		}
		explicit ObjectsArray(MemoryPool& p) : A(p) { }
		ObjectsArray() : A() { }
		~ObjectsArray() {
			for (int i = 0; i < getCount(); i++) {
				delete getPointer(i);
			}
		}
		int getCount() const {return inherited::getCount();}
		int getCapacity() const {return inherited::getCapacity();}
		void clear() { 
			for (int i = 0; i < getCount(); i++) {
				delete getPointer(i);
			}
			inherited::clear(); 
		}
	};

	// Template to convert object value to index directly
	template <typename T>
	class ObjectKeyValue {
	public:
		static const T& generate(const void* sender, const T* Item) { return Item; }
	};

	// Template for default value comparsion
	template <typename T>
	class ObjectComparator {
	public:
		static bool greaterThan(const T& i1, const T& i2) {
			return *i1 > *i2;
		}
	};

	// Dynamic sorted array of simple objects
	template <typename Value,
		typename Storage = InlineStorage<Value*, 32>, 
		typename Key = Value*, 
		typename KeyOfValue = ObjectKeyValue<Value>, 
		typename Cmp = DefaultComparator<Key> >
	class SortedObjectsArray : public ObjectsArray<Value, 
			SortedArray <Value*, Storage, Key, KeyOfValue, Cmp> >
	{
	private:
		typedef ObjectsArray <Value, SortedArray<Value*, 
				Storage, Key, KeyOfValue, Cmp> > inherited;
	public:
		explicit SortedObjectsArray(MemoryPool& p) : 
			ObjectsArray <Value, SortedArray<Value*, 
				Storage, Key, KeyOfValue, Cmp> >(p) { }
		bool find(const Key& item, int& pos) {
			return inherited::find(item, pos);
		}
	};
}

#endif	// CLASSES_OBJECTS_ARRAY_H
