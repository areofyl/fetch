CC ?= cc
CFLAGS ?= -O2
PREFIX ?= /usr/local
LDFLAGS ?=
LDLIBS = -lm

fetch: fetch.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

install: fetch
	install -Dm755 fetch $(DESTDIR)$(PREFIX)/bin/fetch

clean:
	rm -f fetch

.PHONY: install clean
