/*
 *	PROGRAM:	JRD Access Method
 *	MODULE:		bookmark.cpp
 *	DESCRIPTION:	Bookmark Handler
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


#include "firebird.h"
#include "../jrd/jrd.h"
#include "../jrd/exe.h"
#include "../jrd/rse.h"
#include "gen/iberror.h"
#include "../jrd/all_proto.h"
#include "../jrd/bookmark.h"
#include "../jrd/err_proto.h"
#include "../jrd/evl_proto.h"
#include "../jrd/mov_proto.h"
#include "../jrd/thd_proto.h"


#ifdef PC_ENGINE
Bookmark* BKM_allocate(Rsb* rsb, USHORT length)
{
/**************************************
 *
 *	B K M _ a l l o c a t e
 *
 **************************************
 *
 * Functional description
 *	Allocate and initialize a bookmark structure.
 *
 **************************************/
	thread_db* tdbb = GET_THREAD_DATA;
	Database* dbb = tdbb->tdbb_database;
	jrd_req* request = tdbb->tdbb_request;
	irsb* impure = (irsb*) ((UCHAR *) request + rsb->rsb_impure);

/* allocate the bookmark and link it into the 
   linked list hanging off the attachment block */

	Bookmark* bookmark = FB_NEW_RPT(*dbb->dbb_permanent, length) Bookmark();

	Attachment* attachment = tdbb->tdbb_attachment;
	bookmark->bkm_next = attachment->att_bookmarks;
	attachment->att_bookmarks = bookmark;

/* set up a self-referential descriptor to contain the pointer
   to the bookmark block; used for convenience in passing the
   bookmark descriptor around within looper() */

	bookmark->bkm_desc.dsc_dtype = dtype_long;
	bookmark->bkm_desc.dsc_length = sizeof(SLONG);
	bookmark->bkm_desc.dsc_address = (UCHAR *) & bookmark->bkm_handle;
#if SIZEOF_VOID_P != 8
	bookmark->bkm_handle = (ULONG) bookmark;
#else
	{
		/* The bookmark pointer can't be stored in a ULONG.  Therefore
		   we must generate a ULONG value that can be used to retrieve
		   the pointer.  Basically we will keep a vector of active bookmarks
		   and give the user an index into this vector.  When the user releases
		   a bookmark, its slot in the vector is zeroed and it becomes available
		   for reuse. */

		const ULONG slot =
			ALL_get_free_object(dbb->dbb_permanent,
								&attachment->att_bkm_quick_ref, 50);
		attachment->att_bkm_quick_ref->vec_object[slot] = (BLK) bookmark;
		bookmark->bkm_handle = slot;
	}
#endif

/* set up the bookmark flags */

	if (impure->irsb_flags & irsb_bof)
		bookmark->bkm_flags |= bkm_bof;
	else if (impure->irsb_flags & irsb_eof)
		bookmark->bkm_flags |= bkm_eof;
	else if (impure->irsb_flags & irsb_crack) {
		bookmark->bkm_flags |= bkm_crack;
		if (impure->irsb_flags & irsb_forced_crack)
			bookmark->bkm_flags |= bkm_forced_crack;
	}

	return bookmark;
}
#endif


#ifdef PC_ENGINE
Bookmark* BKM_lookup(NOD node)
{
/**************************************
 *
 *	B K M _ l o o k u p
 *
 **************************************
 *
 * Functional description
 *	Find a bookmark using a user supplied value.
 *
 **************************************/
	Bookmark* bookmark;

#if SIZEOF_VOID_P != 8
	bookmark = (Bookmark*) MOV_get_long(EVL_expr(tdbb, node), 0);
#else
	{
		thread_db* tdbb = GET_THREAD_DATA;
		Attachment* attachment = tdbb->tdbb_attachment;

		bookmark = NULL;
		const ULONG slot = MOV_get_long(EVL_expr(tdbb, node), 0);
		vec* vector = attachment->att_bkm_quick_ref;
		if (vector && slot < vector->vec_count) {
				bookmark = (Bookmark*) vector->vec_object[slot];
		}
	}
#endif

	if (!bookmark || bookmark->bkm_header.blk_type != type_bkm)
		ERR_post(gds__invalid_bookmark, 0);

	return bookmark;
}
#endif


#ifdef PC_ENGINE
void BKM_release(NOD node)
{
/**************************************
 *
 *	B K M _ r e l e a s e
 *
 **************************************
 *
 * Functional description
 *	Release a bookmark using a user supplied value.
 *
 **************************************/
	thread_db* tdbb = GET_THREAD_DATA;
	Attachment* attachment = tdbb->tdbb_attachment;

	Bookmark* bookmark = BKM_lookup(node);

/* unlink the bookmark from the attachment linked list */

	for (Bookmark** bptr = &attachment->att_bookmarks; *bptr; bptr = &(*bptr)->bkm_next)
		if (*bptr == bookmark) {
			*bptr = bookmark->bkm_next;
			break;
		}

#if SIZEOF_VOID_P == 8
	const ULONG slot = MOV_get_long(EVL_expr(tdbb, node), 0);
	attachment->att_bkm_quick_ref->vec_object[slot] = NULL;
#endif

	ALL_release(bookmark);
}
#endif

