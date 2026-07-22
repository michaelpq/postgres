PGFILEDESC = "getopt_long_test - test PostgreSQL getopt_long() function"
PGAPPICON = win32

subdir = src/bin/getopt_long_test
top_builddir = ../../..
include $(top_builddir)/src/Makefile.global

PG_CPPFLAGS += -I$(top_srcdir)/src/port

OBJS = \
	$(WIN32RES) \
	getopt_long.o \
	getopt_long_test.o

all: getopt_long_test

getopt_long_test: $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) $(LDFLAGS_EX) $(LIBS) -o $@$(X)

getopt_long_test.o: getopt_long_test.c $(top_builddir)/src/Makefile.global

getopt_long.c:
	ln -s $(top_srcdir)/src/port/getopt_long.c .

clean distclean:
	rm -f getopt_long_test$(X) $(OBJS) getopt_long.c
