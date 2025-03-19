CC=/usr/bin/g++

CPPFILES=$(wildcard src/*.cpp)
HPPFILES=$(wildcard src/*.hpp)
LIBFILES=$(wildcard src/discord/*.cpp)
CFLAGS=-Llib/ -l:discord_game_sdk.so -lpthread -lX11

build/brpc: $(CPPFILES) $(HPPFILES)
	mkdir -p build
	$(CC) $(CPPFILES) $(LIBFILES) $(CFLAGS) -o $@

clean:
	rm -rf tmp build

install: build/brpc
	mkdir -p ${DESTDIR}${PREFIX}/bin
	mkdir -p ${DESTDIR}${PREFIX}/lib
	cp -f build/brpc ${DESTDIR}${PREFIX}/bin
	cp -f lib/discord_game_sdk.so ${DESTDIR}${PREFIX}/lib
	chmod 755 ${DESTDIR}${PREFIX}/bin/brpc

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/brpc

.PHONY: clean install uninstall