/*
 *	PROGRAM:	JRD Access Method
 *	MODULE:		btr.h
 *	DESCRIPTION:	Index walking data structures
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
 *
 * 2002.10.28 Sean Leyne - Code cleanup, removed obsolete "DecOSF" port
 *
 */

#ifndef JRD_BTR_H_
#define JRD_BTR_H_

#include "../jrd/constants.h"
#include "../common/classes/array.h"
#include "../jrd/jrd_blks.h"
#include "../include/fb_blk.h"

#include "../jrd/err_proto.h"    /* Index error types */

/* 64 turns out not to be enough indexes */
/* #define MAX_IDX		 64	*/	/* that should be plenty of indexes */

#define MAX_KEY_LIMIT		(dbb->dbb_page_size / 4)

enum idx_null_state {
  idx_nulls_none,
  idx_nulls_some,
  idx_nulls_all
};

/* Index descriptor block -- used to hold info from index root page */

typedef struct idx {
	SLONG idx_root;				/* Index root */
	float idx_selectivity;		/* selectivity of index */
	USHORT idx_id;
	UCHAR idx_flags;
	UCHAR idx_runtime_flags;	/* flags used at runtime, not stored on disk */
	USHORT idx_primary_index;	/* id for primary key partner index */
	USHORT idx_primary_relation;	/* id for primary key partner relation */
	USHORT idx_count;			/* number of keys */
	struct vec *idx_foreign_primaries;	/* ids for primary/unique indexes with partners */
	struct vec *idx_foreign_relations;	/* ids for foreign key partner relations */
	struct vec *idx_foreign_indexes;	/* ids for foreign key partner indexes */
	jrd_nod* idx_expression;	/* node tree for indexed expresssion */
	struct dsc idx_expression_desc;	/* descriptor for expression result */
	struct jrd_req *idx_expression_request;	/* stored request for expression evaluation */
	// This structure should exactly match IRTD structure for current ODS
	struct idx_repeat {
		USHORT idx_field;		/* field id */
		USHORT idx_itype;		/* data of field in index */
		float idx_selectivity;	/* segment selectivity */
	} idx_rpt[MAX_INDEX_SEGMENTS];
} IDX;

/* index types and flags */

/* See jrd/intl.h for notes on idx_itype and dsc_sub_type considerations */
/* idx_numeric .. idx_byte_array values are compatible with VMS values */

#define idx_numeric		0
#define idx_string		1
#define idx_timestamp1		2
#define idx_byte_array		3
#define idx_metadata		4
#define idx_sql_date		5
#define idx_sql_time		6
#define idx_timestamp2		7
#define idx_numeric2		8	/* Introduced for 64-bit Integer support */

/* Historical alias for pre v6 applications */
#define idx_date		idx_timestamp1

				   /* idx_itype space for future expansion */
#define	idx_first_intl_string	64	/* .. MAX (short) Range of computed key strings */

#define idx_offset_intl_range	(0x7FFF + idx_first_intl_string)

/* these flags must match the irt_flags */

#define idx_unique	1
#define idx_descending	2
#define idx_in_progress	4
#define idx_foreign	8
#define idx_primary	16
#define idx_expressn	32

/* these flags are for idx_runtime_flags */

#define idx_plan_dont_use	1	/* index is not mentioned in user-specified access plan */
#define idx_plan_navigate	2	/* plan specifies index to be used for ordering */
#define idx_used 		4		/* index was in fact selected for retrieval */
#define idx_navigate		8	/* index was in fact selected for navigation */
#define	idx_plan_missing	16	/* index mentioned in missing clause */
#define	idx_plan_starts		32	/* index mentioned in starts clause */
#define	idx_used_with_and	64	/* marker used in procedure sort_indices */
#define	idx_marker			128	/* marker used in procedure sort_indices */

/* Macro to locate the next IDX block */

#define NEXT_IDX(buffer,count)	(IDX*) (buffer + count)


/* Index insertion block -- parameter block for index insertions */

typedef struct iib {
	SLONG iib_number;			/* record number (or lower level page) */
	SLONG iib_sibling;			/* right sibling page */
	idx* iib_descriptor;		/* index descriptor */
	struct jrd_rel *iib_relation;	/* relation block */
	struct key *iib_key;		/* varying string for insertion */
	struct sbm *iib_duplicates;	/* spare bit map of duplicates */
	class jrd_tra *iib_transaction;	/* insertion transaction */
} IIB;


/* Temporary key block */

typedef struct key {
	USHORT key_length;
	UCHAR key_data[MAX_KEY];
 /* AB: I don't see the use of multiplying with 2 anymore. */
	//UCHAR key_data[MAX_KEY * 2];	
		// This needs to be on a SHORT boundary. 
		// This is because key_data is complemented as 
		// (SSHORT *) if value is negative.
		//  See compress() (JRD/btr.c) for more details
} KEY;


/* Index Sort Record -- fix part of sort record for index fast load */

typedef struct isr {
	USHORT isr_key_length;
	USHORT isr_flags;
	ULONG isr_record_number;
} *ISR;

#define ISR_secondary	1	// Record is secondary version
#define ISR_null		2	// Record consists of NULL values only



/* Index retrieval block -- hold stuff for index retrieval */

class irb : public pool_alloc_rpt<jrd_nod*, type_irb>
{
    public:
	IDX irb_desc;				/* Index descriptor */
	USHORT irb_index;			/* Index id */
	USHORT irb_generic;			/* Flags for generic search */
	struct jrd_rel *irb_relation;	/* Relation for retrieval */
	USHORT irb_lower_count;		/* Number of segments for retrieval */
	USHORT irb_upper_count;		/* Number of segments for retrieval */
	KEY *irb_key;				/* key for equality retrival */
	jrd_nod* irb_value[1];
};
typedef irb *IRB;

#define irb_partial	1			/* Partial match: not all segments or starting of key only */
#define irb_starting	2		/* Only compute "starting with" key for index segment */
#define irb_equality	4		/* Probing index for equality match */
#define irb_ignore_null_value_key  8	/* if lower bound is specified and upper bound unspecified,
										   * ignore looking at null value keys */
#define irb_descending	16		/* ?Base index uses descending order */

// macros used to manipulate btree nodes
#define BTR_SIZE	OFFSETA(btree_page*, btr_nodes);

#define NEXT_NODE(node)	(btn*)(node->btn_data + node->btn_length)
#define NEXT_NODE_RECNR(node)	(btn*)(node->btn_data + node->btn_length + sizeof(SLONG))

#define LAST_NODE(page)	(BTN) ((UCHAR*) page + page->btr_length)

//#define NEXT_EXPANDED(xxx,yyy)	(BTX) ((UCHAR*) xxx->btx_data + (yyy)->btn_prefix + (yyy)->btn_length)

typedef Firebird::HalfStaticArray<float, 4> SelectivityList;

#endif /* JRD_BTR_H_ */
