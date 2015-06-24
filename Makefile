#
#
#
#
#
#
#
#
#
#
#
#####################################################


CC=gcc

all: unit_test lib

unit_test:
	$(CC) -c slre.c
	$(CC) -c unit_test.c
	$(CC) -o unit_test.bin slre.o unit_test.o
	@echo "Done. OK! run $ make test"


lib:
	$(CC) -fPIC -g -c slre.c
	$(CC) -shared -Wl,-soname,libslre.so -o libslre.so slre.o -lc	

test:
	@echo "Running unit_test.bin"
	./unit_test.bin

clean:
	@rm -rf *.o *.bin *.out *.so
