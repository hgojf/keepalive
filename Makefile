SUBDIR = keepalivec keepalived keepalived-session

.include <bsd.subdir.mk>

beforeinstall:
	$(INSTALL) -m 0755 keepalive.rc /etc/rc.d/keepalived
