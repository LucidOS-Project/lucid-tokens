# No meson or cmake required: this builds with a stock compiler and nothing else.
CXX      ?= g++
# Tunable: a packaging build overrides this wholesale with its own hardening
# flags, which is correct and is why the flags this code *requires* are not in
# it. Putting -std=c++20 and -Iinclude here meant dpkg-buildpackage replaced
# them and the build failed with errors about a language standard nobody had
# changed -- it works standalone and breaks only when packaged, which is the
# worst place to find out.
CXXFLAGS ?= -O2 -Wall -Wextra
# Not tunable: without these it does not compile.
REQUIRED  = -std=c++20 -Iinclude
PREFIX   ?= $(HOME)/.local

all: lucid-tokens liblucidtokens.a test

lucid-tokens: src/cli.cpp src/tokens.cpp
	$(CXX) $(CXXFLAGS) $(REQUIRED) $(CPPFLAGS) $(LDFLAGS) $^ -o $@

test: tests/test_tokens.cpp src/tokens.cpp
	$(CXX) $(CXXFLAGS) $(REQUIRED) $(CPPFLAGS) $(LDFLAGS) $^ -o $@

check: test
	./test

# A static library and the header, not a shared object.
#
# The consumers vendored this as a submodule and compiled tokens.cpp straight
# in, which means a changed default needed every consumer rebuilt and
# re-uploaded. A library ends that.
#
# Static while the API is still moving -- lucid-wayland gained two virtual
# methods on 7 September alone -- because a shared object is a promise about an
# ABI, and breaking that promise quietly is worse than not making it. It
# becomes a .so when the interface stops changing under it.
liblucidtokens.a: src/tokens.cpp include/lucid/tokens.h
	$(CXX) $(CXXFLAGS) $(REQUIRED) $(CPPFLAGS) -c src/tokens.cpp -o tokens.o
	$(AR) rcs $@ tokens.o

install: lucid-tokens liblucidtokens.a
	install -Dm755 lucid-tokens $(DESTDIR)$(PREFIX)/bin/lucid-tokens
	install -Dm644 liblucidtokens.a $(DESTDIR)$(PREFIX)/lib/liblucidtokens.a
	install -Dm644 include/lucid/tokens.h $(DESTDIR)$(PREFIX)/include/lucid/tokens.h

clean:
	rm -f lucid-tokens test liblucidtokens.a tokens.o

.PHONY: all check install clean
