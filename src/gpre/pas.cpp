//____________________________________________________________
//  
//		PROGRAM:	PASCAL preprocesser
//		MODULE:		pas.cpp
//		DESCRIPTION:	Inserted text generator for Domain Pascal
//  
//  The contents of this file are subject to the Interbase Public
//  License Version 1.0 (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy
//  of the License at http://www.Inprise.com/IPL.html
//  
//  Software distributed under the License is distributed on an
//  "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express
//  or implied. See the License for the specific language governing
//  rights and limitations under the License.
//  
//  The Original Code was created by Inprise Corporation
//  and its predecessors. Portions created by Inprise Corporation are
//  Copyright (C) Inprise Corporation.
//  
//  All Rights Reserved.
//  Contributor(s): ______________________________________.
//  
//
//____________________________________________________________
//
//	$Id: pas.cpp,v 1.15 2003-09-10 19:48:52 brodsom Exp $
//

#include "firebird.h"
#include "../jrd/ib_stdio.h"
#include "../jrd/common.h"
#include <stdarg.h>
#include <string.h>
#include "../jrd/gds.h"
#include "../gpre/gpre.h"
#include "../gpre/form.h"
#include "../gpre/pat.h"
#include "../gpre/cmp_proto.h"
#include "../gpre/lang_proto.h"
#include "../gpre/pat_proto.h"
#include "../gpre/gpre_proto.h"
#include "../gpre/prett_proto.h"
#include "../jrd/gds_proto.h"

#pragma FB_COMPILER_MESSAGE("This file is not fit for public consumption")
// #error compiler halted, 'rogue' not found...
// TMN: Upon converting this file to C++, it has been noted
// that this code would never have worked because of (SEGV) bug(s),
// why I rather than trying to use it currently remove it from compilation.


static void align(int);
static void asgn_from(ACT, REF, int);
static void asgn_sqlda_from(REF, int, TEXT *, int);
static void asgn_to(ACT, REF, int);
static void asgn_to_proc(REF, int);
static void gen_at_end(ACT, int);
static void gen_based(ACT, int);
static void gen_blob_close(ACT, USHORT);
static void gen_blob_end(ACT, USHORT);
static void gen_blob_for(ACT, USHORT);
static void gen_blob_open(ACT, USHORT);
static int gen_blr(int *, int, TEXT *);
static void gen_compile(ACT, int);
static void gen_create_database(ACT, int);
static int gen_cursor_close(ACT, GPRE_REQ, int);
static void gen_cursor_init(ACT, int);
static int gen_cursor_open(ACT, GPRE_REQ, int);
static void gen_database(ACT, int);
static void gen_ddl(ACT, int);
static void gen_drop_database(ACT, int);
static void gen_dyn_close(ACT, int);
static void gen_dyn_declare(ACT, int);
static void gen_dyn_describe(ACT, int, bool);
static void gen_dyn_execute(ACT, int);
static void gen_dyn_fetch(ACT, int);
static void gen_dyn_immediate(ACT, int);
static void gen_dyn_insert(ACT, int);
static void gen_dyn_open(ACT, int);
static void gen_dyn_prepare(ACT, int);
static void gen_emodify(ACT, int);
static void gen_estore(ACT, int);
static void gen_endfor(ACT, int);
static void gen_erase(ACT, int);
static SSHORT gen_event_block(ACT);
static void gen_event_init(ACT, int);
static void gen_event_wait(ACT, int);
static void gen_fetch(ACT, int);
static void gen_finish(ACT, int);
static void gen_for(ACT, int);
#ifdef PYXIS
static void gen_form_display(ACT, int);
static void gen_form_end(ACT, int);
static void gen_form_for(ACT, int);
#endif
static void gen_get_or_put_slice(ACT, REF, bool, int);
static void gen_get_segment(ACT, int);
#ifdef PYXIS
static void gen_item_end(ACT, int);
static void gen_item_for(ACT, int);
#endif
static void gen_loop(ACT, int);
#ifdef PYXIS
static void gen_menu(ACT, int);
static void gen_menu_display(ACT, int);
static void gen_menu_entree(ACT, int);
static void gen_menu_entree_att(ACT, int);
static void gen_menu_for(ACT, int);
static void gen_menu_item_end(ACT, int);
static void gen_menu_item_for(ACT, int);
static void gen_menu_request(GPRE_REQ, int);
#endif
static TEXT *gen_name(TEXT *, REF, bool);
static void gen_on_error(ACT, USHORT);
static void gen_procedure(ACT, int);
static void gen_put_segment(ACT, int);
static void gen_raw(UCHAR *, int, int);
static void gen_ready(ACT, int);
static void gen_receive(ACT, int, POR);
static void gen_release(ACT, int);
static void gen_request(GPRE_REQ, int);
static void gen_return_value(ACT, int);
static void gen_routine(ACT, int);
static void gen_s_end(ACT, int);
static void gen_s_fetch(ACT, int);
static void gen_s_start(ACT, int);
static void gen_segment(ACT, int);
static void gen_select(ACT, int);
static void gen_send(ACT, POR, int);
static void gen_slice(ACT, int);
static void gen_start(ACT, POR, int);
static void gen_store(ACT, int);
static void gen_t_start(ACT, int);
static void gen_tpb(TPB, int);
static void gen_trans(ACT, int);
static void gen_update(ACT, int);
static void gen_variable(ACT, int);
static void gen_whenever(SWE, int);
#ifdef PYXIS
static void gen_window_create(ACT, int);
static void gen_window_delete(ACT, int);
static void gen_window_suspend(ACT, int);
#endif
static void make_array_declaration(REF);
static TEXT *make_name(TEXT *, SYM);
static void make_ok_test(ACT, GPRE_REQ, int);
static void make_port(POR, int);
static void make_ready(DBB, TEXT *, TEXT *, USHORT, GPRE_REQ);
static void printa(int, TEXT *, ...);
static TEXT *request_trans(ACT, GPRE_REQ);
static TEXT *status_vector(ACT);
static void t_start_auto(ACT, GPRE_REQ, TEXT *, int);

static int first_flag;

#define INDENT 3
#define BEGIN		printa (column, "begin")
#define END		printa (column, "end")
#define ENDS		printa (column, "end;")

#ifdef VMS
#define SHORT_DCL	"gds__short"
#define LONG_DCL	"integer"
#define POINTER_DCL	"gds__ptr_type"
#define PACKED_ARRAY	"packed array"
#define OPEN_BRACKET	"("
#define CLOSE_BRACKET	")"
#define REF_PAR		"%REF "
#define SIZEOF		"size"
#define STATIC_STRING	"[STATIC]"
#define ISC_BADDRESS	"ISC_BADDRESS"
#else
#define SHORT_DCL	"integer16"
#define LONG_DCL	"integer32"
#define POINTER_DCL	"UNIV_PTR"
#define PACKED_ARRAY	"array"
#define OPEN_BRACKET	"["
#define CLOSE_BRACKET	"]"
#define REF_PAR		""
#define SIZEOF		"sizeof"
#define STATIC_STRING	"STATIC"
#define ISC_BADDRESS	"ADDR"
#endif

#define FB_DP_VOLATILE		""
#define GDS_EVENT_COUNTS	"GDS__EVENT_COUNTS"
#define GDS_EVENT_WAIT		"GDS__EVENT_WAIT"

#define SET_SQLCODE	if (action->act_flags & ACT_sql) printa (column, "SQLCODE := gds__sqlcode (gds__status);")


//____________________________________________________________
//  
//		Code generator for Domain Pascal.  Not to be confused with
//		the language "Pascal".
//  

void PAS_action( ACT action, int column)
{

//  Put leading braces where required 

	switch (action->act_type) {
	case ACT_alter_database:
	case ACT_alter_domain:
	case ACT_alter_table:
	case ACT_alter_index:
	case ACT_blob_close:
	case ACT_blob_create:
	case ACT_blob_for:
	case ACT_blob_open:
	case ACT_close:
	case ACT_commit:
	case ACT_commit_retain_context:
	case ACT_create_database:
	case ACT_create_domain:
	case ACT_create_generator:
	case ACT_create_index:
	case ACT_create_shadow:
	case ACT_create_table:
	case ACT_create_view:
	case ACT_declare_filter:
	case ACT_declare_udf:
	case ACT_disconnect:
	case ACT_drop_database:
	case ACT_drop_domain:
	case ACT_drop_filter:
	case ACT_drop_index:
	case ACT_drop_shadow:
	case ACT_drop_table:
	case ACT_drop_udf:
	case ACT_drop_view:
	case ACT_dyn_close:
	case ACT_dyn_cursor:
	case ACT_dyn_describe:
	case ACT_dyn_describe_input:
	case ACT_dyn_execute:
	case ACT_dyn_fetch:
	case ACT_dyn_grant:
	case ACT_dyn_immediate:
	case ACT_dyn_insert:
	case ACT_dyn_open:
	case ACT_dyn_prepare:
	case ACT_dyn_revoke:
	case ACT_fetch:
	case ACT_finish:
	case ACT_for:
#ifdef PYXIS
	case ACT_form_display:
	case ACT_form_for:
#endif
	case ACT_get_segment:
	case ACT_get_slice:
	case ACT_insert:
#ifdef PYXIS
	case ACT_item_for:
	case ACT_item_put:
#endif
	case ACT_loop:
#ifdef PYXIS
	case ACT_menu_for:
#endif
	case ACT_modify:
	case ACT_open:
	case ACT_prepare:
	case ACT_procedure:
	case ACT_put_slice:
	case ACT_ready:
	case ACT_release:
	case ACT_rfinish:
	case ACT_rollback:
	case ACT_s_fetch:
	case ACT_s_start:
	case ACT_select:
	case ACT_start:
	case ACT_store:
	case ACT_update:
	case ACT_statistics:
		BEGIN;
	};

	switch (action->act_type) {
	case ACT_alter_domain:
	case ACT_create_domain:
	case ACT_create_generator:
	case ACT_create_shadow:
	case ACT_declare_filter:
	case ACT_declare_udf:
	case ACT_drop_domain:
	case ACT_drop_filter:
	case ACT_drop_shadow:
	case ACT_drop_udf:
	case ACT_statistics:
	case ACT_alter_index:
	case ACT_alter_table:
		gen_ddl(action, column);
		break;
	case ACT_at_end:
		gen_at_end(action, column);
		return;
	case ACT_b_declare:
		gen_database(action, column);
		gen_routine(action, column);
		return;
	case ACT_basedon:
		gen_based(action, column);
		return;
	case ACT_blob_cancel:
		gen_blob_close(action, column);
		return;
	case ACT_blob_close:
		gen_blob_close(action, column);
		break;
	case ACT_blob_create:
		gen_blob_open(action, column);
		break;
	case ACT_blob_for:
		gen_blob_for(action, column);
		return;
	case ACT_blob_handle:
		gen_segment(action, column);
		return;
	case ACT_blob_open:
		gen_blob_open(action, column);
		break;
	case ACT_close:
		gen_s_end(action, column);
		break;
	case ACT_commit:
		gen_trans(action, column);
		break;
	case ACT_commit_retain_context:
		gen_trans(action, column);
		break;
	case ACT_create_database:
		gen_create_database(action, column);
		break;
	case ACT_create_index:
		gen_ddl(action, column);
		break;
	case ACT_create_table:
		gen_ddl(action, column);
		break;
	case ACT_create_view:
		gen_ddl(action, column);
		break;
	case ACT_cursor:
		gen_cursor_init(action, column);
		return;
	case ACT_database:
		gen_database(action, column);
		return;
	case ACT_disconnect:
		gen_finish(action, column);
		break;
	case ACT_drop_database:
		gen_drop_database(action, column);
		break;
	case ACT_drop_index:
		gen_ddl(action, column);
		break;
	case ACT_drop_table:
		gen_ddl(action, column);
		break;
	case ACT_drop_view:
		gen_ddl(action, column);
		break;
	case ACT_dyn_close:
		gen_dyn_close(action, column);
		break;
	case ACT_dyn_cursor:
		gen_dyn_declare(action, column);
		break;
	case ACT_dyn_describe:
		gen_dyn_describe(action, column, false);
		break;
	case ACT_dyn_describe_input:
		gen_dyn_describe(action, column, true);
		break;
	case ACT_dyn_execute:
		gen_dyn_execute(action, column);
		break;
	case ACT_dyn_fetch:
		gen_dyn_fetch(action, column);
		break;
	case ACT_dyn_grant:
		gen_ddl(action, column);
		break;
	case ACT_dyn_immediate:
		gen_dyn_immediate(action, column);
		break;
	case ACT_dyn_insert:
		gen_dyn_insert(action, column);
		break;
	case ACT_dyn_open:
		gen_dyn_open(action, column);
		break;
	case ACT_dyn_prepare:
		gen_dyn_prepare(action, column);
		break;
	case ACT_dyn_revoke:
		gen_ddl(action, column);
		break;
	case ACT_endblob:
		gen_blob_end(action, column);
		return;
	case ACT_enderror:{
			column += INDENT;
			END;
			column -= INDENT;
		}
		break;
	case ACT_endfor:
		gen_endfor(action, column);
		break;
	case ACT_endmodify:
		gen_emodify(action, column);
		break;
	case ACT_endstore:
		gen_estore(action, column);
		break;
	case ACT_erase:
		gen_erase(action, column);
		return;
	case ACT_event_init:
		gen_event_init(action, column);
		break;
	case ACT_event_wait:
		gen_event_wait(action, column);
		break;
	case ACT_fetch:
		gen_fetch(action, column);
		break;
	case ACT_finish:
		gen_finish(action, column);
		break;
	case ACT_for:
		gen_for(action, column);
		return;
#ifdef PYXIS
	case ACT_form_display:
		gen_form_display(action, column);
		break;
	case ACT_form_end:
		gen_form_end(action, column);
		break;
	case ACT_form_for:
		gen_form_for(action, column);
		return;
#endif
	case ACT_get_segment:
		gen_get_segment(action, column);
		break;
	case ACT_get_slice:
		gen_slice(action, column);
		break;
	case ACT_hctef:
		ENDS;
		break;
	case ACT_insert:
		gen_s_start(action, column);
		break;
#ifdef PYXIS
	case ACT_item_for:
	case ACT_item_put:
		gen_item_for(action, column);
		return;
	case ACT_item_end:
		gen_item_end(action, column);
		break;
#endif
	case ACT_loop:
		gen_loop(action, column);
		break;
#ifdef PYXIS
	case ACT_menu:
		gen_menu(action, column);
		return;
	case ACT_menu_display:
		gen_menu_display(action, column);
		return;
	case ACT_menu_end:
		break;
	case ACT_menu_entree:
		gen_menu_entree(action, column);
		return;
	case ACT_menu_for:
		gen_menu_for(action, column);
		return;

	case ACT_title_text:
	case ACT_title_length:
	case ACT_terminator:
	case ACT_entree_text:
	case ACT_entree_length:
	case ACT_entree_value:
		gen_menu_entree_att(action, column);
		return;
#endif
	case ACT_on_error:
		gen_on_error(action, column);
		return;
	case ACT_open:
		gen_s_start(action, column);
		break;
	case ACT_ready:
		gen_ready(action, column);
		break;
	case ACT_put_segment:
		gen_put_segment(action, column);
		break;
	case ACT_put_slice:
		gen_slice(action, column);
		break;
	case ACT_prepare:
		gen_trans(action, column);
		break;
	case ACT_procedure:
		gen_procedure(action, column);
		break;
	case ACT_release:
		gen_release(action, column);
		break;
	case ACT_rfinish:
		gen_finish(action, column);
		break;
	case ACT_rollback:
		gen_trans(action, column);
		break;
	case ACT_routine:
		gen_routine(action, column);
		return;
	case ACT_s_end:
		gen_s_end(action, column);
		return;
	case ACT_s_fetch:
		gen_s_fetch(action, column);
		return;
	case ACT_s_start:
		gen_s_start(action, column);
		break;
	case ACT_segment:
		gen_segment(action, column);
		return;
	case ACT_segment_length:
		gen_segment(action, column);
		return;
	case ACT_sql_dialect:
		sw_sql_dialect = ((SDT) action->act_object)->sdt_dialect;
		return;
	case ACT_select:
		gen_select(action, column);
		break;
	case ACT_start:
		gen_t_start(action, column);
		break;
	case ACT_store:
		gen_store(action, column);
		return;
	case ACT_store2:
		gen_return_value(action, column);
		return;
	case ACT_update:
		gen_update(action, column);
		break;
	case ACT_variable:
		gen_variable(action, column);
		return;
#ifdef PYXIS
	case ACT_window_create:
		gen_window_create(action, column);
		return;
	case ACT_window_delete:
		gen_window_delete(action, column);
		return;
	case ACT_window_suspend:
		gen_window_suspend(action, column);
		return;
#endif
	default:
		return;
	};

//  Put in a trailing brace for those actions still with us 

	if (action->act_flags & ACT_sql)
		gen_whenever(action->act_whenever, column);

	if (action->act_error)
		ib_fprintf(out_file, ";");
	else
		END;
}


