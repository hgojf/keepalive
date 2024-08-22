#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <event.h>
#include <grp.h>
#include <imsg.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "keepalived.h"
#include "session.h"

struct keepalived {
	struct event_base *eb;

	struct event sigint;
	struct event sigterm;
	struct event sigchld;

	struct session {
		struct event ev;
		struct imsgbuf msgbuf;
		pid_t pid;
		int timeout;
	} session;
};

static int keepalived_close(struct keepalived *);
static int keepalived_init(int, struct keepalived_conf *, struct keepalived *);
static void session_cb(int, short, void *);
static int session_close(struct session *);
static int session_dispatch_imsg(struct session *, struct imsg *);
static int session_init(struct keepalived_conf *, struct session *);
static void signal_cb(int, short, void *);
static void usage(void);

int
main(int argc, char *argv[])
{
	struct keepalived_conf conf;
	struct keepalived keepalived;
	const char *config_path;
	int ch, configtest, debug, pretend, rv;

	config_path = PATH_KEEPALIVE_CONF;
	configtest = debug = pretend = 0;
	rv = 1;
	while ((ch = getopt(argc, argv, "df:np")) != -1) {
		switch (ch) {
		case 'd':
			debug = 1;
			break;
		case 'f':
			config_path = optarg;
			break;
		case 'n':
			configtest = 1;
			break;
		case 'p':
			pretend = 1;
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 0)
		usage();

	if (configure(config_path, &conf) == -1)
		errx(1, "configuration failed");

	if (configtest) {
		puts("configuration OK");
		rv = 0;
		goto config;
	}

	if (!debug) {
		if (daemon(0, 0) == -1) {
			warn("daemon");
			goto config;
		}
	}

	if (keepalived_init(pretend, &conf, &keepalived) == -1) {
		warnx("failed to initialized");
		goto config;
	}

	if (event_dispatch() == -1) {
		warn("event_dispatch");
		goto keepalived;
	}

	rv = 0;
	keepalived:
	keepalived_close(&keepalived);

	if (keepalived.session.timeout) {
		if (pretend)
			puts("We're done.");
		else {
			execl(PATH_SHUTDOWN, "shutdown", "-p", "now", NULL);
			err(1, "%s", PATH_SHUTDOWN);
		}
	}

	config:
	config_free(&conf);
	return rv;
}

static void
session_cb(int fd, short event, void *arg)
{
	struct session *session;
	ssize_t n;

	session = arg;
	if (event & EV_WRITE) {
		if (imsg_flush(&session->msgbuf) == -1) {
			if (errno == EAGAIN)
				goto read;
			warn("imsg_flush");
			goto abort;
		}

		event_del(&session->ev);
		event_set(&session->ev, session->msgbuf.fd, EV_READ | EV_PERSIST, session_cb, session);
		if (event_add(&session->ev, NULL) == -1) {
			warn("event_add");
			goto abort;
		}
	}

	read:
	if (!(event & EV_READ))
		return;

	if ((n = imsg_read(&session->msgbuf)) == -1) {
		if (errno == EAGAIN)
			return;
		warn("imsg_read");
		goto abort;
	}

	if (n == 0) {
		warnx("imsg_read EOF");
		goto abort;
	}

	for (;;) {
		struct imsg msg;

		if ((n = imsg_get(&session->msgbuf, &msg)) == -1) {
			warn("imsg_get");
			goto abort;
		}

		if (n == 0)
			break;

		if (session_dispatch_imsg(session, &msg) == -1) {
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
session_dispatch_imsg(struct session *session, struct imsg *msg)
{
	switch (imsg_get_type(msg)) {
	case IMSG_SESSION_TIMER:
		session->timeout = 1;
		event_loopbreak();
		break;
	default:
		warnx("session sent bogus imsg");
		return -1;
	}

	return 0;
}

static int
keepalived_close(struct keepalived *keepalived)
{
	session_close(&keepalived->session);

	signal_del(&keepalived->sigterm);
	signal_del(&keepalived->sigint);

	event_base_free(keepalived->eb);

	return 0;
}

static int
keepalived_init(int pretend, struct keepalived_conf *conf, struct keepalived *out)
{
	struct group *grp;
	struct passwd *pwd;

	if ((out->eb = event_init()) == NULL) {
		warn("event_init");
		return -1;
	}

	signal_set(&out->sigint, SIGINT, signal_cb, NULL);
	signal_set(&out->sigterm, SIGTERM, signal_cb, NULL);

	if (signal_add(&out->sigint, NULL) == -1) {
		warn("signal_add");
		goto eb;
	}

	if (signal_add(&out->sigterm, NULL) == -1) {
		warn("signal_add");
		goto sigint;
	}

	signal(SIGPIPE, SIG_IGN);

	if (session_init(conf, &out->session) == -1) {
		warnx("failed to start session process");
		goto sigterm;
	}

	if ((grp = getgrnam(GRP_SHUTDOWN)) == NULL) {
		warn("getgrnam %s", GRP_SHUTDOWN);
		goto session;
	}

	if ((pwd = getpwnam(KEEPALIVED_USER_PRIV)) == NULL) {
		warn("getpwnam %s", KEEPALIVED_USER_PRIV);
		goto session;
	}

	if (setgroups(1, &grp->gr_gid) == -1) {
		warn("setgroups");
		goto session;
	}

	if (setresgid(pwd->pw_gid, pwd->pw_gid, pwd->pw_gid) == -1) {
		warn("setresgid");
		goto session;
	}

	if (setresuid(pwd->pw_uid, pwd->pw_uid, pwd->pw_uid) == -1) {
		warn("setresuid");
		goto session;
	}

	if (!pretend) {
		if (unveil(PATH_SHUTDOWN, "x") == -1) {
			warn("%s", PATH_SHUTDOWN);
			goto session;
		}

		if (pledge("stdio proc exec", NULL) == -1)
			err(1, "pledge");
	}
	else
		if (pledge("stdio proc", NULL) == -1)
			err(1, "pledge");

	return 0;

	session:
	session_close(&out->session);
	sigterm:
	signal_del(&out->sigterm);
	sigint:
	signal_del(&out->sigint);
	eb:
	event_base_free(out->eb);
	return -1;
}

static int
session_close(struct session *session)
{
	int status;

	event_del(&session->ev);
	close(session->msgbuf.fd);
	imsg_clear(&session->msgbuf);

	kill(session->pid, SIGINT);
	if (waitpid(session->pid, &status, 0) == -1) {
		warn("waitpid");
		return -1;
	}

	if (WEXITSTATUS(status) != 0) {
		warnx("session process exited with nonzero status");
		return -1;
	}

	return 0;
}

static int
session_init(struct keepalived_conf *conf, struct session *session)
{
	struct timeval tv;
	struct passwd *pwd;
	int sv[2];

	if ((pwd = getpwnam(KEEPALIVED_USER)) == NULL) {
		warn("getpwnam %s", KEEPALIVED_USER);
		return -1;
	}

	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
			PF_UNSPEC, sv) == -1) {
		warn("socketpair");
		return -1;
	}

	switch (session->pid = fork()) {
	case -1:
		warn("fork");
		goto sv;
	case 0:
		if (setgroups(1, &pwd->pw_gid) == -1)
			err(1, "setgroups");
		if (setresgid(pwd->pw_gid, pwd->pw_gid, pwd->pw_gid) == -1)
			err(1, "setresgid");
		if (setresuid(pwd->pw_uid, pwd->pw_uid, pwd->pw_uid) == -1)
			err(1, "setresuid");

		if (dup2(sv[1], 3) == -1)
			err(1, "dup2");
		execl(PATH_KEEPALIVED_SESSION, "keepalived-session", NULL);
		err(1, "%s", PATH_KEEPALIVED_SESSION);
	default:
		break;
	}

	imsg_init(&session->msgbuf, sv[0]);

	for (size_t i = 0; i < conf->nlistener; i++) {
		if (imsg_compose(&session->msgbuf, IMSG_SESSION_LISTENER, 0, -1, -1,
				&conf->listeners[i].ss, sizeof(conf->listeners[i].ss)) == -1) {
			warn("imsg_compose");
			goto msgbuf;
		}
	}

	if (imsg_compose(&session->msgbuf, IMSG_SESSION_LISTENER_DONE, 0, 
			-1, -1, NULL, 0) == -1) {
		warn("imsg_compose");
		goto msgbuf;
	}

	for (size_t i = 0; i < conf->nclient; i++) {
		if (imsg_compose(&session->msgbuf, IMSG_SESSION_CLIENT, 0, -1, -1,
				&conf->clients[i].ss, sizeof(conf->clients[i].ss)) == -1) {
			warn("imsg_compose");
			goto msgbuf;
		}
	}

	memset(&tv, 0, sizeof(tv));
	tv.tv_sec = conf->timeout;
	if (imsg_compose(&session->msgbuf, IMSG_SESSION_TIMEOUT, 0, -1, -1, &tv, 
			sizeof(tv)) == -1) {
		warn("imsg_compose");
		goto msgbuf;
	}

	event_set(&session->ev, sv[0], EV_READ | EV_PERSIST | EV_WRITE, 
		session_cb, session);
	if (event_add(&session->ev, NULL) == -1) {
		warn("event_add");
		goto msgbuf;
	}

	session->timeout = 0;

	close(sv[1]);
	return 0;

	msgbuf:
	imsg_clear(&session->msgbuf);
	sv:
	close(sv[0]);
	close(sv[1]);
	return -1;
}

static void
signal_cb(int signo, short event, void *arg)
{
	event_loopbreak();
}

static void
usage(void)
{
	fprintf(stderr, "usage: keepalived [-dnp] [-f file]\n");
	exit(2);
}
