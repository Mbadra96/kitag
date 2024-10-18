output1.kicad_sym: main
	./build/main.exe
main:
	gcc main.c -O3 -Wall -Wextra -pedantic -o build/main 