//____________________________________________________________
//  
//		Align output to a specific column for output.
//  

static void align( int column)
{
	int i;

	ib_putc('\n', out_file);

	if (column < 0)
		return;

	for (i = column / 8; i; --i)
		ib_putc('\t', out_file);

	for (i = column % 8; i; --i)
		ib_putc(' ', out_file);
}


//____________________________________________________________
//  
//		Build an assignment from a host language variable to
//		a port variable.
//  

static void asgn_from( ACT action, REF reference, int column)
{
	GPRE_FLD field;
	TEXT *value, name[64], variable[20], temp[20];

	for (; reference; reference = reference->ref_next) {
		field = reference->ref_field;
		if (field->fld_array_info)
			if (!(reference->ref_flags & REF_array_elem)) {
				printa(column, "%s := gds__blob_null;\n",
					   gen_name(name, reference, true));
				gen_get_or_put_slice(action, reference, false, column);
				continue;
			}

		if (!reference->ref_source && !reference->ref_value)
			continue;
		align(column);
		gen_name(variable, reference, true);
		if (reference->ref_source)
			value = gen_name(temp, reference->ref_source, true);
		else
			value = reference->ref_value;
		if (reference->ref_value && (reference->ref_flags & REF_array_elem))
			field = field->fld_array;
		if (!field || (field->fld_flags & FLD_text))
			ib_fprintf(out_file, "gds__ftof (%s%s, %s (%s), %s%s, %d);",
					   REF_PAR, value, SIZEOF, value, REF_PAR, variable,
					   field->fld_length);
		else if (!reference->ref_master
				 || (reference->ref_flags & REF_literal)) ib_fprintf(out_file,
																	 "%s := %s;",
																	 variable,
																	 value);
		else {
			ib_fprintf(out_file, "if %s < 0 then", value);
			align(column + 4);
			ib_fprintf(out_file, "%s := -1", variable);
			align(column);
			ib_fprintf(out_file, "else");
			align(column + 4);
			ib_fprintf(out_file, "%s := 0;", variable);
		}
	}
}


//____________________________________________________________
//  
//		Build an assignment from a host language variable to
//		a sqlda variable.
//  

static void asgn_sqlda_from( REF reference, int number, TEXT * string, int column)
{
	TEXT *value, temp[20];

	for (; reference; reference = reference->ref_next) {
		align(column);
		if (reference->ref_source)
			value = gen_name(temp, reference->ref_source, true);
		else
			value = reference->ref_value;
		ib_fprintf(out_file,
				   "gds__to_sqlda (gds__sqlda, %d, %s, %s(%s), %s);", number,
				   SIZEOF, value, value, string);
	}
}


//____________________________________________________________
//  
//		Build an assignment to a host language variable from
//		a port variable.
//  

static void asgn_to(ACT action, REF reference, int column)
{
	GPRE_FLD field;
	REF source;
	TEXT s[128];

	source = reference->ref_friend;
	field = source->ref_field;

	if (field && field->fld_array_info) {
		source->ref_value = reference->ref_value;
		gen_get_or_put_slice(action, source, true, column);
		return;
	}

	if (field && (field->fld_flags & FLD_text))
		ib_fprintf(out_file, "gds__ftof (%s%s, %d, %s%s, %s (%s));",
				   REF_PAR, gen_name(s, source, true),
				   field->fld_length,
				   REF_PAR, reference->ref_value,
				   SIZEOF, reference->ref_value);
	else
		ib_fprintf(out_file, "%s := %s;", reference->ref_value,
				   gen_name(s, source, true));

//  Pick up NULL value if one is there 

	if (reference = reference->ref_null)
		ib_fprintf(out_file, "%s := %s;", reference->ref_value,
				   gen_name(s, reference, true));
}


//____________________________________________________________
//  
//		Build an assignment to a host language variable from
//		a port variable.
//  

static void asgn_to_proc(REF reference, int column)
{
	GPRE_FLD field;
	TEXT s[64];

	for (; reference; reference = reference->ref_next) {
		if (!reference->ref_value)
			continue;
		field = reference->ref_field;
		gen_name(s, reference, true);
		align(column);
		if (field && (field->fld_flags & FLD_text))
			ib_fprintf(out_file, "gds__ftof (%s%s, %d, %s%s, %s (%s));",
					   REF_PAR, gen_name(s, reference, true),
					   field->fld_length,
					   REF_PAR, reference->ref_value,
					   SIZEOF, reference->ref_value);
		else
			ib_fprintf(out_file, "%s := %s;",
					   reference->ref_value, gen_name(s, reference, true));

	}
}


//____________________________________________________________
//  
//		Generate code for AT END clause of FETCH.
//  

static void gen_at_end( ACT action, int column)
{
	GPRE_REQ request;
	TEXT s[20];

	request = action->act_request;
	printa(column, "if %s = 0 then begin",
		   gen_name(s, request->req_eof, true));
}


//____________________________________________________________
//  
//		Substitute for a BASED ON <field name> clause.
//  

static void gen_based( ACT action, int column)
{
	BAS based_on;
	GPRE_FLD field;
	TEXT s[64];
	SSHORT datatype;
	SLONG length;
	DIM dimension;

	align(column);
	based_on = (BAS) action->act_object;
	field = based_on->bas_field;

	if (based_on->bas_flags & BAS_segment) {
		datatype = dtype_text;
		if (!(length = field->fld_seg_length))
			length = 256;
		ib_fprintf(out_file, "%s [1..%"SLONGFORMAT"] of ", PACKED_ARRAY, length);
	}
	else if (field->fld_array_info) {
		datatype = field->fld_array_info->ary_dtype;
		if (datatype <= dtype_varying)
			ib_fprintf(out_file, "%s [", PACKED_ARRAY);
		else
			ib_fprintf(out_file, "array [");

		for (dimension = field->fld_array_info->ary_dimension; dimension;
			 dimension = dimension->dim_next) {
			ib_fprintf(out_file, "%"SLONGFORMAT"..%"SLONGFORMAT, dimension->dim_lower,
					   dimension->dim_upper);
			if (dimension->dim_next)
				ib_fprintf(out_file, ", ");
		}
		if (datatype <= dtype_varying)
			ib_fprintf(out_file, ", 1..%d", field->fld_array->fld_length);

		ib_fprintf(out_file, "] of ");
	}
	else {
		datatype = field->fld_dtype;
		if (datatype <= dtype_varying)
			ib_fprintf(out_file, "%s [1..%d] of ", PACKED_ARRAY,
					   field->fld_length);
	}

	switch (datatype) {
	case dtype_short:
		ib_fprintf(out_file, "%s;", SHORT_DCL);
		break;

	case dtype_long:
		ib_fprintf(out_file, "%s;", LONG_DCL);
		break;

	case dtype_date:
	case dtype_blob:
	case dtype_quad:
		ib_fprintf(out_file, "gds__quad;");
		break;

	case dtype_text:
		ib_fprintf(out_file, "char;");
		break;

	case dtype_float:
		ib_fprintf(out_file, "real;");
		break;

	case dtype_double:
		ib_fprintf(out_file, "double;");
		break;

	default:
		sprintf(s, "datatype %d unknown\n", field->fld_dtype);
		CPR_error(s);
		return;
	}
}


//____________________________________________________________
//  
//		Make a blob FOR loop.
//  

static void gen_blob_close( ACT action, USHORT column)
{
	TEXT *command;
	BLB blob;

	if (action->act_error || (action->act_flags & ACT_sql))
		BEGIN;

	if (action->act_flags & ACT_sql) {
		column = gen_cursor_close(action, action->act_request, column);
		blob = (BLB) action->act_request->req_blobs;
	}
	else
		blob = (BLB) action->act_object;

	command = (action->act_type == ACT_blob_cancel) ? (TEXT*) "CANCEL" : 
	                                                          (TEXT*) "CLOSE";
	printa(column, "GDS__%s_BLOB (%s, gds__%d);",
		   command, status_vector(action), blob->blb_ident);

	if (action->act_flags & ACT_sql) {
		END;
		column -= INDENT;
		ENDS;
		column -= INDENT;
		SET_SQLCODE;
		END;
	}
}


//____________________________________________________________
//  
//		End a blob FOR loop.
//  

static void gen_blob_end(ACT action, USHORT column)
{
	BLB blob;

	blob = (BLB) action->act_object;
#ifdef VMS
	gen_get_segment(action, column + INDENT);
#endif
	printa(column + INDENT, "end;");
	if (action->act_error)
		printa(column, "GDS__CLOSE_BLOB (gds__status2, gds__%d);",
			   blob->blb_ident);
	else
		printa(column, "GDS__CLOSE_BLOB (%s, gds__%d);",
			   status_vector(0), blob->blb_ident);
	if (action->act_error)
		ENDS;
	else
		END;
}


//____________________________________________________________
//  
//		Make a blob FOR loop.
//  

static void gen_blob_for( ACT action, USHORT column)
{

	gen_blob_open(action, column);
	if (action->act_error)
		printa(column, "if (gds__status[2] = 0) then begin");
#ifdef VMS
	gen_get_segment(action, column + INDENT);
	printa(column + INDENT,
		   "while ((gds__status[2] = 0) or (gds__status[2] = gds__segment)) do");
	printa(column + INDENT, "begin");
#else
	printa(column, "while (true) do begin");
	gen_get_segment(action, column + INDENT);
	printa(column + INDENT,
		   "if ((gds__status[2] <> 0) and (gds__status[2] <> gds__segment)) then");
	printa(column + 2 * INDENT, "exit;");
#endif
}


//____________________________________________________________
//  
//		Generate the call to open (or create) a blob.
//  

static void gen_blob_open( ACT action, USHORT column)
{
	BLB blob;
	PAT args;
	REF reference;
	TEXT s[20];
	TEXT *pattern1 =
		"GDS__%IFCREATE%ELOPEN%EN_BLOB2 (%V1, %RF%DH, %RF%RT, %RF%BH, %RF%FR, %N1, %RF%I1);",
		*pattern2 =
		"GDS__%IFCREATE%ELOPEN%EN_BLOB2 (%V1, %RF%DH, %RF%RT, %RF%BH, %RF%FR, 0, 0);";

	if (sw_auto && (action->act_flags & ACT_sql)) {
		t_start_auto(action, action->act_request, status_vector(action),
					 column);
		printa(column, "if %s <> nil then",
			   request_trans(action, action->act_request));
		column += INDENT;
	}

	if ((action->act_error && (action->act_type != ACT_blob_for)) ||
		action->act_flags & ACT_sql)
		BEGIN;

	if (action->act_flags & ACT_sql) {
		column = gen_cursor_open(action, action->act_request, column);
		blob = (BLB) action->act_request->req_blobs;
		reference = ((OPN) action->act_object)->opn_using;
		gen_name(s, reference, true);
	}
	else {
		blob = (BLB) action->act_object;
		reference = blob->blb_reference;
	}

	args.pat_condition = action->act_type == ACT_blob_create;	/*  open or create blob  */
	args.pat_vector1 = status_vector(action);	/*  status vector        */
	args.pat_database = blob->blb_request->req_database;	/*  database handle      */
	args.pat_request = blob->blb_request;	/*  transaction handle   */
	args.pat_blob = blob;		/*  blob handle          */
	args.pat_reference = reference;	/*  blob identifier      */
	args.pat_ident1 = blob->blb_bpb_ident;

	if ((action->act_flags & ACT_sql) && action->act_type == ACT_blob_open)
		printa(column, "%s := %s;", s, reference->ref_value);

	if (args.pat_value1 = blob->blb_bpb_length)
		PATTERN_expand(column, pattern1, &args);
	else
		PATTERN_expand(column, pattern2, &args);

	if (action->act_flags & ACT_sql) {
		END;
		column -= INDENT;
		END;
		column -= INDENT;
		ENDS;
		column -= INDENT;
		if (sw_auto) {
			END;
			column -= INDENT;
		}
		SET_SQLCODE;
		if (action->act_type == ACT_blob_create) {
			printa(column, "if SQLCODE = 0 then");
			align(column + INDENT);
			ib_fprintf(out_file, "%s := %s;", reference->ref_value, s);
		}
	}
}


//____________________________________________________________
//  
//		Callback routine for BLR pretty printer.
//  

static int gen_blr( int *user_arg, int offset, TEXT * string)
{
	int indent, length;
	TEXT *p, *q, c;
	bool open_quote;
	bool first_line = true;

	indent = 2 * INDENT;
	p = string;
	while (*p == ' ') {
		p++;
		indent++;
	}

//  Limit indentation to 192 characters 

	indent = MIN(indent, 192);

	length = strlen(p);
	while (length + indent > 255) {
		/* if we did not find somewhere to break between the 200th and 256th character
		   just print out 256 characters */

		for (q = p, open_quote = false; (q - p + indent) < 255; q++) {
			if ((q - p + indent) > 220 && *q == ',' && !open_quote)
				break;
			if (*q == '\'' && *(q - 1) != '\\')
				open_quote = !open_quote;
		}
		c = *++q;
		*q = 0;
		printa(indent, p);
		*q = c;
		length = length - (q - p);
		p = q;
		if (first_line) {
			indent = MIN(indent + INDENT, 192);
			first_line = false;
		}
	}
	printa(indent, p);
	return TRUE;
}


//____________________________________________________________
//  
//		Generate text to compile a request.
//  

static void gen_compile( ACT action, int column)
{
	GPRE_REQ request;
	DBB db;
	SYM symbol;
	TEXT *filename;
	BLB blob;

	request = action->act_request;
	db = request->req_database;
	symbol = db->dbb_name;

	if (sw_auto) {
		if ((filename = db->dbb_runtime) || !(db->dbb_flags & DBB_sqlca)) {
			printa(column, "if %s = nil then", symbol->sym_string);
			make_ready(db, filename, status_vector(action), column + INDENT,
					   0);
		}
		if (action->act_error || (action->act_flags & ACT_sql))
			printa(column, "if (%s = nil) and (%s <> nil) then",
				   request_trans(action, request), symbol->sym_string);
		else
			printa(column, "if %s = nil then",
				   request_trans(action, request));
		t_start_auto(action, request, status_vector(action), column + INDENT);
	}

	if ((action->act_error || (action->act_flags & ACT_sql)) && sw_auto)
		printa(column, "if (%s = nil) and (%s <> nil) then",
			   request->req_handle, request_trans(action, request));
	else
		printa(column, "if %s = nil then", request->req_handle);

	align(column + INDENT);
	ib_fprintf(out_file,
			   "GDS__COMPILE_REQUEST%s (%s, %s, %s, %d, gds__%d);\n",
			   (request->req_flags & REQ_exp_hand) ? "" : "2",
			   status_vector(action), symbol->sym_string, request->req_handle,
			   request->req_length, request->req_ident);

//  If blobs are present, zero out all of the blob handles.  After this
//  point, the handles are the user's responsibility 

	for (blob = request->req_blobs; blob; blob = blob->blb_next)
		printa(column - INDENT, "gds__%d := nil;", blob->blb_ident);
}


