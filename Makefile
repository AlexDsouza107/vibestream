CC = gcc
CFLAGS = -std=c99 -Wall -Wextra
OBJS = main.o playlist.o liked.o stack.o queue.o

all: spotify engine

spotify: $(OBJS)
	$(CC) $(CFLAGS) -o spotify $(OBJS)

engine: engine.o playlist.o liked.o stack.o queue.o
	$(CC) $(CFLAGS) -o engine engine.o playlist.o liked.o stack.o queue.o

%.o: %.c
	$(CC) $(CFLAGS) -c $<

main.o: main.c song.h playlist.h liked.h stack.h queue.h
playlist.o: playlist.c playlist.h song.h
liked.o: liked.c liked.h song.h
stack.o: stack.c stack.h song.h
queue.o: queue.c queue.h song.h
engine.o: engine.c song.h playlist.h liked.h stack.h queue.h

clean:
	rm -f spotify engine $(OBJS) engine.o

.PHONY: clean
