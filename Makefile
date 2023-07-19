CFLAGS ?= -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -g
PKG_CONFIG ?= pkg-config

# Host deps
WAYLAND_FLAGS = $(shell $(PKG_CONFIG) wayland-client --cflags --libs)
WAYLAND_PROTOCOLS_DIR = $(shell $(PKG_CONFIG) wayland-protocols --variable=pkgdatadir)

# Build deps
WAYLAND_SCANNER = $(shell pkg-config --variable=wayland_scanner wayland-scanner)

XDG_SHELL_PROTOCOL = $(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml

HEADERS=cat.h xdg-shell-client-protocol.h shm.h
SOURCES=main.c xdg-shell-protocol.c shm.c

HEADERS:=$(addprefix src/,$(HEADERS))
SOURCES:=$(addprefix src/,$(SOURCES))

all: build/hello-wayland

build/hello-wayland: $(HEADERS) $(SOURCES)
	$(CC) $(CFLAGS) -o $@ $(SOURCES) -lrt $(WAYLAND_FLAGS)

src/xdg-shell-client-protocol.h:
	$(WAYLAND_SCANNER) client-header $(XDG_SHELL_PROTOCOL) src/xdg-shell-client-protocol.h

src/xdg-shell-protocol.c:
	$(WAYLAND_SCANNER) private-code $(XDG_SHELL_PROTOCOL) src/xdg-shell-protocol.c

src/cat.h: src/cat.png
	convert src/cat.png -define h:format=bgra -depth 8 src/cat.h

.PHONY: clean
clean:
	$(RM) build/hello-wayland src/cat.h src/xdg-shell-protocol.c src/xdg-shell-client-protocol.h
