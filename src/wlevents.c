#include "config.h"
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_idle.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_shell.h>

/* cursor_axis */
void axisnotify(struct wl_listener *listener, void *data) {
  /* This event is forwarded by the cursor when a pointer emits an axis event,
   * for example when you move the scroll wheel. */
  struct wlr_event_pointer_axis *event = data;
  wlr_idle_notify_activity(idle, seat);
  /* Notify the client with pointer focus of the axis event. */
  wlr_seat_pointer_notify_axis(seat, event->time_msec, event->orientation,
                               event->delta, event->delta_discrete,
                               event->source);
}

/* cursor_button */
void buttonpress(struct wl_listener *listener, void *data) {
  struct wlr_event_pointer_button *event = data;
  struct wlr_keyboard *keyboard;
  uint32_t mods;
  Client *c;
  const Button *b;

  wlr_idle_notify_activity(idle, seat);

  switch (event->state) {
  case WLR_BUTTON_PRESSED:;
    /* Change focus if the button was _pressed_ over a client */
    if ((c = xytoclient(cursor->x, cursor->y)))
      focusclient(c, 1);

    keyboard = wlr_seat_get_keyboard(seat);
    mods = wlr_keyboard_get_modifiers(keyboard);
    for (b = buttons; b < END(buttons); b++) {
      if (CLEANMASK(mods) == CLEANMASK(b->mod) && event->button == b->button &&
          b->func) {
        b->func(&b->arg);
        return;
      }
    }
    break;
  case WLR_BUTTON_RELEASED:
    /* If you released any buttons, we exit interactive move/resize mode. */
    /* TODO should reset to the pointer focus's current setcursor */
    if (cursor_mode != CurNormal) {
      wlr_xcursor_manager_set_cursor_image(cursor_mgr, "left_ptr", cursor);
      cursor_mode = CurNormal;
      /* Drop the window off on its new monitor */
      selmon = xytomon(cursor->x, cursor->y);
      setmon(grabc, selmon, 0);
      return;
    }
    break;
  }
  /* If the event wasn't handled by the compositor, notify the client with
   * pointer focus that a button press has occurred */
  wlr_seat_pointer_notify_button(seat, event->time_msec, event->button,
                                 event->state);
}

/* cursor_frame */
void cursorframe(struct wl_listener *listener, void *data) {
  /* This event is forwarded by the cursor when a pointer emits an frame
   * event. Frame events are sent after regular pointer events to group
   * multiple events together. For instance, two axis events may happen at the
   * same time, in which case a frame event won't be sent in between. */
  /* Notify the client with pointer focus of the frame event. */
  wlr_seat_pointer_notify_frame(seat);
}

/* cursor_motion */
void motionrelative(struct wl_listener *listener, void *data) {
  /* This event is forwarded by the cursor when a pointer emits a _relative_
   * pointer motion event (i.e. a delta) */
  struct wlr_event_pointer_motion *event = data;
  /* The cursor doesn't move unless we tell it to. The cursor automatically
   * handles constraining the motion to the output layout, as well as any
   * special configuration applied for the specific input device which
   * generated the event. You can pass NULL for the device if you want to move
   * the cursor around without any input. */
  wlr_cursor_move(cursor, event->device, event->delta_x, event->delta_y);
  motionnotify(event->time_msec);
}

/* cursor_motion_absolute */
void motionabsolute(struct wl_listener *listener, void *data) {
  /* This event is forwarded by the cursor when a pointer emits an _absolute_
   * motion event, from 0..1 on each axis. This happens, for example, when
   * wlroots is running under a Wayland window rather than KMS+DRM, and you
   * move the mouse over the window. You could enter the window from any edge,
   * so we have to warp the mouse there. There is also some hardware which
   * emits these events. */
  struct wlr_event_pointer_motion_absolute *event = data;
  wlr_cursor_warp_absolute(cursor, event->device, event->x, event->y);
  motionnotify(event->time_msec);
}

