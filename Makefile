CC=gcc
CFLAGS=-fPIC -std=c99 -pedantic -o
PROC=bolt
#INCPATH=
INCLIB=-lpthread -lMagickWand -levent

all:
	$(CC) $(CFLAGS) $(PROC) bolt.c connection.c gc.c hash.c http_parser.c net.c special_response.c utils.c worker.c $(INCLIB)
