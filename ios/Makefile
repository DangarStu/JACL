# ios/Makefile --- desktop "simulator" builds of the iOS code path.
#
# These build the EXACT C that the iPad app will run (ios_startup.c + the
# shared interpreter core), linked against a desktop Glk so it runs on a
# Mac. They let us compile-check and smoke-test the iOS start-up shim and
# the RemGlk JSON contract without Xcode or a device.
#
# Self-contained: references ../src but never edits src/Makefile, so none of
# the existing targets (jacl, cjacl, cgijacl, fcgijacl, bjorb) are affected.
#
#   make sim        ./iosjacl-sim     GlkTerm, interactive terminal UI
#   make cheap      ./iosjacl-cheap   CheapGlk, scriptable stdin/stdout
#   make remglk     ./iosjacl-remglk  RemGlk, JSON stdin/stdout (the device backend)
#   make remglk-test                  feed an init event to a .j2, show JSON
#   make clean
#
# On device the SAME ios_startup.c + core is compiled by the Xcode project,
# linked against libremglk.a, with a SwiftUI front-end driving the JSON.

SRC_DIR     = ../src
GLKTERM_DIR = $(SRC_DIR)/glkterm
CHEAP_DIR   = $(SRC_DIR)/CheapGlk
REMGLK_DIR  = remglk

CC      = gcc -std=gnu23

# Mirrors src/Makefile CFLAGS, plus -DGLK (so create_paths sets up the
# blorb/bookmark/walkthru paths) and NOT -DGARGLK / -DWINGLK (so the blorb
# opens via glk_fileref_create_by_name -- the sandbox-friendly path).
CFLAGS  = -Wall -O2 -Wno-unused-result -Wno-unused-but-set-variable \
          -DNATIVE_LANGUAGE=1 -DGLK

NCURSES_LIBS = -L/opt/homebrew/lib -lncurses

# The shared, portable interpreter core -- identical list to src/Makefile's
# COMMON_SRC, path-qualified. Exactly what the Xcode target compiles too.
COMMON_SRC = \
	$(SRC_DIR)/findroute.c \
	$(SRC_DIR)/interpreter.c \
	$(SRC_DIR)/loader.c \
	$(SRC_DIR)/logging.c \
	$(SRC_DIR)/parser.c \
	$(SRC_DIR)/display.c \
	$(SRC_DIR)/utils.c \
	$(SRC_DIR)/jpp.c \
	$(SRC_DIR)/resolvers.c \
	$(SRC_DIR)/errors.c \
	$(SRC_DIR)/encapsulate.c \
	$(SRC_DIR)/libcsv.c

# The iOS build swaps glk_startup.c -> ios_startup.c, keeping glk_saver.c.
IOS_SRC = ios_startup.c $(SRC_DIR)/jacl.c $(COMMON_SRC) $(SRC_DIR)/glk_saver.c

# All three sims share these include paths. GlkTerm's headers are the Glk
# API headers the core #includes ("glkterm/glk.h"); they are ABI-compatible
# with CheapGlk and RemGlk (all Plotkin-lineage Glk 0.7.x), which is why the
# same core links against any of the three .a files unchanged.
INCS = -I. -I$(SRC_DIR) -I$(GLKTERM_DIR)

.PHONY: all sim cheap remglk remglk-test run clean
all: remglk

# --- GlkTerm: interactive terminal UI (needs ncurses) ---------------------
sim: iosjacl-sim
iosjacl-sim: ios_startup.c jacl_ios.h $(GLKTERM_DIR)/libglkterm.a
	$(CC) $(CFLAGS) $(IOS_SRC) $(INCS) \
		-L$(GLKTERM_DIR) -lglkterm -lm $(NCURSES_LIBS) -o iosjacl-sim

# --- CheapGlk: scriptable plain stdin/stdout ------------------------------
cheap: iosjacl-cheap
iosjacl-cheap: ios_startup.c jacl_ios.h $(CHEAP_DIR)/libcheapglk.a
	$(CC) $(CFLAGS) $(IOS_SRC) $(INCS) \
		-L$(CHEAP_DIR) -lcheapglk -lm -o iosjacl-cheap

# --- RemGlk: JSON stdin/stdout = the device backend -----------------------
remglk: iosjacl-remglk
iosjacl-remglk: ios_startup.c jacl_ios.h $(REMGLK_DIR)/libremglk.a
	$(CC) $(CFLAGS) $(IOS_SRC) $(INCS) \
		-L$(REMGLK_DIR) -lremglk -lm -o iosjacl-remglk

$(REMGLK_DIR)/libremglk.a:
	$(MAKE) -C $(REMGLK_DIR)

$(GLKTERM_DIR)/libglkterm.a:
	$(MAKE) -C $(GLKTERM_DIR) libglkterm.a

# Feed a single RemGlk "init" event to a game and print the JSON it emits.
# Shows the first update (window layout + opening text + input request).
#   make remglk-test G=../projects/temp/grail.j2
G ?= ../projects/temp/life.j2
remglk-test: iosjacl-remglk
	@printf '{"type":"init","gen":0,"metrics":{"width":80,"height":40}}\n' \
		| ./iosjacl-remglk $(G) || true
	@echo "(trailing 'Unexpected end of input' is just EOF after the one init event)"

run: iosjacl-sim
	./iosjacl-sim $(G)

clean:
	rm -f iosjacl-sim iosjacl-cheap iosjacl-remglk