/* layout_change */
void updatemons(struct wl_listener *listener, void *data) {
  /*
   * Called whenever the output layout changes: adding or removing a
   * monitor, changing an output's mode or position, etc.  This is where
   * the change officially happens and we update geometry, window
   * positions, focus, and the stored configuration in wlroots'
   * output-manager implementation.
   */
  struct wlr_output_configuration_v1 *config =
      wlr_output_configuration_v1_create();
  Monitor *m;
  sgeom = *wlr_output_layout_get_box(output_layout, NULL);
  wl_list_for_each(m, &mons, link) {
    struct wlr_output_configuration_head_v1 *config_head =
        wlr_output_configuration_head_v1_create(config, m->wlr_output);

    /* TODO: move clients off disabled monitors */
    /* TODO: move focus if selmon is disabled */

    /* Get the effective monitor geometry to use for surfaces */
    m->m = m->w = *wlr_output_layout_get_box(output_layout, m->wlr_output);
    /* Calculate the effective monitor geometry to use for clients */
    arrangelayers(m);
    /* Don't move clients to the left output when plugging monitors */
    arrange(m);

    config_head->state.enabled = m->wlr_output->enabled;
    config_head->state.mode = m->wlr_output->current_mode;
    config_head->state.x = m->m.x;
    config_head->state.y = m->m.y;
  }

  wlr_output_manager_v1_set_configuration(output_mgr, config);
}

/* new_input */
void inputdevice(struct wl_listener *listener, void *data) {
  /* This event is raised by the backend when a new input device becomes
   * available. */
  struct wlr_input_device *device = data;
  uint32_t caps;

  switch (device->type) {
  case WLR_INPUT_DEVICE_KEYBOARD:
    createkeyboard(device);
    break;
  case WLR_INPUT_DEVICE_POINTER:
    createpointer(device);
    break;
  default:
    /* TODO handle other input device types */
    break;
  }

  /* We need to let the wlr_seat know what our capabilities are, which is
   * communiciated to the client. In dwl we always have a cursor, even if
   * there are no pointer devices, so we always include that capability. */
  /* TODO do we actually require a cursor? */
  caps = WL_SEAT_CAPABILITY_POINTER;
  if (!wl_list_empty(&keyboards))
    caps |= WL_SEAT_CAPABILITY_KEYBOARD;
  wlr_seat_set_capabilities(seat, caps);
}

/* new_virtual_keyboard */
void virtualkeyboard(struct wl_listener *listener, void *data) {
  struct wlr_virtual_keyboard_v1 *keyboard = data;
  struct wlr_input_device *device = &keyboard->input_device;
  createkeyboard(device);
}

/* new_output */
void createmon(struct wl_listener *listener, void *data) {
  /* This event is raised by the backend when a new output (aka a display or
   * monitor) becomes available. */
  struct wlr_output *wlr_output = data;
  const MonitorRule *r;
  Monitor *m = wlr_output->data = calloc(1, sizeof(*m));
  m->wlr_output = wlr_output;

  /* Initialize monitor state using configured rules */
  for (size_t i = 0; i < LENGTH(m->layers); i++)
    wl_list_init(&m->layers[i]);
  m->tagset[0] = m->tagset[1] = 1;
  for (r = monrules; r < END(monrules); r++) {
    if (!r->name || strstr(wlr_output->name, r->name)) {
      m->mfact = r->mfact;
      m->nmaster = r->nmaster;
      wlr_output_set_scale(wlr_output, r->scale);
      wlr_xcursor_manager_load(cursor_mgr, r->scale);
      m->lt[0] = m->lt[1] = r->lt;
      wlr_output_set_transform(wlr_output, r->rr);
      break;
    }
  }

  /* The mode is a tuple of (width, height, refresh rate), and each
   * monitor supports only a specific set of modes. We just pick the
   * monitor's preferred mode; a more sophisticated compositor would let
   * the user configure it. */
  wlr_output_set_mode(wlr_output, wlr_output_preferred_mode(wlr_output));
  wlr_output_enable_adaptive_sync(wlr_output, 1);

  /* Set up event listeners */
  LISTEN(&wlr_output->events.frame, &m->frame, rendermon);
  LISTEN(&wlr_output->events.destroy, &m->destroy, cleanupmon);

  wlr_output_enable(wlr_output, 1);
  if (!wlr_output_commit(wlr_output))
    return;

  wl_list_insert(&mons, &m->link);
  printstatus();

  /* Adds this to the output layout in the order it was configured in.
   *
   * The output layout utility automatically adds a wl_output global to the
   * display, which Wayland clients can see to find out information about the
   * output (such as DPI, scale factor, manufacturer, etc).
   */
  wlr_output_layout_add(output_layout, wlr_output, r->x, r->y);
  sgeom = *wlr_output_layout_get_box(output_layout, NULL);

  /* When adding monitors, the geometries of all monitors must be updated */
  wl_list_for_each(m, &mons, link) {
    /* The first monitor in the list is the most recently added */
    Client *c;
    wl_list_for_each(c, &clients, link) {
      if (c->isfloating)
        resize(c, c->geom.x + m->w.width, c->geom.y, c->geom.width,
               c->geom.height, 0);
    }
    return;
  }
}

