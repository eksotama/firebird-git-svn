/*
 *  The contents of this file are subject to the Initial
 *  Developer's Public License Version 1.0 (the "License");
 *  you may not use this file except in compliance with the
 *  License. You may obtain a copy of the License at
 *  http://www.ibphoenix.com/main.nfs?a=ibphoenix&page=ibp_idpl.
 *
 *  Software distributed under the License is distributed AS IS,
 *  WITHOUT WARRANTY OF ANY KIND, either express or implied.
 *  See the License for the specific language governing rights
 *  and limitations under the License.
 *
 *  The Original Code was created by Adriano dos Santos Fernandes
 *  for the Firebird Open Source RDBMS project.
 *
 *  Copyright (c) 2008 Adriano dos Santos Fernandes <adrianosf@uol.com.br>
 *  and all contributors signed below.
 *
 *  All Rights Reserved.
 *  Contributor(s): ______________________________________.
 */

#ifndef DSQL_PARSER_H
#define DSQL_PARSER_H

#include "../jrd/common.h"
#include "../dsql/dsql.h"

namespace Dsql {
	enum nod_t;
}

namespace Jrd {

class dsql_nod;

class Parser
{
private:
	typedef int Yshort;
	typedef dsql_nod* YYSTYPE;
	typedef int YYPOSN;	// user-defined text position type

	struct yyparsestate
	{
	  yyparsestate* save;		// Previously saved parser state
	  int state;
	  int errflag;
	  Yshort* ssp;				// state stack pointer
	  YYSTYPE* vsp;				// value stack pointer
	  YYPOSN* psp;				// position stack pointer
	  YYSTYPE val;				// value as returned by actions
	  YYPOSN pos;				// position as returned by universal action
	  Yshort* ss;				// state stack base
	  YYSTYPE* vs;				// values stack base
	  YYPOSN* ps;				// position stack base
	  int lexeme;				// index of the conflict lexeme in the lexical queue
	  unsigned int stacksize;	// current maximum stack size
	  Yshort ctry;				// index in yyctable[] for this conflict
	};

	struct LexerState
	{
		// This is, in fact, parser state. Not used in lexer itself
		dsql_fld* g_field;
		dsql_fil* g_file;
		YYSTYPE g_field_name;
		int dsql_debug;
	
		// Actual lexer state begins from here
		const TEXT* beginning;
		const TEXT* ptr;
		const TEXT* end;
		const TEXT* last_token;
		const TEXT* line_start;
		const TEXT* last_token_bk;
		const TEXT* line_start_bk;
		SSHORT lines, att_charset;
		SSHORT lines_bk;
		int prev_keyword;
		USHORT param_number;
	};

public:
	Parser(USHORT aClientDialect, USHORT aDbDialect, USHORT aParserVersion,
		const TEXT* string, USHORT length, SSHORT characterSet);
	~Parser();

public:
	YYSTYPE parse();

	bool isStmtAmbiguous()
	{
		return stmt_ambiguous;
	}

// start - defined in btyacc_fb.ske
private:
	static void yySCopy(YYSTYPE* to, YYSTYPE* from, int size);
	static void yyPCopy(YYPOSN* to, YYPOSN* from, int size);
	static void yyMoreStack(yyparsestate* yyps);
	static yyparsestate* yyNewState(int size);
	static void yyFreeState(yyparsestate* p);

private:
	int parseAux();
	int yylex1();
	int yyexpand();
// end - defined in btyacc_fb.ske

// start - defined in parse.y
private:
	int yylex();
	int yylexAux();

	void yyerror(const TEXT* error_string);
	void yyerror_detailed(const TEXT* error_string, int yychar, YYSTYPE&, YYPOSN&);

	const TEXT* lex_position();
	YYSTYPE make_list (YYSTYPE node);
	YYSTYPE make_parameter();
	YYSTYPE make_node(Dsql::nod_t type, int count, ...);
	YYSTYPE make_flag_node(Dsql::nod_t type, SSHORT flag, int count, ...);
// end - defined in parse.y

private:
	USHORT client_dialect;
	USHORT db_dialect;
	USHORT parser_version;

	bool stmt_ambiguous;
	YYSTYPE DSQL_parse;

	// These value/posn are taken from the lexer
	YYSTYPE yylval;
	YYPOSN yyposn;

	// These value/posn of the root non-terminal are returned to the caller
	YYSTYPE yyretlval;
	int yyretposn;

	int yynerrs;

	// Current parser state
	yyparsestate* yyps;
	// yypath!=NULL: do the full parse, starting at *yypath parser state.
	yyparsestate* yypath;
	// Base of the lexical value queue
	YYSTYPE* yylvals;
	// Current posistion at lexical value queue
	YYSTYPE* yylvp;
	// End position of lexical value queue
	YYSTYPE* yylve;
	// The last allocated position at the lexical value queue
	YYSTYPE* yylvlim;
	// Base of the lexical position queue
	int* yylpsns;
	// Current posistion at lexical position queue
	int* yylpp;
	// End position of lexical position queue
	int* yylpe;
	// The last allocated position at the lexical position queue
	int* yylplim;
	// Current position at lexical token queue
	Yshort* yylexp;
	Yshort* yylexemes;

public:
	LexerState lex;
};

}; // namespace

#endif	// DSQL_PARSER_H
