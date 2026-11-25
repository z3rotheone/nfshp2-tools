# hp2launcher - settings and input prototype
#
#   make           build the launcher (needs SDL2)
#   make hp2fe     build the front-end menus (needs SDL2 2.0.18 or newer)
#   make hp2track  build the track viewer (needs SDL2 and OpenGL)
#   make test      build and run the unit tests (no SDL needed)
#   make fecheck FRONTB=... GLOBALB=... FELANG=... [BANK=...] [DISC=...]
#                  check the front-end modules against the real files
#                  (archive entries 21, 302, 3, music bank 322; DISC =
#                  the NFSHP2 folder)
#                  and against tools/hp2_fescreens.py and hp2_tpk.py
#                  (needs python3 with numpy and pillow)
#   make trackcheck SCENERY="FILE..."
#                  check the track scenery module against tools/hp2_scenery.py
#                  (extracted TRACKBnn entries, .BUN or .LZC; 187-224)
#   make stubcheck compile-check the SDL sources without SDL installed
#   make clean
#
# Linux:   sudo pacman -S sdl2      (CachyOS/Arch)
#          sudo apt install libsdl2-dev   (Debian/Ubuntu)
# Windows: MSYS2 -> pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-gcc
#          then build from the MSYS2 MinGW64 shell.

CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -O2
SRC      = src/hp2_settings.c src/hp2_glyphs.c src/hp2_pad.c src/hp2_menu.c src/hp2_fe.c \
           src/hp2_tex.c src/hp2_font.c src/hp2_lang.c src/hp2_zdir.c src/hp2_tree.c src/hp2_geom.c src/hp2_scene.c src/hp2_car.c src/hp2_coll.c src/hp2_drive.c src/hp2_race.c src/hp2_ai.c src/hp2_engine.c src/hp2_femenu.c src/hp2_jukebox.c \
           src/hp2_vag.c src/hp2_jbplay.c src/hp2_progress.c src/hp2_sfx.c src/hp2_ambient.c
SDLSRC   = src/hp2_input.c src/main.c
FESRC    = src/hp2_fedraw.c src/hp2_fecar.c src/hp2_input.c src/fe_main.c
BIN      = hp2launcher
FEBIN    = hp2fe
TRACKSRC = src/hp2_geom.c src/hp2_scene.c src/hp2_car.c src/hp2_coll.c src/hp2_drive.c src/hp2_race.c src/hp2_ai.c src/hp2_engine.c src/hp2_vag.c src/hp2_jukebox.c src/hp2_jbplay.c src/hp2_settings.c src/hp2_fe.c src/hp2_font.c src/hp2_hud.c src/hp2_tex.c src/hp2_zdir.c src/hp2_sfx.c src/hp2_ambient.c src/track_main.c
TRACKBIN = hp2track

SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null)
ifeq ($(strip $(SDL_LIBS)),)
SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL_LIBS   := $(shell sdl2-config --libs 2>/dev/null)
endif

# OpenGL: libGL on Linux, opengl32 on Windows (MSYS2), the framework on macOS
UNAME := $(shell uname -s 2>/dev/null)
ifneq (,$(findstring MINGW,$(UNAME))$(findstring MSYS,$(UNAME)))
GL_LIBS := -lopengl32
else ifeq ($(UNAME),Darwin)
GL_LIBS := -framework OpenGL
else
GL_LIBS := $(shell pkg-config --libs gl 2>/dev/null)
ifeq ($(strip $(GL_LIBS)),)
GL_LIBS := -lGL
endif
endif

all: $(BIN)

$(FEBIN): $(SRC) $(FESRC)
	@if [ -z "$(strip $(SDL_LIBS))" ]; then \
	  echo "SDL2 not found. Install it (see the top of this Makefile)."; \
	  exit 1; \
	fi
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -Isrc -o $@ $(SRC) $(FESRC) $(SDL_LIBS) -lm

$(TRACKBIN): $(TRACKSRC) src/hp2_geom.h src/hp2_scene.h src/hp2_car.h src/hp2_coll.h src/hp2_drive.h src/hp2_race.h src/hp2_ai.h src/hp2_engine.h src/hp2_hud.h
	@if [ -z "$(strip $(SDL_LIBS))" ]; then \
	  echo "SDL2 not found. Install it (see the top of this Makefile)."; \
	  exit 1; \
	fi
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -Isrc -o $@ $(TRACKSRC) $(SDL_LIBS) $(GL_LIBS) -lm

