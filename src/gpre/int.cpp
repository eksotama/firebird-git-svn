//____________________________________________________________
//  
//		PROGRAM:	C preprocess
//		MODULE:		int.cpp
//		DESCRIPTION:	Code generate for internal JRD modules
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
//  TMN (Mike Nordell) 11.APR.2001 - Reduce compiler warnings in generated code
//  
//
//____________________________________________________________
//
//	$Id: int.cpp,v 1.3 2001-12-24 02:50:49 tamlin Exp $
//

#include "firebird.h"
#include "../jrd/ib_stdio.h"
#include "../jrd/common.h"
#include <stdarg.h>
#include "../jrd/gds.h"
#include "../gpre/gpre.h"
#include "../gpre/gpre_proto.h"
#include "../gpre/lang_proto.h"
#include "../jrd/gds_proto.h"

static void align(int);
static void asgn_from(REF, int);
static void asgn_to(REF);
static void gen_at_end(ACT, int);
static int gen_blr(int *, int, TEXT *);
static void gen_compile(REQ, int);
static void gen_database(ACT, int);
static void gen_emodify(ACT, int);
static void gen_estore(ACT, int);
static void gen_endfor(ACT, int);
static void gen_erase(ACT, int);
static void gen_for(ACT, int);
static TEXT *gen_name(TEXT *, REF);
static void gen_raw(REQ);
static void gen_receive(REQ, POR);
static void gen_request(REQ);
static void gen_routine(ACT, int);
static void gen_s_end(ACT, int);
static void gen_s_fetch(ACT, int);
static void gen_s_start(ACT, int);
static void gen_send(REQ, POR, int);
static void gen_start(REQ, POR, int);
static void gen_type(ACT, int);
static void gen_variable(ACT, int);
static void make_port(POR, int);
static void printa(int, TEXT *, ...);

static int first_flag = 0;

#define INDENT 3
#define BEGIN		printa (column, "{")
#define END		printa (column, "}")

#if !(defined JPN_SJIS || defined JPN_EUC)
#define GDS_VTOV	"gds__vtov"
#define JRD_VTOF	"jrd_vtof"
#define VTO_CALL	"%s (%s, %s, %d);"
#else
#define GDS_VTOV	"gds__vtov2"
#define JRD_VTOF	"jrd_vtof2"
#define VTO_CALL	"%s (%s, %s, %d, %d);"
#endif


//____________________________________________________________
//  
//  

void INT_action( ACT action, int column)
{

//  Put leading braces where required 

	switch (action->act_type) {
	case ACT_for:
	case ACT_insert:
	case ACT_modify:
	case ACT_store:
	case ACT_s_fetch:
	case ACT_s_start:
		BEGIN;
		align(column);
	}

	switch (action->act_type) {
	case ACT_at_end:
		gen_at_end(action, column);
		return;
	case ACT_b_declare:
	case ACT_database:
		gen_database(action, column);
		return;
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
	case ACT_for:
		gen_for(action, column);
		return;
	case ACT_hctef:
		break;
	case ACT_insert:
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
	case ACT_type:
		gen_type(action, column);
		return;
	case ACT_variable:
		gen_variable(action, column);
		return;
	default:
		return;
	}

//  Put in a trailing brace for those actions still with us 

	END;
}


//____________________________________________________________
//  
//		Align output to a specific column for output.
//  

