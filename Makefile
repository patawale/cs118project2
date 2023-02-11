USERID=123456789

default: build

build: server.c client.c
	gcc -Wall -Wextra -o server server.c
	gcc -Wall -Wextra -o client client.c

clean:
	rm -rf *.o server client *.tar.gz

dist: zip
zip: clean
	zip ${USERID}.zip server.c client.c Makefile