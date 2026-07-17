CC      ?= cc
KITTY_TERMINAL_SESSION_DIR ?= third_party/kitty-terminal-session
KITTY_KEYBOARD_DIR ?= $(KITTY_TERMINAL_SESSION_DIR)/third_party/kitty_keyboard
KITTY_FRAMEBUFFER_DIR ?= $(KITTY_TERMINAL_SESSION_DIR)/third_party/kitty-framebuffer
SOFT_RASTER_DIR ?= third_party/soft-raster
PCM_MIXER_DIR ?= third_party/pcm-mixer
override CPPFLAGS += -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L \
	-I$(KITTY_KEYBOARD_DIR)/include \
	-I$(KITTY_FRAMEBUFFER_DIR)/include \
	-I$(KITTY_TERMINAL_SESSION_DIR)/include \
	-I$(SOFT_RASTER_DIR)/include \
	-I$(PCM_MIXER_DIR)/include
CFLAGS  ?= -O2 -Wall -Wextra -Wpedantic -std=c11
LDFLAGS ?=
LDLIBS  ?= -lz -lm -pthread
PREFIX  ?= /usr/local
DESTDIR ?=

SRC = src/main.c src/game.c src/render.c src/term.c src/sound.c
VENDOR_OBJ = src/vendor_kitty_terminal_session.o src/vendor_kitty_keyboard.o \
	src/vendor_kitty_keyboard_posix.o src/vendor_kitty_framebuffer.o src/vendor_soft_raster.o \
	src/vendor_pcm_mixer.o src/vendor_pcm_wav.o
OBJ = $(SRC:.c=.o) $(VENDOR_OBJ)
BIN = joustix
ASSET_FILES = assets/stage.ppm assets/gameover.ppm assets/player.ppm assets/bounder.ppm \
	assets/hunter.ppm assets/shadow.ppm assets/props.ppm assets/platform.ppm
