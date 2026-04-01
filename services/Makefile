# Makefile – two independent user-space services
CC      = gcc
CFLAGS  = -O2 -Wall -pipe
LDFLAGS = -static   # makes booting simpler

all: audio-service net-service

audio-service: audio-service.c mailbox.c
	${CC} ${CFLAGS} -o $@ $^ ${LDFLAGS}

net-service: net-service.c mailbox.c
	${CC} ${CFLAGS} -o $@ $^ ${LDFLAGS}

clean:
	rm -f audio-service net-service

install: all
	install -m 755 audio-service /sbin/audio-service
	install -m 755 net-service   /sbin/net-service