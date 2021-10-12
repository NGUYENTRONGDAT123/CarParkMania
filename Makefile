CC = gcc
CFLAGS = -Wall -pedantic #Show all ressaonable warnings
LDFLAGS = 

all: cars_demo

cars_demo: manager.o simulator.o firealarm.o

manager.o: manager.c

simulator.o: simulator.c

firealarm: firealarm.c

clean: 
	rm -f cars_demo *.o

.PHONY: all clean