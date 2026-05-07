#-------------------------------------------------------------------------
#
# src/tools/pgindent/Makefile
#
# Copyright (c) 2024-2026, PostgreSQL Global Development Group
#
#-------------------------------------------------------------------------

PGFILEDESC = "pgindent - PostgreSQL source code indenter"
PGAPPICON = win32

subdir = src/tools/pgindent
top_builddir = ../../..
include $(top_builddir)/src/Makefile.global

override CPPFLAGS := -I$(srcdir) $(CPPFLAGS)

OBJS = \
	$(WIN32RES) \
	args.o \
	err.o \
	indent_core.o \
	io.o \
	lexi.o \
	parse.o \
	postprocess.o \
	pr_comment.o \
	preprocess.o

$(OBJS): CFLAGS += $(PERMIT_MISSING_VARIABLE_DECLARATIONS)

all: pgindent

pgindent: $(OBJS) | submake-libpgport
	$(CC) $(CFLAGS) $^ $(LDFLAGS) $(LDFLAGS_EX) $(LIBS) -o $@$(X)

install: all installdirs
	$(INSTALL_PROGRAM) pgindent$(X) '$(DESTDIR)$(bindir)/pgindent$(X)'

installdirs:
	$(MKDIR_P) '$(DESTDIR)$(bindir)'

uninstall:
	rm -f '$(DESTDIR)$(bindir)/pgindent$(X)'

clean distclean:
	rm -f pgindent$(X) $(OBJS)
	rm -rf log/ tmp_check/

check: pgindent
	$(prove_check)

installcheck:
	$(prove_installcheck)

.PHONY: test

test: pgindent
	$(prove_installcheck)
