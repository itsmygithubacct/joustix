CC      ?= cc
override CPPFLAGS += -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L
CFLAGS  ?= -O2 -Wall -Wextra -Wpedantic -std=c11
LDFLAGS ?=
LDLIBS  ?= -lz -lm -pthread
PREFIX  ?= /usr/local
DESTDIR ?=

SRC = src/main.c src/game.c src/render.c src/term.c src/sound.c
OBJ = $(SRC:.c=.o)
BIN = joustix
ASSET_FILES = assets/stage.ppm assets/player.ppm assets/bounder.ppm \
	assets/hunter.ppm assets/shadow.ppm assets/props.ppm assets/platform.ppm
ASSET_DEST = $(DESTDIR)$(PREFIX)/share/joustix/assets

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

src/%.o: src/%.c src/joustix.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

test: $(BIN) validate-assets
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
	[ "$$(find assets -maxdepth 1 -type f | wc -l)" -eq 7 ] || \
		{ echo "assets/ must contain only the seven production images" >&2; exit 1; }; \
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
	for image in player bounder hunter shadow; do check_ppm "assets/$$image.ppm" 512 768; done; \
	check_ppm assets/props.ppm 512 512; \
	check_ppm assets/platform.ppm 640 128

install: $(BIN) validate-assets
	install -Dm755 $(BIN) "$(DESTDIR)$(PREFIX)/bin/$(BIN)"
	install -Dm644 docs/joustix.6 "$(DESTDIR)$(PREFIX)/share/man/man6/joustix.6"
	install -d -m755 "$(ASSET_DEST)"
	install -m644 $(ASSET_FILES) "$(ASSET_DEST)/"

uninstall:
	rm -f "$(DESTDIR)$(PREFIX)/bin/$(BIN)"
	rm -f "$(DESTDIR)$(PREFIX)/share/man/man6/joustix.6"
	rm -rf "$(DESTDIR)$(PREFIX)/share/joustix"

clean:
	rm -f $(OBJ) $(BIN) render_*.ppm

.PHONY: all test validate-assets install uninstall clean
