#include "config.h"
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include <stdlib.h>
#include <string.h>

#include <wlr/types/wlr_xcursor_manager.h>

#ifdef XWAYLAND
/* configure xserver for use with wayland */
void configurex11(struct wl_listener *listener, void *data) {
  Client *c = wl_container_of(listener, c, configure);
  struct wlr_xwayland_surface_configure_event *event = data;
  wlr_xwayland_surface_configure(c->surface.xwayland, event->x, event->y,
                                 event->width, event->height);
}

/* TODO */
void activatex11(struct wl_listener *listener, void *data) {
  Client *c = wl_container_of(listener, c, activate);

  /* Only "managed" windows can be activated */
  if (c->type == X11Managed)
    wlr_xwayland_surface_activate(c->surface.xwayland, 1);
}

/* TODO */
Atom getatom(xcb_connection_t *xc, const char *name) {
  Atom atom = 0;
  xcb_intern_atom_reply_t *reply;
  xcb_intern_atom_cookie_t cookie = xcb_intern_atom(xc, 0, strlen(name), name);
  if ((reply = xcb_intern_atom_reply(xc, cookie, NULL)))
    atom = reply->atom;
  free(reply);

  return atom;
}

/* TODO */
void renderindependents(struct wlr_output *output, struct timespec *now) {
  Client *c;
  struct render_data rdata;
  struct wlr_box geom;

  wl_list_for_each_reverse(c, &independents, link) {
    geom.x = c->surface.xwayland->x;
    geom.y = c->surface.xwayland->y;
    geom.width = c->surface.xwayland->width;
    geom.height = c->surface.xwayland->height;

    /* Only render visible clients which show on this output */
    if (!wlr_output_layout_intersects(output_layout, output, &geom))
      continue;

    rdata.output = output;
    rdata.when = now;
    rdata.x = c->surface.xwayland->x;
    rdata.y = c->surface.xwayland->y;
    wlr_surface_for_each_surface(c->surface.xwayland->surface, render, &rdata);
  }
}

/* Get the topmost x11 client by a x,y coordinate */
Client *xytoindependent(double x, double y) {
  /* Find the topmost visible independent at point (x, y).
   * For independents, the most recently created can be used as the "top".
   * We rely on the X11 convention of unmapping unmanaged when the "owning"
   * client loses focus, which ensures that unmanaged are only visible on
   * the current tag. */
  Client *c;
  wl_list_for_each_reverse(c, &independents, link) {
    struct wlr_box geom = {
        .x = c->surface.xwayland->x,
        .y = c->surface.xwayland->y,
        .width = c->surface.xwayland->width,
        .height = c->surface.xwayland->height,
    };
    if (wlr_box_contains_point(&geom, x, y))
      return c;
  }
  return NULL;
}
#endif
