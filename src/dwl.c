/*
 * See LICENSE file for copyright and license details.
 */
#define _POSIX_C_SOURCE 200809L

#include <linux/input-event-codes.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <libinput.h>

#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/libinput.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_idle.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

#ifdef XWAYLAND
#include <X11/Xlib.h>
#include <wlr/xwayland.h>
#endif

#include "functions.h"
#include "macros.h"
#include "types.h"
#include "variables.h"

/* configuration, allows nested code to access above variables */
#include "config.h"

/* attempt to encapsulate suck into one file */
#include "client.h"

/* compile-time check if all tags fit into an unsigned int bit array. */
struct NumTags {
  char limitexceeded[LENGTH(tags) > 31 ? -1 : 1];
};

/* function implementations */
void applybounds(Client *c, struct wlr_box *bbox) {
  /* set minimum possible */
  c->geom.width = MAX(1, c->geom.width);
  c->geom.height = MAX(1, c->geom.height);

  if (c->geom.x >= bbox->x + bbox->width)
    c->geom.x = bbox->x + bbox->width - c->geom.width;
  if (c->geom.y >= bbox->y + bbox->height)
    c->geom.y = bbox->y + bbox->height - c->geom.height;
  if (c->geom.x + c->geom.width + 2 * c->bw <= bbox->x)
    c->geom.x = bbox->x;
  if (c->geom.y + c->geom.height + 2 * c->bw <= bbox->y)
    c->geom.y = bbox->y;
}

void applyexclusive(struct wlr_box *usable_area, uint32_t anchor,
                    int32_t exclusive, int32_t margin_top, int32_t margin_right,
                    int32_t margin_bottom, int32_t margin_left) {
  Edge edges[] = {{
                      // Top
                      .singular_anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP,
                      .anchor_triplet = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
                                        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP,
                      .positive_axis = &usable_area->y,
                      .negative_axis = &usable_area->height,
                      .margin = margin_top,
                  },
                  {
                      // Bottom
                      .singular_anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM,
                      .anchor_triplet = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
                                        ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM,
                      .positive_axis = NULL,
                      .negative_axis = &usable_area->height,
                      .margin = margin_bottom,
                  },
                  {
                      // Left
                      .singular_anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT,
                      .anchor_triplet = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                                        ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM,
                      .positive_axis = &usable_area->x,
                      .negative_axis = &usable_area->width,
                      .margin = margin_left,
                  },
                  {
                      // Right
                      .singular_anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT,
                      .anchor_triplet = ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
                                        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                                        ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM,
                      .positive_axis = NULL,
                      .negative_axis = &usable_area->width,
                      .margin = margin_right,
                  }};
  for (size_t i = 0; i < LENGTH(edges); i++) {
    if ((anchor == edges[i].singular_anchor ||
         anchor == edges[i].anchor_triplet) &&
        exclusive + edges[i].margin > 0) {
      if (edges[i].positive_axis)
        *edges[i].positive_axis += exclusive + edges[i].margin;
      if (edges[i].negative_axis)
        *edges[i].negative_axis -= exclusive + edges[i].margin;
      break;
    }
  }
}

void applyrules(Client *c) {
  /* rule matching */
  const char *appid, *title;
  unsigned int i, newtags = 0;
  const Rule *r;
  Monitor *mon = selmon, *m;

  c->isfloating = client_is_float_type(c);
  if (!(appid = client_get_appid(c)))
    appid = broken;
  if (!(title = client_get_title(c)))
    title = broken;

  for (r = rules; r < END(rules); r++) {
    if ((!r->title || strstr(title, r->title)) &&
        (!r->id || strstr(appid, r->id))) {
      c->isfloating = r->isfloating;
      newtags |= r->tags;
      i = 0;
      wl_list_for_each(m, &mons, link) if (r->monitor == i++) mon = m;
    }
  }
  setmon(c, mon, newtags);
}

void arrange(Monitor *m) {
  if (m->lt[m->sellt]->arrange)
    m->lt[m->sellt]->arrange(m);
  /* TODO recheck pointer focus here... or in resize()? */
}

void arrangelayer(Monitor *m, struct wl_list *list, struct wlr_box *usable_area,
                  int exclusive) {
  LayerSurface *layersurface;
  struct wlr_box full_area = m->m;

  wl_list_for_each(layersurface, list, link) {
    struct wlr_layer_surface_v1 *wlr_layer_surface =
        layersurface->layer_surface;
    struct wlr_layer_surface_v1_state *state = &wlr_layer_surface->current;
    struct wlr_box bounds;
    struct wlr_box box = {.width = state->desired_width,
                          .height = state->desired_height};
    const uint32_t both_horiz =
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    const uint32_t both_vert =
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;

    if (exclusive != (state->exclusive_zone > 0))
      continue;

    bounds = state->exclusive_zone == -1 ? full_area : *usable_area;

    // Horizontal axis
    if ((state->anchor & both_horiz) && box.width == 0) {
      box.x = bounds.x;
      box.width = bounds.width;
    } else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT)) {
      box.x = bounds.x;
    } else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT)) {
      box.x = bounds.x + (bounds.width - box.width);
    } else {
      box.x = bounds.x + ((bounds.width / 2) - (box.width / 2));
    }
    // Vertical axis
    if ((state->anchor & both_vert) && box.height == 0) {
      box.y = bounds.y;
      box.height = bounds.height;
    } else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP)) {
      box.y = bounds.y;
    } else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM)) {
      box.y = bounds.y + (bounds.height - box.height);
    } else {
      box.y = bounds.y + ((bounds.height / 2) - (box.height / 2));
    }
    // Margin
    if ((state->anchor & both_horiz) == both_horiz) {
      box.x += state->margin.left;
      box.width -= state->margin.left + state->margin.right;
    } else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT)) {
      box.x += state->margin.left;
    } else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT)) {
      box.x -= state->margin.right;
    }
    if ((state->anchor & both_vert) == both_vert) {
      box.y += state->margin.top;
      box.height -= state->margin.top + state->margin.bottom;
    } else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP)) {
      box.y += state->margin.top;
    } else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM)) {
      box.y -= state->margin.bottom;
    }
    if (box.width < 0 || box.height < 0) {
      wlr_layer_surface_v1_close(wlr_layer_surface);
      continue;
    }
    layersurface->geo = box;

    if (state->exclusive_zone > 0)
      applyexclusive(usable_area, state->anchor, state->exclusive_zone,
                     state->margin.top, state->margin.right,
                     state->margin.bottom, state->margin.left);
    wlr_layer_surface_v1_configure(wlr_layer_surface, box.width, box.height);
  }
}

