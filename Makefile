all: 
	gcc xcheck.c -o xcheck -Wall -Werror -O
clean: 
	rm -rf ./xcheck
test: 
	./xcheck 20k.txt
