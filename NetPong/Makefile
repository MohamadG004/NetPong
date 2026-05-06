# Makefile — quick build for Linux & macOS (uses sdl2-config)
# Usage:
#   make           — build debug binary
#   make release   — build optimised binary
#   make run       — build and run
#   make clean     — remove build artefacts

CXX      ?= g++
CXXFLAGS := -std=c++17 -Wall -Wextra $(shell sdl2-config --cflags)
LDFLAGS  := $(shell sdl2-config --libs)

SRC   := src/main.cpp
BIN   := pong

.PHONY: all release run clean

all: $(BIN)

$(BIN): $(SRC)
	$(CXX) $(CXXFLAGS) -g -o $@ $^ $(LDFLAGS)

release: $(SRC)
	$(CXX) $(CXXFLAGS) -O2 -DNDEBUG -o $(BIN) $^ $(LDFLAGS)

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN)