void arrangelayers(Monitor *m) {
  struct wlr_box usable_area = m->m;
  uint32_t layers_above_shell[] = {
      ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
      ZWLR_LAYER_SHELL_V1_LAYER_TOP,
  };
  LayerSurface *layersurface;
  struct wlr_keyboard *kb = wlr_seat_get_keyboard(seat);

  // Arrange exclusive surfaces from top->bottom
  arrangelayer(m, &m->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY], &usable_area,
               1);
  arrangelayer(m, &m->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP], &usable_area, 1);
  arrangelayer(m, &m->layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM], &usable_area,
               1);
  arrangelayer(m, &m->layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND],
               &usable_area, 1);

  if (memcmp(&usable_area, &m->w, sizeof(struct wlr_box))) {
    m->w = usable_area;
    arrange(m);
  }

  // Arrange non-exlusive surfaces from top->bottom
  arrangelayer(m, &m->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY], &usable_area,
               0);
  arrangelayer(m, &m->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP], &usable_area, 0);
  arrangelayer(m, &m->layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM], &usable_area,
               0);
  arrangelayer(m, &m->layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND],
               &usable_area, 0);

  // Find topmost keyboard interactive layer, if such a layer exists
  for (size_t i = 0; i < LENGTH(layers_above_shell); i++) {
    wl_list_for_each_reverse(layersurface, &m->layers[layers_above_shell[i]],
                             link) {
      if (layersurface->layer_surface->current.keyboard_interactive &&
          layersurface->layer_surface->mapped) {
        // Deactivate the focused client.
        focusclient(NULL, 0);
        wlr_seat_keyboard_notify_enter(
            seat, layersurface->layer_surface->surface, kb->keycodes,
            kb->num_keycodes, &kb->modifiers);
        return;
      }
    }
  }
}

void chvt(const Arg *arg) {
  wlr_session_change_vt(wlr_backend_get_session(backend), arg->ui);
}

void cleanup(void) {
#ifdef XWAYLAND
  wlr_xwayland_destroy(xwayland);
#endif
  wl_display_destroy_clients(dpy);

  wlr_backend_destroy(backend);
  wlr_xcursor_manager_destroy(cursor_mgr);
  wlr_cursor_destroy(cursor);
  wlr_output_layout_destroy(output_layout);
  wlr_seat_destroy(seat);
  wl_display_destroy(dpy);
}

void cleanupkeyboard(struct wl_listener *listener, void *data) {
  struct wlr_input_device *device = data;
  Keyboard *kb = device->data;

  wl_list_remove(&kb->link);
  wl_list_remove(&kb->modifiers.link);
  wl_list_remove(&kb->key.link);
  wl_list_remove(&kb->destroy.link);
  free(kb);
}

void cleanupmon(struct wl_listener *listener, void *data) {
  struct wlr_output *wlr_output = data;
  Monitor *m = wlr_output->data;
  int nmons, i = 0;

  wl_list_remove(&m->destroy.link);
  wl_list_remove(&m->frame.link);
  wl_list_remove(&m->link);
  wlr_output_layout_remove(output_layout, m->wlr_output);

  nmons = wl_list_length(&mons);
  do // don't switch to disabled mons
    selmon = wl_container_of(mons.prev, selmon, link);
  while (!selmon->wlr_output->enabled && i++ < nmons);
  focusclient(focustop(selmon), 1);
  closemon(m);
  free(m);
}

void closemon(Monitor *m) {
  // move closed monitor's clients to the focused one
  Client *c;

  wl_list_for_each(c, &clients, link) {
    if (c->isfloating && c->geom.x > m->m.width)
      resize(c, c->geom.x - m->w.width, c->geom.y, c->geom.width,
             c->geom.height, 0);
    if (c->mon == m)
      setmon(c, selmon, c->tags);
  }
}

void commitlayersurfacenotify(struct wl_listener *listener, void *data) {
  LayerSurface *layersurface =
      wl_container_of(listener, layersurface, surface_commit);
  struct wlr_layer_surface_v1 *wlr_layer_surface = layersurface->layer_surface;
  struct wlr_output *wlr_output = wlr_layer_surface->output;
  Monitor *m;

  if (!wlr_output)
    return;

  m = wlr_output->data;
  arrangelayers(m);

  if (layersurface->layer != wlr_layer_surface->current.layer) {
    wl_list_remove(&layersurface->link);
    wl_list_insert(&m->layers[wlr_layer_surface->current.layer],
                   &layersurface->link);
    layersurface->layer = wlr_layer_surface->current.layer;
  }
}

void commitnotify(struct wl_listener *listener, void *data) {
  Client *c = wl_container_of(listener, c, commit);

  /* mark a pending resize as completed */
  if (c->resize && c->resize <= c->surface.xdg->configure_serial)
    c->resize = 0;
}

