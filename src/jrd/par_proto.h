/*
 *	PROGRAM:	JRD Access Method
 *	MODULE:		par_proto.h
 *	DESCRIPTION:	Prototype header file for par.c
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

#ifndef _JRD_PAR_PROTO_H_
#define _JRD_PAR_PROTO_H_

#ifdef __cplusplus
extern "C" {
#endif

struct nod *PAR_blr(TDBB, struct rel *, UCHAR *, struct csb *, struct csb **,
					struct req **, BOOLEAN, USHORT);
int PAR_desc(struct csb **, struct dsc *);
struct nod *PAR_gen_field(TDBB, USHORT, USHORT);
struct nod *PAR_make_field(TDBB, struct csb *, USHORT, TEXT *);
struct nod *PAR_make_list(TDBB, struct lls *);
struct nod *PAR_make_node(TDBB, int);
struct csb *PAR_parse(TDBB, UCHAR *, USHORT);
SLONG PAR_symbol_to_gdscode(SCHAR *);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _JRD_PAR_PROTO_H_ */
