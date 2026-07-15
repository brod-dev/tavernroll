CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
LDLIBS  := -lm
BIN     := tavernroll

.PHONY: all test clean run

all: $(BIN)

$(BIN): src/main.c src/dice.c src/dice.h
	$(CC) $(CFLAGS) -o $(BIN) src/main.c src/dice.c $(LDLIBS)

test: test_dice
	./test_dice

test_dice: tests/test_dice.c src/dice.c src/dice.h
	$(CC) $(CFLAGS) -o test_dice tests/test_dice.c src/dice.c $(LDLIBS)

run: all
	./$(BIN) 4d6kh3

clean:
	rm -f $(BIN) test_dice
