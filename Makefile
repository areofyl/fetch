CC ?= cc
CFLAGS ?= -O2
PREFIX ?= /usr/local

fetch: fetch.c
	$(CC) $(CFLAGS) -o $@ $< -lm

install: fetch
	install -Dm755 fetch $(DESTDIR)$(PREFIX)/bin/fetch

clean:
	rm -f fetch

.PHONY: install clean
