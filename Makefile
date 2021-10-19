all:
	gcc -O2 -g -Wall -pthread collatz.c -o collatz

clean:
	rm -f collatz