void createkeyboard(struct wlr_input_device *device) {
  struct xkb_context *context;
  struct xkb_keymap *keymap;
  Keyboard *kb = device->data = calloc(1, sizeof(*kb));
  kb->device = device;

  /* Prepare an XKB keymap and assign it to the keyboard. */
  context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  keymap =
      xkb_map_new_from_names(context, &xkb_rules, XKB_KEYMAP_COMPILE_NO_FLAGS);

  wlr_keyboard_set_keymap(device->keyboard, keymap);
  xkb_keymap_unref(keymap);
  xkb_context_unref(context);
  wlr_keyboard_set_repeat_info(device->keyboard, repeat_rate, repeat_delay);

  /* Here we set up listeners for keyboard events. */
  LISTEN(&device->keyboard->events.modifiers, &kb->modifiers, keypressmod);
  LISTEN(&device->keyboard->events.key, &kb->key, keypress);
  LISTEN(&device->events.destroy, &kb->destroy, cleanupkeyboard);

  wlr_seat_set_keyboard(seat, device);

  /* And add the keyboard to our list of keyboards */
  wl_list_insert(&keyboards, &kb->link);
}

void createpointer(struct wlr_input_device *device) {
  if (wlr_input_device_is_libinput(device)) {
    struct libinput_device *libinput_device =
        (struct libinput_device *)wlr_libinput_get_device_handle(device);

    if (tap_to_click &&
        libinput_device_config_tap_get_finger_count(libinput_device))
      libinput_device_config_tap_set_enabled(libinput_device,
                                             LIBINPUT_CONFIG_TAP_ENABLED);

    if (libinput_device_config_scroll_has_natural_scroll(libinput_device))
      libinput_device_config_scroll_set_natural_scroll_enabled(
          libinput_device, natural_scrolling);
  }

  /* We don't do anything special with pointers. All of our pointer handling
   * is proxied through wlr_cursor. On another compositor, you might take this
   * opportunity to do libinput configuration on the device to set
   * acceleration, etc. */
  wlr_cursor_attach_input_device(cursor, device);
}

void destroylayersurfacenotify(struct wl_listener *listener, void *data) {
  LayerSurface *layersurface = wl_container_of(listener, layersurface, destroy);

  if (layersurface->layer_surface->mapped)
    unmaplayersurface(layersurface);
  wl_list_remove(&layersurface->link);
  wl_list_remove(&layersurface->destroy.link);
  wl_list_remove(&layersurface->map.link);
  wl_list_remove(&layersurface->unmap.link);
  wl_list_remove(&layersurface->surface_commit.link);
  if (layersurface->layer_surface->output) {
    Monitor *m = layersurface->layer_surface->output->data;
    if (m)
      arrangelayers(m);
    layersurface->layer_surface->output = NULL;
  }
  free(layersurface);
}

void destroynotify(struct wl_listener *listener, void *data) {
  /* Called when the surface is destroyed and should never be shown again. */
  Client *c = wl_container_of(listener, c, destroy);
  wl_list_remove(&c->map.link);
  wl_list_remove(&c->unmap.link);
  wl_list_remove(&c->destroy.link);
  wl_list_remove(&c->set_title.link);
  wl_list_remove(&c->fullscreen.link);
#ifdef XWAYLAND
  if (c->type == X11Managed)
    wl_list_remove(&c->activate.link);
  else if (c->type == XDGShell)
#endif
    wl_list_remove(&c->commit.link);
  free(c);
}

void togglefullscreen(const Arg *arg) {
  Client *sel = selclient();
  if (sel)
    setfullscreen(sel, !sel->isfullscreen);
}

void setfullscreen(Client *c, int fullscreen) {
  c->isfullscreen = fullscreen;
  c->bw = (1 - fullscreen) * borderpx;
  client_set_fullscreen(c, fullscreen);

  if (fullscreen) {
    c->prevx = c->geom.x;
    c->prevy = c->geom.y;
    c->prevheight = c->geom.height;
    c->prevwidth = c->geom.width;
    resize(c, c->mon->m.x, c->mon->m.y, c->mon->m.width, c->mon->m.height, 0);
  } else {
    /* restore previous size instead of arrange for floating windows since
     * client positions are set by the user and cannot be recalculated */
    resize(c, c->prevx, c->prevy, c->prevwidth, c->prevheight, 0);
    arrange(c->mon);
  }
}

void fullscreennotify(struct wl_listener *listener, void *data) {
  Client *c = wl_container_of(listener, c, fullscreen);
  setfullscreen(c, !c->isfullscreen);
}

Monitor *dirtomon(enum wlr_direction dir) {
  struct wlr_output *next;
  if ((next = wlr_output_layout_adjacent_output(
           output_layout, dir, selmon->wlr_output, selmon->m.x, selmon->m.y)))
    return next->data;
  if ((next = wlr_output_layout_farthest_output(
           output_layout, dir ^ (WLR_DIRECTION_LEFT | WLR_DIRECTION_RIGHT),
           selmon->wlr_output, selmon->m.x, selmon->m.y)))
    return next->data;
  return selmon;
}

void focusclient(Client *c, int lift) {
  struct wlr_surface *old = seat->keyboard_state.focused_surface;
  struct wlr_keyboard *kb;

  /* Raise client in stacking order if requested */
  if (c && lift) {
    wl_list_remove(&c->slink);
    wl_list_insert(&stack, &c->slink);
  }

  if (c && client_surface(c) == old)
    return;

  /* Put the new client atop the focus stack and select its monitor */
  if (c) {
    wl_list_remove(&c->flink);
    wl_list_insert(&fstack, &c->flink);
    selmon = c->mon;
    c->isurgent = 0;
  }
  printstatus();

  /* Deactivate old client if focus is changing */
  if (old && (!c || client_surface(c) != old)) {
    /* If an overlay is focused, don't focus or activate the client,
     * but only update its position in fstack to render its border with
     * focuscolor and focus it after the overlay is closed. It's probably
     * pointless to check if old is a layer surface since it can't be anything
     * else at this point. */
    if (wlr_surface_is_layer_surface(old)) {
      struct wlr_layer_surface_v1 *wlr_layer_surface =
          wlr_layer_surface_v1_from_wlr_surface(old);

      if (wlr_layer_surface->mapped &&
          (wlr_layer_surface->current.layer == ZWLR_LAYER_SHELL_V1_LAYER_TOP ||
           wlr_layer_surface->current.layer ==
               ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY))
        return;
    } else {
      client_activate_surface(old, 0);
    }
  }

  if (!c) {
    /* With no client, all we have left is to clear focus */
    wlr_seat_keyboard_notify_clear_focus(seat);
    return;
  }

  /* Have a client, so focus its top-level wlr_surface */
  kb = wlr_seat_get_keyboard(seat);
  wlr_seat_keyboard_notify_enter(seat, client_surface(c), kb->keycodes,
                                 kb->num_keycodes, &kb->modifiers);

  /* Activate the new client */
  client_activate_surface(client_surface(c), 1);
}

