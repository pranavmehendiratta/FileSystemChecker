all: 
	gcc xcheck.c -o xcheck -Wall -Werror -O
clean: 
	rm -rf ./xcheck
test: 
	./xcheck fs.img
valgrind: 
	valgrind --leak-check=full --show-leak-kinds=all -v ./xcheck fs.img
