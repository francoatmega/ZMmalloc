CC     = gcc
CFLAGS = -Wall -Wextra -g -O0 -fno-omit-frame-pointer -m64

all: libzmmalloc.so demo

demo: demo.c zmmalloc.c zmmalloc.h
	$(CC) $(CFLAGS) demo.c zmmalloc.c -o demo

libzmmalloc.so: zmmalloc.c zmmalloc.h
	$(CC) $(CFLAGS) -fPIC -shared zmmalloc.c -o libzmmalloc.so

clean:
	rm -f demo libzmmalloc.so

.PHONY: all clean
