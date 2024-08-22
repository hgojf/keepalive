%{
#include <limits.h>
#include <stdlib.h>

#include "parse.h"
%}

%option noinput nounput
%option yylineno
%%
"addr" { return ADDR; }
"client" { return CLIENT; }
"listen" { return LISTEN; }
"on" { return ON; }
"port" { return PORT; }
"timeout" { return TIMEOUT; }

-?[0-9]+ {
	const char *errstr;

	yylval.number = strtonum(yytext, LLONG_MIN, LLONG_MAX, &errstr);
	if (errstr != NULL)
		return BADNUM;

	return NUMBER;
}

[a-zA-Z0-9.]+ {
	if ((yylval.string = strdup(yytext)) == NULL)
		return OOM;
	return STRING;
}

"\""[^\"\n]*"\"" {
	if ((yylval.string = strndup(yytext + 1, yyleng - 2)) == NULL)
		return OOM;
	return STRING;
}

[ \ t] { }
"#"[^\n]* { }
"\\"\n { }

.|\n { return yytext[0]; }
%%
