CC = gcc
CFLAGS = -Wall -Wextra -pedantic -Wformat=2 -O2 $$(pkg-config --cflags libpq)
LDLIBS = -lm $$(pkg-config --libs libpq)

name = stromzaehler
objects = main.o smlReader.o crc16.o date.o

$(name): $(objects)
	$(CC) $(CFLAGS) -o $@ $(objects) $(LDLIBS)

# A rule file.o : <dependencies> automatically depends on file.c
main.o:smlReader.h date.h
smlReader.o: smlReader.h crc16.h
crc16.o: crc16.h
date.o: date.h

.PHONY: clean
clean:
	rm -f $(name) $(objects)
