CFLAGS := -Wall -std=gnu99 -lm -g $(CFLAGS)

all: fs.o disk.o main

disk.o: disk.c disk.h

fs.o: fs.c fs.h

main: main.c fs.o disk.o
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f *.o main