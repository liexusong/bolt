
CC=gcc
CFLAGS=-fPIC -g -o
PROC=bolt
INCPATH=-I/usr/local/include/ImageMagick-6
INCLIB=-lpthread -lMagickWand-6.Q16 -levent

all:
	$(CC) $(INCPATH) $(CFLAGS) $(PROC) bolt.c connection.c gc.c hash.c http_parser.c net.c utils.c worker.c log.c $(INCLIB)