void focusmon(const Arg *arg) {
  do
    selmon = dirtomon(arg->i);
  while (!selmon->wlr_output->enabled);
  focusclient(focustop(selmon), 1);
}

void focusstack(const Arg *arg) {
  /* Focus the next or previous client (in tiling order) on selmon */
  Client *c, *sel = selclient();
  if (!sel)
    return;
  if (arg->i > 0) {
    wl_list_for_each(c, &sel->link, link) {
      if (&c->link == &clients)
        continue; /* wrap past the sentinel node */
      if (VISIBLEON(c, selmon))
        break; /* found it */
    }
  } else {
    wl_list_for_each_reverse(c, &sel->link, link) {
      if (&c->link == &clients)
        continue; /* wrap past the sentinel node */
      if (VISIBLEON(c, selmon))
        break; /* found it */
    }
  }
  /* If only one client is visible on selmon, then c == sel */
  focusclient(c, 1);
}

Client *focustop(Monitor *m) {
  Client *c;
  wl_list_for_each(c, &fstack, flink) if (VISIBLEON(c, m)) return c;
  return NULL;
}

void incnmaster(const Arg *arg) {
  selmon->nmaster = MAX(selmon->nmaster + arg->i, 0);
  arrange(selmon);
}

int keybinding(uint32_t mods, xkb_keysym_t sym) {
  /*
   * Here we handle compositor keybindings. This is when the compositor is
   * processing keys, rather than passing them on to the client for its own
   * processing.
   */
  int handled = 0;
  const Key *k;
  for (k = keys; k < END(keys); k++) {
    if (CLEANMASK(mods) == CLEANMASK(k->mod) && sym == k->keysym && k->func) {
      k->func(&k->arg);
      handled = 1;
    }
  }
  return handled;
}

void keypress(struct wl_listener *listener, void *data) {
  int i;
  /* This event is raised when a key is pressed or released. */
  Keyboard *kb = wl_container_of(listener, kb, key);
  struct wlr_event_keyboard_key *event = data;

  /* Translate libinput keycode -> xkbcommon */
  uint32_t keycode = event->keycode + 8;
  /* Get a list of keysyms based on the keymap for this keyboard */
  const xkb_keysym_t *syms;
  int nsyms =
      xkb_state_key_get_syms(kb->device->keyboard->xkb_state, keycode, &syms);

  int handled = 0;
  uint32_t mods = wlr_keyboard_get_modifiers(kb->device->keyboard);

  wlr_idle_notify_activity(idle, seat);

  /* On _press_, attempt to process a compositor keybinding. */
  if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED)
    for (i = 0; i < nsyms; i++)
      handled = keybinding(mods, syms[i]) || handled;

  if (!handled) {
    /* Pass unhandled keycodes along to the client. */
    wlr_seat_set_keyboard(seat, kb->device);
    wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode,
                                 event->state);
  }
}

void keypressmod(struct wl_listener *listener, void *data) {
  /* This event is raised when a modifier key, such as shift or alt, is
   * pressed. We simply communicate this to the client. */
  Keyboard *kb = wl_container_of(listener, kb, modifiers);
  /*
   * A seat can only have one keyboard, but this is a limitation of the
   * Wayland protocol - not wlroots. We assign all connected keyboards to the
   * same seat. You can swap out the underlying wlr_keyboard like this and
   * wlr_seat handles this transparently.
   */
  wlr_seat_set_keyboard(seat, kb->device);
  /* Send modifiers to the client. */
  wlr_seat_keyboard_notify_modifiers(seat, &kb->device->keyboard->modifiers);
}

void killclient(const Arg *arg) {
  Client *sel = selclient();
  if (!sel)
    return;
  client_send_close(sel);
}

void maplayersurfacenotify(struct wl_listener *listener, void *data) {
  LayerSurface *layersurface = wl_container_of(listener, layersurface, map);
  wlr_surface_send_enter(layersurface->layer_surface->surface,
                         layersurface->layer_surface->output);
  motionnotify(0);
}

void mapnotify(struct wl_listener *listener, void *data) {
  /* Called when the surface is mapped, or ready to display on-screen. */
  Client *c = wl_container_of(listener, c, map);

  if (client_is_unmanaged(c)) {
    /* Insert this independent into independents lists. */
    wl_list_insert(&independents, &c->link);
    return;
  }

  /* Insert this client into client lists. */
  wl_list_insert(&clients, &c->link);
  wl_list_insert(&fstack, &c->flink);
  wl_list_insert(&stack, &c->slink);

  client_get_geometry(c, &c->geom);
  c->geom.width += 2 * c->bw;
  c->geom.height += 2 * c->bw;

  /* Tell the client not to try anything fancy */
  client_set_tiled(c, WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT |
                          WLR_EDGE_RIGHT);

  /* Set initial monitor, tags, floating status, and focus */
  applyrules(c);
}

void monocle(Monitor *m) {
  Client *c;

  wl_list_for_each(c, &clients, link) {
    if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
      continue;
    resize(c, m->w.x, m->w.y, m->w.width, m->w.height, 0);
  }
}

