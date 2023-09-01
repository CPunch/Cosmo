# make clean && make && ./bin/cosmo

CC=clang
CFLAGS=-fPIE -Wall -Isrc -O3 -std=c99 #-g -fsanitize=address
LDFLAGS=-lm #-fsanitize=address
OUT=bin/cosmo

CHDR=\
	src/cchunk.h\
	src/cdebug.h\
	src/clex.h\
	src/cmem.h\
	src/coperators.h\
	src/cosmo.h\
	src/cparse.h\
	src/cstate.h\
	src/cvalue.h\
	src/ctable.h\
	src/cvm.h\
	src/cobj.h\
	src/cbaselib.h\
	src/cdump.h\
	src/cundump.h\
	util/linenoise.h\

CSRC=\
	src/cchunk.c\
	src/cdebug.c\
	src/clex.c\
	src/cmem.c\
	src/coperators.c\
	src/cparse.c\
	src/cstate.c\
	src/cvalue.c\
	src/ctable.c\
	src/cvm.c\
	src/cobj.c\
	src/cbaselib.c\
	src/cdump.c\
	src/cundump.c\
	util/linenoise.c\
	main.c\

COBJ=$(CSRC:.c=.o)

.c.o:
	$(CC) -c $(CFLAGS) $< -o $@

$(OUT): $(COBJ) $(CHDR)
	mkdir -p bin
	$(CC) $(COBJ) $(LDFLAGS) -o $(OUT)

clean:
	rm -rf $(COBJ) $(OUT)
