CC      := gcc
CFLAGS  := -Wall -Wextra -ggdb -std=c11 -g -I. 
TARGET  := test/main

SRC = \
    src/PCI_SIG_8B_ECC.c\
    src/PCI_SIG_8B_CRC.c\
	test/read_payload.c \
	test/tester.c \
    test/main.c

OBJ = $(SRC:.c=.o)

.PHONY: all run clean rand help

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

run: $(TARGET)
	cd test && ./main $(ARGS)

rand: $(TARGET)
	cd test && ./main -r $(ARGS)

help: $(TARGET)
	cd test && ./main -h