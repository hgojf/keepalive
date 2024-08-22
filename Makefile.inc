.PATH: ${.CURDIR}/..

CFLAGS += -I${.CURDIR}/..
WARNINGS = Yes

PREFIX ?= /usr/local/

realinstall:
	${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} \
		-m ${BINMODE} ${PROG} ${DESTDIR}${PREFIX}${BINDIR}/${PROG}
