CC ?= ${CROSS_COMPILE}gcc
CCFLAGS ?= -g -Wall -Werror
TARGET ?= aesdsocket
LDFLAGS ?= -lpthread -lrt

all: aesdsocket

aesdsocket.o: aesdsocket.c
	$(CC) $(CCFLAGS) -c aesdsocket.c

aesdsocket: aesdsocket.o
	$(CC) $(LDFLAGS) aesdsocket.o -o aesdsocket

clean:
	rm -f aesdsocket.o aesdsocket