all: eshell

eshell: eshell.c parser.c
	gcc -g -Wall eshell.c parser.c -o eshell

clean:
	rm -f eshell
