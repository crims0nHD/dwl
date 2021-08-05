# wayland-scanner is a tool which generates C headers and rigging for Wayland
# protocols, which are specified in XML. wlroots requires you to rig these up
# to your build system yourself and provide them in the include path.
include/xdg-shell-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

src/xdg-shell-protocol.c:
	$(WAYLAND_SCANNER) private-code \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

obj/xdg-shell-protocol.o: include/xdg-shell-protocol.h

include/wlr-layer-shell-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		protocols/wlr-layer-shell-unstable-v1.xml $@

src/wlr-layer-shell-unstable-v1-protocol.c:
	$(WAYLAND_SCANNER) private-code \
		protocols/wlr-layer-shell-unstable-v1.xml $@

obj/wlr-layer-shell-unstable-v1-protocol.o: include/wlr-layer-shell-unstable-v1-protocol.h

include/idle-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		protocols/idle.xml $@

src/idle-protocol.c:
	$(WAYLAND_SCANNER) private-code \
		protocols/idle.xml $@

obj/idle-protocol.o: include/idle-protocol.h
