# No meson or cmake required: this builds with a stock compiler and nothing else.
CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Iinclude
PREFIX   ?= $(HOME)/.local

all: lucid-tokens test

lucid-tokens: src/cli.cpp src/tokens.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@

test: tests/test_tokens.cpp src/tokens.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@

check: test
	./test

install: lucid-tokens
	install -Dm755 lucid-tokens $(PREFIX)/bin/lucid-tokens

clean:
	rm -f lucid-tokens test

.PHONY: all check install clean