//____________________________________________________________
//  
//		Generate a call to create a database.
//  

static void gen_create_database( ACT action, int column)
{
	GPRE_REQ request;
	DBB db;

	request = ((MDBB) action->act_object)->mdbb_dpb_request;
	db = (DBB) request->req_database;
	align(column);

	if (request->req_length)
		ib_fprintf(out_file,
				   "GDS__CREATE_DATABASE (%s, %d, '%s', %s, %d, gds__%d, 0);",
				   status_vector(action), strlen(db->dbb_filename),
				   db->dbb_filename, db->dbb_name->sym_string,
				   request->req_length, request->req_ident);
	else
		ib_fprintf(out_file,
				   "GDS__CREATE_DATABASE (%s, %d, '%s', %s, 0, 0, 0);",
				   status_vector(action), strlen(db->dbb_filename),
				   db->dbb_filename, db->dbb_name->sym_string);

	bool save_sw_auto = sw_auto;
	sw_auto = true;
	printa(column, "if (gds__status [2] = 0) then");
	BEGIN;
	gen_ddl(action, column);
	ENDS;
	sw_auto = save_sw_auto;
	column -= INDENT;
	SET_SQLCODE;
}


//____________________________________________________________
//  
//		Generate substitution text for END_STREAM.
//  

static int gen_cursor_close( ACT action, GPRE_REQ request, int column)
{
	PAT args;
	TEXT *pattern1 = "if %RIs <> nil then";
	TEXT *pattern2 = "isc_dsql_free_statement (%V1, %RF%RIs, %N1);";

	args.pat_request = request;
	args.pat_vector1 = status_vector(action);
	args.pat_value1 = 1;

	PATTERN_expand(column, pattern1, &args);
	column += INDENT;
	BEGIN;
	PATTERN_expand(column, pattern2, &args);
	printa(column, "if (gds__status[2] = 0) then");
	column += INDENT;
	BEGIN;

	return column;
}


//____________________________________________________________
//  
//		Generate text to initialize a cursor.
//  

static void gen_cursor_init( ACT action, int column)
{

//  If blobs are present, zero out all of the blob handles.  After this
//  point, the handles are the user's responsibility 

	if (action->act_request->
		req_flags & (REQ_sql_blob_open | REQ_sql_blob_create)) printa(column,
																	  "gds__%d := nil;",
																	  action->
																	  act_request->
																	  req_blobs->
																	  blb_ident);
}


//____________________________________________________________
//  
//		Generate text to open an embedded SQL cursor.
//  

static int gen_cursor_open( ACT action, GPRE_REQ request, int column)
{
	PAT args;
	TEXT s[64];
	TEXT *pattern1 =
		"if (%RIs = nil) and (%RH <> nil)%IF and (%DH <> nil)%EN then",
		*pattern2 = "if (%RIs = nil)%IF and (%DH <> nil)%EN then", *pattern3 =
		"isc_dsql_alloc_statement2 (%V1, %RF%DH, %RF%RIs);", *pattern4 =
		"if (%RIs <> nil)%IF and (%S3 <> nil)%EN then", *pattern5 =
		"isc_dsql_set_cursor_name (%V1, %RF%RIs, %S1, 0);", *pattern6 =
		"isc_dsql_execute_m (%V1, %RF%S3, %RF%RIs, 0, gds__null, %N2, 0, gds__null);";

	args.pat_request = request;
	args.pat_database = request->req_database;
	args.pat_vector1 = status_vector(action);
	args.pat_condition = sw_auto && TRUE;
	args.pat_string1 = make_name(s, ((OPN) action->act_object)->opn_cursor);
	args.pat_string3 = request_trans(action, request);
	args.pat_value2 = -1;

	PATTERN_expand(column,
				   (action->act_type == ACT_open) ? pattern1 : pattern2,
				   &args);
	PATTERN_expand(column + INDENT, pattern3, &args);
	PATTERN_expand(column, pattern4, &args);
	column += INDENT;
	BEGIN;
	PATTERN_expand(column, pattern5, &args);
	printa(column, "if (gds__status[2] = 0) then");
	column += INDENT;
	BEGIN;
	PATTERN_expand(column, pattern6, &args);
	printa(column, "if (gds__status[2] = 0) then");
	column += INDENT;
	BEGIN;

	return column;
}


//____________________________________________________________
//  
//		Generate insertion text for the database statement,
//		including the definitions of all requests, and blob
//		ans port declarations for requests in the main routine.
//  

static void gen_database( ACT action, int column)
{
	DBB db;
	GPRE_REQ request;
	POR port;
	BLB blob;
	USHORT count;
	TPB tpb_val;
#ifdef PYXIS
	FORM form;
#endif
	int indent;
	REF reference;
	SSHORT event_count;
	SSHORT max_count;
	LLS stack_ptr;

	if (first_flag++ != 0)
		return;

	ib_fprintf(out_file, "\n(**** GPRE Preprocessor Definitions ****)\n");
#ifdef VMS
	ib_fprintf(out_file, "%%include 'interbase:[syslib]gds.pas'\n");
#else
	ib_fprintf(out_file, "%%include '/interbase/include/gds.ins.pas';\n");
#endif

	indent = column + INDENT;
	bool flag = true;

	for (request = requests; request; request = request->req_next) {
		if (request->req_flags & REQ_local)
			continue;
		for (port = request->req_ports; port; port = port->por_next) {
			if (flag) {
				flag = false;
				ib_fprintf(out_file, "type");
			}
			make_port(port, indent);
		}
	}
	ib_fprintf(out_file, "\nvar");
#ifdef PYXIS
	for (db = isc_databases; db; db = db->dbb_next)
		for (form = db->dbb_forms; form; form = form->form_next)
			printa(indent, "%s\t\t: %s gds__handle := nil;\t\t(* form %s *)",
				   form->form_handle, STATIC_STRING,
				   form->form_name->sym_string);
#endif
	for (request = requests; request; request = request->req_routine) {
		if (request->req_flags & REQ_local)
			continue;
		for (port = request->req_ports; port; port = port->por_next)
			printa(indent, "gds__%d\t: gds__%dt;\t\t\t(* message *)",
				   port->por_ident, port->por_ident);

		for (blob = request->req_blobs; blob; blob = blob->blb_next) {
			printa(indent, "gds__%d\t: gds__handle;\t\t\t(* blob handle *)",
				   blob->blb_ident);
			printa(indent,
				   "gds__%d\t: %s [1 .. %d] of char;\t(* blob segment *)",
				   blob->blb_buff_ident, PACKED_ARRAY, blob->blb_seg_length);
			printa(indent, "gds__%d\t: %s;\t\t\t(* segment length *)",
				   blob->blb_len_ident, SHORT_DCL);
		}
	}

	bool all_static = true;
	bool all_extern = true;

	for (db = isc_databases; db; db = db->dbb_next) {
		all_static = all_static && (db->dbb_scope == DBB_STATIC);
		all_extern = all_extern && (db->dbb_scope == DBB_EXTERN);
		if (db->dbb_scope == DBB_STATIC)
			printa(indent,
				   "%s\t: %s gds__handle\t:= nil;	(* database handle *)",
				   db->dbb_name->sym_string, STATIC_STRING);
	}

	count = 0;
	for (db = isc_databases; db; db = db->dbb_next) {
		count++;
		for (tpb_val = db->dbb_tpbs; tpb_val; tpb_val = tpb_val->tpb_dbb_next)
			gen_tpb(tpb_val, indent);
	}

	printa(indent,
		   "gds__teb\t: array [1..%d] of gds__teb_t;\t(* transaction vector *)",
		   count);

//  generate event parameter block for each event in module 

	max_count = 0;
	for (stack_ptr = events; stack_ptr; stack_ptr = stack_ptr->lls_next) {
		event_count = gen_event_block(reinterpret_cast<act*>(stack_ptr->lls_object));
		max_count = MAX(event_count, max_count);
	}

	if (max_count) {
		printa(indent,
			   "gds__events\t\t: %s array [1..%d] of %s;\t\t(* event vector *)",
			   STATIC_STRING, max_count, LONG_DCL);
		printa(indent,
			   "gds__event_names\t\t: %s array [1..%d] of %s;\t\t(* event buffer *)",
			   STATIC_STRING, max_count, POINTER_DCL);
		printa(indent,
			   "gds__event_names2\t\t: %s %s [1..%d, 1..31] of char;\t\t(* event string buffer *)",
			   STATIC_STRING, PACKED_ARRAY, max_count);
	}

	bool array_flag = false;
	for (request = requests; request; request = request->req_next) {
		gen_request(request, indent);
		/*  Array declarations  */
		if (request->req_type == REQ_slice)
			array_flag = true;

		if (port = request->req_primary)
			for (reference = port->por_references; reference;
				 reference =
				 reference->ref_next) if (reference->
										  ref_flags & REF_fetch_array) {
					make_array_declaration(reference);
					array_flag = true;
				}
	}

#ifdef VMS
	if (array_flag)
		printa(indent,
			   "gds__array_length\t: integer;\t\t\t(* slice return value *)");
#else
	if (array_flag)
		printa(indent,
			   "gds__array_length\t: integer32;\t\t\t(* slice return value *)");
#endif

	printa(indent,
		   "gds__null\t\t: ^gds__status_vector := nil;\t(* null status vector *)");
	printa(indent,
		   "gds__blob_null\t: gds__quad := %s0,0%s;\t\t(* null blob id *)",
		   OPEN_BRACKET, CLOSE_BRACKET);

#ifdef VMS
	if (all_static) {
		printa(indent,
			   "gds__trans\t\t: %s gds__handle := nil;\t\t(* default transaction *)",
			   STATIC_STRING);
		printa(indent,
			   "gds__status\t\t: %s gds__status_vector;\t\t(* status vector *)",
			   STATIC_STRING);
		printa(indent,
			   "gds__status2\t\t: %s gds__status_vector;\t\t(* status vector *)",
			   STATIC_STRING);
		printa(indent,
			   "SQLCODE\t: %s integer := 0;\t\t\t(* SQL status code *)",
			   STATIC_STRING);
#ifdef PYXIS
		if (sw_pyxis) {
			printa(indent,
				   "gds__window\t\t: [COMMON (gds__window)] gds__handle;\t\t(* window handle *)");
			printa(indent,
				   "gds__width\t\t: [COMMON (gds__width)] %s;\t(* window width *)",
				   SHORT_DCL);
			printa(indent,
				   "gds__height\t\t: [COMMON (gds__height)] %s;\t(* window height *)",
				   SHORT_DCL);
		}
#endif
	}
	else if (all_extern) {
		printa(indent,
			   "gds__trans\t\t: [COMMON (gds__trans)] gds__handle;\t\t(* default transaction *)");
		printa(indent,
			   "gds__status\t\t: [COMMON (gds__status)] gds__status_vector;\t\t(* status vector *)");
		printa(indent,
			   "gds__status2\t\t: [COMMON (gds__status2)] gds__status_vector;\t\t(* status vector *)");
		printa(indent,
			   "SQLCODE\t: [COMMON (SQLCODE)] integer;\t\t\t(* SQL status code *)");
	}
	else {
		printa(indent,
			   "gds__trans\t\t: [COMMON (gds__trans)] gds__handle := nil;\t\t(* default transaction *)");
		printa(indent,
			   "gds__status\t\t: [COMMON (gds__status)] gds__status_vector;\t\t(* status vector *)");
		printa(indent,
			   "gds__status2\t\t: [COMMON (gds__status2)] gds__status_vector;\t\t(* status vector *)");
		printa(indent,
			   "SQLCODE\t: [COMMON (SQLCODE)] integer := 0;\t\t\t(* SQL status code *)");
	}
	printa(indent,
		   "gds__window\t\t: [COMMON (gds__window)] gds__handle := nil;\t\t(* window handle *)");
	printa(indent,
		   "gds__width\t\t: [COMMON (gds__width)] %s := 80;\t(* window width *)",
		   SHORT_DCL);
	printa(indent,
		   "gds__height\t\t: [COMMON (gds__height)] %s := 24;\t(* window height *)",
		   SHORT_DCL);
#else
	if (all_static) {
		printa(indent,
			   "gds__trans\t\t: %s gds__handle := nil;\t\t(* default transaction *)",
			   STATIC_STRING);
		printa(indent,
			   "gds__status\t\t: %s gds__status_vector;\t\t(* status vector *)",
			   STATIC_STRING);
		printa(indent,
			   "gds__status2\t\t: %s gds__status_vector;\t\t(* status vector *)",
			   STATIC_STRING);
		printa(indent,
			   "SQLCODE\t: %s integer := 0;\t\t\t(* SQL status code *)",
			   STATIC_STRING);
	}
	else {
		printa(column, "\nvar (gds__trans)");
		printa(indent,
			   "gds__trans\t\t: gds__handle%s;\t\t(* default transaction *)",
			   (all_extern) ? "" : "\t:= nil");
		printa(column, "\nvar (gds__status)");
		printa(indent,
			   "gds__status\t\t: gds__status_vector;\t\t(* status vector *)");
		printa(column, "\nvar (gds__status2)");
		printa(indent,
			   "gds__status2\t\t: gds__status_vector;\t\t(* status vector *)");
		printa(column, "\nvar (SQLCODE)");
		printa(indent, "SQLCODE\t: integer%s;\t\t\t(* SQL status code *)",
			   (all_extern) ? "" : "\t:= 0");
	}
	printa(column, "\nvar (gds__window)");
	printa(indent,
		   "gds__window\t\t:  gds__handle := nil;\t\t(* window handle *)");
	printa(column, "\nvar (gds__width)");
	printa(indent, "gds__width\t\t: %s := 80;\t(* window width *)",
		   SHORT_DCL);
	printa(column, "\nvar (gds__height)");
	printa(indent, "gds__height\t\t: %s := 24;\t(* window height *)",
		   SHORT_DCL);
#endif

	for (db = isc_databases; db; db = db->dbb_next) {
		if (db->dbb_scope != DBB_STATIC) {
#ifdef VMS
			printa(indent,
				   "%s\t: [COMMON (%s)] gds__handle%s;	(* database handle *)",
				   db->dbb_name->sym_string, db->dbb_name->sym_string,
				   (db->dbb_scope == DBB_EXTERN) ? "" : "\t:= nil");
#else
			printa(column, "\nvar (%s)", db->dbb_name->sym_string);
			printa(indent, "%s\t: gds__handle%s;	(* database handle *)",
				   db->dbb_name->sym_string,
				   (db->dbb_scope == DBB_EXTERN) ? "" : "\t:= nil");
#endif
		}
	}

	printa(column, " ");
	printa(column, "(**** end of GPRE definitions ****)");
}


//____________________________________________________________
//  
//		Generate a call to update metadata.
//  

static void gen_ddl( ACT action, int column)
{
	GPRE_REQ request;

	if (sw_auto) {
		printa(column, "if (gds__trans = nil) then");
		column += INDENT;
		t_start_auto(action, 0, status_vector(action), column + INDENT);
		column -= INDENT;
	}

//  Set up command type for call to RDB$DDL 

	request = action->act_request;

	if (sw_auto) {
		printa(column, "if (gds__trans <> nil) then");
		column += INDENT;
	}

	align(column);
	ib_fprintf(out_file, "GDS__DDL (%s, %s, gds__trans, %d, gds__%d);",
			   status_vector(action),
			   request->req_database->dbb_name->sym_string,
			   request->req_length, request->req_ident);

	if (sw_auto) {
		column -= INDENT;
		printa(column, "if (gds__status [2] = 0) then");
		printa(column + INDENT, "GDS__COMMIT_TRANSACTION (%s, gds__trans);",
			   status_vector(action));
		printa(column, "if (gds__status [2] <> 0) then");
		printa(column + INDENT,
			   "GDS__ROLLBACK_TRANSACTION (gds__null^ , gds__trans);");
	}

	SET_SQLCODE;
}