$(BIN): $(SRC) $(SDLSRC)
	@if [ -z "$(strip $(SDL_LIBS))" ]; then \
	  echo "SDL2 not found. Install it (see the top of this Makefile),"; \
	  echo "or run 'make test' and 'make stubcheck', which need no SDL."; \
	  exit 1; \
	fi
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -Isrc -o $@ $(SRC) $(SDLSRC) $(SDL_LIBS) -lm

test: test_core test_pad test_menu test_fe test_assets
	@echo
	@./test_core
	@echo
	@./test_pad
	@echo
	@./test_menu
	@echo
	@./test_fe
	@echo
	@./test_assets

test_core: tests/test_core.c $(SRC)
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_core.c $(SRC) -lm

test_pad: tests/test_pad.c $(SRC)
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_pad.c $(SRC) -lm

test_menu: tests/test_menu.c $(SRC)
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_menu.c $(SRC) -lm

test_fe: tests/test_fe.c src/hp2_fe.c src/hp2_fe.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_fe.c src/hp2_fe.c -lm

test_assets: tests/test_assets.c $(SRC)
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_assets.c $(SRC) -lm

# The game's files are not in the repo: pass the paths of your own copies
# (extracted entries 21, 302 and 3, e.g. ~/hp2work/extracted/021_14c57a19.bin).
fecheck: test_fe test_assets
	@if [ -z "$(FRONTB)" ] || [ -z "$(GLOBALB)" ] || [ -z "$(FELANG)" ]; then \
	  echo "usage: make fecheck FRONTB=... GLOBALB=... FELANG=... [BANK=entry 322] [DISC=NFSHP2 folder] [EXE=SLUS_203.62]"; exit 1; fi
	./test_fe "$(FRONTB)"
	python3 tests/check_fe.py ./test_fe "$(FRONTB)"
	./test_assets --frontb "$(FRONTB)" --globalb "$(GLOBALB)" --lang "$(FELANG)" $(if $(BANK),--jukebox "$(BANK)") $(if $(DISC),--disc "$(DISC)") $(if $(EXE),--exe "$(EXE)")
	python3 tests/check_tex.py ./test_assets "$(FRONTB)" "$(GLOBALB)"
	python3 tests/check_tree.py ./test_assets "$(FRONTB)"
	$(if $(BANK),python3 tests/check_vag.py ./test_assets "$(BANK)")

# The scenery files are not in the repo either: extracted TRACKBnn entries
# (e.g. ~/hp2work/extracted/196_5ebf48d3.bin; a .comp entry is decompressed).
trackcheck: test_assets
	@if [ -z "$(SCENERY)" ]; then echo "usage: make trackcheck SCENERY=\"FILE...\""; exit 1; fi
	@for f in $(SCENERY); do python3 tests/check_scene.py ./test_assets "$$f" || exit 1; done

# Compiles the SDL-dependent sources against a stand-in header, to catch
# mistakes in them on a machine with no SDL. Never links, never run.
stubcheck:
	$(CC) $(CFLAGS) -Isrc -Istub/SDL2 -c -o /dev/null src/hp2_input.c
	$(CC) $(CFLAGS) -Isrc -Istub/SDL2 -c -o /dev/null src/main.c
	$(CC) $(CFLAGS) -Isrc -Istub/SDL2 -c -o /dev/null src/hp2_fedraw.c
	$(CC) $(CFLAGS) -Isrc -Istub/SDL2 -c -o /dev/null src/hp2_fecar.c
	$(CC) $(CFLAGS) -Isrc -Istub/SDL2 -c -o /dev/null src/fe_main.c
	$(CC) $(CFLAGS) -Isrc -Istub/SDL2 -c -o /dev/null src/track_main.c
	$(CC) $(CFLAGS) -Isrc -Istub/SDL2 -c -o /dev/null src/hp2_hud.c
	@echo "stub compile-check passed"

clean:
	rm -f $(BIN) $(FEBIN) $(TRACKBIN) hp2track_*.bmp test_core test_pad test_menu test_fe test_assets *.o
	@# hp2.ini is the user's settings: never removed here

.PHONY: all test fecheck trackcheck stubcheck clean
