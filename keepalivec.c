#include <sys/socket.h>

#include <err.h>
#include <limits.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "keepalived.h"

static void usage(void);

int
main(int argc, char *argv[])
{
	struct addrinfo hints, *res;
	int *socks;
	const char *errstr, *host, *port;
	size_t nsock;
	int ch, rv;
	unsigned int gai, timeout;
	char byte;

	rv = 1;

	if (pledge("stdio inet dns", NULL) == -1)
		err(1, "pledge");

	timeout = KEEPALIVED_TIMEOUT - 5;
	while ((ch = getopt(argc, argv, "t:")) != -1) {
		switch (ch) {
		case 't':
			timeout = strtonum(optarg, 0, UINT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "timeout is %s", errstr);
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 2)
		usage();

	host = argv[0];
	port = argv[1];

	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_DGRAM;

	gai = getaddrinfo(host, port, &hints, &res);
	if (gai != 0)
		errx(1, "getaddrinfo: %s", gai_strerror(gai));

	if (pledge("stdio inet", NULL) == -1)
		err(1, "pledge");

	socks = NULL;
	nsock = 0;
	for (struct addrinfo *i = res; i != NULL; i = i->ai_next) {
		int s, *t;

		if ((s = socket(i->ai_family, i->ai_socktype, i->ai_protocol)) == -1) {
			warn("socket");
			goto sockets;
		}

		if (connect(s, i->ai_addr, i->ai_addrlen) == -1) {
			warn("connect");
			close(s);
			goto sockets;
		}

		t = reallocarray(socks, nsock + 1, sizeof(*socks));
		if (t == NULL) {
			warn(NULL);
			close(s);
			goto sockets;
		}

		socks = t;
		socks[nsock++] = s;
	}

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	byte = 0;
	for (;;) {
		for (size_t i = 0; i < nsock; i++)
			if (send(socks[i], &byte, 1, 0) != 1) {
				warn("send");
				goto sockets;
			}

		sleep(timeout);
	}

	rv = 0;
	sockets:
	for (size_t i = 0; i < nsock; i++)
		close(socks[i]);
	free(socks);
	freeaddrinfo(res);
	return rv;
}

static void
usage(void)
{
	fprintf(stderr, "usage: keepalivec [-t timeout] host port\n");
	exit(2);
}
