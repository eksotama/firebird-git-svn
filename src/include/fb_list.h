#ifndef FB_LIST_H
#define FB_LIST_H
 
#include "../common/memory/allocators.h"

#include <list>

namespace Firebird
{
	template <class T>
	class list : public std::list<T, Firebird::allocator<T> >
	{
	};
};

#endif	// FB_LIST_H
