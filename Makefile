CC=gcc
CFLAGS=-fPIC -o
PROC=bolt
INCPATH=-I/usr/local/include/ImageMagick
INCLIB=-lpthread -lMagickWand -levent

all:
	$(CC) $(INCPATH) $(CFLAGS) $(PROC) bolt.c connection.c gc.c hash.c http_parser.c net.c utils.c worker.c log.c $(INCLIB)
