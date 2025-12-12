all: aesdsocket

aesdsocket.o: aesdsocket.c
	$(CC) $(CCFLAGS) -c aesdsocket.c -Wall -Werror

aesdsocket: aesdsocket.o
	$(CC) $(LDFLAGS) aesdsocket.o -o aesdsocket

clean:
	rm -f aesdsocket.o aesdsocket

valgrind:
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose --log-file=/tmp/valgrind-out.txt ./aesdsocket