WM=wmii

SYS=x11
CC=gcc
PROG=awm
CFLAGS=-g -Wall
LIBS=-Wl,--as-needed -lX11

#SYS=win32
#CC=i686-pc-mingw32-gcc
#CFLAGS=-g -Wall -mwindows
#PROG=awm.exe

test: $(PROG)
	DISPLAY=:2.0 ./$<

$(PROG): main.o util.o sys-$(SYS).o wm-$(WM).o
	$(CC) $(CFLAGS) -o $@ $+ $(LIBS)

%.o: %.c $(wildcard *.h)
	$(CC) --std=gnu99 $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(PROG) *.o
