#ifndef KEEPALIVE_SESSION_H
#define KEEPALIVE_SESSION_H
/* session -> parent */
enum {
	IMSG_SESSION_TIMER,
};

/* parent -> session */
enum {
	IMSG_SESSION_CLIENT,
	IMSG_SESSION_LISTENER,
	IMSG_SESSION_LISTENER_DONE,
	IMSG_SESSION_TIMEOUT,
};
#endif /* KEEPALIVE_SESSION_H */
