/*
 *
 *     The contents of this file are subject to the Initial
 *     Developer's Public License Version 1.0 (the "License");
 *     you may not use this file except in compliance with the
 *     License. You may obtain a copy of the License at
 *     http://www.ibphoenix.com/idpl.html.
 *
 *     Software distributed under the License is distributed on
 *     an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either
 *     express or implied.  See the License for the specific
 *     language governing rights and limitations under the License.
 *
 *
 *  The Original Code was created by Claudio Valderrama C. for IBPhoenix.
 *  The development of the Original Code was sponsored by Craig Leonardi.
 *
 *  Copyright (c) 2001 IBPhoenix
 *  All Rights Reserved.
 */


// The following ifdef block is the standard way of creating macros which make exporting 
// from a DLL simpler. All files within this DLL are compiled with the FBUDF_EXPORTS
// symbol defined on the command line. This symbol should not be defined on any project
// that uses this DLL. This way any other project whose source files include this file see 
// FBUDF_API functions as being imported from a DLL, whereas this DLL sees symbols
// defined with this macro as being exported.
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__)
#ifdef FBUDF_EXPORTS
#define FBUDF_API __declspec(dllexport)
#else
#define FBUDF_API __declspec(dllimport)
#endif
#else
#define FBUDF_API
#endif

//Original code for this library was written by Claudio Valderrama
// on July 2001 for IBPhoenix.


#ifdef __cplusplus
extern "C" {
#endif

#ifdef DEV_BUILD
//This function shouldn't be defined in production.
FBUDF_API paramdsc* testreflect(paramdsc* rc);
#endif


FBUDF_API paramdsc* idNvl(paramdsc* v, paramdsc* v2);
FBUDF_API paramdsc* sNvl(paramdsc* v, paramdsc* v2, paramdsc* rc);

FBUDF_API paramdsc* iNullIf(paramdsc* v, paramdsc* v2);
FBUDF_API paramdsc* dNullIf(paramdsc* v, paramdsc* v2);
FBUDF_API paramdsc* sNullIf(paramdsc* v, paramdsc* v2, paramdsc* rc);

FBUDF_API char* DOW(ISC_TIMESTAMP* v, char* rc);
FBUDF_API char* SDOW(ISC_TIMESTAMP* v, char* rc);

FBUDF_API paramdsc* right(paramdsc* v, short* rl, paramdsc* rc);

FBUDF_API ISC_TIMESTAMP* addDay(ISC_TIMESTAMP* v, int& ndays);
FBUDF_API ISC_TIMESTAMP* addWeek(ISC_TIMESTAMP* v, int& nweeks);
FBUDF_API ISC_TIMESTAMP* addMonth(ISC_TIMESTAMP* v, int& nmonths);
FBUDF_API ISC_TIMESTAMP* addYear(ISC_TIMESTAMP* v, int& nyears);

FBUDF_API ISC_TIMESTAMP* addMilliSecond(ISC_TIMESTAMP* v, int& nmseconds);
FBUDF_API ISC_TIMESTAMP* addSecond(ISC_TIMESTAMP* v, int& nseconds);
FBUDF_API ISC_TIMESTAMP* addMinute(ISC_TIMESTAMP* v, int& nminutes);
FBUDF_API ISC_TIMESTAMP* addHour(ISC_TIMESTAMP* v, int& nhours);

FBUDF_API ISC_TIMESTAMP* getExactTimestamp(ISC_TIMESTAMP* rc);

FBUDF_API paramdsc* fbtruncate(paramdsc* v, paramdsc* rc);
FBUDF_API paramdsc* fbround(paramdsc* v, paramdsc* rc);
FBUDF_API paramdsc* power(paramdsc* v, paramdsc* v2, paramdsc* rc);

FBUDF_API blobcallback* string2blob(paramdsc* v, blobcallback* outblob);

#ifdef __cplusplus
}
#endif

