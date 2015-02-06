APP = pingtcp
OBJS = pingtcp.o
CFLAGS ?= -O3 -std=c99 -Wall -Wextra -pedantic -ffast-math -funroll-loops -D_GNU_SOURCE
LDADD ?= -lrt

all: $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LDADD) -o $(APP)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f $(APP) $(OBJS)

.PHONY: all clean