void motionnotify(uint32_t time) {
  double sx = 0, sy = 0;
  struct wlr_surface *surface = NULL;
  Client *c = NULL;

  // time is 0 in internal calls meant to restore pointer focus.
  if (time) {
    wlr_idle_notify_activity(idle, seat);

    /* Update selmon (even while dragging a window) */
    if (sloppyfocus)
      selmon = xytomon(cursor->x, cursor->y);
  }

  /* If we are currently grabbing the mouse, handle and return */
  if (cursor_mode == CurMove) {
    /* Move the grabbed client to the new position. */
    resize(grabc, cursor->x - grabcx, cursor->y - grabcy, grabc->geom.width,
           grabc->geom.height, 1);
    return;
  } else if (cursor_mode == CurResize) {
    resize(grabc, grabc->geom.x, grabc->geom.y, cursor->x - grabc->geom.x,
           cursor->y - grabc->geom.y, 1);
    return;
  }

  if ((surface =
           xytolayersurface(&selmon->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY],
                            cursor->x, cursor->y, &sx, &sy)))
    ;
  else if ((surface =
                xytolayersurface(&selmon->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP],
                                 cursor->x, cursor->y, &sx, &sy)))
    ;
#ifdef XWAYLAND
  /* Find an independent under the pointer and send the event along. */
  else if ((c = xytoindependent(cursor->x, cursor->y))) {
    surface = wlr_surface_surface_at(c->surface.xwayland->surface,
                                     cursor->x - c->surface.xwayland->x - c->bw,
                                     cursor->y - c->surface.xwayland->y - c->bw,
                                     &sx, &sy);

    /* Otherwise, find the client under the pointer and send the event along. */
  }
#endif
  else if ((c = xytoclient(cursor->x, cursor->y))) {
    surface = client_surface_at(c, cursor->x - c->geom.x - c->bw,
                                cursor->y - c->geom.y - c->bw, &sx, &sy);
  } else if ((surface = xytolayersurface(
                  &selmon->layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM], cursor->x,
                  cursor->y, &sx, &sy)))
    ;
  else
    surface =
        xytolayersurface(&selmon->layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND],
                         cursor->x, cursor->y, &sx, &sy);

  /* If there's no client surface under the cursor, set the cursor image to a
   * default. This is what makes the cursor image appear when you move it
   * off of a client or over its border. */
  if (!surface && time)
    wlr_xcursor_manager_set_cursor_image(cursor_mgr, "left_ptr", cursor);

  pointerfocus(c, surface, sx, sy, time);
}

void moveresize(const Arg *arg) {
  if (cursor_mode != CurNormal || !(grabc = xytoclient(cursor->x, cursor->y)))
    return;

  /* Float the window and tell motionnotify to grab it */
  setfloating(grabc, 1);
  switch (cursor_mode = arg->ui) {
  case CurMove:
    grabcx = cursor->x - grabc->geom.x;
    grabcy = cursor->y - grabc->geom.y;
    wlr_xcursor_manager_set_cursor_image(cursor_mgr, "fleur", cursor);
    break;
  case CurResize:
    /* Doesn't work for X11 output - the next absolute motion event
     * returns the cursor to where it started */
    wlr_cursor_warp_closest(cursor, NULL, grabc->geom.x + grabc->geom.width,
                            grabc->geom.y + grabc->geom.height);
    wlr_xcursor_manager_set_cursor_image(cursor_mgr, "bottom_right_corner",
                                         cursor);
    break;
  }
}

void outputmgrapplyortest(struct wlr_output_configuration_v1 *config,
                          int test) {
  /*
   * Called when a client such as wlr-randr requests a change in output
   * configuration.  This is only one way that the layout can be changed,
   * so any Monitor information should be updated by updatemons() after an
   * output_layout.change event, not here.
   */
  struct wlr_output_configuration_head_v1 *config_head;
  int ok = 1;

  wl_list_for_each(config_head, &config->heads, link) {
    struct wlr_output *wlr_output = config_head->state.output;

    wlr_output_enable(wlr_output, config_head->state.enabled);
    if (config_head->state.enabled) {
      if (config_head->state.mode)
        wlr_output_set_mode(wlr_output, config_head->state.mode);
      else
        wlr_output_set_custom_mode(wlr_output,
                                   config_head->state.custom_mode.width,
                                   config_head->state.custom_mode.height,
                                   config_head->state.custom_mode.refresh);

      wlr_output_layout_move(output_layout, wlr_output, config_head->state.x,
                             config_head->state.y);
      wlr_output_set_transform(wlr_output, config_head->state.transform);
      wlr_output_set_scale(wlr_output, config_head->state.scale);
    }

    if (!(ok = wlr_output_test(wlr_output)))
      break;
  }
  wl_list_for_each(config_head, &config->heads, link) {
    if (ok && !test)
      wlr_output_commit(config_head->state.output);
    else
      wlr_output_rollback(config_head->state.output);
  }
  if (ok)
    wlr_output_configuration_v1_send_succeeded(config);
  else
    wlr_output_configuration_v1_send_failed(config);
  wlr_output_configuration_v1_destroy(config);
}

void pointerfocus(Client *c, struct wlr_surface *surface, double sx, double sy,
                  uint32_t time) {
  struct timespec now;
  int internal_call = !time;

  /* Use top level surface if nothing more specific given */
  if (c && !surface)
    surface = client_surface(c);

  /* If surface is NULL, clear pointer focus */
  if (!surface) {
    wlr_seat_pointer_notify_clear_focus(seat);
    return;
  }

  if (internal_call) {
    clock_gettime(CLOCK_MONOTONIC, &now);
    time = now.tv_sec * 1000 + now.tv_nsec / 1000000;
  }

  /* If surface is already focused, only notify of motion */
  if (surface == seat->pointer_state.focused_surface) {
    wlr_seat_pointer_notify_motion(seat, time, sx, sy);
    return;
  }

  /* Otherwise, let the client know that the mouse cursor has entered one
   * of its surfaces, and make keyboard focus follow if desired. */
  wlr_seat_pointer_notify_enter(seat, surface, sx, sy);

  if (!c || client_is_unmanaged(c))
    return;

  if (sloppyfocus && !internal_call)
    focusclient(c, 0);
}

