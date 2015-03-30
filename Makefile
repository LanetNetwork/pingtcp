APP = pingtcp
OBJS = pingtcp.o
CC ?= cc
CFLAGS ?= -O3 -std=c99 -Wall -Wextra -pedantic \
	-D_DEFAULT_SOURCE -D_GNU_SOURCE \
	-Wwrite-strings \
	-Winit-self \
	-Wcast-qual \
	-Wpointer-arith \
	-Wstrict-aliasing \
	-Wformat=2 \
	-Wmissing-declarations \
	-Wmissing-include-dirs \
	-Wno-unused-parameter \
	-Wuninitialized \
	-Wold-style-definition \
	-Wstrict-prototypes \
	-Wmissing-prototypes
LDADD ?= -lrt

all: $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LDADD) -o $(APP)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f $(APP) $(OBJS)

.PHONY: all clean

