CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=gnu11
LDLIBS  = -pthread -lm

meeseeks: src/meeseeks.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f meeseeks

.PHONY: clean
