/*
 *	PROGRAM:	InterBase International support
 *	MODULE:		ldcommon.h
 *	DESCRIPTION:	Common declarations for InterBase language drivers
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

#ifndef _INTL_LDCOMMON_H_
#define _INTL_LDCOMMON_H_

/* #include "../jrd/gdsassert.h" */
// Put the assert in here

#include "../jrd/intlobj.h"
#include "../intl/langdrv.h"
#include "../intl/charsets.h"
#include "../intl/country_codes.h"
#include "../intl/ld.h"

#undef DEBUG

typedef USHORT UNICODE;

/* Redirect the assertion code defined by gdsassert.h to a local routine */
#ifdef assert
#undef assert
#endif
#ifndef DEV_BUILD
#define ERR_assert				/* nothing */
#define assert(ex)				/* nothing */
#else
#include <stdlib.h> /* prototype for abort() */
#define ERR_assert	LD_assert
#define assert(ex)	{if (!(ex)){(void) LD_assert (__FILE__, __LINE__); abort();}}

// Although LD_assert is not exported in win32, it is defined with DLL_EXPORT
// most probably it could use C++ convention (see also ld_proto.h)
#ifdef __cplusplus
extern "C" {
#endif

extern void LD_assert(const SCHAR*, int);

#ifdef __cplusplus
} // extern C
#endif

#endif

#define MIN(x,y) ((x)<(y)?(x):(y))
#define MAX(x,y) ((x)>(y)?(x):(y))



#define	TEXTTYPE_ENTRY(name)	USHORT name (TEXTTYPE cache, SSHORT parm1, SSHORT dummy)

#define	TEXTTYPE_RETURN	return (0)




#define CONVERT_ENTRY(cs1, cs2, name)	USHORT	name (CSCONVERT csptr, SSHORT dest_cs, SSHORT source_cs)

#define	CONVERT_RETURN	return (0)




#define CHARSET_ENTRY(name)	USHORT	name (CHARSET csptr, SSHORT cs_id, SSHORT dummy)

#define CHARSET_RETURN	return (0)

extern void CV_convert_init(CSCONVERT, SSHORT, SSHORT, FPTR_SHORT, const void*, const void*);

#endif /* _INTL_LDCOMMON_H_ */
