CC       = gcc
LIBS     = -lusb-1.0
CFLAGS   = -O2 -g -Wno-unused-result

.PHONY: all clean

all:    fbrflash

clean: 
	rm *.o
	rm fbrflash

#.c.o:
#	$(CC) -o $@ $(LIBS) $^ qcio.o

fbrflash: fbrflash.o
	$(CC) $^ -o $@ $(LIBS)
