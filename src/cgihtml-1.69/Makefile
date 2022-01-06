# Makefile for cgihtml.a
# $Id: Makefile 2 2007-09-28 04:43:04Z jacl $

# macros and variables
CC= gcc
RANLIB= ranlib
CFLAGS= -O -Wall -DUNIX #-DUPLOADDIR='"/tmp"'
#CFLAGS = -g -Wall -DWINDOWS #-DUPLOADDIR='"/tmp"'

INSTALLDIR= /usr/local/etc/httpd/cgi-src

OBJS = string-lib.o cgi-llist.o cgi-lib.o html-lib.o
LIB = libcgihtml.a

# targets
$(LIB): $(OBJS)
	ar cr $@ $(OBJS)
	$(RANLIB) $@

examples: $(LIB)
	cd examples; make all

all: examples

install: all
	cp -f *.h $(LIB) $(INSTALLDIR)
	cd examples; make install

clean:
	rm -f *.o $(LIB)

clobber:
	rm -f *.o $(LIB)
	cd examples; make clean

