CC = gcc
LINKERFLAG = -lrt -lpthread -lm 


all: cars_demo

cars_demo: simulator manager firealarm

simulator: simulator.c
	${CC} simulator.c -o simulator ${LINKERFLAG}

manager: manager.c
	${CC} manager.c -o manager ${LINKERFLAG}

firealarm: firealarm.c
	${CC} firealarm.c -o firealarm ${LINKERFLAG}

clean: 
	rm -f simulator manager firealarm