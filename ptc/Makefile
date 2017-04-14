CC=$(CROSS_COMPILE)gcc
CFLAGS=-Wall

all: atqt1-xpro_demo atqt2-xpro_demo

atqt1-xpro_demo: atqt1-xpro_demo.o
	$(CC) -o atqt1-xpro_demo atqt1-xpro_demo.o --static

atqt1-xpro_demo.o: atqt1-xpro_demo.c
	$(CC) -o atqt1-xpro_demo.o -c atqt1-xpro_demo.c $(CFLAGS)

atqt2-xpro_demo: atqt2-xpro_demo.o
	$(CC) -o atqt2-xpro_demo atqt2-xpro_demo.o --static

atqt2-xpro_demo.o: atqt2-xpro_demo.c
	$(CC) -o atqt2-xpro_demo.o -c atqt2-xpro_demo.c $(CFLAGS)

clean:
	rm -rf *.o

mrproper: clean
	rm -rf atqt1-xpro_demo atqt2-xpro_demo
