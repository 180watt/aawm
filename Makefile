# Makefile for aawm
.POSIX:
.PHONY: install uninstall clean

PREFIX = /usr/local
BINDIR = $(PREFIX)/bin
SHAREDIR = $(PREFIX)/share

CC = clang
LDLIBS += `pkgconf --libs x11`
CFLAGS += -g -std=c99 -Wall -Wextra -O2 \
	`pkgconf --cflags x11`

aawm: wm.c
	$(CC) $(CFLAGS) $(LDLIBS) -o aawm wm.c

install:
	test -d $(DESTDIR)$(BINDIR) || mkdir -p $(DESTDIR)$(BINDIR)
	cp -f aawm $(DESTDIR)$(BINDIR)/aawm
	test -d $(DESTDIR)$(SHAREDIR)/xsessions || mkdir -p $(DESTDIR)$(SHAREDIR)/xsessions
	cp -f aawm.desktop $(DESTDIR)$(SHAREDIR)/xsessions/aawm.desktop

uinstall:
	rm -f $(DESTDIR)$(BINDIR)/aawm
	rm -f $(DESTDIR)$(SHAREDIR)/xsessions/aawm.desktop

clean:
	rm -f aawm

