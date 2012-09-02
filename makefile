# wmpus - cross platofrm window manager
# See LICENSE file for copyright and license details.

-include config.mk

VERSION   ?= 0.1-p0
WM        ?= wmii
SYS       ?= x11
CFLAGS    ?= -g -Wall
PREFIX    ?= /usr/local
MANPREFIX ?= ${PREFIX}/share/man

ifeq ($(SYS),x11)
GCC       ?= gcc
PROG      ?= wmpus
LDFLAGS   += -lX11 -lXinerama
endif

ifeq ($(SYS),win32)
GCC       ?= i486-mingw32-gcc
PROG      ?= wmpus.exe
LDFLAGS   += -lgdi32
endif

all: $(PROG)

clean:
	rm -f wmpus *.exe *.o

dist:
	tar -czf wmpus-$(VERSION).tar.gz --transform s::wmpus-$(VERSION)/: \
		README LICENSE config.mk.example makefile *.1 *.c *.h

install: all
	install -m 755 -D $(PROG) $(DESTDIR)$(PREFIX)/bin/$(PROG)
	install -m 644 -D wmpus.1 $(DESTDIR)$(MANPREFIX)/man1/wmpus.1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(PROG)
	rm -f $(DESTDIR)$(MANPREFIX)/man1/wmpus.1

$(PROG): main.o conf.o util.o sys-$(SYS).o wm-$(WM).o
	$(GCC) $(CFLAGS) -o $@ $+ $(LDFLAGS)

%.o: %.c $(wildcard *.h) makefile
	$(GCC) $(CFLAGS) --std=gnu99 -c -o $@ $<

.PHONY: all clean dist install uninstall
