/*
 *	PROGRAM:	InterBase International support
 *	MODULE:		cv_gb2312.h
 *	DESCRIPTION:	Codeset conversion for GB2312 family codesets
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

/* These macros have a duplicate in lc_gb2312.cpp */
#define	GB1(uc)	((UCHAR)((uc)&0xff)>=0xa1 && \
			 (UCHAR)((uc)&0xff)<=0xfe)	/* GB2312 1st-byte */
#define	GB2(uc)	((UCHAR)((uc)&0xff)>=0xa1 && \
			 (UCHAR)((uc)&0xff)<=0xfe)	/* GB2312 2nd-byte */

USHORT CVGB_gb2312_to_unicode(csconvert* obj, USHORT *dest_ptr, USHORT dest_len,
							const UCHAR* src_ptr,
							USHORT src_len, SSHORT *err_code, USHORT *err_position);

USHORT CVGB_unicode_to_gb2312(csconvert* obj, UCHAR *gb_str, USHORT gb_len,
							const USHORT* unicode_str,
							USHORT unicode_len, SSHORT *err_code, USHORT *err_position);

USHORT CVGB_check_gb2312(const UCHAR* gb_str, USHORT gb_len);

USHORT CVGB_gb2312_byte2short(TEXTTYPE obj, USHORT* dst, USHORT dst_len,
							const UCHAR* src, USHORT src_len,
							SSHORT *err_code, USHORT *err_position);

SSHORT CVGB_gb2312_mbtowc(TEXTTYPE obj, UCS2_CHAR* wc, const UCHAR* src, USHORT src_len);

