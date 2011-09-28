WM=wmii

SYS=x11
CC=gcc
PROG=awm
CFLAGS=-g -Werror -Wall
LIBS=-Wl,--as-needed -lX11 -lXinerama
TEST=DISPLAY=:2.0

WIN32?=
ifdef WIN32
SYS=win32
CC=i686-pc-mingw32-gcc
CFLAGS=-g -Werror -Wall -D_NO_OLDNAMES -DMARGIN=15
LIBS=
PROG=awm.exe
TEST=cp -t /t/htdocs/temp
endif

test: $(PROG)
	$(TEST) ./$<

debug: $(PROG)
	$(TEST) gdb ./$<

$(PROG): main.o util.o sys-$(SYS).o wm-$(WM).o
	$(CC) $(CFLAGS) -o $@ $+ $(LIBS)

%.o: %.c $(wildcard *.h) makefile
	$(CC) --std=gnu99 $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(PROG) *.o
