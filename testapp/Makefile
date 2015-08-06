 
ALL  = test_app image

INCGTK = `pkg-config --cflags gtk+-2.0`
LDGTK = `pkg-config --libs gtk+-2.0` -lm 
CFLAGS=-g -I.. -I../module $(INCGTK)
CC=gcc
 
all: $(ALL)
 
clean:
	rm -f *.o *.ver *~
 
test_app: lib.o test_app.o dinter.o demux.o
	$(CC) -g -o test_app lib.o test_app.o dinter.o demux.o

image: image.o uart.o lib.o demux.o
	$(CC) -g -o image image.o uart.o lib.o demux.o $(LDGTK)  -lpthread