static void align( int column)
{
	int i;

	if (column < 0)
		return;

	ib_putc('\n', out_file);

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

static void asgn_from( REF reference, int column)
{
	FLD field;
	TEXT *value, variable[20], temp[20];

	for (; reference; reference = reference->ref_next) {
		field = reference->ref_field;
		align(column);
		gen_name(variable, reference);
		if (reference->ref_source)
			value = gen_name(temp, reference->ref_source);
		else
			value = reference->ref_value;

		/* To avoid chopping off a double byte kanji character in between
		   the two bytes, generate calls to gds__ftof2 gds$_vtof2,
		   gds$_vtov2 and jrd_vtof2 wherever necessary */

		if (!field || field->fld_dtype == dtype_text)
			ib_fprintf(out_file, VTO_CALL,
					   JRD_VTOF,
					   value, variable, field->fld_length, sw_interp);
		else if (!field || field->fld_dtype == dtype_cstring)
			ib_fprintf(out_file, VTO_CALL,
					   GDS_VTOV,
					   value, variable, field->fld_length, sw_interp);
		else
			ib_fprintf(out_file, "%s = %s;", variable, value);
	}

}


//____________________________________________________________
//  
//		Build an assignment to a host language variable from
//		a port variable.
//  

static void asgn_to( REF reference)
{
	FLD field;
	REF source;
	TEXT s[20];

	source = reference->ref_friend;
	field = source->ref_field;
	gen_name(s, source);

#if (! (defined JPN_SJIS || defined JPN_EUC) )

	if (!field || field->fld_dtype == dtype_text)
		ib_fprintf(out_file, "gds__ftov (%s, %d, %s, sizeof (%s));",
				   s,
				   field->fld_length,
				   reference->ref_value, reference->ref_value);
	else if (!field || field->fld_dtype == dtype_cstring)
		ib_fprintf(out_file, "gds__vtov (%s, %s, sizeof (%s));",
				   s, reference->ref_value, reference->ref_value);

#else

//  To avoid chopping off a double byte kanji character in between
//  the two bytes, generate calls to gds__ftof2 gds$_vtof2 and
//  gds$_vtov2 wherever necessary 

	if (!field || field->fld_dtype == dtype_text)
		ib_fprintf(out_file, "gds__ftov2 (%s, %d, %s, sizeof (%s), %d);",
				   s,
				   field->fld_length,
				   reference->ref_value, reference->ref_value, sw_interp);
	else if (!field || field->fld_dtype == dtype_cstring)
		ib_fprintf(out_file, "gds__vtov2 (%s, %s, sizeof (%s), %d);",
				   s, reference->ref_value, reference->ref_value, sw_interp);

#endif
	else
		ib_fprintf(out_file, "%s = %s;", reference->ref_value, s);
}


//____________________________________________________________
//  
//		Generate code for AT END clause of FETCH.
//  

static void gen_at_end( ACT action, int column)
{
	REQ request;
	TEXT s[20];

	request = action->act_request;
	printa(column, "if (!%s) ", gen_name(s, request->req_eof));
}


//____________________________________________________________
//  
//		Callback routine for BLR pretty printer.
//  

static int gen_blr( int *user_arg, int offset, TEXT * string)
{

	ib_fprintf(out_file, "%s\n", string);

	return TRUE;
}


//____________________________________________________________
//  
//		Generate text to compile a request.
//  

static void gen_compile( REQ request, int column)
{
	DBB db;
	SYM symbol;

	column += INDENT;
	db = request->req_database;
	symbol = db->dbb_name;
	ib_fprintf(out_file, "if (!%s)", request->req_handle);
	align(column);
	ib_fprintf(out_file,
			   "%s = (BLK) CMP_compile2 (tdbb, (UCHAR*)jrd_%d, TRUE);",
			   request->req_handle, request->req_ident);
}


//____________________________________________________________
//  
//		Generate insertion text for the database statement.
//  

static void gen_database( ACT action, int column)
{
	REQ request;

	if (first_flag++ != 0)
		return;

	align(0);
	for (request = requests; request; request = request->req_next)
		gen_request(request);
}


//____________________________________________________________
//  
//		Generate substitution text for END_MODIFY.
//  

static void gen_emodify( ACT action, int column)
{
	UPD modify;
	REF reference, source;
	FLD field;
	TEXT s1[20], s2[20];

	modify = (UPD) action->act_object;

	for (reference = modify->upd_port->por_references; reference;
		 reference = reference->ref_next) {
		if (!(source = reference->ref_source))
			continue;
		field = reference->ref_field;
		align(column);

#if (! (defined JPN_SJIS || defined JPN_EUC) )

		if (field->fld_dtype == dtype_text)
			ib_fprintf(out_file, "jrd_ftof (%s, %d, %s, %d);",
					   gen_name(s1, source),
					   field->fld_length,
					   gen_name(s2, reference), field->fld_length);
		else if (field->fld_dtype == dtype_cstring)
			ib_fprintf(out_file, "gds__vtov (%s, %s, %d);",
					   gen_name(s1, source),
					   gen_name(s2, reference), field->fld_length);

#else

		/* To avoid chopping off a double byte kanji character in between
		   the two bytes, generate calls to gds__ftof2 gds$_vtof2 and
		   gds$_vtov2 wherever necessary
		   Cannot find where jrd_fof is defined. It needs Japanization too */

		if (field->fld_dtype == dtype_text)
			ib_fprintf(out_file, "jrd_ftof (%s, %d, %s, %d);",
					   gen_name(s1, source),
					   field->fld_length,
					   gen_name(s2, reference), field->fld_length);
		else if (field->fld_dtype == dtype_cstring)
			ib_fprintf(out_file, "gds__vtov2 (%s, %s, %d, %d);",
					   gen_name(s1, source),
					   gen_name(s2, reference), field->fld_length, sw_interp);

#endif
		else
			ib_fprintf(out_file, "%s = %s;",
					   gen_name(s1, reference), gen_name(s2, source));
	}

	gen_send(action->act_request, modify->upd_port, column);
}


//____________________________________________________________
//  
//		Generate substitution text for END_STORE.
//  

static void gen_estore( ACT action, int column)
{
	REQ request;

	request = action->act_request;
	align(column);
	gen_compile(request, column);
	gen_start(request, request->req_primary, column);
}


//____________________________________________________________
//  
//		Generate definitions associated with a single request.
//  

static void gen_endfor( ACT action, int column)
{
	REQ request;

	request = action->act_request;
	column += INDENT;

	if (request->req_sync)
		gen_send(request, request->req_sync, column);

	END;
}


//____________________________________________________________
//  
//		Generate substitution text for ERASE.
//  

static void gen_erase( ACT action, int column)
{
	UPD erase;

	erase = (UPD) action->act_object;
	gen_send(erase->upd_request, erase->upd_port, column);
}


//____________________________________________________________
//  
//		Generate substitution text for FOR statement.
//  

static void gen_for( ACT action, int column)
{
	REQ request;
	TEXT s[20];

	gen_s_start(action, column);

	align(column);
	request = action->act_request;
	ib_fprintf(out_file, "while (1)");
	column += INDENT;
	BEGIN;
	align(column);
	gen_receive(action->act_request, request->req_primary);
	align(column);
	ib_fprintf(out_file, "if (!%s) break;", gen_name(s, request->req_eof));
}


//____________________________________________________________
//  
//		Generate a name for a reference.  Name is constructed from
//		port and parameter idents.
//  

static TEXT *gen_name( TEXT * string, REF reference)
{

	sprintf(string, "jrd_%d.jrd_%d",
			reference->ref_port->por_ident, reference->ref_ident);

	return string;
}


//____________________________________________________________
//  
//		Generate BLR in raw, numeric form.  Ugly but dense.
//  

static void gen_raw( REQ request)
{
	UCHAR *blr, c;
	TEXT buffer[80], *p;
	int blr_length;

	blr = request->req_blr;
	blr_length = request->req_length;
	p = buffer;
	align(0);

	while (--blr_length) {
		c = *blr++;
		if ((c >= 'A' && c <= 'Z') || c == '$' || c == '_')
			sprintf(p, "'%c',", c);
		else
			sprintf(p, "%d,", c);
		while (*p)
			p++;
		if (p - buffer > 60) {
			ib_fprintf(out_file, "%s\n", buffer);
			p = buffer;
			*p = 0;
		}
	}

	ib_fprintf(out_file, "%s%d", buffer, blr_eoc);
}


//____________________________________________________________
//  
//		Generate a send or receive call for a port.
//  

static void gen_receive( REQ request, POR port)
{

	ib_fprintf(out_file,
			   "EXE_receive (tdbb, (REQ)%s, %d, %d, (UCHAR*)&jrd_%d);",
			   request->req_handle, port->por_msg_number, port->por_length,
			   port->por_ident);
}


//____________________________________________________________
//  
//		Generate definitions associated with a single request.
//  

static void gen_request( REQ request)
{

	if (!(request->req_flags & REQ_exp_hand))
		ib_fprintf(out_file, "static void\t*%s;\t/* request handle */\n",
				   request->req_handle);

	ib_fprintf(out_file, "static CONST UCHAR\tjrd_%d [%d] =",
			   request->req_ident, request->req_length);
	align(INDENT);
	ib_fprintf(out_file, "{\t/* blr string */\n", request->req_ident);

	if (sw_raw)
		gen_raw(request);
	else
		gds__print_blr(request->req_blr, (FPTR_VOID) gen_blr, 0, 0);

	printa(INDENT, "};\t/* end of blr string */\n");
}


//____________________________________________________________
//  
//		Process routine head.  If there are requests in the
//		routine, insert local definitions.
//  

static void gen_routine( ACT action, int column)
{
	REQ request;
	POR port;

	for (request = (REQ) action->act_object; request;
		 request = request->req_routine) for (port = request->req_ports; port;
											  port = port->por_next)
			make_port(port, column + INDENT);
}


//____________________________________________________________
//  
//		Generate substitution text for END_STREAM.
//  

static void gen_s_end( ACT action, int column)
{
	REQ request;

	request = action->act_request;
	printa(column, "EXE_unwind (tdbb, %s);", request->req_handle);
}


//____________________________________________________________
//  
//		Generate substitution text for FETCH.
//  

static void gen_s_fetch( ACT action, int column)
{
	REQ request;

	request = action->act_request;
	if (request->req_sync)
		gen_send(request, request->req_sync, column);

	gen_receive(action->act_request, request->req_primary);
}


//____________________________________________________________
//  
//		Generate text to compile and start a stream.  This is
//		used both by START_STREAM and FOR
//  

static void gen_s_start( ACT action, int column)
{
	REQ request;
	POR port;

	request = action->act_request;
	gen_compile(request, column);

	if (port = request->req_vport)
		asgn_from(port->por_references, column);

	gen_start(request, port, column);
}


//____________________________________________________________
//  
//		Generate a send or receive call for a port.
//  

static void gen_send( REQ request, POR port, int column)
{
	align(column);
	ib_fprintf(out_file, "EXE_send (tdbb, (REQ)%s, %d, %d, (UCHAR*)&jrd_%d);",
			   request->req_handle,
			   port->por_msg_number, port->por_length, port->por_ident);
}


//____________________________________________________________
//  
//		Generate a START.
//  

static void gen_start( REQ request, POR port, int column)
{
	align(column);

	ib_fprintf(out_file, "EXE_start (tdbb, (REQ)%s, %s);",
			   request->req_handle, request->req_trans);

	if (port)
		gen_send(request, port, column);
}


//____________________________________________________________
//  
//		Substitute for a variable reference.
//  

static void gen_type( ACT action, int column)
{

	printa(column, "%ld", action->act_object);
}


//____________________________________________________________
//  
//		Substitute for a variable reference.
//  

static void gen_variable( ACT action, int column)
{
	TEXT s[20];

	align(column);
	ib_fprintf(out_file, gen_name(s, action->act_object));

}


//____________________________________________________________
//  
//		Insert a port record description in output.
//  

static void make_port( POR port, int column)
{
	FLD field;
	REF reference;
	SYM symbol;
	TEXT *name, s[50];

	printa(column, "struct {");

	for (reference = port->por_references; reference;
		 reference = reference->ref_next) {
		align(column + INDENT);
		field = reference->ref_field;
		symbol = field->fld_symbol;
		name = symbol->sym_string;
		switch (field->fld_dtype) {
		case dtype_short:
			ib_fprintf(out_file, "    SSHORT jrd_%d;\t/* %s */",
					   reference->ref_ident, name);
			break;

		case dtype_long:
			ib_fprintf(out_file, "    SLONG  jrd_%d;\t/* %s */",
					   reference->ref_ident, name);
			break;

// ** Begin sql date/time/timestamp *
		case dtype_sql_date:
			ib_fprintf(out_file, "    ISC_DATE  jrd_%d;\t/* %s */",
					   reference->ref_ident, name);
			break;

		case dtype_sql_time:
			ib_fprintf(out_file, "    ISC_TIME  jrd_%d;\t/* %s */",
					   reference->ref_ident, name);
			break;

		case dtype_timestamp:
			ib_fprintf(out_file, "    ISC_TIMESTAMP  jrd_%d;\t/* %s */",
					   reference->ref_ident, name);
			break;
// ** End sql date/time/timestamp *

		case dtype_int64:
			ib_fprintf(out_file, "    ISC_INT64  jrd_%d;\t/* %s */",
					   reference->ref_ident, name);
			break;

		case dtype_quad:
		case dtype_blob:
			ib_fprintf(out_file, "    GDS__QUAD  jrd_%d;\t/* %s */",
					   reference->ref_ident, name);
			break;

		case dtype_cstring:
		case dtype_text:
			ib_fprintf(out_file, "    TEXT  jrd_%d [%d];\t/* %s */",
					   reference->ref_ident, field->fld_length, name);
			break;

		case dtype_float:
			ib_fprintf(out_file, "    float  jrd_%d;\t/* %s */",
					   reference->ref_ident, name);
			break;

		case dtype_double:
			ib_fprintf(out_file, "    double  jrd_%d;\t/* %s */",
					   reference->ref_ident, name);
			break;

		default:
			sprintf(s, "datatype %d unknown for field %s, msg %d",
					field->fld_dtype, name, port->por_msg_number);
			CPR_error(s);
			return;
		}
	}
	align(column);
	ib_fprintf(out_file, "} jrd_%d;", port->por_ident);
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
