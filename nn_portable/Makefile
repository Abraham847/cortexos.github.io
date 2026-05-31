CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lm

all: nn_demo

nn_demo: main.c nn.c nn.h
	$(CC) $(CFLAGS) main.c nn.c -o nn_demo $(LDFLAGS)

clean:
	rm -f nn_demo trained_xor.bin trained_xor.txt

run: nn_demo
	./nn_demo

.PHONY: all clean run
