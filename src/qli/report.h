/*
 *	PROGRAM:	JRD Command Oriented Query Language
 *	MODULE:		report.h
 *	DESCRIPTION:	Report writer definitions
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

#ifndef QLI_REPORT_H
#define QLI_REPORT_H

/* Control break block */

typedef struct qli_brk {
    blk		brk_header;
    qli_brk*	brk_next;		/* Next break */
    SYN		brk_field;		/* Field expression for break */
    SYN		brk_line;		/* Print line */
    LLS		brk_statisticals;	/* Statistical expressions */
} *QLI_BRK;

/* Report block */

typedef struct rpt {
    blk		rpt_hdr;
    qli_nod*	rpt_detail_line;	/* Detail line print list */
    QLI_BRK		rpt_top_page;		/* Top of page print list */
    QLI_BRK		rpt_bottom_page;	/* Bottom of page print list */
    QLI_BRK		rpt_top_rpt;		/* Top of report print list */
    QLI_BRK		rpt_bottom_rpt;		/* Bottom of report print list */
    QLI_BRK		rpt_top_breaks;		/* Top of <field> break list */
    QLI_BRK		rpt_bottom_breaks;	/* Bottom of <field> break list */
    TEXT	*rpt_column_header;
    TEXT	*rpt_name;		/* Parsed report name */
    TEXT	*rpt_header;		/* Expanded report header */
    UCHAR	*rpt_buffer;		/* Data buffer */
    USHORT	rpt_columns;		/* Columns per page */
    USHORT	rpt_lines;		/* Lines per page */
} *RPT;

#endif /* QLI_REPORT_H */
