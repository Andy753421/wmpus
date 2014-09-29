# wmpus - cross platofrm window manager
# See LICENSE file for copyright and license details.

-include config.mk

VERSION   ?= 0.1-rc1
WM        ?= wmii
SYS       ?= x11
CFLAGS    ?= -g -Wall
PREFIX    ?= /usr/local
MANPREFIX ?= ${PREFIX}/share/man

ifeq ($(SYS),xcb)
GCC       ?= gcc
PROG      ?= wmpus
LDFLAGS   += -lxcb -lxcb-keysyms -lxcb-util -lxcb-icccm -lxcb-ewmh -lxcb-xinerama
endif

ifeq ($(SYS),xlib)
GCC       ?= gcc
PROG      ?= wmpus
LDFLAGS   += -lX11 -lXinerama
endif

ifeq ($(SYS),xwl)
GCC       ?= gcc
PROG      ?= wmpus
LDFLAGS   += -lwayland-client -lwayland-server

sys-xwl.o: CFLAGS  += $(shell pkg-config --cflags gtk+-2.0)
wmpus:     LDFLAGS += $(shell pkg-config --libs gtk+-2.0)
endif

ifeq ($(SYS),wl)
GCC       ?= gcc
PROG      ?= wmpus
LDFLAGS   += -lwayland-client -lwayland-server
endif

ifeq ($(SYS),swc)
GCC       ?= gcc
PROG      ?= wmpus
LDFLAGS   += -lswc -lwayland-client -lwayland-server
endif

ifeq ($(SYS),win32)
GCC       ?= mingw32-gcc
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
	sed -i 's:/usr.*:$(PREFIX)/bin/wmpus:' wmpus.session
	install -m 755 -D $(PROG) $(DESTDIR)$(PREFIX)/bin/$(PROG)
	install -m 644 -D wmpus.1 $(DESTDIR)$(MANPREFIX)/man1/wmpus.1
	install -m 755 -D wmpus.session $(DESTDIR)/etc/X11/Sessions/wmpus
	install -m 644 -D wmpus.desktop $(DESTDIR)$(PREFIX)/share/xsessions/wmpus.desktop

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(PROG)
	rm -f $(DESTDIR)$(MANPREFIX)/man1/wmpus.1

$(PROG): main.o conf.o util.o sys-$(SYS).o wm-$(WM).o
	$(GCC) $(CFLAGS) -o $@ $+ $(LDFLAGS)

%.o: %.c $(wildcard *.h) makefile
	$(GCC) $(CFLAGS) --std=gnu99 -c -o $@ $<

.PHONY: all clean dist install uninstall
