# Not many C source files left now...
CSOURCES=blowfish.c md5.c openat.c
CPPSOURCES=counted_stringset.cpp istr.cpp rofile.cpp rwfile.cpp stringset.cpp stringtbl.cpp
CPPSOURCES+=blob.cpp blob_buffer.cpp params.cpp sub_blob.cpp journal.cpp sys_journal.cpp transaction.cpp
CPPSOURCES+=simple_dtable.cpp simple_ctable.cpp simple_stable.cpp simple_ext_index.cpp
CPPSOURCES+=journal_dtable.cpp overlay_dtable.cpp ustr_dtable.cpp managed_dtable.cpp
CPPSOURCES+=dtable_factory.cpp ctable_factory.cpp index_factory.cpp toilet++.cpp
SOURCES=$(CSOURCES) $(CPPSOURCES)

HEADERS=$(wildcard *.h)

COBJECTS=$(CSOURCES:.c=.o)
CPPOBJECTS=$(CPPSOURCES:.cpp=.o)
OBJECTS=$(COBJECTS) $(CPPOBJECTS)

.PHONY: all clean clean-all count count-all php

ifeq ($(findstring -pg,$(CFLAGS)),-pg)
ifeq ($(findstring -pg,$(LDFLAGS)),)
LDFLAGS:=-pg $(LDFLAGS)
endif
# We can't actually do this without a small change to openat.c;
# dlsym(RTLD_NEXT, ...) crashes when libc is linked statically
#ifeq ($(findstring -lc_p,$(LDFLAGS)),)
#LDFLAGS:=-lc_p $(LDFLAGS)
#endif
endif

CFLAGS:=-Wall -Ifstitch/include $(CFLAGS)
LDFLAGS:=-Lfstitch/obj/kernel/lib -lpatchgroup -Wl,-R,$(PWD)/fstitch/obj/kernel/lib $(LDFLAGS)

all: tags main

%.o: %.c
	gcc -c $< -O2 $(CFLAGS)

%.o: %.cpp
	g++ -c $< -O2 $(CFLAGS) -fno-exceptions -fno-rtti $(CPPFLAGS)

libtoilet.so: libtoilet.o fstitch/obj/kernel/lib/libpatchgroup.so
	g++ -shared -o $@ $< -ldl $(LDFLAGS)

libtoilet.o: $(OBJECTS)
	ld -r -o $@ $^

# Make libtoilet.a from libtoilet.o instead of $(OBJECTS) directly so that
# classes not directly referenced still get included and register themselves
# to be looked up via factory registries, which is how most *tables work.
libtoilet.a: libtoilet.o
	ar csr $@ $<

ifeq ($(findstring -pg,$(CFLAGS)),-pg)
main: libtoilet.a main.o main++.o
	g++ -o $@ main.o main++.o libtoilet.a -lreadline -ltermcap $(LDFLAGS)
else
main: libtoilet.so main.o main++.o
	g++ -o $@ main.o main++.o -Wl,-R,$(PWD) -L. -ltoilet -lreadline -ltermcap $(LDFLAGS)
endif

clean:
	rm -f main libtoilet.so libtoilet.a *.o .depend tags

clean-all: clean
	php/clean

count:
	wc -l *.[ch] *.cpp | sort -n

count-all:
	wc -l *.[ch] *.cpp php/*.[ch] invite/*.php | sort -n

php:
	if [ -f php/Makefile ]; then make -C php; else php/compile; fi

.depend: $(SOURCES) $(HEADERS) main.c
	g++ -MM $(CFLAGS) *.c *.cpp > .depend

tags: $(SOURCES) main.c $(HEADERS)
	ctags -R

-include .depend
