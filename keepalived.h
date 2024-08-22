#ifndef KEEPALIVE_KEEPALIVED_H
#define KEEPALIVE_KEEPALIVED_H
#include <sys/socket.h>

struct keepalived_client_conf {
	struct sockaddr_storage ss;
};

struct keepalived_listener_conf {
	struct sockaddr_storage ss;
};

struct keepalived_conf {
	struct keepalived_client_conf *clients;
	size_t nclient;

	struct keepalived_listener_conf *listeners;
	size_t nlistener;

	long long timeout;
};

#define GRP_SHUTDOWN "_shutdown"
#define KEEPALIVED_TIMEOUT 300
#define KEEPALIVED_USER "_keepalived"
#define KEEPALIVED_USER_PRIV "_keepalived-priv"
#define PATH_KEEPALIVE_CONF "/etc/keepalive.conf"
#define PATH_KEEPALIVED_SESSION "/usr/local/libexec/keepalived-session"
#define PATH_SHUTDOWN "/sbin/shutdown"

void config_free(struct keepalived_conf *);
int configure(const char *, struct keepalived_conf *);
#endif /* KEEPALIVE_KEEPALIVED_H */
