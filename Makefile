all: myshell

myshell: main.o LineParser.o
	gcc -g -m32 main.o LineParser.o  -o myshell

main.o: main.c
	gcc -g -c -m32 -o main.o main.c

LineParser.o: LineParser.c
	gcc -g -c -m32 -o LineParser.o LineParser.c

clean:
	rm -rf ./*.o myshell

