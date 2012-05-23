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
 *  The Original Code was created by Adriano dos Santos Fernandes
 *  for the Firebird Open Source RDBMS project.
 *
 *  Copyright (c) 2011 Adriano dos Santos Fernandes <adrianosf@uol.com.br>
 *  and all contributors signed below.
 *
 *  All Rights Reserved.
 *  Contributor(s): ______________________________________.
 */

#ifndef FIREBIRD_MESSAGE_H
#define FIREBIRD_MESSAGE_H

#include "ibase.h"
#include "firebird/impl/boost/preprocessor/seq/for_each_i.hpp"
#include <assert.h>
#include <string.h>

#define FB_MESSAGE(name, fields)	\
	FB_MESSAGE_I(name, FB_BOOST_PP_CAT(FB_MESSAGE_X fields, 0))

#define FB_MESSAGE_X(x, y) ((x, y)) FB_MESSAGE_Y
#define FB_MESSAGE_Y(x, y) ((x, y)) FB_MESSAGE_X
#define FB_MESSAGE_X0
#define FB_MESSAGE_Y0

#define FB_MESSAGE_I(name, fields)	\
	struct name	\
	{	\
		static const unsigned char* getBlr(unsigned* length)	\
		{	\
			static const unsigned char blr[] = {	\
				blr_version5,	\
				blr_begin,	\
				blr_message, 0,	\
					(2 * (FB_BOOST_PP_SEQ_SIZE(fields))) & 0xFF,	\
					(2 * (FB_BOOST_PP_SEQ_SIZE(fields))) >> 8,	\
				FB_BOOST_PP_SEQ_FOR_EACH_I(FB_MESSAGE_BLR, _, fields)	\
				blr_end,	\
				blr_eoc	\
			};	\
			*length = sizeof(blr);	\
			return blr;	\
		}	\
		\
		static unsigned getSize()	\
		{	\
			return (unsigned)(size_t) (&((name*) 0)->FB_BOOST_PP_CAT(	\
				FB_BOOST_PP_TUPLE_ELEM(2, 1,	\
					FB_BOOST_PP_SEQ_ELEM(FB_BOOST_PP_DEC(FB_BOOST_PP_SEQ_SIZE(fields)), fields)),	\
				Null) - 0) + sizeof(ISC_SHORT);	\
		}	\
		\
		void clear()	\
		{	\
			memset(this, 0, sizeof(*this));	\
		}	\
		\
		FB_BOOST_PP_SEQ_FOR_EACH_I(FB_MESSAGE_FIELD, _, fields)	\
	}

#define FB_MESSAGE_FIELD(r, _, i, xy)	\
	FB_BOOST_PP_CAT(FB_TYPE_, FB_BOOST_PP_TUPLE_ELEM(2, 0, xy)) FB_BOOST_PP_TUPLE_ELEM(2, 1, xy);	\
	ISC_SHORT FB_BOOST_PP_CAT(FB_BOOST_PP_TUPLE_ELEM(2, 1, xy), Null);

#define FB_MESSAGE_BLR(r, _, i, xy)	\
	FB_BOOST_PP_CAT(FB_BLR_, FB_BOOST_PP_TUPLE_ELEM(2, 0, xy)),	\
	FB_BLR_FB_SMALLINT,

#define FB_BLR_FB_SCALED_SMALLINT(scale)	blr_short, (scale)
#define FB_BLR_FB_SCALED_INTEGER(scale)		blr_long, (scale)
#define FB_BLR_FB_SCALED_BIGINT(scale)		blr_int64, (scale)
#define FB_BLR_FB_SMALLINT					FB_BLR_FB_SCALED_SMALLINT(0)
#define FB_BLR_FB_INTEGER					FB_BLR_FB_SCALED_INTEGER(0)
#define FB_BLR_FB_BIGINT					FB_BLR_FB_SCALED_BIGINT(0)
#define FB_BLR_FB_FLOAT						blr_float
#define FB_BLR_FB_DOUBLE					blr_double
#define FB_BLR_FB_BLOB						blr_blob2, 0, 0, 0, 0
#define FB_BLR_FB_BOOLEAN					blr_bool
#define FB_BLR_FB_DATE						blr_sql_date
#define FB_BLR_FB_TIME						blr_sql_time
#define FB_BLR_FB_TIMESTAMP					blr_timestamp
#define FB_BLR_FB_CHAR(len)					blr_text2, 0, 0, (len) & 0xFF, (len) >> 8
#define FB_BLR_FB_VARCHAR(len)				blr_varying2, 0, 0, (len) & 0xFF, (len) >> 8

#define FB_TYPE_FB_SCALED_SMALLINT(x)	ISC_SHORT
#define FB_TYPE_FB_SCALED_INTEGER(x)	ISC_LONG
#define FB_TYPE_FB_SCALED_BIGINT(x)		ISC_INT64
#define FB_TYPE_FB_SMALLINT				ISC_SHORT
#define FB_TYPE_FB_INTEGER				ISC_LONG
#define FB_TYPE_FB_BIGINT				ISC_INT64
#define FB_TYPE_FB_FLOAT				float
#define FB_TYPE_FB_DOUBLE				double
#define FB_TYPE_FB_BLOB					ISC_QUAD
#define FB_TYPE_FB_BOOLEAN				unsigned char
#define FB_TYPE_FB_DATE					ISC_DATE
#define FB_TYPE_FB_TIME					ISC_TIME
#define FB_TYPE_FB_TIMESTAMP			ISC_TIMESTAMP
#define FB_TYPE_FB_CHAR(len)			::Firebird::FbChar<(len)>
#define FB_TYPE_FB_VARCHAR(len)			::Firebird::FbVarChar<(len)>

#define FB_MESSAGE_DESC(name, fields)	\
	FB_MESSAGE(name, fields);	\
	struct name##Desc : public name	\
	{	\
		::Firebird::FbMessage desc;	\
		\
		name##Desc()	\
		{	\
			desc.blr = getBlr(&desc.blrLength);	\
			desc.buffer = (unsigned char*) this;	\
			desc.bufferLength = getSize();	\
		}	\
	}


namespace Firebird {


template <unsigned N>
struct FbChar
{
	char str[N];
};

template <unsigned N>
struct FbVarChar
{
	ISC_USHORT length;
	char str[N];

	void set(const char* s)
	{
		length = strlen(s);
		assert(length <= N);
		memcpy(str, s, length);
	}
};


}	// namespace Firebird

#endif	// FIREBIRD_MESSAGE_H
