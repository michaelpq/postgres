/*-------------------------------------------------------------------------
 *
 * jsonpath_internal.h
 *     Private definitions for jsonpath scanner & parser
 *
 * Portions Copyright (c) 1996-2025, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/backend/utils/adt/jsonpath_internal.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef JSONPATH_INTERNAL_H
#define JSONPATH_INTERNAL_H

/* struct JsonPathString is shared between scan and gram */
typedef struct JsonPathString
{
	char	   *val;
	int			len;
	int			total;
} JsonPathString;

#ifndef YY_TYPEDEF_YY_SCANNER_T
#define YY_TYPEDEF_YY_SCANNER_T
typedef void *yyscan_t;
#endif

#include "utils/jsonpath.h"
#include "jsonpath_gram.h"

#define YY_DECL extern int     jsonpath_yylex(YYSTYPE *yylval_param, \
							  JsonPathParseResult **result, \
							  struct Node *escontext, \
							  yyscan_t yyscanner)
YY_DECL;
extern int	jsonpath_yyparse(JsonPathParseResult **result,
							 struct Node *escontext,
							 yyscan_t yyscanner);
extern void jsonpath_yyerror(JsonPathParseResult **result,
							 struct Node *escontext,
							 yyscan_t yyscanner,
							 const char *message);

#endif							/* JSONPATH_INTERNAL_H */
