CFLAGS?=-Wall -O2
LDADD?=`pkg-config --cflags --libs x11 xinerama`

all:
	$(CC) -o pixelclock pixelclock.c $(CFLAGS) $(LDADD) $(LDFLAGS)

clean:
	rm -f pixelclock