//____________________________________________________________
//  
//		Generate a call to create a database.
//  

static void gen_drop_database( ACT action, int column)
{
	DBB db;

	db = (DBB) action->act_object;
	align(column);

	ib_fprintf(out_file,
			   "GDS__DROP_DATABASE (%s, %d, '%s', RDB$K_DB_TYPE_GDS);",
			   status_vector(action),
			   strlen(db->dbb_filename), db->dbb_filename);
	SET_SQLCODE;
}


//____________________________________________________________
//  
//		Generate a dynamic SQL statement.
//  

static void gen_dyn_close( ACT action, int column)
{
	DYN statement;
	TEXT s[64];

	statement = (DYN) action->act_object;
	printa(column,
		   "isc_embed_dsql_close (gds__status, %s);",
		   make_name(s, statement->dyn_cursor_name));
	SET_SQLCODE;
}


//____________________________________________________________
//  
//		Generate a dynamic SQL statement.
//  

static void gen_dyn_declare( ACT action, int column)
{
	DYN statement;
	TEXT s1[64], s2[64];

	statement = (DYN) action->act_object;
	printa(column,
		   "isc_embed_dsql_declare (gds__status, %s, %s);",
		   make_name(s1, statement->dyn_statement_name),
		   make_name(s2, statement->dyn_cursor_name));
	SET_SQLCODE;
}


//____________________________________________________________
//  
//		Generate a dynamic SQL statement.
//  

static void gen_dyn_describe(ACT action,
							 int column,
							 bool bind_flag)
{
	DYN statement;
	TEXT s[64];

	statement = (DYN) action->act_object;
	printa(column,
		   "isc_embed_dsql_describe%s (gds__status, %s, %d, %s %s);",
		   bind_flag ? "_bind" : "",
		   make_name(s, statement->dyn_statement_name),
		   sw_sql_dialect, REF_PAR, statement->dyn_sqlda);
	SET_SQLCODE;
}


//____________________________________________________________
//  
//		Generate a dynamic SQL statement.
//  

static void gen_dyn_execute( ACT action, int column)
{
	DYN statement;
	TEXT *transaction, s[64];
	gpre_req* request;
	gpre_req req_const;
	GPRE_NOD var_list;
	int i;

	statement = (DYN) action->act_object;
	if (statement->dyn_trans) {
		transaction = statement->dyn_trans;
		request = &req_const;
		request->req_trans = transaction;
	}
	else {
		transaction = "gds__trans";
		request = NULL;
	}

	if (sw_auto) {
		printa(column, "if (%s = nil) then", transaction);
		column += INDENT;
		t_start_auto(action, request, status_vector(action), column + INDENT);
		column -= INDENT;
	}

	make_name(s, statement->dyn_cursor_name);

	if (var_list = statement->dyn_using) {
		printa(column, "gds__sqlda.sqln = %s;", sw_dyn_using);
		printa(column, "gds__sqlda.sqld = %s;", sw_dyn_using);
		for (i = 0; i < var_list->nod_count; i++)
			asgn_sqlda_from(reinterpret_cast<ref*>(var_list->nod_arg[i]), i, s, column);
	}

	printa(column,
		   (statement->dyn_sqlda2) ?
		   (TEXT*) "isc_embed_dsql_execute2 (gds__status, %s, %s, %d, %s %s, %s %s);"
		   : (TEXT*) "isc_embed_dsql_execute (gds__status, %s, %s, %d, %s %s);",
		   transaction, make_name(s, statement->dyn_statement_name),
		   sw_sql_dialect, REF_PAR,
		   (statement->dyn_sqlda) ? statement->dyn_sqlda : "gds__null^",
		   REF_PAR,
		   (statement->dyn_sqlda2) ? statement->dyn_sqlda2 : "gds__null^");
	SET_SQLCODE;
}


//____________________________________________________________
//  
//		Generate a dynamic SQL statement.
//  

static void gen_dyn_fetch( ACT action, int column)
{
	DYN statement;
	TEXT s[64];

	statement = (DYN) action->act_object;
	printa(column,
		   "SQLCODE := isc_embed_dsql_fetch (gds__status, %s, %d, %s %s);",
		   make_name(s, statement->dyn_cursor_name),
		   sw_sql_dialect,
		   REF_PAR,
		   (statement->dyn_sqlda) ? statement->dyn_sqlda : "gds__null^");
	printa(column,
		   "if sqlcode <> 100 then sqlcode := gds__sqlcode (gds__status);");
}


//____________________________________________________________
//  
//		Generate code for an EXECUTE IMMEDIATE dynamic SQL statement.
//  

static void gen_dyn_immediate( ACT action, int column)
{
	DYN statement;
	DBB database;
	TEXT *transaction;
	gpre_req* request;
	gpre_req req_const;

	statement = (DYN) action->act_object;
	if (statement->dyn_trans) {
		transaction = statement->dyn_trans;
		request = &req_const;
		request->req_trans = transaction;
	}
	else {
		transaction = "gds__trans";
		request = NULL;
	}

	if (sw_auto) {
		printa(column, "if (%s = nil) then", transaction);
		column += INDENT;
		t_start_auto(action, request, status_vector(action), column + INDENT);
		column -= INDENT;
	}

	database = statement->dyn_database;
	printa(column,
		   (statement->dyn_sqlda2) ?
		   (TEXT*) "isc_embed_dsql_execute_immed2 (gds__status, %s, %s, %s(%s), %s, %d, %s %s, %s %s);"
		   :
		   (TEXT*) "isc_embed_dsql_execute_immed (gds__status, %s, %s, %s(%s), %s, %d, %s %s);",
		   database->dbb_name->sym_string, transaction, SIZEOF,
		   statement->dyn_string, statement->dyn_string, sw_sql_dialect,
		   REF_PAR,
		   (statement->dyn_sqlda) ? statement->dyn_sqlda : "gds__null^",
		   REF_PAR,
		   (statement->dyn_sqlda2) ? statement->dyn_sqlda2 : "gds__null^");
	SET_SQLCODE;
}


//____________________________________________________________
//  
//		Generate a dynamic SQL statement.
//  

static void gen_dyn_insert( ACT action, int column)
{
	DYN statement;
	TEXT s[64];

	statement = (DYN) action->act_object;
	printa(column,
		   "isc_embed_dsql_insert (gds__status, %s, %d, %s %s);",
		   make_name(s, statement->dyn_cursor_name),
		   sw_sql_dialect,
		   REF_PAR,
		   (statement->dyn_sqlda) ? statement->dyn_sqlda : "gds__null^");

	SET_SQLCODE;
}


//____________________________________________________________
//  
//		Generate a dynamic SQL statement.
//  

static void gen_dyn_open( ACT action, int column)
{
	DYN statement;
	TEXT *transaction, s[64];
	gpre_req* request;
	gpre_req req_const;
	GPRE_NOD var_list;
	int i;

	statement = (DYN) action->act_object;
	if (statement->dyn_trans) {
		transaction = statement->dyn_trans;
		request = &req_const;
		request->req_trans = transaction;
	}
	else {
		transaction = "gds__trans";
		request = NULL;
	}

	if (sw_auto) {
		printa(column, "if (%s = nil) then", transaction);
		column += INDENT;
		t_start_auto(action, request, status_vector(action), column + INDENT);
		column -= INDENT;
	}

	make_name(s, statement->dyn_cursor_name);

	if (var_list = statement->dyn_using) {
		printa(column, "gds__sqlda.sqln = %d;", sw_dyn_using);
		printa(column, "gds__sqlda.sqld = %d;", sw_dyn_using);
		for (i = 0; i < var_list->nod_count; i++)
			asgn_sqlda_from(reinterpret_cast<ref*>(var_list->nod_arg[i]), i, s, column);
	}

	printa(column,
		   (statement->dyn_sqlda2) ?
		   (TEXT*) "isc_embed_dsql_open2 (gds__status, %s, %s, %d, %s %s, %s %s);" :
		   (TEXT*) "isc_embed_dsql_open (gds__status, %s, %s, %d, %s %s);",
		   s,
		   transaction,
		   sw_sql_dialect,
		   REF_PAR,
		   (statement->dyn_sqlda) ? statement->dyn_sqlda : "gds__null^",
		   REF_PAR,
		   (statement->dyn_sqlda2) ? statement->dyn_sqlda2 : "gds__null^");
	SET_SQLCODE;
}


//____________________________________________________________
//  
//		Generate a dynamic SQL statement.
//  

static void gen_dyn_prepare( ACT action, int column)
{
	TEXT s[64], *transaction;
	gpre_req* request;
	gpre_req req_const;

    DYN statement = (DYN) action->act_object;
	if (statement->dyn_trans) {
		transaction = statement->dyn_trans;
		request = &req_const;
		request->req_trans = transaction;
	}
	else {
		transaction = "gds__trans";
		request = NULL;
	}

	if (sw_auto) {
		printa(column, "if (%s = nil) then", transaction);
		column += INDENT;
		t_start_auto(action, request, status_vector(action), column + INDENT);
		column -= INDENT;
	}

	DBB database = statement->dyn_database;

	printa(column,
		   "isc_embed_dsql_prepare (gds__status, %s, transaction, %s, %s(%s), %s, %d, %s %s);",
		   database->dbb_name->sym_string, transaction, make_name(s,
																  statement->
																  dyn_statement_name),
		   SIZEOF, statement->dyn_string, statement->dyn_string,
		   sw_sql_dialect, REF_PAR,
		   (statement->dyn_sqlda) ? statement->dyn_sqlda : "gds__null^");
	SET_SQLCODE;
}


//____________________________________________________________
//  
//		Generate substitution text for END_MODIFY.
//  

static void gen_emodify( ACT action, int column)
{
	UPD modify;
	REF reference, source;
	GPRE_FLD field;
	TEXT s1[20], s2[20];

	modify = (UPD) action->act_object;

	for (reference = modify->upd_port->por_references; reference;
		 reference = reference->ref_next) {
		if (!(source = reference->ref_source))
			continue;
		field = reference->ref_field;
		align(column);
		ib_fprintf(out_file, "%s := %s;",
				   gen_name(s1, reference, true), gen_name(s2, source, true));
		if (field->fld_array_info)
			gen_get_or_put_slice(action, reference, false, column);
	}

	gen_send(action, modify->upd_port, column);
}


//____________________________________________________________
//  
//		Generate substitution text for END_STORE.
//  

static void gen_estore( ACT action, int column)
{
	GPRE_REQ request;

	request = action->act_request;
//  if we did a store ... returning_values aka store2
//  just wrap up pending error 
	if (request->req_type == REQ_store2) {
		if (action->act_error || (action->act_flags & ACT_sql))
			END;
		return;
	}
	if (action->act_error)
		column += INDENT;
	gen_start(action, request->req_primary, column);
	if (action->act_error || (action->act_flags & ACT_sql))
		END;
}


//____________________________________________________________
//  
//		Generate definitions associated with a single request.
//  

static void gen_endfor( ACT action, int column)
{
	GPRE_REQ request;

	request = action->act_request;
	column += INDENT;

	if (request->req_sync)
		gen_send(action, request->req_sync, column);

	gen_receive(action, column, request->req_primary);

	END;
	if (action->act_error || (action->act_flags & ACT_sql))
		END;
}


//____________________________________________________________
//  
//		Generate substitution text for ERASE.
//  

static void gen_erase( ACT action, int column)
{
	UPD erase;

	if (action->act_error || (action->act_flags & ACT_sql))
		BEGIN;

	erase = (UPD) action->act_object;
	gen_send(action, erase->upd_port, column);

	if (action->act_flags & ACT_sql)
		END;
}


//____________________________________________________________
//  
//		Generate event parameter blocks for use
//		with a particular call to gds__event_wait.
//  

static SSHORT gen_event_block( ACT action)
{
	GPRE_NOD init, list;
	int ident;

	init = (GPRE_NOD) action->act_object;

	ident = CMP_next_ident();
	init->nod_arg[2] = (GPRE_NOD) ident;

	printa(INDENT, "gds__%da\t\t: ^char;\t\t(* event parameter block *)",
		   ident);
	printa(INDENT, "gds__%db\t\t: ^char;\t\t(* result parameter block *)",
		   ident);
	printa(INDENT,
		   "gds__%dl\t\t: %s;\t\t(* length of event parameter block *)",
		   ident, SHORT_DCL);

	list = init->nod_arg[1];
	return list->nod_count;
}


//____________________________________________________________
//  
//		Generate substitution text for EVENT_INIT.
//  

static void gen_event_init( ACT action, int column)
{
	GPRE_NOD init, event_list, *ptr, *end, node;
	REF reference;
	PAT args;
	SSHORT count;
	TEXT variable[20];
	TEXT *pattern1 =
		"gds__%N1l := GDS__EVENT_BLOCK_A (%RFgds__%N1a, %RFgds__%N1b, %N2, %RFgds__event_names%RE);";
	TEXT *pattern2 = "%S1 (%V1, %RF%DH, gds__%N1l, gds__%N1a, gds__%N1b);";
	TEXT *pattern3 = "%S2 (gds__events, gds__%N1l, gds__%N1a, gds__%N1b);";

	if (action->act_error)
		BEGIN;
	BEGIN;

	init = (GPRE_NOD) action->act_object;
	event_list = init->nod_arg[1];

	args.pat_database = (DBB) init->nod_arg[3];
	args.pat_vector1 = status_vector(action);
	args.pat_value1 = (int) init->nod_arg[2];
	args.pat_value2 = (int) event_list->nod_count;
	args.pat_string1 = GDS_EVENT_WAIT;
	args.pat_string2 = GDS_EVENT_COUNTS;

//  generate call to dynamically generate event blocks 

	for (ptr = event_list->nod_arg, count = 0, end =
		 ptr + event_list->nod_count; ptr < end; ptr++) {
		count++;
		node = *ptr;
		if (node->nod_type == nod_field) {
			reference = (REF) node->nod_arg[0];
			gen_name(variable, reference, true);
			printa(column, "gds__event_names2[%d] := %s;", count, variable);
		}
		else
			printa(column, "gds__event_names2[%d] := %s;", count,
				   node->nod_arg[0]);

		printa(column, "gds__event_names[%d] := %s (gds__event_names2[%d]);",
			   count, ISC_BADDRESS, count);
	}

	PATTERN_expand(column, pattern1, &args);

//  generate actual call to event_wait 

	PATTERN_expand(column, pattern2, &args);

//  get change in event counts, copying event parameter block for reuse 

	PATTERN_expand(column, pattern3, &args);

	if (action->act_error)
		END;
	SET_SQLCODE;
}


//____________________________________________________________
//  
//		Generate substitution text for EVENT_WAIT.
//  

static void gen_event_wait( ACT action, int column)
{
	PAT args;
	GPRE_NOD event_init;
	SYM event_name, stack_name;
	DBB database;
	LLS stack_ptr;
	ACT event_action;
	int ident;
	TEXT s[64];
	TEXT *pattern1 = "%S1 (%V1, %RF%DH, gds__%N1l, gds__%N1a, gds__%N1b);";
	TEXT *pattern2 = "%S2 (gds__events, gds__%N1l, gds__%N1a, gds__%N1b);";

	if (action->act_error)
		BEGIN;
	BEGIN;

	event_name = (SYM) action->act_object;

//  go through the stack of events, checking to see if the
//  event has been initialized and getting the event identifier 

	ident = -1;
	for (stack_ptr = events; stack_ptr; stack_ptr = stack_ptr->lls_next) {
		event_action = (ACT) stack_ptr->lls_object;
		event_init = (GPRE_NOD) event_action->act_object;
		stack_name = (SYM) event_init->nod_arg[0];
		if (!strcmp(event_name->sym_string, stack_name->sym_string)) {
			ident = (int) event_init->nod_arg[2];
			database = (DBB) event_init->nod_arg[3];
		}
	}

	if (ident < 0) {
		sprintf(s, "event handle \"%s\" not found", event_name->sym_string);
		IBERROR(s);
		return;
	}

	args.pat_database = database;
	args.pat_vector1 = status_vector(action);
	args.pat_value1 = (int) ident;
	args.pat_string1 = GDS_EVENT_WAIT;
	args.pat_string2 = GDS_EVENT_COUNTS;

//  generate calls to wait on the event and to fill out the events array 

	PATTERN_expand(column, pattern1, &args);
	PATTERN_expand(column, pattern2, &args);

	if (action->act_error)
		END;
	SET_SQLCODE;
}


