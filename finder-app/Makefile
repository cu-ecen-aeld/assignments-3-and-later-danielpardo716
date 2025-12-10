all: writer

writer.o: writer.c
	$(CC) $(CCFLAGS) -c writer.c

writer: writer.o
	$(CC) $(LDFLAGS) writer.o -o writer

clean:
	rm -f writer.o writer

sysroot:
	aarch64-none-linux-gnu-gcc -v --print-sysroot > ../assignments/assignment2/cross-compile.txt 2>&1
