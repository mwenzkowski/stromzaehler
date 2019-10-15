CC = gcc
CFLAGS = -Wall -Wextra -pedantic -Wformat=2 -O2
LDLIBS = -lcurl

objects = main.o smlReader.o crc16.o

stromzähler: $(objects)
	$(CC) $(CFLAGS) -o $@ $(objects) $(LDLIBS)

.PHONY: clean
clean:
	rm stromzähler $(objects)