void printstatus(void) {
  Monitor *m = NULL;
  Client *c;
  unsigned int occ, urg, sel;

  wl_list_for_each(m, &mons, link) {
    occ = urg = 0;
    wl_list_for_each(c, &clients, link) {
      if (c->mon != m)
        continue;
      occ |= c->tags;
      if (c->isurgent)
        urg |= c->tags;
    }
    if ((c = focustop(m))) {
      printf("%s title %s\n", m->wlr_output->name,
             client_get_title(focustop(m)));
      sel = c->tags;
    } else {
      printf("%s title \n", m->wlr_output->name);
      sel = 0;
    }

    printf("%s selmon %u\n", m->wlr_output->name, m == selmon);
    printf("%s tags %u %u %u %u\n", m->wlr_output->name, occ,
           m->tagset[m->seltags], sel, urg);
    printf("%s layout %s\n", m->wlr_output->name, m->lt[m->sellt]->symbol);
  }
  fflush(stdout);
}

void quit(const Arg *arg) { wl_display_terminate(dpy); }

void quitsignal(int signo) { quit(NULL); }

void render(struct wlr_surface *surface, int sx, int sy, void *data) {
  /* This function is called for every surface that needs to be rendered. */
  struct render_data *rdata = data;
  struct wlr_output *output = rdata->output;
  double ox = 0, oy = 0;
  struct wlr_box obox;
  float matrix[9];
  enum wl_output_transform transform;

  /* We first obtain a wlr_texture, which is a GPU resource. wlroots
   * automatically handles negotiating these with the client. The underlying
   * resource could be an opaque handle passed from the client, or the client
   * could have sent a pixel buffer which we copied to the GPU, or a few other
   * means. You don't have to worry about this, wlroots takes care of it. */
  struct wlr_texture *texture = wlr_surface_get_texture(surface);
  if (!texture)
    return;

  /* The client has a position in layout coordinates. If you have two displays,
   * one next to the other, both 1080p, a client on the rightmost display might
   * have layout coordinates of 2000,100. We need to translate that to
   * output-local coordinates, or (2000 - 1920). */
  wlr_output_layout_output_coords(output_layout, output, &ox, &oy);

  /* We also have to apply the scale factor for HiDPI outputs. This is only
   * part of the puzzle, dwl does not fully support HiDPI. */
  obox.x = ox + rdata->x + sx;
  obox.y = oy + rdata->y + sy;
  obox.width = surface->current.width;
  obox.height = surface->current.height;
  scalebox(&obox, output->scale);

  /*
   * Those familiar with OpenGL are also familiar with the role of matrices
   * in graphics programming. We need to prepare a matrix to render the
   * client with. wlr_matrix_project_box is a helper which takes a box with
   * a desired x, y coordinates, width and height, and an output geometry,
   * then prepares an orthographic projection and multiplies the necessary
   * transforms to produce a model-view-projection matrix.
   *
   * Naturally you can do this any way you like, for example to make a 3D
   * compositor.
   */
  transform = wlr_output_transform_invert(surface->current.transform);
  wlr_matrix_project_box(matrix, &obox, transform, 0, output->transform_matrix);

  /* This takes our matrix, the texture, and an alpha, and performs the actual
   * rendering on the GPU. */
  wlr_render_texture_with_matrix(drw, texture, matrix, 1);

  /* This lets the client know that we've displayed that frame and it can
   * prepare another one now if it likes. */
  wlr_surface_send_frame_done(surface, rdata->when);
}

void renderclients(Monitor *m, struct timespec *now) {
  Client *c, *sel = selclient();
  const float *color;
  double ox, oy;
  int i, w, h;
  struct render_data rdata;
  struct wlr_box *borders;
  struct wlr_surface *surface;
  /* Each subsequent window we render is rendered on top of the last. Because
   * our stacking list is ordered front-to-back, we iterate over it backwards.
   */
  wl_list_for_each_reverse(c, &stack, slink) {
    /* Only render visible clients which show on this monitor */
    if (!VISIBLEON(c, c->mon) ||
        !wlr_output_layout_intersects(output_layout, m->wlr_output, &c->geom))
      continue;

    surface = client_surface(c);
    ox = c->geom.x, oy = c->geom.y;
    wlr_output_layout_output_coords(output_layout, m->wlr_output, &ox, &oy);

    if (c->bw) {
      w = surface->current.width;
      h = surface->current.height;
      borders = (struct wlr_box[4]){
          {ox, oy, w + 2 * c->bw, c->bw},             /* top */
          {ox, oy + c->bw, c->bw, h},                 /* left */
          {ox + c->bw + w, oy + c->bw, c->bw, h},     /* right */
          {ox, oy + c->bw + h, w + 2 * c->bw, c->bw}, /* bottom */
      };

      /* Draw window borders */
      color = (c == sel) ? focuscolor : bordercolor;
      for (i = 0; i < 4; i++) {
        scalebox(&borders[i], m->wlr_output->scale);
        wlr_render_rect(drw, &borders[i], color,
                        m->wlr_output->transform_matrix);
      }
    }

    /* This calls our render function for each surface among the
     * xdg_surface's toplevel and popups. */
    rdata.output = m->wlr_output;
    rdata.when = now;
    rdata.x = c->geom.x + c->bw;
    rdata.y = c->geom.y + c->bw;
    client_for_each_surface(c, render, &rdata);
  }
}

void renderlayer(struct wl_list *layer_surfaces, struct timespec *now) {
  LayerSurface *layersurface;
  wl_list_for_each(layersurface, layer_surfaces, link) {
    struct render_data rdata = {
        .output = layersurface->layer_surface->output,
        .when = now,
        .x = layersurface->geo.x,
        .y = layersurface->geo.y,
    };

    wlr_surface_for_each_surface(layersurface->layer_surface->surface, render,
                                 &rdata);
  }
}

