/*
 *  fb_vector.h
 *  firebird_test
 *
 *  Created by john on Fri Dec 14 2001.
 *  Copyright (c) 2001 __MyCompanyName__. All rights reserved.
 *
 */

#ifndef FB_MAP_H
#define FB_MAP_H
 
#include "../common/memory/allocators.h"

#include <map>

namespace Firebird
{
	template <class A, class B>
	class map :
		public std::map<A, B, std::less<A>, Firebird::allocator<std::pair<A,B> > >
	{
	};
};

#endif	// FB_MAP_H