//____________________________________________________________
//  
//		Generate replacement text for the SQL FETCH statement.  The
//		epilog FETCH statement is handled by GEN_S_FETCH (generate
//		stream fetch).
//  

static void gen_fetch( ACT action, int column)
{
	GPRE_REQ		request;
	GPRE_NOD		var_list;
	int		i;
	TEXT	s[20];

	request = action->act_request;

#ifdef SCROLLABLE_CURSORS
	POR		port;
	REF		reference;
	VAL		value;
	TEXT*	direction;
	TEXT*	offset;

	if (port = request->req_aport) {
		/* set up the reference to point to the correct value 
		   in the linked list of values, and prepare for the 
		   next FETCH statement if applicable */

		for (reference = port->por_references; reference;
			 reference = reference->ref_next) {
			value = reference->ref_values;
			reference->ref_value = value->val_value;
			reference->ref_values = value->val_next;
		}

		/* find the direction and offset parameters */

		reference = port->por_references;
		offset = reference->ref_value;
		reference = reference->ref_next;
		direction = reference->ref_value;

		/* the direction in which the engine will scroll is sticky, so check to see 
		   the last direction passed to the engine; if the direction is the same and 
		   the offset is 1, then there is no need to pass the message; this prevents 
		   extra packets and allows for batch fetches in either direction */

		printa(column, "if (isc_%ddirection MOD 2 <> %s) or (%s <> 1) then",
			   request->req_ident, direction, offset);
		column += INDENT;
		BEGIN;

		/* assign the direction and offset parameters to the appropriate message, 
		   then send the message to the engine */

		asgn_from(action, port->por_references, column);
		gen_send(action, port, column);
		printa(column, "isc_%ddirection := %s;", request->req_ident,
			   direction);
		column -= INDENT;
		END;

		printa(column, "if SQLCODE = 0 then");
		column += INDENT;
		BEGIN;
	}
#endif

	if (request->req_sync) {
		gen_send(action, request->req_sync, column);
		printa(column, "if SQLCODE = 0 then");
		column += INDENT;
		BEGIN;
	}

	gen_receive(action, column, request->req_primary);
	printa(column, "if SQLCODE = 0 then");
	column += INDENT;
	printa(column, "if %s <> 0 then", gen_name(s, request->req_eof, true));
	column += INDENT;
	BEGIN;

	if (var_list = (GPRE_NOD) action->act_object)
		for (i = 0; i < var_list->nod_count; i++) {
			align(column);
			asgn_to(action, reinterpret_cast<ref*>(var_list->nod_arg[i]), column);
		}

	END;
	printa(column - INDENT, "else");
	printa(column, "SQLCODE := 100;");
	if (request->req_sync) {
		column -= INDENT;
		END;
	}

#ifdef SCROLLABLE_CURSORS
	if (port) {
		column -= INDENT;
		END;
	}
#endif
}


//____________________________________________________________
//  
//		Generate substitution text for FINISH
//  

static void gen_finish( ACT action, int column)
{
	DBB db;
	RDY ready;

	db = NULL;

	if (sw_auto || ((action->act_flags & ACT_sql) &&
					(action->act_type != ACT_disconnect))) {
		printa(column, "if gds__trans <> nil");
		printa(column + 4, "then GDS__%s_TRANSACTION (%s, gds__trans);",
			   (action->act_type != ACT_rfinish) ? "COMMIT" : "ROLLBACK",
			   status_vector(action));
	}

//   Got rid of tests of gds__trans <> nil which were causing the skipping
//   of trying to detach the databases.  Related to bug#935.  mao 6/22/89  

	for (ready = (RDY) action->act_object; ready; ready = ready->rdy_next) {
		db = ready->rdy_database;
		printa(column, "GDS__DETACH_DATABASE (%s, %s);",
			   status_vector(action), db->dbb_name->sym_string);
	}
	if (!db)
		for (db = isc_databases; db; db = db->dbb_next) {
			if ((action->act_error || (action->act_flags & ACT_sql))
				&& (db != isc_databases)) printa(column,
												 "if (%s <> nil) and (gds__status[2] = 0) then",
												 db->dbb_name->sym_string);
			else
				printa(column, "if %s <> nil then", db->dbb_name->sym_string);
			printa(column + INDENT, "GDS__DETACH_DATABASE (%s, %s);",
				   status_vector(action), db->dbb_name->sym_string);
		}

	SET_SQLCODE;
}


//____________________________________________________________
//  
//		Generate substitution text for FOR statement.
//  

static void gen_for( ACT action, int column)
{
	POR port;
	GPRE_REQ request;
	TEXT s[20];
	REF reference;

	gen_s_start(action, column);
	request = action->act_request;

	if (action->act_error || (action->act_flags & ACT_sql))
		printa(column, "if gds__status[2] = 0 then begin");

	gen_receive(action, column, request->req_primary);
	if (action->act_error || (action->act_flags & ACT_sql))
		printa(column, "while (%s <> 0) and (gds__status[2] = 0) do",
			   gen_name(s, request->req_eof, true));
	else
		printa(column, "while %s <> 0 do",
			   gen_name(s, request->req_eof, true));
	column += INDENT;
	BEGIN;

	if (port = action->act_request->req_primary)
		for (reference = port->por_references; reference;
			 reference =
			 reference->ref_next) if (reference->ref_field->
									  fld_array_info)
					gen_get_or_put_slice(action, reference, true, column);
}

#ifdef PYXIS
//____________________________________________________________
//  
//		Generate code for a form interaction.
//  

static void gen_form_display( ACT action, int column)
{
	FINT display;
	GPRE_REQ request;
	REF reference, master;
	POR port;
	DBB db;
	TEXT s[32], *status, out[16];
	int code;

	display = (FINT) action->act_object;
	request = display->fint_request;
	db = request->req_database;
	port = request->req_ports;
	status = (action->act_error) ? "gds__status" : "gds__null^";

//  Initialize field options 

	for (reference = port->por_references; reference;
		 reference = reference->ref_next)
			if ((master = reference->ref_master) &&
				(code = CMP_display_code(display, master)) >= 0)
			printa(column, "%s := %d;", gen_name(s, reference, true), code);

	if (display->fint_flags & FINT_no_wait)
		strcpy(out, "0");
	else
		sprintf(out, "gds__%d", port->por_ident);

	printa(column,
		   "pyxis__drive_form (%s, %s, %s, gds__window, %s, gds__%d, %s);",
		   status, db->dbb_name->sym_string, request->req_trans,
		   request->req_handle, port->por_ident, out);
}
#endif
#ifdef PYXIS
//____________________________________________________________
//  
//		Generate code for a form block.
//  

static void gen_form_end( ACT action, int column)
{

	printa(column, "pyxis__pop_window (gds__window);");
}
#endif
#ifdef PYXIS
//____________________________________________________________
//  
//		Generate code for a form block.
//  

static void gen_form_for( ACT action, int column)
{
	GPRE_REQ request;
	FORM form;
	TEXT *status;
	DBB db;
	int indent;

	indent = column + INDENT;
	request = action->act_request;
	form = request->req_form;
	db = request->req_database;
	status = status_vector(action);

//  Get database attach and transaction started 

	if (sw_auto) {
		printa(column, "if (gds__trans = nil) then");
		t_start_auto(action, 0, status_vector(action), indent);
	}

//  Get form loaded first 

	printa(column, "if %s = nil then", request->req_form_handle);
	align(indent);
	ib_fprintf(out_file, "pyxis__load_form (%s, %s, %s, %s, %d, '%s');",
			   status,
			   db->dbb_name->sym_string,
			   request->req_trans,
			   request->req_form_handle,
			   strlen(form->form_name->sym_string),
			   form->form_name->sym_string);

//  Get map compiled 

	printa(column, "if %s = nil then", request->req_handle);
	printa(indent, "pyxis__compile_map (%s, %s, %s, %d, gds__%d);",
		   status,
		   request->req_form_handle,
		   request->req_handle, request->req_length, request->req_ident);

//  Reset form to known state 

	printa(column, "pyxis__reset_form (%s, %s);",
		   status, request->req_handle);
}
#endif

//____________________________________________________________
//  
//		Generate a call to gds__get_slice
//		or gds__put_slice for an array.
//  

static void gen_get_or_put_slice(ACT action,
								 REF reference,
								 bool get,
								 int column)
{
	PAT args;
	TEXT s1[25], s2[10], s3[10], s4[10];
	TEXT *pattern1 =
		"GDS__GET_SLICE (%V1, %RF%DH%RE, %RF%S1%RE, %S2, %N1, %S3, %N2, %S4, %L1, %S5, %S6);\n";
	TEXT *pattern2 =
		"GDS__PUT_SLICE (%V1, %RF%DH%RE, %RF%S1%RE, %S2, %N1, %S3, %N2, %S4, %L1, %S5);\n";

	if (!(reference->ref_flags & REF_fetch_array))
		return;

	args.pat_vector1 = status_vector(action);	/*  status vector        */
	args.pat_database = action->act_request->req_database;	/*  database handle      */
	args.pat_string1 = action->act_request->req_trans;	/*  transaction handle   */

	gen_name(s1, reference, true);	//  blob handle
	args.pat_string2 = s1;

	args.pat_value1 = reference->ref_sdl_length;	/*  slice descr. length  */

	sprintf(s2, "gds__%d", reference->ref_sdl_ident);	/*  slice description    */
	args.pat_string3 = s2;

	args.pat_value2 = 0;		/*  parameter length     */

	sprintf(s3, "0");			/*  parameter            */
	args.pat_string4 = s3;

	args.pat_long1 = reference->ref_field->fld_array_info->ary_size;
	/*  slice size           */

	if (action->act_flags & ACT_sql) {
		args.pat_string5 = reference->ref_value;
	}
	else {
		sprintf(s4, "gds__%d",
				reference->ref_field->fld_array_info->ary_ident);
		args.pat_string5 = s4;	/*  array name           */
	}

	args.pat_string6 = "gds__array_length";	/*  return length        */

	if (get)
		PATTERN_expand(column, pattern1, &args);
	else
		PATTERN_expand(column, pattern2, &args);
}


//____________________________________________________________
//  
//		Generate the code to do a get segment.
//  

static void gen_get_segment( ACT action, int column)
{
	BLB blob;
	PAT args;
	REF into;
	TEXT *pattern1 =
		"%IFgds__status[2] := %ENGDS__GET_SEGMENT (%V1, %BH, %I1, %S1 (%I2), %RF%I2);";

	if (action->act_error && (action->act_type != ACT_blob_for))
		BEGIN;

	if (action->act_flags & ACT_sql)
		blob = (BLB) action->act_request->req_blobs;
	else
		blob = (BLB) action->act_object;

	args.pat_blob = blob;
	args.pat_vector1 = status_vector(action);
	args.pat_condition = TRUE;
	args.pat_ident1 = blob->blb_len_ident;
	args.pat_ident2 = blob->blb_buff_ident;
	args.pat_string1 = SIZEOF;
	PATTERN_expand(column, pattern1, &args);

	if (action->act_flags & ACT_sql) {
		into = action->act_object;
		SET_SQLCODE;
		printa(column, "if (SQLCODE = 0) or (SQLCODE = 101) then");
		column += INDENT;
		BEGIN;
		align(column);
		ib_fprintf(out_file, "gds__ftof (gds__%d, gds__%d, %s, gds__%d);",
				   blob->blb_buff_ident, blob->blb_len_ident,
				   into->ref_value, blob->blb_len_ident);
		if (into->ref_null_value) {
			align(column);
			ib_fprintf(out_file, "%s := gds__%d;",
					   into->ref_null_value, blob->blb_len_ident);
		}
		END;
		column -= INDENT;
	}
}

#ifdef PYXIS
//____________________________________________________________
//  
//		Generate end of block for PUT_ITEM and FOR_ITEM.
//  

static void gen_item_end( ACT action, int column)
{
	GPRE_REQ request;
	REF reference;
	POR port;
	DBB db;
	TEXT s[32], *status, index[16];

	request = action->act_request;
	if (request->req_type == REQ_menu) {
		gen_menu_item_end(action, column);
		return;
	}

	status = (action->act_error) ? "gds__status" : "gds__null^";
	if (action->act_pair->act_type == ACT_item_for) {
		column += INDENT;
		gen_name(index, request->req_index, true);
		printa(column, "%s := %s + 1;", index, index);
		align(column);
		ib_fprintf(out_file,
				   "pyxis__fetch (%s, %s, %s, %s, gds__%d);",
				   status,
				   request->req_database->dbb_name->sym_string,
				   request->req_trans,
				   request->req_handle, request->req_ports->por_ident);
		if (action->act_error)
			ENDS;
		else
			END;
		return;
	}

	db = request->req_database;
	port = request->req_ports;

//  Initialize field options 

	for (reference = port->por_references; reference;
		 reference = reference->ref_next) if (reference->ref_master)
			printa(column, "%s := %d;", gen_name(s, reference, true),
				   PYXIS_OPT_DISPLAY);

	align(column);
	ib_fprintf(out_file,
			   "pyxis__insert (%s, %s, %s, %s, gds__%d);",
			   status,
			   db->dbb_name->sym_string,
			   request->req_trans, request->req_handle, port->por_ident);
}
#endif
#ifdef PYXIS
//____________________________________________________________
//  
//		Generate insert text for FOR_ITEM and PUT_ITEM.
//  

static void gen_item_for( ACT action, int column)
{
	GPRE_REQ request, parent;
	FORM form;
	TEXT *status, index[30];

	request = action->act_request;
	if (request->req_type == REQ_menu) {
		gen_menu_item_for(action, column);
		return;
	}

	column += INDENT;
	form = request->req_form;
	parent = form->form_parent;

	status = (action->act_error) ? "gds__status" : "gds__null^";

//  Get map compiled 

	printa(column, "if %s = nil then", request->req_handle);
	printa(column + INDENT,
		   "pyxis__compile_sub_map (%s, %s, %s, %d, gds__%d);", status,
		   parent->req_handle, request->req_handle, request->req_length,
		   request->req_ident);

	if (action->act_type != ACT_item_for)
		return;

//  Build stuff for item loop 

	gen_name(index, request->req_index, true);
	printa(column, "%s := 1;", index);
	align(column);
	ib_fprintf(out_file,
			   "pyxis__fetch (%s, %s, %s, %s, gds__%d);",
			   status,
			   request->req_database->dbb_name->sym_string,
			   request->req_trans,
			   request->req_handle, request->req_ports->por_ident);
	printa(column, "while (%s <> 0) do", index);
	column += INDENT;
	BEGIN;
}
#endif

//____________________________________________________________
//  
//		Generate text to compile and start a stream.  This is
//		used both by START_STREAM and FOR
//  

static void gen_loop( ACT action, int column)
{
	GPRE_REQ request;
	POR port;
	TEXT name[20];

	gen_s_start(action, column);
	request = action->act_request;
	port = request->req_primary;
	printa(column, "if SQLCODE = 0 then");
	column += INDENT;
	BEGIN;
	gen_receive(action, column, port);
	gen_name(name, port->por_references, true);
	printa(column, "if (SQLCODE = 0) and (%s = 0)", name);
	printa(column + INDENT, "then SQLCODE := 100;");
	END;
	column -= INDENT;
}