void rendermon(struct wl_listener *listener, void *data) {
  Client *c;
  int render = 1;

  /* This function is called every time an output is ready to display a frame,
   * generally at the output's refresh rate (e.g. 60Hz). */
  Monitor *m = wl_container_of(listener, m, frame);

  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  /* Do not render if any XDG clients have an outstanding resize. */
  wl_list_for_each(c, &stack, slink) {
    if (c->resize) {
      wlr_surface_send_frame_done(client_surface(c), &now);
      render = 0;
    }
  }

  /* HACK: This loop is the simplest way to handle ephemeral pageflip
   * failures but probably not the best. Revisit if damage tracking is
   * added. */
  do {
    /* wlr_output_attach_render makes the OpenGL context current. */
    if (!wlr_output_attach_render(m->wlr_output, NULL))
      return;

    if (render) {
      /* Begin the renderer (calls glViewport and some other GL sanity checks)
       */
      wlr_renderer_begin(drw, m->wlr_output->width, m->wlr_output->height);
      wlr_renderer_clear(drw, rootcolor);

      renderlayer(&m->layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND], &now);
      renderlayer(&m->layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM], &now);
      renderclients(m, &now);
#ifdef XWAYLAND
      renderindependents(m->wlr_output, &now);
#endif
      renderlayer(&m->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP], &now);
      renderlayer(&m->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY], &now);

      /* Hardware cursors are rendered by the GPU on a separate plane, and can
       * be moved around without re-rendering what's beneath them - which is
       * more efficient. However, not all hardware supports hardware cursors.
       * For this reason, wlroots provides a software fallback, which we ask it
       * to render here. wlr_cursor handles configuring hardware vs software
       * cursors for you,
       * and this function is a no-op when hardware cursors are in use. */
      wlr_output_render_software_cursors(m->wlr_output, NULL);

      /* Conclude rendering and swap the buffers, showing the final frame
       * on-screen. */
      wlr_renderer_end(drw);
    }

  } while (!wlr_output_commit(m->wlr_output));
}

void resize(Client *c, int x, int y, int w, int h, int interact) {
  /*
   * Note that I took some shortcuts here. In a more fleshed-out
   * compositor, you'd wait for the client to prepare a buffer at
   * the new size, then commit any movement that was prepared.
   */
  struct wlr_box *bbox = interact ? &sgeom : &c->mon->w;
  c->geom.x = x;
  c->geom.y = y;
  c->geom.width = w;
  c->geom.height = h;
  applybounds(c, bbox);
  /* wlroots makes this a no-op if size hasn't changed */
  c->resize =
      client_set_size(c, c->geom.width - 2 * c->bw, c->geom.height - 2 * c->bw);
}

void run(char *startup_cmd) {
  pid_t startup_pid = -1;

  /* Add a Unix socket to the Wayland display. */
  const char *socket = wl_display_add_socket_auto(dpy);
  if (!socket)
    BARF("startup: display_add_socket_auto");
  setenv("WAYLAND_DISPLAY", socket, 1);

  /* Now that the socket exists, run the startup command */
  if (startup_cmd) {
    int piperw[2];
    pipe(piperw);
    startup_pid = fork();
    if (startup_pid < 0)
      EBARF("startup: fork");
    if (startup_pid == 0) {
      dup2(piperw[0], STDIN_FILENO);
      close(piperw[1]);
      execl("/bin/sh", "/bin/sh", "-c", startup_cmd, NULL);
      EBARF("startup: execl");
    }
    dup2(piperw[1], STDOUT_FILENO);
    close(piperw[0]);
  }
  /* If nobody is reading the status output, don't terminate */
  signal(SIGPIPE, SIG_IGN);
  printstatus();

  /* Start the backend. This will enumerate outputs and inputs, become the DRM
   * master, etc */
  if (!wlr_backend_start(backend))
    BARF("startup: backend_start");

  /* Now that outputs are initialized, choose initial selmon based on
   * cursor position, and set default cursor image */
  selmon = xytomon(cursor->x, cursor->y);

  /* TODO hack to get cursor to display in its initial location (100, 100)
   * instead of (0, 0) and then jumping.  still may not be fully
   * initialized, as the image/coordinates are not transformed for the
   * monitor when displayed here */
  wlr_cursor_warp_closest(cursor, NULL, cursor->x, cursor->y);
  wlr_xcursor_manager_set_cursor_image(cursor_mgr, "left_ptr", cursor);

  /* Run the Wayland event loop. This does not return until you exit the
   * compositor. Starting the backend rigged up all of the necessary event
   * loop configuration to listen to libinput events, DRM events, generate
   * frame events at the refresh rate, and so on. */
  wl_display_run(dpy);

  if (startup_cmd) {
    kill(startup_pid, SIGTERM);
    waitpid(startup_pid, NULL, 0);
  }
}

void scalebox(struct wlr_box *box, float scale) {
  box->width = ROUND((box->x + box->width) * scale) - ROUND(box->x * scale);
  box->height = ROUND((box->y + box->height) * scale) - ROUND(box->y * scale);
  box->x = ROUND(box->x * scale);
  box->y = ROUND(box->y * scale);
}

Client *selclient(void) {
  Client *c = wl_container_of(fstack.next, c, flink);
  if (wl_list_empty(&fstack) || !VISIBLEON(c, selmon))
    return NULL;
  return c;
}

void setfloating(Client *c, int floating) {
  c->isfloating = floating;
  arrange(c->mon);
}

void setlayout(const Arg *arg) {
  if (!arg || !arg->v || arg->v != selmon->lt[selmon->sellt])
    selmon->sellt ^= 1;
  if (arg && arg->v)
    selmon->lt[selmon->sellt] = (Layout *)arg->v;
  /* TODO change layout symbol? */
  arrange(selmon);
  printstatus();
}

/* arg > 1.0 will set mfact absolutely */
void setmfact(const Arg *arg) {
  float f;

  if (!arg || !selmon->lt[selmon->sellt]->arrange)
    return;
  f = arg->f < 1.0 ? arg->f + selmon->mfact : arg->f - 1.0;
  if (f < 0.1 || f > 0.9)
    return;
  selmon->mfact = f;
  arrange(selmon);
}

