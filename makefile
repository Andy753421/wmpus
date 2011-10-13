WM     ?= wmii
SYS    ?= x11
CFLAGS ?= -g -Wall -Werror

ifeq ($(SYS),x11)
CC      = gcc
LIBS   += -lX11 -lXinerama
PROG    = wmpus
endif

ifeq ($(SYS),win32)
CC      = i686-pc-mingw32-gcc
CFLAGS += -D_MODE_T_
LIBS   += -lgdi32
PROG    = wmpus.exe
endif

include config.mk

$(PROG): main.o conf.o util.o sys-$(SYS).o wm-$(WM).o
	$(CC) $(CFLAGS) -o $@ $+ $(LIBS)

%.o: %.c $(wildcard *.h) makefile
	$(CC) --std=gnu99 $(CFLAGS) -c -o $@ $<

clean:
	rm -f wmpus *.exe *.o
