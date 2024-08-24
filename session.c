#include <netinet/in.h>
#include <sys/queue.h>
#include <sys/socket.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <event.h>
#include <imsg.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "session.h"

struct listener {
	struct event ev;
	int fd;
	SLIST_ENTRY(listener) entries;
};

struct session {
	struct event_base *eb;

	struct event sigint;
	struct event sigterm;

	struct event timer;
	struct timeval timeout;

	SLIST_HEAD(, listener) listeners;

	struct parent {
		struct event ev;
		struct imsgbuf msgbuf;
	} parent;

	struct sockaddr_storage *clients;
	size_t nclient;

	#define STATE_CONFIG 0
	#define STATE_LISTEN 1
	int state;
};

#define PARENT_FD 3

static void listener_cb(int, short, void *);
static void parent_cb(int, short, void *);
static int parent_dispatch_imsg(struct session *, struct imsg *);
static void session_close(struct session *);
static int session_init(struct session *);
static void signal_cb(int, short, void *);
static int sockaddr_cmp(struct sockaddr_storage *, struct sockaddr_storage *);
static void timer_cb(int, short, void *);

int
main(int argc, char *argv[])
{
	struct session session;
	int rv;

	rv = 1;

	if (session_init(&session) == -1) {
		warnx("failed to initialize");
		goto parent;
	}

	if (event_dispatch() == -1) {
		warn("event_dispatch");
		goto session;
	}

	rv = 0;
	session:
	session_close(&session);
	parent:
	close(PARENT_FD);
	return rv;
}

static void
listener_cb(int fd, short event, void *arg)
{
	struct sockaddr_storage ss;
	struct session *session;
	socklen_t ss_len;
	char byte;
	ssize_t n;

	session = arg;

	assert(session->state >= STATE_LISTEN);

	ss_len = sizeof(ss);
	if ((n = recvfrom(fd, &byte, 1, 0, (struct sockaddr *)&ss, &ss_len)) == -1) {
		if (errno == EAGAIN)
			return;
		warn("recvfrom");
		goto abort;
	}

	/* dont care whats in the byte (if one was received) */

	for (size_t i = 0; i < session->nclient; i++) {
		if (sockaddr_cmp(&ss, &session->clients[i]) != 0)
			continue;

		if (evtimer_pending(&session->timer, NULL)) {
			evtimer_del(&session->timer);

			if (evtimer_add(&session->timer, &session->timeout) == -1) {
				warn("evtimer_add");
				goto abort;
			}
		}

		break;
	}

	return;

	abort:
	event_loopbreak();
}

static void
parent_cb(int fd, short event, void *arg)
{
	struct session *session;
	ssize_t n;

	session = arg;

	if (event & EV_WRITE) {
		if (imsg_flush(&session->parent.msgbuf) == -1) {
			if (errno == EAGAIN)
				goto read;
			warn("imsg_flush");
			goto abort;
		}

		event_del(&session->parent.ev);

		event_set(&session->parent.ev, session->parent.msgbuf.fd, 
			EV_READ | EV_PERSIST, parent_cb, session);
		if (event_add(&session->parent.ev, NULL) == -1) {
			warn("event_add");
			goto abort;
		}
	}

	read:
	if (!(event & EV_READ))
		return;

	if ((n = imsg_read(&session->parent.msgbuf)) == -1) {
		if (errno == EAGAIN)
			return;
		warn("imsg_read");
		goto abort;
	}

	if (n == 0) {
		event_loopbreak();
		return;
	}

	for (;;) {
		struct imsg msg;

		if ((n = imsg_get(&session->parent.msgbuf, &msg)) == -1) {
			warn("imsg_get");
			goto abort;
		}

		if (n == 0)
			break;

		if (parent_dispatch_imsg(session, &msg) == -1) {
			imsg_free(&msg);
			goto abort;
		}

		imsg_free(&msg);
	}

	return;

	abort:
	event_loopbreak();
}

