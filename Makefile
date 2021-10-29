CC = gcc
CFLAGS = -lrt -lpthread  -lm #Show all ressaonable warnings
LDFLAGS = 
DEPS = header.h

all: cars_demo

cars_demo: simulator.o manager.o firealarm.o

manager.o: manager.c

simulator.o: simulator.c

firealarm.o: firealarm.c

clean: 
	rm -f cars_demo *.o

.PHONY: all clean