void setmon(Client *c, Monitor *m, unsigned int newtags) {
  Monitor *oldmon = c->mon;

  if (oldmon == m)
    return;
  c->mon = m;

  /* TODO leave/enter is not optimal but works */
  if (oldmon) {
    wlr_surface_send_leave(client_surface(c), oldmon->wlr_output);
    arrange(oldmon);
  }
  if (m) {
    /* Make sure window actually overlaps with the monitor */
    applybounds(c, &m->m);
    wlr_surface_send_enter(client_surface(c), m->wlr_output);
    c->tags = newtags
                  ? newtags
                  : m->tagset[m->seltags]; /* assign tags of target monitor */
    arrange(m);
  }
  focusclient(focustop(selmon), 1);
}

void sigchld(int unused) {
  /* We should be able to remove this function in favor of a simple
   *     signal(SIGCHLD, SIG_IGN);
   * but the Xwayland implementation in wlroots currently prevents us from
   * setting our own disposition for SIGCHLD.
   */
  if (signal(SIGCHLD, sigchld) == SIG_ERR)
    EBARF("can't install SIGCHLD handler");
  while (0 < waitpid(-1, NULL, WNOHANG))
    ;
}

void spawn(const Arg *arg) {
  if (fork() == 0) {
    dup2(STDERR_FILENO, STDOUT_FILENO);
    setsid();
    execvp(((char **)arg->v)[0], (char **)arg->v);
    EBARF("dwl: execvp %s failed", ((char **)arg->v)[0]);
  }
}

void tag(const Arg *arg) {
  Client *sel = selclient();
  if (sel && arg->ui & TAGMASK) {
    sel->tags = arg->ui & TAGMASK;
    focusclient(focustop(selmon), 1);
    arrange(selmon);
  }
  printstatus();
}

void tagmon(const Arg *arg) {
  Client *sel = selclient();
  if (!sel)
    return;
  setmon(sel, dirtomon(arg->i), 0);
}

void toggletag(const Arg *arg) {
  unsigned int newtags;
  Client *sel = selclient();
  if (!sel)
    return;
  newtags = sel->tags ^ (arg->ui & TAGMASK);
  if (newtags) {
    sel->tags = newtags;
    focusclient(focustop(selmon), 1);
    arrange(selmon);
  }
  printstatus();
}

void toggleview(const Arg *arg) {
  unsigned int newtagset =
      selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK);

  if (newtagset) {
    selmon->tagset[selmon->seltags] = newtagset;
    focusclient(focustop(selmon), 1);
    arrange(selmon);
  }
  printstatus();
}

void unmaplayersurface(LayerSurface *layersurface) {
  layersurface->layer_surface->mapped = 0;
  if (layersurface->layer_surface->surface ==
      seat->keyboard_state.focused_surface)
    focusclient(selclient(), 1);
  motionnotify(0);
}

void unmaplayersurfacenotify(struct wl_listener *listener, void *data) {
  LayerSurface *layersurface = wl_container_of(listener, layersurface, unmap);
  unmaplayersurface(layersurface);
}

void unmapnotify(struct wl_listener *listener, void *data) {
  /* Called when the surface is unmapped, and should no longer be shown. */
  Client *c = wl_container_of(listener, c, unmap);
  wl_list_remove(&c->link);
  if (client_is_unmanaged(c))
    return;

  setmon(c, NULL, 0);
  wl_list_remove(&c->flink);
  wl_list_remove(&c->slink);
}

void updatetitle(struct wl_listener *listener, void *data) {
  Client *c = wl_container_of(listener, c, set_title);
  if (c == focustop(c->mon))
    printstatus();
}

void view(const Arg *arg) {
  if ((arg->ui & TAGMASK) == selmon->tagset[selmon->seltags])
    return;
  selmon->seltags ^= 1; /* toggle sel tagset */
  if (arg->ui & TAGMASK)
    selmon->tagset[selmon->seltags] = arg->ui & TAGMASK;
  focusclient(focustop(selmon), 1);
  arrange(selmon);
  printstatus();
}

Client *xytoclient(double x, double y) {
  /* Find the topmost visible client (if any) at point (x, y), including
   * borders. This relies on stack being ordered from top to bottom. */
  Client *c;
  wl_list_for_each(c, &stack,
                   slink) if (VISIBLEON(c, c->mon) &&
                              wlr_box_contains_point(&c->geom, x, y)) return c;
  return NULL;
}

struct wlr_surface *xytolayersurface(struct wl_list *layer_surfaces, double x,
                                     double y, double *sx, double *sy) {
  LayerSurface *layersurface;
  wl_list_for_each_reverse(layersurface, layer_surfaces, link) {
    struct wlr_surface *sub;
    if (!layersurface->layer_surface->mapped)
      continue;
    sub = wlr_layer_surface_v1_surface_at(layersurface->layer_surface,
                                          x - layersurface->geo.x,
                                          y - layersurface->geo.y, sx, sy);
    if (sub)
      return sub;
  }
  return NULL;
}

Monitor *xytomon(double x, double y) {
  struct wlr_output *o = wlr_output_layout_output_at(output_layout, x, y);
  return o ? o->data : NULL;
}

void zoom(const Arg *arg) {
  Client *c, *sel = selclient();

  if (!sel || !selmon->lt[selmon->sellt]->arrange || sel->isfloating)
    return;

  /* Search for the first tiled window that is not sel, marking sel as
   * NULL if we pass it along the way */
  wl_list_for_each(c, &clients,
                   link) if (VISIBLEON(c, selmon) && !c->isfloating) {
    if (c != sel)
      break;
    sel = NULL;
  }

  /* Return if no other tiled window was found */
  if (&c->link == &clients)
    return;

  /* If we passed sel, move c to the front; otherwise, move sel to the
   * front */
  if (!sel)
    sel = c;
  wl_list_remove(&sel->link);
  wl_list_insert(&clients, &sel->link);

  focusclient(sel, 1);
  arrange(selmon);
}