static int
parent_dispatch_imsg(struct session *session, struct imsg *msg)
{
	switch (imsg_get_type(msg)) {
	case IMSG_SESSION_CLIENT: {
		struct sockaddr_storage ss, *t;

		if (imsg_get_data(msg, &ss, sizeof(ss)) == -1) {
			warnx("parent sent bogus imsg");
			return -1;
		}

		t = reallocarray(session->clients, session->nclient + 1, 
			sizeof(*session->clients));
		if (t == NULL) {
			warn(NULL);
			return -1;
		}
		session->clients = t;
		session->clients[session->nclient++] = ss;
		break;
	}
	case IMSG_SESSION_LISTENER: {
		struct sockaddr_storage ss;
		socklen_t ss_len;
		struct listener *listener;
		int s;

		if (session->state > STATE_CONFIG) {
			warnx("parent sent config after lock");
			return -1;
		}

		if (imsg_get_data(msg, &ss, sizeof(ss)) == -1) {
			warnx("parent sent bogus imsg");
			return -1;
		}

		if ((s = socket(ss.ss_family, SOCK_DGRAM | SOCK_NONBLOCK, 
				IPPROTO_UDP)) == -1) {
			warn("socket");
			return -1;
		}

		switch (ss.ss_family) {
		case AF_INET:
			ss_len = sizeof(struct sockaddr_in);
			break;
		case AF_INET6:
			ss_len = sizeof(struct sockaddr_in6);
			break;
		default:
			warnx("parent sent bogus sockaddr");
			close(s);
			return -1;
		}
		if (bind(s, (struct sockaddr *)&ss, ss_len) == -1) {
			warn("bind");
			close(s);
			return -1;
		}

		if ((listener = malloc(sizeof(*listener))) == NULL) {
			warn(NULL);
			close(s);
			return -1;
		}

		event_set(&listener->ev, s, EV_READ | EV_PERSIST, listener_cb, 
			session);
		listener->fd = s;
		SLIST_INSERT_HEAD(&session->listeners, listener, entries);
		break;
	}
	case IMSG_SESSION_LISTENER_DONE: {
		struct listener *l;

		if (pledge("stdio", NULL) == -1)
			err(1, "pledge");

		session->state = STATE_LISTEN;

		SLIST_FOREACH(l, &session->listeners, entries) {
			if (event_add(&l->ev, NULL) == -1) {
				warn("event_add");
				return -1;
			}
		}
		break;
	}
	case IMSG_SESSION_TIMEOUT:
		if (imsg_get_data(msg, &session->timeout, 
				sizeof(session->timeout)) == -1) {
			warnx("parent sent bogus imsg");
			return -1;
		}

		if (evtimer_add(&session->timer, &session->timeout) == -1) {
			warn("evtimer_add");
			return -1;
		}
		break;
	default:
		warnx("parent sent bogus imsg");
		return -1;
	}

	return 0;
}

static void
session_close(struct session *session)
{
	free(session->clients);

	while (!SLIST_EMPTY(&session->listeners)) {
		struct listener *listener;

		listener = SLIST_FIRST(&session->listeners);

		SLIST_REMOVE_HEAD(&session->listeners, entries);
		event_del(&listener->ev);
		close(listener->fd);
		free(listener);
	}

	event_del(&session->parent.ev);
	imsg_clear(&session->parent.msgbuf);

	signal_del(&session->sigint);
	signal_del(&session->sigterm);

	event_base_free(session->eb);
}

static int
session_init(struct session *session)
{
	if (pledge("stdio inet", NULL) == -1)
		err(1, "pledge");

	if ((session->eb = event_init()) == NULL) {
		warn("event_init");
		return -1;
	}

	signal_set(&session->sigint, SIGINT, signal_cb, NULL);
	signal_set(&session->sigterm, SIGTERM, signal_cb, NULL);

	if (signal_add(&session->sigint, NULL) == -1) {
		warn("signal_add");
		goto eb;
	}

	if (signal_add(&session->sigterm, NULL) == -1) {
		warn("signal_add");
		goto sigint;
	}

	event_set(&session->parent.ev, PARENT_FD, EV_READ | EV_PERSIST, 
			parent_cb, session);
	if (event_add(&session->parent.ev, NULL) == -1) {
		warn("event_add");
		goto sigterm;
	}

	imsg_init(&session->parent.msgbuf, PARENT_FD);

	session->clients = NULL;
	session->nclient = 0;

	SLIST_INIT(&session->listeners);

	evtimer_set(&session->timer, timer_cb, session);
	memset(&session->timeout, 0, sizeof(session->timeout));

	session->state = STATE_CONFIG;

	return 0;

	sigterm:
	signal_del(&session->sigterm);
	sigint:
	signal_del(&session->sigint);
	eb:
	event_base_free(session->eb);
	return -1;
}

static void
signal_cb(int signo, short event, void *arg)
{
	event_loopbreak();
}

/* compare the address componenet of two sockaddrs. */
static int
sockaddr_cmp(struct sockaddr_storage *one, struct sockaddr_storage *two)
{
	if (one->ss_family != two->ss_family)
		return 1;

	switch (one->ss_family) {
	case AF_INET: {
		const struct sockaddr_in *n1 = (void *)one, *n2 = (void *)two;

		return memcmp(&n1->sin_addr, &n2->sin_addr, sizeof(n1->sin_addr));
	}
	case AF_INET6: {
		const struct sockaddr_in6 *n1 = (void *)one, *n2 = (void *)two;

		return memcmp(&n1->sin6_addr, &n2->sin6_addr, sizeof(n1->sin6_addr));
	}
	default:
		return 1;
	}
}

static void
timer_cb(int unused, short event, void *arg)
{
	struct session *session = arg;

	if (imsg_compose(&session->parent.msgbuf, IMSG_SESSION_TIMER, 0, 
			-1, -1, NULL, 0) == -1) {
		warn("imsg_compose");
		goto abort;
	}

	event_del(&session->parent.ev);
	event_set(&session->parent.ev, session->parent.msgbuf.fd, 
		EV_READ | EV_WRITE | EV_PERSIST, parent_cb, session);

	if (event_add(&session->parent.ev, NULL) == -1) {
		warn("event_add");
		goto abort;
	}

	/* parent will send SIGINT when it receives the mssage */

	return;

	abort:
	event_loopbreak();
}
