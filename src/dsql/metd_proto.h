/*
 *	PROGRAM:	Dynamic SQL runtime support
 *	MODULE:		metd_proto.h
 *	DESCRIPTION:	Prototype Header file for metd.e
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

#ifndef _DSQL_METD_PROTO_H
#define _DSQL_METD_PROTO_H

void METD_drop_procedure(class req*, class str*);
void METD_drop_relation(class req*, class str*);
INTLSYM METD_get_charset(class req*, USHORT, UCHAR *);
INTLSYM METD_get_collation(class req*, class str*);
void METD_get_col_default(REQ, TEXT*, TEXT*, BOOLEAN*, TEXT*, USHORT);
STR METD_get_default_charset(class req*);
USHORT METD_get_domain(class req*, class fld*, UCHAR*);
void METD_get_domain_default(class req*, TEXT*, BOOLEAN*, TEXT*, USHORT);
UDF METD_get_function(class req*, class str*);
NOD METD_get_primary_key(class req*, class str*);
PRC METD_get_procedure(class req*, class str*);
DSQL_REL METD_get_relation(class req*, class str*);
STR METD_get_trigger_relation(class req*, class str*, USHORT*);
USHORT METD_get_type(class req*, class str*, UCHAR*, SSHORT*);
DSQL_REL METD_get_view_relation(class req*, UCHAR*, UCHAR*, USHORT);


#endif /*_DSQL_METD_PROTO_H */
