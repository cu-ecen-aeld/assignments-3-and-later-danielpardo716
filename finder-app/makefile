all:
	${CROSS_COMPILE}gcc -o writer writer.c
clean:
	rm -f writer
sysroot:
	aarch64-none-linux-gnu-gcc -v --print-sysroot > ../assignments/assignment2/cross-compile.txt 2>&1