#ifdef PYXIS
//____________________________________________________________
//  
//  

static void gen_menu( ACT action, int column)
{
	GPRE_REQ request;

	request = action->act_request;
	printa(column, "case pyxis__menu (gds__window, %s, %d, gds__%d) of",
		   request->req_handle, request->req_length, request->req_ident);
}
#endif
#ifdef PYXIS
//____________________________________________________________
//  
//		Generate code for a menu interaction.
//  

static void gen_menu_display( ACT action, int column)
{
	MENU menu;
	GPRE_REQ request, display_request;

	request = action->act_request;
	display_request = (GPRE_REQ) action->act_object;

	menu = NULL;

	for (action = request->req_actions; action; action = action->act_next)
		if (action->act_type == ACT_menu_for) {
			menu = (MENU) action->act_object;
			break;
		}

	printa(column,
		   "pyxis__drive_menu (gds__window, %s, %d, gds__%d, gds__%dl, gds__%d,",
		   request->req_handle,
		   display_request->req_length,
		   display_request->req_ident, menu->menu_title, menu->menu_title);

	printa(column,
		   "\n\t\t\tgds__%d, gds__%dl, gds__%d, gds__%d);",
		   menu->menu_terminator,
		   menu->menu_entree_entree,
		   menu->menu_entree_entree, menu->menu_entree_value);
}
#endif
#ifdef PYXIS
//____________________________________________________________
//  
//  

static void gen_menu_entree( ACT action, int column)
{

	printa(column, "%d:", action->act_count);
}
#endif
#ifdef PYXIS
//____________________________________________________________
//  
//  
//    Generate code for a reference to a menu or entree attribute.

static void gen_menu_entree_att( ACT action, int column)
{
	MENU menu;
	SSHORT ident;
	bool length = false;

	menu = (MENU) action->act_object;

	switch (action->act_type) {
	case ACT_entree_text:
		ident = menu->menu_entree_entree;
		break;
	case ACT_entree_length:
		ident = menu->menu_entree_entree;
		length = true;
		break;
	case ACT_entree_value:
		ident = menu->menu_entree_value;
		break;
	case ACT_title_text:
		ident = menu->menu_title;
		break;
	case ACT_title_length:
		ident = menu->menu_title;
		length = true;
		break;
	case ACT_terminator:
		ident = menu->menu_terminator;
		break;
	default:
		ident = -1;
		break;
	}

	if (length)
		printa(column, "gds__%dl", ident);
	else
		printa(column, "gds__%d", ident);
}
#endif
#ifdef PYXIS
//____________________________________________________________
//  
//		Generate code for a menu block.
//  

static void gen_menu_for( ACT action, int column)
{
	GPRE_REQ request;

	request = action->act_request;

//  Get menu created 

	if (!(request->req_flags & REQ_exp_hand))
		printa(column, "pyxis__initialize_menu (%s);", request->req_handle);
}
#endif
#ifdef PYXIS
//____________________________________________________________
//  
//		Generate end of block for PUT_ITEM and FOR_ITEM
//		for a dynamic menu.
//  

static void gen_menu_item_end( ACT action, int column)
{
	GPRE_REQ request;
	ENTREE entree;

	entree = (ENTREE) action->act_pair->act_object;
	request = entree->entree_request;

	if (action->act_pair->act_type == ACT_item_for) {
		align(column);
		printa(column,
			   "pyxis__get_entree (%s, gds__%dl, gds__%d, gds__%d, gds__%d);",
			   request->req_handle, entree->entree_entree,
			   entree->entree_entree, entree->entree_value,
			   entree->entree_end);
		column += INDENT;
		END;
		return;
	}

	align(column);
	ib_fprintf(out_file,
			   "pyxis__put_entree (%s, gds__%dl, gds__%d, gds__%d);",
			   request->req_handle,
			   entree->entree_entree,
			   entree->entree_entree, entree->entree_value);
}
#endif
#ifdef PYXIS
//____________________________________________________________
//  
//		Generate insert text for FOR_ITEM and PUT_ITEM
//		for a dynamic menu.
//  

static void gen_menu_item_for( ACT action, int column)
{
	ENTREE entree;
	GPRE_REQ request;

	if (action->act_type != ACT_item_for)
		return;

//  Build stuff for item loop 

	entree = (ENTREE) action->act_object;
	request = entree->entree_request;

	align(column);
	printa(column,
		   "pyxis__get_entree (%s, gds__%dl, gds__%d, gds__%d, gds__%d);",
		   request->req_handle, entree->entree_entree, entree->entree_entree,
		   entree->entree_value, entree->entree_end);
	printa(column, "while (gds__%d = 0) do", entree->entree_end);
	column += INDENT;
	BEGIN;
}
#endif
#ifdef PYXIS
//____________________________________________________________
//  
//		Generate definitions associated with a dynamic menu request.
//  

static void gen_menu_request( GPRE_REQ request, int column)
{
	ACT action;
	MENU menu;
	ENTREE entree;

	menu = NULL;
	entree = NULL;

	for (action = request->req_actions; action; action = action->act_next) {
		if (action->act_type == ACT_menu_for) {
			menu = (MENU) action->act_object;
			break;
		}
		else if ((action->act_type == ACT_item_for)
				 || (action->act_type == ACT_item_put)) {
			entree = (ENTREE) action->act_object;
			break;
		}
	}

	if (menu) {
		menu->menu_title = CMP_next_ident();
		menu->menu_terminator = CMP_next_ident();
		menu->menu_entree_value = CMP_next_ident();
		menu->menu_entree_entree = CMP_next_ident();
		printa(column, "gds__%dl\t: %s;\t\t(* TITLE_LENGTH *)",
			   menu->menu_title, SHORT_DCL);
		printa(column, "gds__%d\t: %s [1..81] of char;\t\t(* TITLE_TEXT *)",
			   menu->menu_title, PACKED_ARRAY);
		printa(column, "gds__%d\t: %s;\t\t(* TERMINATOR *)",
			   menu->menu_terminator, SHORT_DCL);
		printa(column, "gds__%dl\t: %s;\t\t(* ENTREE_LENGTH *)",
			   menu->menu_entree_entree, SHORT_DCL);
		printa(column, "gds__%d\t: %s [1..81] of char;\t\t(* ENTREE_TEXT *)",
			   menu->menu_entree_entree, PACKED_ARRAY);
		printa(column, "gds__%d\t: %s;\t\t(* ENTREE_VALUE *)",
			   menu->menu_entree_value, LONG_DCL);
	}

	if (entree) {
		entree->entree_entree = CMP_next_ident();
		entree->entree_value = CMP_next_ident();
		entree->entree_end = CMP_next_ident();
		printa(column, "gds__%dl\t: %s;\t\t(* ENTREE_LENGTH *)",
			   entree->entree_entree, SHORT_DCL);
		printa(column, "gds__%d\t: %s [1..81] of char;\t\t(* ENTREE_TEXT *)",
			   entree->entree_entree, PACKED_ARRAY);
		printa(column, "gds__%d\t: %s;\t\t(* ENTREE_VALUE *)",
			   entree->entree_value, LONG_DCL);
		printa(column, "gds__%d\t: %s;\t\t(* *)",
			   entree->entree_end, SHORT_DCL);
	}

}
#endif

//____________________________________________________________
//  
//		Generate a name for a reference.  Name is constructed from
//		port and parameter idents.
//  

static TEXT *gen_name(TEXT * string,
					  REF reference,
					  bool as_blob)
{

	if (reference->ref_field->fld_array_info && !as_blob)
		sprintf(string, "gds__%d",
				reference->ref_field->fld_array_info->ary_ident);
	else
		sprintf(string, "gds__%d.gds__%d",
				reference->ref_port->por_ident, reference->ref_ident);

	return string;
}


//____________________________________________________________
//  
//		Generate a block to handle errors.
//  

static void gen_on_error( ACT action, USHORT column)
{
	ACT err_action;

	err_action = (ACT) action->act_object;
	if ((err_action->act_type == ACT_get_segment) ||
		(err_action->act_type == ACT_put_segment) ||
		(err_action->act_type == ACT_endblob))
			printa(column,
				   "if (gds__status [2] <> 0) and (gds__status[2] <> gds__segment) and (gds__status[2] <> gds__segstr_eof) then");
	else
		printa(column, "if (gds__status [2] <> 0) then");
	column += INDENT;
	BEGIN;
}


//____________________________________________________________
//  
//		Generate code for an EXECUTE PROCEDURE.
//  

static void gen_procedure( ACT action, int column)
{
	PAT args;
	TEXT *pattern;
	GPRE_REQ request;
	POR in_port, out_port;
	DBB db;

	column += INDENT;
	request = action->act_request;
	in_port = request->req_vport;
	out_port = request->req_primary;

	db = request->req_database;
	args.pat_database = db;
	args.pat_request = action->act_request;
	args.pat_vector1 = status_vector(action);
	args.pat_request = request;
	args.pat_port = in_port;
	args.pat_port2 = out_port;
	if (in_port && in_port->por_length)
		pattern =
			"isc_transact_request (%V1, %RF%DH%RE, %RF%RT%RE, %VF%RS%VE, %RI, %VF%PL%VE, %RF%PI%RE, %VF%QL%VE, %RF%QI%RE);";
	else
		pattern =
			"isc_transact_request (%V1, %RF%DH%RE, %RF%RT%RE, %VF%RS%VE, %RI, %VF0%VE, 0, %VF%QL%VE, %RF%QI%RE);";


//  Get database attach and transaction started 

	if (sw_auto)
		t_start_auto(action, 0, status_vector(action), column);

//  Move in input values 

	asgn_from(action, request->req_values, column);

//  Execute the procedure 

	PATTERN_expand(column, pattern, &args);

	SET_SQLCODE;

	printa(column, "if SQLCODE = 0 then");
	column += INDENT;
	BEGIN;

//  Move out output values 

	asgn_to_proc(request->req_references, column);
	END;
}


//____________________________________________________________
//  
//		Generate the code to do a put segment.
//  

static void gen_put_segment( ACT action, int column)
{
	BLB blob;
	PAT args;
	REF from;
	TEXT *pattern1 =
		"%IFgds__status[2] := %ENGDS__PUT_SEGMENT (%V1, %BH, %I1, %I2);";

	if (!action->act_error)
		BEGIN;
	if (action->act_error || (action->act_flags & ACT_sql))
		BEGIN;

	if (action->act_flags & ACT_sql) {
		blob = (BLB) action->act_request->req_blobs;
		from = action->act_object;
		align(column);
		ib_fprintf(out_file, "gds__%d := %s;",
				   blob->blb_len_ident, from->ref_null_value);
		align(column);
		ib_fprintf(out_file, "gds__ftof (%s, gds__%d, gds__%d, gds__%d);",
				   from->ref_value, blob->blb_len_ident,
				   blob->blb_buff_ident, blob->blb_len_ident);
	}
	else
		blob = (BLB) action->act_object;

	args.pat_blob = blob;
	args.pat_vector1 = status_vector(action);
	args.pat_condition = TRUE;
	args.pat_ident1 = blob->blb_len_ident;
	args.pat_ident2 = blob->blb_buff_ident;
	PATTERN_expand(column, pattern1, &args);

	SET_SQLCODE;

	if (action->act_flags & ACT_sql)
		END;
}


//____________________________________________________________
//  
//		Generate BLR in raw, numeric form.  Ughly but dense.
//  

static void gen_raw( UCHAR * blr, int request_length, int column)
{
	UCHAR *end, c;
	TEXT *p, *limit, buffer[80];

	p = buffer;
	limit = buffer + 60;

	for (end = blr + request_length - 1; blr <= end; blr++) {
		c = *blr;
		if ((c >= 'A' && c <= 'Z') || c == '$' || c == '_')
			sprintf(p, "'%c'", c);
		else
			sprintf(p, "chr(%d)", c);
		while (*p)
			p++;
		if (blr != end)
			*p++ = ',';
		if (p < limit)
			continue;
		*p = 0;
		printa(INDENT, buffer);
		p = buffer;
	}

	*p = 0;
	printa(INDENT, buffer);
}


//____________________________________________________________
//  
//		Generate substitution text for READY
//  

static void gen_ready( ACT action, int column)
{
	RDY ready;
	DBB db;
	TEXT *filename, *vector;

	vector = status_vector(action);

	for (ready = (RDY) action->act_object; ready; ready = ready->rdy_next) {
		db = ready->rdy_database;
		if (!(filename = ready->rdy_filename))
			filename = db->dbb_runtime;
		if ((action->act_error || (action->act_flags & ACT_sql)) &&
			ready != (RDY) action->act_object)
			printa(column, "if (gds__status[2] = 0) then begin");
		make_ready(db, filename, vector, column, ready->rdy_request);
		if ((action->act_error || (action->act_flags & ACT_sql)) &&
			ready != (RDY) action->act_object)
			END;
	}
	SET_SQLCODE;
}


//____________________________________________________________
//  
//		Generate receive call for a port.
//  

static void gen_receive( ACT action, int column, POR port)
{
	GPRE_REQ request;

	align(column);

	request = action->act_request;
	ib_fprintf(out_file, "GDS__RECEIVE (%s, %s, %d, %d, %sgds__%d, %s);",
			   status_vector(action),
			   request->req_handle,
			   port->por_msg_number,
			   port->por_length,
			   REF_PAR, port->por_ident, request->req_request_level);

	SET_SQLCODE;
}


//____________________________________________________________
//  
//		Generate substitution text for RELEASE_REQUESTS
//		For active databases, call gds__release_request.
//		for all others, just zero the handle.  For the
//		release request calls, ignore error returns, which
//		are likely if the request was compiled on a database
//		which has been released and re-readied.  If there is
//		a serious error, it will be caught on the next statement.
//  

static void gen_release( ACT action, int column)
{
	DBB db, exp_db;
	GPRE_REQ request;

	exp_db = (DBB) action->act_object;

	for (request = requests; request; request = request->req_next) {
		db = request->req_database;
		if (exp_db && db != exp_db)
			continue;
		if (!(request->req_flags & REQ_exp_hand)) {
			printa(column, "if %s <> NIL then", db->dbb_name->sym_string);
			printa(column + INDENT, "gds__release_request (gds__status, %s);",
				   request->req_handle);
			printa(column, "%s := NIL;", request->req_handle);
		}
	}
}


//____________________________________________________________
//  
//		Generate definitions associated with a single request.
//  

