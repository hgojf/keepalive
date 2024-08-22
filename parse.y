%{
#include <err.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "keepalived.h"

extern int yylex(void);
extern void yylex_destroy(void);

static int client_addr(const char *);
static int listen_on(const char *, int);
void yyerror(const char *);
int yywrap(void);

extern FILE *yyin;
extern int yylineno;

static struct keepalived_conf *conf;
static const char *filename;
%}

%union {
	long long number;
	char *string;
}

%token BADNUM OOM
%token ADDR CLIENT LISTEN ON PORT TIMEOUT
%token<number> NUMBER
%token<string> STRING
%type<number> port
%type<string> addr
%%
grammar: /* empty */
	| grammar client '\n'
	| grammar listen '\n'
	| grammar timeout '\n'
	| grammar '\n'
	;

addr: STRING {
		$$ = $1;
	}
	| '*' {
		$$ = NULL;
	}
	;

client: CLIENT ADDR STRING {
		if (client_addr($3) == -1) {
			free($3);
			YYERROR;
		}
		free($3);
	}
	;

listen: LISTEN ON addr PORT port {
		if (listen_on($3, $5) == -1) {
			free($3);
			YYERROR;
		}
		free($3);
	}
	;

port: NUMBER {
		if ($1 < 0) {
			yyerror("port was negative");
			YYERROR;
		}

		if ($1 > 65536) {
			yyerror("port was too large");
			YYERROR;
		}

		$$ = $1;
	}
	;

timeout: TIMEOUT NUMBER {
		conf->timeout = $2;
	}
	;
%%
static int
client_addr(const char *host)
{
	struct addrinfo *res;
	int gai, rv;

	rv = -1;

	gai = getaddrinfo(host, NULL, NULL, &res);

	if (gai != 0) {
		warnx("getaaddrinfo: %s", gai_strerror(gai));
		return -1;
	}

	for (struct addrinfo *i = res; i != NULL; i = i->ai_next) {
		struct keepalived_client_conf *t;

		t = recallocarray(conf->clients, conf->nclient, conf->nclient + 1,
				sizeof(*conf->clients));
		if (t == NULL) {
			warn(NULL);
			goto addrinfo;
		}
		conf->clients = t;
		memcpy(&conf->clients[conf->nclient++].ss, i->ai_addr, i->ai_addrlen);
	}

	rv = 0;
	addrinfo:
	freeaddrinfo(res);
	return rv;
}

void
config_free(struct keepalived_conf *c)
{
	free(c->clients);
	free(c->listeners);
}

int
configure(const char *path, struct keepalived_conf *c)
{
	FILE *fp;

	if ((fp = fopen(path, "r")) == NULL) {
		warn("%s", path);
		return -1;
	}

	memset(c, 0, sizeof(*c));

	c->timeout = KEEPALIVED_TIMEOUT;

	conf = c;
	filename = path;
	yyin = fp;

	if (yyparse() != 0) {
		config_free(c);
		fclose(fp);
		yylex_destroy();
		return -1;
	}

	fclose(fp);
	yylex_destroy();
	return 0;
}

static int
listen_on(const char *host, int port)
{
	struct addrinfo hints, *res;
	char aport[6];
	int gai, n, rv;

	rv = -1;
	n = snprintf(aport, sizeof(aport), "%d", port);
	if (n < 0 || (size_t)n >= sizeof(aport)) {
		warn("snprintf overflow");
		return -1;
	}

	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_PASSIVE;
	hints.ai_socktype = SOCK_DGRAM;

	gai = getaddrinfo(host, aport, &hints, &res);
	if (gai != 0) {
		warnx("getaddrinfo: %s", gai_strerror(gai));
		return -1;
	}

	for (struct addrinfo *i = res; i != NULL; i = i->ai_next) {
		struct keepalived_listener_conf *t;

		t = recallocarray(conf->listeners, conf->nlistener,
				conf->nlistener + 1, sizeof(*conf->listeners));
		if (t == NULL) {
			warn(NULL);
			goto addrinfo;
		}

		conf->listeners = t;
		memcpy(&conf->listeners[conf->nlistener++].ss, i->ai_addr, 
				i->ai_addrlen);
	}

	rv = 0;
	addrinfo:
	freeaddrinfo(res);
	return rv;
}

void
yyerror(const char *s)
{
	fprintf(stderr, "%s: %s on line %d\n", filename, s, yylineno);
}

int
yywrap(void)
{
	return 1;
}