SFX_ASSETS := $(sort $(wildcard assets/sfx/*.wav))
EXPECTED_SFX = 40
ASSET_DEST = $(DESTDIR)$(PREFIX)/share/joustix/assets

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

src/%.o: src/%.c src/joustix.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

src/sound.o: $(PCM_MIXER_DIR)/include/pcm_mixer.h
src/render.o: $(SOFT_RASTER_DIR)/include/soft_raster.h
src/term.o: $(KITTY_TERMINAL_SESSION_DIR)/include/kitty_terminal_session.h \
	$(KITTY_KEYBOARD_DIR)/include/kitty_keyboard.h \
	$(KITTY_KEYBOARD_DIR)/include/kitty_keyboard_posix.h \
	$(KITTY_FRAMEBUFFER_DIR)/include/kitty_framebuffer.h

src/vendor_kitty_terminal_session.o: \
	$(KITTY_TERMINAL_SESSION_DIR)/src/kitty_terminal_session.c \
	$(KITTY_TERMINAL_SESSION_DIR)/include/kitty_terminal_session.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

src/vendor_kitty_keyboard.o: $(KITTY_KEYBOARD_DIR)/src/kitty_keyboard.c \
	$(KITTY_KEYBOARD_DIR)/include/kitty_keyboard.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

src/vendor_kitty_keyboard_posix.o: $(KITTY_KEYBOARD_DIR)/src/kitty_keyboard_posix.c \
	$(KITTY_KEYBOARD_DIR)/include/kitty_keyboard.h \
	$(KITTY_KEYBOARD_DIR)/include/kitty_keyboard_posix.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

src/vendor_kitty_framebuffer.o: $(KITTY_FRAMEBUFFER_DIR)/src/kitty_framebuffer.c \
	$(KITTY_FRAMEBUFFER_DIR)/src/kitty_framebuffer_internal.h \
	$(KITTY_FRAMEBUFFER_DIR)/include/kitty_framebuffer.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

src/vendor_soft_raster.o: $(SOFT_RASTER_DIR)/src/soft_raster.c \
	$(SOFT_RASTER_DIR)/include/soft_raster.h \
	$(SOFT_RASTER_DIR)/src/font8x16.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

src/vendor_pcm_mixer.o: $(PCM_MIXER_DIR)/src/pcm_mixer.c \
	$(PCM_MIXER_DIR)/include/pcm_mixer.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

src/vendor_pcm_wav.o: $(PCM_MIXER_DIR)/src/pcm_wav.c \
	$(PCM_MIXER_DIR)/include/pcm_mixer.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

test: $(BIN) validate-assets validate-audio
	./$(BIN) --rules-test
	./$(BIN) --selftest 1337 12000
	./$(BIN) --selftest 42 12000
	@render_dir=$$(mktemp -d); \
	trap 'rm -rf "$$render_dir"' EXIT HUP INT TERM; \
	JOUSTIX_RENDER_DIR="$$render_dir" ./$(BIN) --render-test 7; \
	set -- "$$render_dir"/render_*.ppm; \
	[ "$$#" -eq 4 ]; \
	for image do [ -s "$$image" ]; done

validate-assets: $(ASSET_FILES)
	@set -eu; \
	[ "$$(find assets -maxdepth 1 -type f | wc -l)" -eq 8 ] || \
		{ echo "assets/ must contain only the eight production images" >&2; exit 1; }; \
	check_ppm() { \
		file=$$1; width=$$2; height=$$3; \
		test -f "$$file" || { echo "missing production image: $$file" >&2; return 1; }; \
		header=$$(head -n 3 "$$file"); set -- $$header; \
		[ "$$#" -eq 4 ] && [ "$$1" = P6 ] && [ "$$2" = "$$width" ] && \
			[ "$$3" = "$$height" ] && [ "$$4" = 255 ] || \
			{ echo "invalid PPM header: $$file" >&2; return 1; }; \
		header_bytes=$$(printf 'P6\n%s %s\n255\n' "$$width" "$$height" | wc -c); \
		expected=$$((width * height * 3 + header_bytes)); \
		actual=$$(wc -c < "$$file"); \
		[ "$$actual" -eq "$$expected" ] || \
			{ echo "invalid PPM payload: $$file ($$actual, expected $$expected)" >&2; return 1; }; \
	}; \
	check_ppm assets/stage.ppm 640 360; \
	check_ppm assets/gameover.ppm 640 360; \
	for image in player bounder hunter shadow; do check_ppm "assets/$$image.ppm" 512 768; done; \
	check_ppm assets/props.ppm 512 512; \
	check_ppm assets/platform.ppm 640 128

validate-audio:
	@set -eu; \
	count=$$(find assets/sfx -maxdepth 1 -type f -name '*.wav' 2>/dev/null | wc -l); \
	[ "$$count" -eq $(EXPECTED_SFX) ] || \
		{ echo "expected $(EXPECTED_SFX) SFX WAVs, found $$count" >&2; exit 1; }; \
	for sound in assets/sfx/*.wav; do \
		[ "$$(dd if="$$sound" bs=1 count=4 2>/dev/null)" = RIFF ]; \
		[ "$$(dd if="$$sound" bs=1 skip=8 count=4 2>/dev/null)" = WAVE ]; \
	done

install: $(BIN) validate-assets validate-audio
	install -Dm755 $(BIN) "$(DESTDIR)$(PREFIX)/bin/$(BIN)"
	install -Dm644 docs/joustix.6 "$(DESTDIR)$(PREFIX)/share/man/man6/joustix.6"
	install -d -m755 "$(ASSET_DEST)" "$(ASSET_DEST)/sfx"
	install -m644 $(ASSET_FILES) "$(ASSET_DEST)/"
	install -m644 $(SFX_ASSETS) "$(ASSET_DEST)/sfx/"

uninstall:
	rm -f "$(DESTDIR)$(PREFIX)/bin/$(BIN)"
	rm -f "$(DESTDIR)$(PREFIX)/share/man/man6/joustix.6"
	rm -rf "$(DESTDIR)$(PREFIX)/share/joustix"

clean:
	rm -f $(OBJ) $(BIN) render_*.ppm

.PHONY: all test validate-assets validate-audio install uninstall clean