static void gen_request( GPRE_REQ request, int column)
{
	BLB blob;
	POR port;
	TEXT *sw_volatile, *string_type;
	REF reference;

//  generate request handle, blob handles, and ports 

	sw_volatile = FB_DP_VOLATILE;
	printa(column, " ");

	if (!
		(request->
		 req_flags & (REQ_exp_hand 
#ifdef PYXIS
		| REQ_menu_for_item 
#endif
		| REQ_sql_blob_open |
					  REQ_sql_blob_create)) && request->req_type != REQ_slice
&& request->req_type != REQ_procedure)
		printa(column, "%s\t: %s gds__handle := nil;\t\t(* request handle *)",
			   request->req_handle, sw_volatile);

	if (request->req_flags & (REQ_sql_blob_open | REQ_sql_blob_create))
		printa(column,
			   "gds__%ds\t: %s gds__handle := nil;\t\t(* SQL statement handle *)",
			   request->req_ident, sw_volatile);

//  generate actual BLR string 

	if (request->req_length) {
		printa(column, " ");
		if (request->req_flags & REQ_sql_cursor)
			printa(column,
				   "gds__%ds\t: %s gds__handle := nil;\t\t(* SQL statement handle *)",
				   request->req_ident, sw_volatile);
#ifdef SCROLLABLE_CURSORS
		if (request->req_flags & REQ_scroll)
			printa(column,
				   "gds__%ddirection\t: %s := 0;\t\t(* last direction sent to engine *)",
				   request->req_ident, SHORT_DCL);
#endif
		printa(column, "gds__%dl\t: %s := %d;\t\t(* request length *)",
			   request->req_ident, SHORT_DCL, request->req_length);
		printa(column, "gds__%d\t: %s [1..%d] of char := %s",
			   request->req_ident, PACKED_ARRAY, request->req_length,
			   OPEN_BRACKET);
		string_type = "BLR";
		if (sw_raw) {
			gen_raw(request->req_blr, request->req_length, column);
			switch (request->req_type) {
			case REQ_create_database:
			case REQ_ready:
				string_type = "DPB";
				break;

			case REQ_ddl:
				string_type = "DYN";
				break;
#ifdef PYXIS
			case REQ_form:
				string_type = "form map";
				break;

			case REQ_menu:
				string_type = "menu";
				break;
#endif
			case REQ_slice:
				string_type = "SDL";
				break;

			default:
				string_type = "BLR";
				break;
			}
		}
		else
			switch (request->req_type) {
			case REQ_create_database:
			case REQ_ready:
				string_type = "DPB";
				if (PRETTY_print_cdb(reinterpret_cast<char*>(request->req_blr),
									reinterpret_cast<int(*)()>(gen_blr), 0, 1))
					IBERROR("internal error during parameter generation");
				break;

			case REQ_ddl:
				string_type = "DYN";
				if (PRETTY_print_dyn(reinterpret_cast<char*>(request->req_blr),
									reinterpret_cast<int(*)()>(gen_blr), 0, 1))
					IBERROR("internal error during dynamic DDL generation");
				break;
#ifdef PYXIS
			case REQ_form:
				string_type = "form map";
				if (PRETTY_print_form_map(reinterpret_cast<char*>(request->req_blr),
										reinterpret_cast<int(*)()>(gen_blr), 0, 1))
					IBERROR("internal error during form map generation");
				break;

			case REQ_menu:
				string_type = "menu";
				if (PRETTY_print_menu(reinterpret_cast<char*>(request->req_blr),
									reinterpret_cast<int(*)()>(gen_blr), 0, 1))
					IBERROR("internal error during menu generation");
				break;
#endif
			case REQ_slice:
				string_type = "SDL";
				if (PRETTY_print_sdl(reinterpret_cast<char*>(request->req_blr),
									reinterpret_cast<int(*)()>(gen_blr), 0, 1))
					IBERROR("internal error during SDL generation");
				break;

			default:
				string_type = "BLR";
				if (gds__print_blr(request->req_blr,
									reinterpret_cast<void(*)()>(gen_blr), 0, 1))
					IBERROR("internal error during BLR generation");
				break;
			}
		printa(column, "%s;\t(* end of %s string for request gds__%d *)\n",
			   CLOSE_BRACKET, string_type, request->req_ident);
	}

//   Print out slice description language if there are arrays associated with request  

	for (port = request->req_ports; port; port = port->por_next)
		for (reference = port->por_references; reference;
			 reference = reference->ref_next) if (reference->ref_sdl) {
				printa(column, "gds__%d\t: %s [1..%d] of char := %s",
					   reference->ref_sdl_ident, PACKED_ARRAY,
					   reference->ref_sdl_length, OPEN_BRACKET);
				if (sw_raw)
					gen_raw(reinterpret_cast<UCHAR*>(reference->ref_sdl), reference->ref_sdl_length,
							column);
				else if (PRETTY_print_sdl(reference->ref_sdl,
										reinterpret_cast<int(*)()>(gen_blr),
										0, 1))
					IBERROR("internal error during SDL generation");
				printa(column, "%s; \t(* end of SDL string for gds__%d *)\n",
					   CLOSE_BRACKET, reference->ref_sdl_ident);
			}

//  Print out any blob parameter blocks required 

	for (blob = request->req_blobs; blob; blob = blob->blb_next)
		if (blob->blb_bpb_length) {
			printa(column, "gds__%d\t: %s [1..%d] of char := %s",
				   blob->blb_bpb_ident,
				   PACKED_ARRAY, blob->blb_bpb_length, OPEN_BRACKET);
			gen_raw(blob->blb_bpb, blob->blb_bpb_length, column);
			printa(column, "%s;\n", CLOSE_BRACKET);
		}
#ifdef PYXIS
	if (request->req_type == REQ_menu)
		gen_menu_request(request, column);
#endif
//  If this is GET_SLICE/PUT_SLICE, allocate some variables 

	if (request->req_type == REQ_slice) {
		printa(column, "gds__%dv\t: array [1..%d] of %s;",
			   request->req_ident, MAX(request->req_slice->slc_parameters, 1),
			   LONG_DCL);
		printa(column, "gds__%ds\t: %s;", request->req_ident, LONG_DCL);
	}
}


//____________________________________________________________
//  
//		Generate receive call for a port
//		in a store2 statement.
//  

static void gen_return_value( ACT action, int column)
{
	UPD update;
	REF reference;
	GPRE_REQ request;

	request = action->act_request;
	if (action->act_pair->act_error)
		column += INDENT;
	gen_start(action, request->req_primary, column);
	update = (UPD) action->act_object;
	reference = update->upd_references;
	gen_receive(action, column, reference->ref_port);
}


//____________________________________________________________
//  
//		Process routine head.  If there are requests in the
//		routine, insert local definitions.
//  

static void gen_routine( ACT action, int column)
{
	BLB blob;
	GPRE_REQ request;
	POR port;

	column += INDENT;

	for (request = (GPRE_REQ) action->act_object; request;
		 request = request->req_routine) {
		for (port = request->req_ports; port; port = port->por_next) {
			printa(column - INDENT, "type");
			make_port(port, column);
		}

		/*  Only write a var reserved word if there are variables which will be
		   associated with a var section.  Fix for bug#809.  mao  03/22/89 */

		if (request->req_ports) {
			printa(column - INDENT, "var");
		}

		for (port = request->req_ports; port; port = port->por_next)
			printa(column, "gds__%d\t: gds__%dt;\t\t\t(* message *)",
				   port->por_ident, port->por_ident);

		for (blob = request->req_blobs; blob; blob = blob->blb_next) {
			printa(column, "gds__%d\t: gds__handle;\t\t\t(* blob handle *)",
				   blob->blb_ident);
			printa(column,
				   "gds__%d\t: %s [1 .. %d] of char;\t(* blob segment *)",
				   blob->blb_buff_ident, PACKED_ARRAY, blob->blb_seg_length);
			printa(column, "gds__%d\t: %s;\t\t\t(* segment length *)",
				   blob->blb_len_ident, SHORT_DCL);
		}
	}
	column -= INDENT;
}


//____________________________________________________________
//  
//		Generate substitution text for END_STREAM.
//  

static void gen_s_end( ACT action, int column)
{
	GPRE_REQ request;

	if (action->act_error)
		BEGIN;
	request = action->act_request;

	if (action->act_type == ACT_close)
		column = gen_cursor_close(action, request, column);

	printa(column, "GDS__UNWIND_REQUEST (%s, %s, %s);",
		   status_vector(action),
		   request->req_handle, request->req_request_level);

	if (action->act_type == ACT_close) {
		END;
		column -= INDENT;
		ENDS;
		column -= INDENT;
	}

	SET_SQLCODE;
}


//____________________________________________________________
//  
//		Generate substitution text for FETCH.
//  

static void gen_s_fetch( ACT action, int column)
{
	GPRE_REQ request;

	request = action->act_request;
	if (request->req_sync)
		gen_send(action, request->req_sync, column);

	gen_receive(action, column, request->req_primary);
	if (!action->act_pair && !action->act_error)
		END;
}


//____________________________________________________________
//  
//		Generate text to compile and start a stream.  This is
//		used both by START_STREAM and FOR
//  

static void gen_s_start( ACT action, int column)
{
	GPRE_REQ request;
	POR port;

	request = action->act_request;

	gen_compile(action, column);

	if (action->act_type == ACT_open)
		column = gen_cursor_open(action, request, column);

	if (port = request->req_vport)
		asgn_from(action, port->por_references, column);

	if (action->act_error || (action->act_flags & ACT_sql)) {
		make_ok_test(action, request, column);
		column += INDENT;
	}

	gen_start(action, port, column);

	if (action->act_error || (action->act_flags & ACT_sql))
		column -= INDENT;

	if (action->act_type == ACT_open) {
		END;
		column -= INDENT;
		END;
		column -= INDENT;
		ENDS;
		column -= INDENT;
	}

	SET_SQLCODE;
}


//____________________________________________________________
//  
//		Substitute for a segment, segment length, or blob handle.
//  

static void gen_segment( ACT action, int column)
{
	BLB blob;

	blob = (BLB) action->act_object;

	printa(column, "gds__%d",
		   (action->act_type == ACT_segment) ? blob->blb_buff_ident :
		   (action->act_type == ACT_segment_length) ? blob->blb_len_ident :
		   blob->blb_ident);
}


//____________________________________________________________
//  
//  

static void gen_select( ACT action, int column)
{
	GPRE_REQ request;
	POR port;
	GPRE_NOD var_list;
	int i;
	TEXT name[20];

	request = action->act_request;
	port = request->req_primary;
	gen_name(name, request->req_eof, true);

	gen_s_start(action, column);
	BEGIN;
	gen_receive(action, column, port);
	printa(column, "if SQLCODE = 0 then", name);
	column += INDENT;
	printa(column, "if %s <> 0 then", name);
	column += INDENT;

	BEGIN;
	if (var_list = (GPRE_NOD) action->act_object)
		for (i = 0; i < var_list->nod_count; i++) {
			align(column);
			asgn_to(action, reinterpret_cast<REF>(var_list->nod_arg[i]), column);
		}

	if (request->req_database->dbb_flags & DBB_v3) {
		gen_receive(action, column, port);
		printa(column, "if (SQLCODE = 0) AND (%s <> 0) then", name);
		printa(column + INDENT, "SQLCODE := -1");
		END;
	}

	printa(column - INDENT, "else");
	printa(column, "SQLCODE := 100;");
	column -= INDENT;
	ENDS;
}


//____________________________________________________________
//  
//		Generate a send or receive call for a port.
//  

static void gen_send( ACT action, POR port, int column)
{
	GPRE_REQ request;

	request = action->act_request;
	align(column);

	ib_fprintf(out_file, "GDS__SEND (%s, %s, %d, %d, gds__%d, %s);",
			   status_vector(action),
			   request->req_handle,
			   port->por_msg_number,
			   port->por_length, port->por_ident, request->req_request_level);

	SET_SQLCODE;
}


//____________________________________________________________
//  
//		Generate support for get/put slice statement.
//  

static void gen_slice( ACT action, int column)
{
	GPRE_REQ request, parent_request;
	REF reference, upper, lower;
	SLC slice;
	PAT args;
	slc::slc_repeat *tail, *end;
	TEXT *pattern1 =
		"GDS__GET_SLICE (%V1, %RF%DH%RE, %RF%RT%RE, %RF%FR%RE, %N1, \
%I1, %N2, %I1v, %I1s, %RF%S5%RE, %RF%S6%RE);";
	TEXT *pattern2 =
		"GDS__PUT_SLICE (%V1, %RF%DH%RE, %RF%RT%RE, %RF%FR%RE, %N1, \
%I1, %N2, %I1v, %I1s, %RF%S5%RE);";

	request = action->act_request;
	slice = (SLC) action->act_object;
	parent_request = slice->slc_parent_request;

//  Compute array size 

	printa(column, "gds__%ds := %d", request->req_ident,
		   slice->slc_field->fld_array->fld_length);

	for (tail = slice->slc_rpt, end = tail + slice->slc_dimensions;
		 tail < end; ++tail)
		if (tail->slc_upper != tail->slc_lower) {
			lower = (REF) tail->slc_lower->nod_arg[0];
			upper = (REF) tail->slc_upper->nod_arg[0];
			if (lower->ref_value)
				ib_fprintf(out_file, " * ( %s - %s + 1)", upper->ref_value,
						   lower->ref_value);
			else
				ib_fprintf(out_file, " * ( %s + 1)", upper->ref_value);
		}

	ib_fprintf(out_file, ";");

//  Make assignments to variable vector 

	for (reference = request->req_values; reference;
		 reference =
		 reference->ref_next) printa(column, "gds__%dv [%d] := %s;",
									 request->req_ident, reference->ref_id,
									 reference->ref_value);

	args.pat_reference = slice->slc_field_ref;
	args.pat_request = parent_request;	/* blob id request */
	args.pat_vector1 = status_vector(action);	/* status vector */
	args.pat_database = parent_request->req_database;	/* database handle */
	args.pat_string1 = action->act_request->req_trans;	/* transaction handle */
	args.pat_value1 = request->req_length;	/* slice descr. length */
	args.pat_ident1 = request->req_ident;	/* request name */
	args.pat_value2 = slice->slc_parameters * sizeof(SLONG);	/* parameter length */

	reference = (REF) slice->slc_array->nod_arg[0];
	args.pat_string5 = reference->ref_value;	/* array name */
	args.pat_string6 = "gds__array_length";

	PATTERN_expand(column,
				   (action->act_type == ACT_get_slice) ? pattern1 : pattern2,
				   &args);
}


//____________________________________________________________
//  
//		Generate either a START or START_AND_SEND depending
//		on whether or a not a port is present.
//  

static void gen_start( ACT action, POR port, int column)
{
	GPRE_REQ request;
	TEXT *vector;
	REF reference;

	request = action->act_request;
	vector = status_vector(action);

	align(column);

	if (port) {
		for (reference = port->por_references; reference;
			 reference =
			 reference->ref_next) if (reference->ref_field->
									  fld_array_info)
					gen_get_or_put_slice(action, reference, false, column);

		ib_fprintf(out_file,
				   "GDS__START_AND_SEND (%s, %s, %s, %d, %d, gds__%d, %s);",
				   vector, request->req_handle, request_trans(action,
															  request),
				   port->por_msg_number, port->por_length, port->por_ident,
				   request->req_request_level);
	}

	else
		ib_fprintf(out_file, "GDS__START_REQUEST (%s, %s, %s, %s);",
				   vector,
				   request->req_handle,
				   request_trans(action, request),
				   request->req_request_level);
}


//____________________________________________________________
//  
//		Generate text for STORE statement.  This includes the compile
//		call and any variable initialization required.
//  

static void gen_store( ACT action, int column)
{
	GPRE_REQ request;
	REF reference;
	GPRE_FLD field;
	POR port;
	TEXT name[64];

	request = action->act_request;
	gen_compile(action, column);
	if (action->act_error || (action->act_flags & ACT_sql)) {
		make_ok_test(action, request, column);
		column += INDENT;
		if (action->act_error)
			BEGIN;
	}

//  Initialize any blob fields 

	port = request->req_primary;
	for (reference = port->por_references; reference;
		 reference = reference->ref_next) {
		field = reference->ref_field;
		if (field->fld_flags & FLD_blob)
			printa(column, "%s := gds__blob_null;\n",
				   gen_name(name, reference, true));
	}
}


//____________________________________________________________
//  
//		Generate substitution text for START_TRANSACTION.
//  

