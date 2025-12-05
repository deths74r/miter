CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c99 -g
LDFLAGS = -lpcre2-8

miter: miter.c
	rm -f miter
	$(CC) $(CFLAGS) miter.c -o miter $(LDFLAGS)

clean:
	rm -f miter
