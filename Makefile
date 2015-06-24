#
#
#
#
#
#															   #
#  This Makefile builds the Unit test executable and the	   #	
#  shared library:  libslre.so								   #
#															   #
################################################################


CC=gcc

libname=libslre.so
unit_test=unit_test.bin
unit_test_shared=unit_test_shared.bin

all: $(unit_test) $(libname) $(unit_test_shared)


# Unit test compiled without shared library
#
$(unit_test):
	$(CC) -c slre.c
	$(CC) -c unit_test.c
	$(CC) -o unit_test.bin slre.o unit_test.o
	@echo "Done. OK! run $ make test"

# Unit Test compiled with shared library
#
$(unit_test_shared): $(libname)
	gcc  -c unit_test.c
	gcc -L$(shell pwd) -Wall -o unit_test_shared.bin unit_test.o -lslre

# Build shared library
#
$(libname):
	$(CC) -fPIC -g -c slre.c
	$(CC) -shared -Wl,-soname,$(libname) -o $(libname) slre.o -lc	

tests: $(unit_test) $(unit_test_shared)
	bash unit_test.sh


install:
	sudo cp slre.h /usr/include/slre.h
	sudo cp $(lib) /usr/lib/$(lib)

uninstall:
	rm -rf /usr/include/slre.h
	rm -rf /usr/lib/$(lib)

clean:
	@rm -rf *.o *.bin *.out *.so


