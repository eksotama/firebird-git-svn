/*
 *	PROGRAM:	JRD Backup and Restore Program
 *	MODULE:		mvol_proto.h		
 *	DESCRIPTION:	Prototype Header file for mvol.cpp
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

#ifndef _BURP_MVOL_PROTO_H_
#define _BURP_MVOL_PROTO_H_

#ifndef __cplusplus
#error This is a C++ only header file
#endif

#include "../burp/burp.h"


UINT64  MVOL_fini_read();
UINT64  MVOL_fini_write(int*, UCHAR**);
void    MVOL_init(ULONG);
void    MVOL_init_read(UCHAR*, UCHAR*, USHORT*, int*, UCHAR**);
void    MVOL_init_write(UCHAR*, UCHAR*, int*, UCHAR**);
bool    MVOL_split_hdr_write();
bool    MVOL_split_hdr_read();
int     MVOL_read(int*, UCHAR**);
UCHAR*  MVOL_read_block(TGBL, UCHAR*, ULONG);
void    MVOL_skip_block(TGBL, ULONG);
UCHAR   MVOL_write(UCHAR, int*, UCHAR**);
UCHAR*  MVOL_write_block(TGBL, UCHAR*, ULONG);

#if defined WIN_NT
extern DESC MVOL_open (char *,ULONG,ULONG);
#endif


#endif	/* _BURP_MVOL_PROTO_H_ */