/* new_xdg_surface */
void createnotify(struct wl_listener *listener, void *data) {
  /* This event is raised when wlr_xdg_shell receives a new xdg surface from a
   * client, either a toplevel (application window) or popup. */
  struct wlr_xdg_surface *xdg_surface = data;
  Client *c;

  if (xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL)
    return;

  /* Allocate a Client for this surface */
  c = xdg_surface->data = calloc(1, sizeof(*c));
  c->surface.xdg = xdg_surface;
  c->bw = borderpx;

  LISTEN(&xdg_surface->surface->events.commit, &c->commit, commitnotify);
  LISTEN(&xdg_surface->events.map, &c->map, mapnotify);
  LISTEN(&xdg_surface->events.unmap, &c->unmap, unmapnotify);
  LISTEN(&xdg_surface->events.destroy, &c->destroy, destroynotify);
  LISTEN(&xdg_surface->toplevel->events.set_title, &c->set_title, updatetitle);
  LISTEN(&xdg_surface->toplevel->events.request_fullscreen, &c->fullscreen,
         fullscreennotify);
  c->isfullscreen = 0;
}

/* new_layer_shell_surface */
void createlayersurface(struct wl_listener *listener, void *data) {
  struct wlr_layer_surface_v1 *wlr_layer_surface = data;
  LayerSurface *layersurface;
  Monitor *m;
  struct wlr_layer_surface_v1_state old_state;

  if (!wlr_layer_surface->output) {
    wlr_layer_surface->output = selmon->wlr_output;
  }

  layersurface = calloc(1, sizeof(LayerSurface));
  LISTEN(&wlr_layer_surface->surface->events.commit,
         &layersurface->surface_commit, commitlayersurfacenotify);
  LISTEN(&wlr_layer_surface->events.destroy, &layersurface->destroy,
         destroylayersurfacenotify);
  LISTEN(&wlr_layer_surface->events.map, &layersurface->map,
         maplayersurfacenotify);
  LISTEN(&wlr_layer_surface->events.unmap, &layersurface->unmap,
         unmaplayersurfacenotify);

  layersurface->layer_surface = wlr_layer_surface;
  wlr_layer_surface->data = layersurface;

  m = wlr_layer_surface->output->data;
  wl_list_insert(&m->layers[wlr_layer_surface->client_pending.layer],
                 &layersurface->link);

  // Temporarily set the layer's current state to client_pending
  // so that we can easily arrange it
  old_state = wlr_layer_surface->current;
  wlr_layer_surface->current = wlr_layer_surface->client_pending;
  arrangelayers(m);
  wlr_layer_surface->current = old_state;
}

/* output_mgr_apply */
void outputmgrapply(struct wl_listener *listener, void *data) {
  struct wlr_output_configuration_v1 *config = data;
  outputmgrapplyortest(config, 0);
}

/* output_mgr_test */
void outputmgrtest(struct wl_listener *listener, void *data) {
  struct wlr_output_configuration_v1 *config = data;
  outputmgrapplyortest(config, 1);
}

/* request_activate */
void urgent(struct wl_listener *listener, void *data) {
  struct wlr_xdg_activation_v1_request_activate_event *event = data;
  Client *c;

  if (!wlr_surface_is_xdg_surface(event->surface))
    return;
  c = wlr_xdg_surface_from_wlr_surface(event->surface)->data;
  if (c != selclient()) {
    c->isurgent = 1;
    printstatus();
  }
}

