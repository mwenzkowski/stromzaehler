CC = gcc
CFLAGS = -Wall -Wextra -pedantic -Wformat=2 -O2
LDLIBS = -lcurl

name = stromzaehler
objects = main.o smlReader.o crc16.o

$(name): $(objects)
	$(CC) $(CFLAGS) -o $@ $(objects) $(LDLIBS)

# Eine Regel file.o : <Abhängigkeiten> hängt automatisch von file.c ab
main.o:smlReader.h
smlReader.o: smlReader.h crc16.h
crc16.o: crc16.h

.PHONY: clean
clean:
	rm -f $(name) $(objects)
