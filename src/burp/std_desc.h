/*
 **************************************************************************
 *
 *	PROGRAM:		standard descriptors for gbak & gsplit
 *	MODULE:			std_desc.h
 *	DESCRIPTION:	standard descriptors for different OS's
 *
 *
 **************************************************************************
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
 * The Original Code was created by Alexander Peshkoff <peshkoff@mail.ru>.
 * Thanks to Achim Kalwa <achim.kalwa@winkhaus.de>.
 *
 * All Rights Reserved.
 * Contributor(s): ______________________________________.
 */

#ifndef GBAK_STD_DESC_H
#define GBAK_STD_DESC_H

#include "firebird.h"

#ifdef WIN_NT
#include <windows.h>
static inline HANDLE GBAK_STDIN_DESC(void)
{
	return GetStdHandle(STD_INPUT_HANDLE); /* standart input  file descriptor */
}
static inline HANDLE GBAK_STDOUT_DESC(void)
{
	return GetStdHandle(STD_OUTPUT_HANDLE);	/* standart output file descriptor */
}
#else //WIN_NT
static inline int GBAK_STDIN_DESC(void)
{
	return 0;	/* standart input  file descriptor */
}
static inline int GBAK_STDOUT_DESC(void)
{
	return 1;	/* standart output file descriptor */
}
#endif //WIN_NT

#endif  //GBAK_STD_DESC_H

