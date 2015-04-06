# wmpus - cross platofrm window manager
# See LICENSE file for copyright and license details.

-include config.mk

# Common configuration
VERSION   ?= 0.1-rc1
WM        ?= wmii
SYS       ?= x11
CFLAGS    ?= -g -Wall
PREFIX    ?= /usr/local
MANPREFIX ?= ${PREFIX}/share/man

# System specific configuration
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

PROTOCOL  ?= wayland gtk-shell xdg-shell drm
HEADERS   += ${PROTOCOL:%=protocol/%-client-protocol.h}
HEADERS   += ${PROTOCOL:%=protocol/%-server-protocol.h}
OBJECTS   += ${PROTOCOL:%=protocol/%-protocol.o}

sys-xwl.o: CFLAGS  += $(shell pkg-config --cflags gtk+-3.0 libevdev)
wmpus:     LDFLAGS += $(shell pkg-config --libs   gtk+-3.0)
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

# Targets
all: $(HEADERS) $(PROG)

clean:
	rm -f wmpus *.exe *.o protocol/*.[cho]

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

# Common Rules
$(PROG): main.o conf.o util.o sys-$(SYS).o wm-$(WM).o $(OBJECTS)
	$(GCC) $(CFLAGS) -o $@ $+ $(LDFLAGS)

%.o: %.c $(wildcard *.h) makefile
	$(GCC) $(CFLAGS) --std=gnu99 -c -o $@ $<

# Wayland Rules
%-protocol.c: %.xml
	wayland-scanner code < $+ > $@

%-server-protocol.h: %.xml
	wayland-scanner server-header < $+ > $@

%-client-protocol.h: %.xml
	wayland-scanner client-header < $+ > $@

.PHONY: all clean dist install uninstall