/* request_cursor */
void setcursor(struct wl_listener *listener, void *data) {
  /* This event is raised by the seat when a client provides a cursor image */
  struct wlr_seat_pointer_request_set_cursor_event *event = data;
  /* If we're "grabbing" the cursor, don't use the client's image */
  /* TODO still need to save the provided surface to restore later */
  if (cursor_mode != CurNormal)
    return;
  /* This can be sent by any client, so we check to make sure this one is
   * actually has pointer focus first. If so, we can tell the cursor to
   * use the provided surface as the cursor image. It will set the
   * hardware cursor on the output that it's currently on and continue to
   * do so as the cursor moves between outputs. */
  if (event->seat_client == seat->pointer_state.focused_client)
    wlr_cursor_set_surface(cursor, event->surface, event->hotspot_x,
                           event->hotspot_y);
}

/* request_set_psel */
void setpsel(struct wl_listener *listener, void *data) {
  /* This event is raised by the seat when a client wants to set the selection,
   * usually when the user copies something. wlroots allows compositors to
   * ignore such requests if they so choose, but in dwl we always honor
   */
  struct wlr_seat_request_set_primary_selection_event *event = data;
  wlr_seat_set_primary_selection(seat, event->source, event->serial);
}

/* request_set_sel */
void setsel(struct wl_listener *listener, void *data) {
  /* This event is raised by the seat when a client wants to set the selection,
   * usually when the user copies something. wlroots allows compositors to
   * ignore such requests if they so choose, but in dwl we always honor
   */
  struct wlr_seat_request_set_selection_event *event = data;
  wlr_seat_set_selection(seat, event->source, event->serial);
}

#ifdef XWAYLAND

/* new_xwayland_surface */
void createnotifyx11(struct wl_listener *listener, void *data) {
  struct wlr_xwayland_surface *xwayland_surface = data;
  Client *c;
  wl_list_for_each(c, &clients,
                   link) if (c->isfullscreen && VISIBLEON(c, c->mon))
      setfullscreen(c, 0);

  /* Allocate a Client for this surface */
  c = xwayland_surface->data = calloc(1, sizeof(*c));
  c->surface.xwayland = xwayland_surface;
  c->type = xwayland_surface->override_redirect ? X11Unmanaged : X11Managed;
  c->bw = borderpx;
  c->isfullscreen = 0;

  /* Listen to the various events it can emit */
  LISTEN(&xwayland_surface->events.map, &c->map, mapnotify);
  LISTEN(&xwayland_surface->events.unmap, &c->unmap, unmapnotify);
  LISTEN(&xwayland_surface->events.request_activate, &c->activate, activatex11);
  LISTEN(&xwayland_surface->events.request_configure, &c->configure,
         configurex11);
  LISTEN(&xwayland_surface->events.set_title, &c->set_title, updatetitle);
  LISTEN(&xwayland_surface->events.destroy, &c->destroy, destroynotify);
  LISTEN(&xwayland_surface->events.request_fullscreen, &c->fullscreen,
         fullscreennotify);
}

/* xwayland_ready */
void xwaylandready(struct wl_listener *listener, void *data) {
  struct wlr_xcursor *xcursor;
  xcb_connection_t *xc = xcb_connect(xwayland->display_name, NULL);
  int err = xcb_connection_has_error(xc);
  if (err) {
    fprintf(stderr,
            "xcb_connect to X server failed with code %d\n. Continuing with "
            "degraded functionality.\n",
            err);
    return;
  }

  /* Collect atoms we are interested in.  If getatom returns 0, we will
   * not detect that window type. */
  netatom[NetWMWindowTypeDialog] = getatom(xc, "_NET_WM_WINDOW_TYPE_DIALOG");
  netatom[NetWMWindowTypeSplash] = getatom(xc, "_NET_WM_WINDOW_TYPE_SPLASH");
  netatom[NetWMWindowTypeToolbar] = getatom(xc, "_NET_WM_WINDOW_TYPE_TOOLBAR");
  netatom[NetWMWindowTypeUtility] = getatom(xc, "_NET_WM_WINDOW_TYPE_UTILITY");

  /* assign the one and only seat */
  wlr_xwayland_set_seat(xwayland, seat);

  /* Set the default XWayland cursor to match the rest of dwl. */
  if ((xcursor = wlr_xcursor_manager_get_xcursor(cursor_mgr, "left_ptr", 1)))
    wlr_xwayland_set_cursor(
        xwayland, xcursor->images[0]->buffer, xcursor->images[0]->width * 4,
        xcursor->images[0]->width, xcursor->images[0]->height,
        xcursor->images[0]->hotspot_x, xcursor->images[0]->hotspot_y);

  xcb_disconnect(xc);
}
#endif
