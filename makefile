CFLAGS?=-march=native -Wall -O2 -g -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fasynchronous-unwind-tables -fpic -fstack-clash-protection -fcf-protection=full -Werror=format-security -Werror=implicit-function-declaration -Wl,-z,relro -ftrapv -Wl,-z,noexecstack
LDFLAGS?=-lsodium
CC?=gcc
SOEXT?=so
STATICEXT?=a

ifeq ($(UNAME),Darwin)
	SOEXT=dylib
	SOFLAGS=-Wl,-install_name,liboprf.$(SOEXT)
else
	CFLAGS+=-Wl,-z,defs
	SOEXT=so
	SOFLAGS=-Wl,-soname,liboprf.$(SOEXT).0
endif

SODIUM_NEWER_THAN_1_0_18 := $(shell pkgconf --atleast-version=1.0.19 libsodium; echo $$?)
ifeq ($(SODIUM_NEWER_THAN_1_0_18),1)
   CFLAGS+= -Iaux
   EXTRA_SOURCES+= aux/kdf_hkdf_sha512.c
else
   CFLAGS+= -DHAVE_SODIUM_HKDF=1
endif

SOURCES=oprf.c toprf.c dkg.c utils.c  $(EXTRA_SOURCES)
OBJECTS=$(patsubst %.c,%.o,$(SOURCES))

all: liboprf.$(SOEXT) liboprf.$(STATICEXT) toprf dkg

asan: CFLAGS=-fsanitize=address -static-libasan -g -march=native -Wall -O2 -g -fstack-protector-strong -fpic -fstack-clash-protection -fcf-protection=full -Werror=format-security -Werror=implicit-function-declaration -Wl, -z,noexecstack
asan: LDFLAGS+= -fsanitize=address -static-libasan
asan: all

liboprf.$(SOEXT): $(SOURCES)
	$(CC) -shared $(CFLAGS) $(SOFLAGS) -o $@ $^ $(LDFLAGS)

liboprf.$(STATICEXT): $(OBJECTS)
	ar rcs $@ $^

toprf: oprf.c toprf.c main.c aux/kdf_hkdf_sha512.c
	gcc -g -o toprf oprf.c toprf.c main.c $(EXTRA_SOURCES) -lsodium

dkg: dkg.c utils.c liboprf.a
	gcc -g -DUNIT_TEST -o dkg dkg.c utils.c liboprf.a -lsodium

clean:
	rm -f *.o liboprf.so liboprf.a toprf aux/*.o dkg

install: $(DESTDIR)$(PREFIX)/lib/liboprf.$(SOEXT) $(DESTDIR)$(PREFIX)/lib/liboprf.$(STATICEXT) $(DESTDIR)$(PREFIX)/include/oprf/oprf.h $(DESTDIR)$(PREFIX)/include/oprf/toprf.h $(DESTDIR)$(PREFIX)/include/oprf/dkg.h

uninstall: $(PREFIX)/lib/liboprf.$(SOEXT) $(PREFIX)/lib/liboprf.$(STATICEXT) $(PREFIX)/include/oprf/oprf.h $(PREFIX)/include/oprf/toprf.h $(PREFIX)/include/oprf/dkg.h
	rm $^
	rmdir $(PREFIX)/include/oprf/

$(DESTDIR)$(PREFIX)/lib/liboprf.$(SOEXT): liboprf.$(SOEXT)
	install -D $< $@

$(DESTDIR)$(PREFIX)/lib/liboprf.$(STATICEXT): liboprf.$(STATICEXT)
	install -D $< $@

$(DESTDIR)$(PREFIX)/include/oprf/oprf.h: oprf.h
	install -D $< $@

$(DESTDIR)$(PREFIX)/include/oprf/toprf.h: toprf.h
	install -D $< $@
	
$(DESTDIR)$(PREFIX)/include/oprf/dkg.h: dkg.h
	install -D $< $@

PHONY: clean
