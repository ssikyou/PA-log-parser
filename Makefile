CC ?= gcc
AM_CFLAGS = -D_FILE_OFFSET_BITS=64 -D_FORTIFY_SOURCE=2
INCLUDE = -I.
CFLAGS ?= $(INCLUDE) -g -O2 -pie -fPIE
LDFLAGS ?= -Lhealthlib -pie -fPIE
LIBS =

CHECKFLAGS = -Wall -Wuninitialized -Wundef -Werror

#DEPFLAGS = -Wp,-MMD,$(@D)/.$(@F).d,-MT,$@

override CFLAGS := $(CHECKFLAGS) $(AM_CFLAGS) $(CFLAGS)

objects = \
	main.o \

progs = PAlogparser
depend_libs =

all: $(progs)

.c.o:
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

ffu: $(objects) $(depend_libs)
	$(CC) -o $@ $(objects) $(LDFLAGS) $(LIBS)

clean:
	rm -f $(progs) $(objects)

.PHONY: all clean
