CC = gcc
CFLAGS := -Wall -Wextra -pedantic -O2 -g -D_REENTRANT $(CFLAGS)
LDLIBS := -lX11

sucktus: sucktus.o

clean:
	$(RM) sucktus sucktus.o