static void gen_t_start( ACT action, int column)
{
	DBB db;
	GPRE_TRA trans;
	TPB tpb_val;
	int count;
	TEXT *filename;

//  
//  for automatically generated transactions, and transactions that are
//  explicitly started, but don't have any arguments so don't get a TPB,
//  generate something plausible.
//  

	if (!action || !(trans = (GPRE_TRA) action->act_object)) {
		t_start_auto(action, 0, status_vector(action), column);
		return;
	}

//  build a complete statement, including tpb's.
//  first generate any appropriate ready statements,
//  On non-VMS machines, fill in the tpb vector (aka TEB). 

	count = 0;
	for (tpb_val = trans->tra_tpb; tpb_val; tpb_val = tpb_val->tpb_tra_next) {
		count++;
		db = tpb_val->tpb_database;
		if (sw_auto)
			if ((filename = db->dbb_runtime) || !(db->dbb_flags & DBB_sqlca)) {
				printa(column, "if (%s = nil) then",
					   db->dbb_name->sym_string);
				make_ready(db, filename, status_vector(action),
						   column + INDENT, 0);
			}

#ifndef VMS
		printa(column, "gds__teb[%d].tpb_len := %d;", count, tpb_val->tpb_length);
		printa(column, "gds__teb[%d].tpb_ptr := ADDR(gds__tpb_%d);",
			   count, tpb_val->tpb_ident);
		printa(column, "gds__teb[%d].dbb_ptr := ADDR(%s);",
			   count, db->dbb_name->sym_string);
#endif
	}

#ifdef VMS
//  Now build the start_transaction.  Use lots of %REF and %IMMED to
//  convince the PASCAL compiler not to hassle us about the argument list. 

	printa(column, "GDS__START_TRANSACTION (%s, %%REF %s, %%IMMED %d",
		   status_vector(action),
		   (trans->tra_handle) ? trans->tra_handle : "gds__trans",
		   trans->tra_db_count);

	for (tpb_val = trans->tra_tpb; tpb_val; tpb_val = tpb_val->tpb_tra_next) {
		ib_putc(',', out_file);
		align(column + INDENT);
		ib_fprintf(out_file, "%%REF %s, %%IMMED %d, %%REF gds__tpb_%d",
				   tpb_val->tpb_database->dbb_name->sym_string,
				   tpb_val->tpb_length, tpb_val->tpb_ident);
	}
	ib_fprintf(out_file, ")");
#else
	printa(column, "GDS__START_MULTIPLE (%s, %s, %d, gds__teb);",
		   status_vector(action),
		   (trans->tra_handle) ? trans->tra_handle : "gds__trans",
		   trans->tra_db_count);
#endif

	SET_SQLCODE;
}


//____________________________________________________________
//  
//		Generate a TPB in the output file
//  

static void gen_tpb( TPB tpb_val, int column)
{
	TEXT *text, buffer[80], c, *p;
	int length;

	printa(column, "gds__tpb_%d\t: %s [1..%d] of char := %s",
		   tpb_val->tpb_ident, PACKED_ARRAY, tpb_val->tpb_length, OPEN_BRACKET);

	length = tpb_val->tpb_length;
	text = (TEXT *) tpb_val->tpb_string;
	p = buffer;

	while (--length) {
		c = *text++;
		if ((c >= 'A' && c <= 'Z') || c == '$' || c == '_')
			sprintf(p, "'%c', ", c);
		else
			sprintf(p, "chr(%d), ", c);
		while (*p)
			p++;
		if (p - buffer > 60) {
			align(column + INDENT);
			ib_fprintf(out_file, " %s", buffer);
			p = buffer;
			*p = 0;
		}
	}

//  handle the last character 
	c = *text++;

	if ((c >= 'A' && c <= 'Z') || c == '$' || c == '_')
		sprintf(p, "'%c',", c);
	else
		sprintf(p, "chr(%d)", c);

	align(column + INDENT);
	ib_fprintf(out_file, "%s", buffer);

	printa(column, "%s;\n", CLOSE_BRACKET);
}


//____________________________________________________________
//  
//		Generate substitution text for COMMIT, ROLLBACK, PREPARE, and SAVE
//  

static void gen_trans( ACT action, int column)
{

	align(column);

	if (action->act_type == ACT_commit_retain_context)
		ib_fprintf(out_file, "GDS__COMMIT_RETAINING (%s, %s);",
				   status_vector(action),
				   (action->act_object) ? (TEXT *) action->
				   act_object : "gds__trans");
	else
		ib_fprintf(out_file, "GDS__%s_TRANSACTION (%s, %s);",
				   (action->act_type ==
					ACT_commit) ? "COMMIT" : (action->act_type ==
											  ACT_rollback) ? "ROLLBACK" :
				   "PREPARE", status_vector(action),
				   (action->act_object) ? (TEXT *) action->
				   act_object : "gds__trans");

	SET_SQLCODE;
}


//____________________________________________________________
//  
//		Generate substitution text for UPDATE ... WHERE CURRENT OF ...
//  

static void gen_update( ACT action, int column)
{
	POR port;
	UPD modify;

	modify = (UPD) action->act_object;
	port = modify->upd_port;
	asgn_from(action, port->por_references, column);
	gen_send(action, port, column);
}


//____________________________________________________________
//  
//		Substitute for a variable reference.
//  

static void gen_variable( ACT action, int column)
{
	TEXT s[20];

	printa(column, gen_name(s, action->act_object, false));
}


//____________________________________________________________
//  
//		Generate tests for any WHENEVER clauses that may have been declared.
//  

static void gen_whenever( SWE label, int column)
{
	TEXT *condition;

	if (label)
		ib_fprintf(out_file, ";");

	while (label) {
		switch (label->swe_condition) {
		case SWE_error:
			condition = "SQLCODE < 0";
			break;

		case SWE_warning:
			condition = "(SQLCODE > 0) AND (SQLCODE <> 100)";
			break;

		case SWE_not_found:
			condition = "SQLCODE = 100";
			break;
		}
		align(column);
		ib_fprintf(out_file, "if %s then goto %s;", condition,
				   label->swe_label);
		label = label->swe_next;
	}
}

#ifdef PYXIS
//____________________________________________________________
//  
//		Create a new window.
//  

static void gen_window_create( ACT action, int column)
{

	printa(column,
		   "pyxis__create_window (gds__window, 0, 0, gds__width, gds__height)");
}
#endif
#ifdef PYXIS
//____________________________________________________________
//  
//		Delete a window.
//  

static void gen_window_delete( ACT action, int column)
{

	printa(column, "pyxis__delete_window (gds__window)");
}
#endif
#ifdef PYXIS
//____________________________________________________________
//  
//		Suspend a window.
//  

static void gen_window_suspend( ACT action, int column)
{

	printa(column, "pyxis__suspend_window (gds__window)");
}
#endif

//____________________________________________________________
//  
//		Generate a declaration of an array in the
//		output file.
//  

static void make_array_declaration( REF reference)
{
	GPRE_FLD field;
	TEXT *name;
	TEXT s[64];
	DIM dimension;

	field = reference->ref_field;
	name = field->fld_symbol->sym_string;

//  Don't generate multiple declarations for the array.  V3 Bug 569.  

	if (field->fld_array_info->ary_declared)
		return;

	field->fld_array_info->ary_declared = TRUE;

	if (field->fld_array_info->ary_dtype <= dtype_varying)
		ib_fprintf(out_file, "gds__%d : %s [",
				   field->fld_array_info->ary_ident, PACKED_ARRAY);
	else
		ib_fprintf(out_file, "gds__%d : array [",
				   field->fld_array_info->ary_ident);

//   Print out the dimension part of the declaration  
	for (dimension = field->fld_array_info->ary_dimension; dimension;
		 dimension = dimension->dim_next) {
		ib_fprintf(out_file, "%"SLONGFORMAT"..%"SLONGFORMAT, dimension->dim_lower,
				   dimension->dim_upper);
		if (dimension->dim_next)
			ib_fprintf(out_file, ", ");
	}

	if (field->fld_array_info->ary_dtype <= dtype_varying)
		ib_fprintf(out_file, ", 1..%d", field->fld_array->fld_length);

	ib_fprintf(out_file, "] of ");

	switch (field->fld_array_info->ary_dtype) {
	case dtype_short:
		ib_fprintf(out_file, SHORT_DCL);
		break;

	case dtype_long:
		ib_fprintf(out_file, LONG_DCL);
		break;

	case dtype_cstring:
	case dtype_text:
	case dtype_varying:
		ib_fprintf(out_file, "char");
		break;

	case dtype_date:
	case dtype_quad:
		ib_fprintf(out_file, "GDS__QUAD");
		break;

	case dtype_float:
		ib_fprintf(out_file, "real");
		break;

	case dtype_double:
		ib_fprintf(out_file, "double");
		break;

	default:
		sprintf(s, "datatype %d unknown for field %s",
				field->fld_array_info->ary_dtype, name);
		IBERROR(s);
		return;
	}

//   Print out the database field  

	ib_fprintf(out_file, ";\t(* %s *)\n", name);
}


//____________________________________________________________
//  
//		Turn a symbol into a varying string.
//  

static TEXT *make_name( TEXT * string, SYM symbol)
{

	sprintf(string, "'%s '", symbol->sym_string);

	return string;
}


//____________________________________________________________
//  
//		Generate code to test existence of compiled request with
//		active transaction
//  

static void make_ok_test( ACT action, GPRE_REQ request, int column)
{

	if (sw_auto)
		printa(column, "if (%s <> nil) and (%s <> nil) then",
			   request_trans(action, request), request->req_handle);
	else
		printa(column, "if (%s <> nil) then", request->req_handle);
}


//____________________________________________________________
//  
//		Insert a port record description in output.
//  

static void make_port( POR port, int column)
{
	GPRE_FLD field;
	REF reference;
	SYM symbol;
	bool flag = false;
	TEXT *name, s[80];

	printa(column, "gds__%dt = record", port->por_ident);

	for (reference = port->por_references; reference;
		 reference = reference->ref_next)
	{
		if (flag)
			ib_fprintf(out_file, ";");
		flag = true;
		align(column + INDENT);
		field = reference->ref_field;
		if (symbol = field->fld_symbol)
			name = symbol->sym_string;
		else
			name = "<expression>";
		if (reference->ref_value && (reference->ref_flags & REF_array_elem))
			field = field->fld_array;
		switch (field->fld_dtype) {
		case dtype_float:
			ib_fprintf(out_file, "gds__%d\t: real\t(* %s *)",
					   reference->ref_ident, name);
			break;

		case dtype_double:
			ib_fprintf(out_file, "gds__%d\t: double\t(* %s *)",
					   reference->ref_ident, name);
			break;

		case dtype_short:
			ib_fprintf(out_file, "gds__%d\t: %s\t(* %s *)",
					   reference->ref_ident, SHORT_DCL, name);
			break;

		case dtype_long:
			ib_fprintf(out_file, "gds__%d\t: %s\t(* %s *)",
					   reference->ref_ident, LONG_DCL, name);
			break;

		case dtype_date:
		case dtype_quad:
		case dtype_blob:
			ib_fprintf(out_file, "gds__%d\t: gds__quad\t(* %s *)",
					   reference->ref_ident, name);
			break;

		case dtype_text:
			ib_fprintf(out_file, "gds__%d\t: %s [1..%d] of char\t(* %s *)",
					   reference->ref_ident, PACKED_ARRAY, field->fld_length,
					   name);
			break;

		default:
			sprintf(s, "datatype %d unknown for field %s, msg %d",
					field->fld_dtype, name, port->por_msg_number);
			CPR_error(s);
			return;
		};
	}

	printa(column, "end;\n");
}


//____________________________________________________________
//  
//		Generate the actual insertion text for a
//		ready;
//  

static void make_ready(
				  DBB db,
				  TEXT * filename, TEXT * vector, USHORT column, GPRE_REQ request)
{
	TEXT s1[32], s2[32];

	if (request) {
		sprintf(s1, "gds__%dL", request->req_ident);
		sprintf(s2, "gds__%d", request->req_ident);
	}

	align(column);
	if (filename)
#ifdef VMS
		ib_fprintf(out_file,
				   "GDS__ATTACH_DATABASE_d (%s, %%STDESCR %s, %s, %s, %s);",
				   vector, filename, db->dbb_name->sym_string,
				   (request ? s1 : "0"), (request ? s2 : "0"));
#else
		ib_fprintf(out_file,
				   "GDS__ATTACH_DATABASE (%s, %s (%s), %s, %s, %s, %s);",
				   vector, SIZEOF, filename, filename,
				   db->dbb_name->sym_string, (request ? s1 : "0"),
				   (request ? s2 : "0"));
#endif
	else
		ib_fprintf(out_file,
				   "GDS__ATTACH_DATABASE (%s, %d, '%s', %s, %s, %s);", vector,
				   strlen(db->dbb_filename), db->dbb_filename,
				   db->dbb_name->sym_string, (request ? s1 : "0"),
				   (request ? s2 : "0"));
}


//____________________________________________________________
//  
//		Print a fixed string at a particular column.
//  

static void printa( int column, TEXT * string, ...)
{
	va_list ptr;

	VA_START(ptr, string);
	align(column);
	ib_vfprintf(out_file, string, ptr);
}


//____________________________________________________________
//  
//		Generate the appropriate transaction handle.
//  

static TEXT *request_trans( ACT action, GPRE_REQ request)
{
	TEXT *trname;

	if (action->act_type == ACT_open) {
		if (!(trname = ((OPN) action->act_object)->opn_trans))
			trname = (TEXT*) "gds__trans";
		return trname;
	}
	else
		return (request) ? request->req_trans : (TEXT*) "gds__trans";
}


//____________________________________________________________
//  
//		Generate the appropriate status vector parameter for a gds
//		call depending on where or not the action has an error clause.
//  

static TEXT *status_vector( ACT action)
{

	if (action && (action->act_error || (action->act_flags & ACT_sql)))
		return "gds__status";

	return "gds__null^";
}


//____________________________________________________________
//  
//		Generate substitution text for START_TRANSACTION.
//		The complications include the fact that all databases
//		must be readied, and that everything should stop if
//		any thing fails so we don't trash the status vector.
//  

static void t_start_auto( ACT action, GPRE_REQ request, TEXT * vector, int column)
{
	DBB db;
	int count, stat, and_count;
	TEXT *filename, buffer[256], temp[40];

	buffer[0] = 0;

//  find out whether we're using a status vector or not 

	stat = !strcmp(vector, "gds__status");

//  this is a default transaction, make sure all databases are ready 

	BEGIN;

	for (db = isc_databases, count = and_count = 0; db; db = db->dbb_next) {
		if (sw_auto)
			if ((filename = db->dbb_runtime) || !(db->dbb_flags & DBB_sqlca)) {
				align(column);
				ib_fprintf(out_file, "if (%s = nil",
						   db->dbb_name->sym_string);
				if (stat && buffer[0])
					ib_fprintf(out_file, ") and (%s[2] = 0", vector);
				ib_fprintf(out_file, ") then");
				make_ready(db, filename, vector, column + INDENT, 0);
				if (buffer[0])
					if (and_count % 4)
						strcat(buffer, ") and (");
					else
						strcat(buffer, ") and\n\t(");
				and_count++;
				sprintf(temp, "%s <> nil", db->dbb_name->sym_string);
				strcat(buffer, temp);
				printa(column, "if (%s) then", buffer);
				align(column + INDENT);
			}

		count++;
#ifndef VMS
		printa(column, "gds__teb[%d].tpb_len:= 0;", count);
		printa(column, "gds__teb[%d].tpb_ptr := ADDR(gds__null);", count);
		printa(column, "gds__teb[%d].dbb_ptr := ADDR(%s);", count,
			   db->dbb_name->sym_string);
#endif
	}

#ifdef VMS
	column += INDENT;
	printa(column, "GDS__START_TRANSACTION (%s, %%REF %s, %%IMMED %d",
		   vector, request_trans(action, request), count);

	for (db = isc_databases; db; db = db->dbb_next) {
		ib_putc(',', out_file);
		align(column + INDENT);
		ib_fprintf(out_file, "%%REF %s, %%IMMED 0, %%REF 0",
				   db->dbb_name->sym_string);
	}
	ib_fprintf(out_file, ");");
	column -= INDENT;

#else
	printa(column, "GDS__START_MULTIPLE (%s, %s, %d, gds__teb);",
		   vector, request_trans(action, request), count);
#endif

	if (sw_auto && request)
		column -= INDENT;

	SET_SQLCODE;
	ENDS;
}
