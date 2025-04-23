CC      := gcc
CFLAGS  := -Wall -g -O2 -pthread -lc

# Executables
all: lfl_sample lfl_criterion

lfl_test: lfl_sample.c lock_free_list.h
	$(CC) $(CFLAGS) -o $@ lfl_test.c

lfl_criterion: lfl_criterion.c lock_free_list.h
	$(CC) $(CFLAGS) -o $@ lfl_criterion.c -lcriterion

clean:
	rm -f lfl_sample lfl_criterion
	
check: lfl_criterion
	./lfl_criterion  --verbose -j1

.PHONY: all clean
