CC=gcc
CFLAGS= -Wall -g -o

BINS=notjustcats

all: $(BINS)

notjustcats: notjustcats.c
	$(CC) $(CFLAGS) notjustcats notjustcats.c

clean:
	rm $(BINS)
	rm -r Output
