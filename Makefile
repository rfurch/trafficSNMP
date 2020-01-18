# This program is free software; you can redistribute
# it and/or modify it under the terms of the GNU GPL

#### Start of system configuration section. ####
srcdir = .

SHELL = /bin/sh

CC = gcc -O
C++ = g++ -O
YACC = bison -y
INSTALL = /usr/local/bin/install -c
INSTALLDATA = /usr/local/bin/install -c -m 644

DEFS =  -DSIGTYPE=int -DDIRENT -DSTRSTR_MISSING \
        -DVPRINTF_MISSING -DBSD42 -D_GNU_SOURCE

WARNING = -Wall
CDEBUG = -g
LIBS = -L/usr/lib -L /lib
CFLAGS =   $(DEFS) $(LIBS) $(CDEBUG) -I. -I$(srcdir) -fPIC -O2 -DNETSNMP_ENABLE_IPV6 -fno-strict-aliasing -DNETSNMP_REMOVE_U64 -g -O2 -Ulinux -Dlinux=linux -I. -I/usr/local/include
CPPFLAGS = $(CDEBUG) -I.  $(WARNING) 
LDFLAGS = -L/usr/local/lib -lpthread -lmysqlclient -lutil   -lnetsnmp -lcrypto -lm



binaries := trafficSNMP

src = $(wildcard *.c)
obj = $(src:.c=.o)

all: $(binaries)

$(binaries): $(obj)
	$(CC) -Wall -o $@ $^ $(LDFLAGS)
	/usr/bin/sudo /sbin/setcap cap_net_raw=pe $@
.c:
	$(CC) $(CFLAGS) $(CPPFLAGS) -fPIC -c -o $@ $<

.cpp:
	$(C++)  $(CPPFLAGS) -o $@ $<


.PHONY: clean
clean:
	rm -f *.o $(binaries) core core.* *.a

distclean: clean
	rm -f TAGS Makefile config.status



