# hp2launcher - settings and input prototype
#
#   make           build the launcher (needs SDL2)
#   make test      build and run the unit tests (no SDL needed)
#   make stubcheck compile-check the SDL sources without SDL installed
#   make clean
#
# Linux:   sudo pacman -S sdl2      (CachyOS/Arch)
#          sudo apt install libsdl2-dev   (Debian/Ubuntu)
# Windows: MSYS2 -> pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-gcc
#          then build from the MSYS2 MinGW64 shell.

CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -O2
SRC      = src/hp2_settings.c src/hp2_glyphs.c src/hp2_pad.c src/hp2_menu.c
SDLSRC   = src/hp2_input.c src/main.c
BIN      = hp2launcher

SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null)
ifeq ($(strip $(SDL_LIBS)),)
SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL_LIBS   := $(shell sdl2-config --libs 2>/dev/null)
endif

all: $(BIN)

$(BIN): $(SRC) $(SDLSRC)
	@if [ -z "$(strip $(SDL_LIBS))" ]; then \
	  echo "SDL2 not found. Install it (see the top of this Makefile),"; \
	  echo "or run 'make test' and 'make stubcheck', which need no SDL."; \
	  exit 1; \
	fi
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -Isrc -o $@ $(SRC) $(SDLSRC) $(SDL_LIBS) -lm

test: test_core test_pad test_menu
	@echo
	@./test_core
	@echo
	@./test_pad
	@echo
	@./test_menu

test_core: tests/test_core.c $(SRC)
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_core.c $(SRC) -lm

test_pad: tests/test_pad.c $(SRC)
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_pad.c $(SRC) -lm

test_menu: tests/test_menu.c $(SRC)
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_menu.c $(SRC) -lm

# Compiles the SDL-dependent sources against a stand-in header, to catch
# mistakes in them on a machine with no SDL. Never links, never run.
stubcheck:
	$(CC) $(CFLAGS) -Isrc -Istub/SDL2 -c -o /dev/null src/hp2_input.c
	$(CC) $(CFLAGS) -Isrc -Istub/SDL2 -c -o /dev/null src/main.c
	@echo "stub compile-check passed"

clean:
	rm -f $(BIN) test_core test_pad test_menu hp2.ini *.o

.PHONY: all test stubcheck clean
