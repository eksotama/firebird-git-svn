/*
 *	PROGRAM:	InterBase International support
 *	MODULE:		cv_jis.h
 *	DESCRIPTION:	Codeset conversion for JIS family codesets
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

USHORT CVJIS_sjis_to_unicode(csconvert* obj, USHORT *dest_ptr, USHORT dest_len,
							const UCHAR* sjis_str,
							USHORT sjis_len, SSHORT *err_code, USHORT *err_position);

USHORT CVJIS_unicode_to_sjis(csconvert* obj, UCHAR *sjis_str, USHORT sjis_len,
							const USHORT* unicode_str,
							 USHORT unicode_len, SSHORT *err_code, USHORT *err_position);

USHORT CVJIS_eucj_to_unicode(csconvert* obj, USHORT *dest_ptr, USHORT dest_len,
							const UCHAR* src_ptr,
							USHORT src_len, SSHORT *err_code, USHORT *err_position);

USHORT CVJIS_unicode_to_eucj(csconvert* obj, UCHAR *eucj_str, USHORT eucj_len,
							const USHORT* unicode_str,
							 USHORT unicode_len, SSHORT *err_code, USHORT *err_position);

/*
static USHORT CVJIS_check_euc(UCHAR *euc_str, USHORT euc_len);

static USHORT CVJIS_check_sjis(UCHAR *sjis_str, USHORT sjis_len);

static USHORT CVJIS_euc2sjis(csconvert* obj, UCHAR *sjis_str, USHORT sjis_len, UCHAR *euc_str
							, USHORT euc_len, SSHORT *err_code, USHORT *err_position);
static USHORT CVJIS_sjis2euc(csconvert* obj, UCHAR *euc_str, USHORT euc_len, UCHAR *sjis_str
							, USHORT sjis_len, SSHORT *err_code, USHORT *err_position);
*/

USHORT CVJIS_euc_byte2short(TEXTTYPE obj, USHORT* dst, USHORT dst_len,
							const UCHAR* src, USHORT src_len,
							SSHORT *err_code,	USHORT *err_position);

SSHORT CVJIS_euc_mbtowc(TEXTTYPE obj, UCS2_CHAR* wc, const UCHAR* src, USHORT src_len);

USHORT CVJIS_sjis_byte2short(TEXTTYPE obj, USHORT* dst, USHORT dst_len,
							const UCHAR* src, USHORT src_len,
							SSHORT *err_code, USHORT *err_position);

SSHORT CVJIS_sjis_mbtowc(TEXTTYPE obj, UCS2_CHAR* wc, const UCHAR* src, USHORT src_len);

