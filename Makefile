CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
LDFLAGS ?= -lncurses
PREFIX ?= /usr/local

pcmd: pcmd.c
	$(CC) $(CFLAGS) -o pcmd pcmd.c $(LDFLAGS)

install: pcmd
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 pcmd $(DESTDIR)$(PREFIX)/bin/pcmd

clean:
	rm -f pcmd
