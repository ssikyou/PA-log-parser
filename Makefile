CC ?= gcc
AM_CFLAGS = -D_FILE_OFFSET_BITS=64 -D_FORTIFY_SOURCE=2
INCLUDE = -I. -I ./include -I ./CsvParser/include -I ./eMMCParser -I ./libxlsxwriter/include -I ./glib/include/glib-2.0 -I ./glib/lib/glib-2.0/include -I./funcs
CFLAGS ?= $(INCLUDE) -g #-static -O2 -pie -fPIE
LDFLAGS ?= -L ./libxlsxwriter/lib -L ./glib/lib #-static -pie -fPIE
LIBS = -lxlsxwriter -lz -lglib-2.0 -pthread

CHECKFLAGS = -Wall -Wuninitialized -Wundef #-w -Werror

#DEPFLAGS = -Wp,-MMD,$(@D)/.$(@F).d,-MT,$@

override CFLAGS := $(CHECKFLAGS) $(AM_CFLAGS) $(CFLAGS)

objects = \
	main.o \
	utils.o \
	CsvParser/src/csvparser.o \
	eMMCParser/emmcparser.o \
	eMMCParser/emmcfunc.o \
	eMMCParser/emmccb.o \
	eMMCParser/emmcxls_cmd_dist.o \
	eMMCParser/emmcxls_rw_dist.o \
	eMMCParser/emmcxls_sc_dist.o \
	funcs/func.o \
	funcs/config.o \
	funcs/cypress/file.o \
	funcs/cypress/cypress.o \
	funcs/cypress/cypress_cfg.o

progs = PAlogparser
depend_libs =

all: $(progs)

.c.o:
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

PAlogparser: $(objects) $(depend_libs)
	$(CC) -o $@ $(objects) $(LDFLAGS) $(LIBS)

clean:
	rm -f $(progs) $(objects)

.PHONY: all clean
