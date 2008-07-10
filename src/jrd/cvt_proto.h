/*
 *	PROGRAM:	JRD Access Method
 *	MODULE:		cvt_proto.h
 *	DESCRIPTION:	Prototype header file for cvt.cpp
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

#ifndef JRD_CVT_PROTO_H
#define JRD_CVT_PROTO_H

double		CVT_date_to_double(const dsc*);
void		CVT_double_to_date(double, SLONG[2]);
UCHAR		CVT_get_numeric(const UCHAR*, const USHORT, SSHORT*, double*);
GDS_DATE	CVT_get_sql_date(const dsc*);
GDS_TIME	CVT_get_sql_time(const dsc*);
GDS_TIMESTAMP CVT_get_timestamp(const dsc*);
void		CVT_move(const dsc*, dsc*);

#endif // JRD_CVT_PROTO_H

