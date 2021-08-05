include config.mk
include wayland-scanner.mk

CFLAGS += -Iinclude -I. -DWLR_USE_UNSTABLE -std=c99

WAYLAND_PROTOCOLS=$(shell pkg-config --variable=pkgdatadir wayland-protocols)
WAYLAND_SCANNER=$(shell pkg-config --variable=wayland_scanner wayland-scanner)

PKGS = wlroots wayland-server xcb xkbcommon libinput
CFLAGS += $(foreach p,$(PKGS),$(shell pkg-config --cflags $(p)))
LDLIBS += $(foreach p,$(PKGS),$(shell pkg-config --libs $(p)))

obj/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

all: dirs dwl

clean:
	rm -rf dwl include/*-protocol.h src/*-protocol.c obj

install: dwl
	install -D dwl $(PREFIX)/bin/dwl

uninstall:
	rm -f $(PREFIX)/bin/dwl

.PHONY: all clean install uninstall dirs

config.h: | config.def.h
	cp config.def.h $@

dirs:
	mkdir -p obj

src/dwl.o: config.mk wayland-scanner.mk config.h include/client.h include/xdg-shell-protocol.h include/wlr-layer-shell-unstable-v1-protocol.h include/idle-protocol.h

src/wallpaper.o: config.h include/wallpaper.h

dwl: obj/xdg-shell-protocol.o obj/wlr-layer-shell-unstable-v1-protocol.o obj/idle-protocol.o obj/dwl.o obj/wallpaper.o
	$(CC) $(LDLIBS) -o $@ $^
