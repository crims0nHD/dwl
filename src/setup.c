#include "functions.h"
#include "macros.h"
#include "variables.h"
#include <stdlib.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_idle.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>

void setup(void) {
  /* The Wayland display is managed by libwayland. It handles accepting
   * clients from the Unix socket, manging Wayland globals, and so on. */
  dpy = wl_display_create();

  /* Set up signal handlers */
  sigchld(0);
  signal(SIGINT, quitsignal);
  signal(SIGTERM, quitsignal);

  /* The backend is a wlroots feature which abstracts the underlying input and
   * output hardware. The autocreate option will choose the most suitable
   * backend based on the current environment, such as opening an X11 window
   * if an X11 server is running. The NULL argument here optionally allows you
   * to pass in a custom renderer if wlr_renderer doesn't meet your needs. The
   * backend uses the renderer, for example, to fall back to software cursors
   * if the backend does not support hardware cursors (some older GPUs
   * don't). */
  if (!(backend = wlr_backend_autocreate(dpy)))
    BARF("couldn't create backend");

  /* If we don't provide a renderer, autocreate makes a GLES2 renderer for us.
   * The renderer is responsible for defining the various pixel formats it
   * supports for shared memory, this configures that for clients. */
  drw = wlr_backend_get_renderer(backend);
  wlr_renderer_init_wl_display(drw, dpy);

  /* This creates some hands-off wlroots interfaces. The compositor is
   * necessary for clients to allocate surfaces and the data device manager
   * handles the clipboard. Each of these wlroots interfaces has room for you
   * to dig your fingers in and play with their behavior if you want. Note that
   * the clients cannot set the selection directly without compositor approval,
   * see the setsel() function. */
  compositor = wlr_compositor_create(dpy, drw);
  wlr_export_dmabuf_manager_v1_create(dpy);
  wlr_screencopy_manager_v1_create(dpy);
  wlr_data_control_manager_v1_create(dpy);
  wlr_data_device_manager_create(dpy);
  wlr_gamma_control_manager_v1_create(dpy);
  wlr_primary_selection_v1_device_manager_create(dpy);
  wlr_viewporter_create(dpy);

  /* Initializes the interface used to implement urgency hints */
  activation = wlr_xdg_activation_v1_create(dpy);
  wl_signal_add(&activation->events.request_activate, &request_activate);

  /* Creates an output layout, which a wlroots utility for working with an
   * arrangement of screens in a physical layout. */
  output_layout = wlr_output_layout_create();
  wl_signal_add(&output_layout->events.change, &layout_change);
  wlr_xdg_output_manager_v1_create(dpy, output_layout);

  /* Configure a listener to be notified when new outputs are available on the
   * backend. */
  wl_list_init(&mons);
  wl_signal_add(&backend->events.new_output, &new_output);

  /* Set up our client lists and the xdg-shell. The xdg-shell is a
   * Wayland protocol which is used for application windows. For more
   * detail on shells, refer to the article:
   *
   * https://drewdevault.com/2018/07/29/Wayland-shells.html
   */
  wl_list_init(&clients);
  wl_list_init(&fstack);
  wl_list_init(&stack);
  wl_list_init(&independents);

  idle = wlr_idle_create(dpy);

  layer_shell = wlr_layer_shell_v1_create(dpy);
  wl_signal_add(&layer_shell->events.new_surface, &new_layer_shell_surface);

  xdg_shell = wlr_xdg_shell_create(dpy);
  wl_signal_add(&xdg_shell->events.new_surface, &new_xdg_surface);

  /* Use decoration protocols to negotiate server-side decorations */
  wlr_server_decoration_manager_set_default_mode(
      wlr_server_decoration_manager_create(dpy),
      WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);
  wlr_xdg_decoration_manager_v1_create(dpy);

  /*
   * Creates a cursor, which is a wlroots utility for tracking the cursor
   * image shown on screen.
   */
  cursor = wlr_cursor_create();
  wlr_cursor_attach_output_layout(cursor, output_layout);

  /* Creates an xcursor manager, another wlroots utility which loads up
   * Xcursor themes to source cursor images from and makes sure that cursor
   * images are available at all scale factors on the screen (necessary for
   * HiDPI support). Scaled cursors will be loaded with each output. */
  cursor_mgr = wlr_xcursor_manager_create(NULL, 24);

  /*
   * wlr_cursor *only* displays an image on screen. It does not move around
   * when the pointer moves. However, we can attach input devices to it, and
   * it will generate aggregate events for all of them. In these events, we
   * can choose how we want to process them, forwarding them to clients and
   * moving the cursor around. More detail on this process is described in my
   * input handling blog post:
   *
   * https://drewdevault.com/2018/07/17/Input-handling-in-wlroots.html
   *
   * And more comments are sprinkled throughout the notify functions above.
   */
  wl_signal_add(&cursor->events.motion, &cursor_motion);
  wl_signal_add(&cursor->events.motion_absolute, &cursor_motion_absolute);
  wl_signal_add(&cursor->events.button, &cursor_button);
  wl_signal_add(&cursor->events.axis, &cursor_axis);
  wl_signal_add(&cursor->events.frame, &cursor_frame);

  /*
   * Configures a seat, which is a single "seat" at which a user sits and
   * operates the computer. This conceptually includes up to one keyboard,
   * pointer, touch, and drawing tablet device. We also rig up a listener to
   * let us know when new input devices are available on the backend.
   */
  wl_list_init(&keyboards);
  wl_signal_add(&backend->events.new_input, &new_input);
  virtual_keyboard_mgr = wlr_virtual_keyboard_manager_v1_create(dpy);
  wl_signal_add(&virtual_keyboard_mgr->events.new_virtual_keyboard,
                &new_virtual_keyboard);
  seat = wlr_seat_create(dpy, "seat0");
  wl_signal_add(&seat->events.request_set_cursor, &request_cursor);
  wl_signal_add(&seat->events.request_set_selection, &request_set_sel);
  wl_signal_add(&seat->events.request_set_primary_selection, &request_set_psel);

  output_mgr = wlr_output_manager_v1_create(dpy);
  wl_signal_add(&output_mgr->events.apply, &output_mgr_apply);
  wl_signal_add(&output_mgr->events.test, &output_mgr_test);

#ifdef XWAYLAND
  /*
   * Initialise the XWayland X server.
   * It will be started when the first X client is started.
   */
  xwayland = wlr_xwayland_create(dpy, compositor, 1);
  if (xwayland) {
    wl_signal_add(&xwayland->events.ready, &xwayland_ready);
    wl_signal_add(&xwayland->events.new_surface, &new_xwayland_surface);

    /*
     * Create the XWayland cursor manager at scale 1, setting its default
     * pointer to match the rest of dwl.
     */
    xcursor_mgr = wlr_xcursor_manager_create(NULL, 24);
    wlr_xcursor_manager_load(xcursor_mgr, 1);
    if ((xcursor = wlr_xcursor_manager_get_xcursor(xcursor_mgr, "left_ptr", 1)))
      wlr_xwayland_set_cursor(
          xwayland, xcursor->images[0]->buffer, xcursor->images[0]->width * 4,
          xcursor->images[0]->width, xcursor->images[0]->height,
          xcursor->images[0]->hotspot_x, xcursor->images[0]->hotspot_y);

    setenv("DISPLAY", xwayland->display_name, 1);
  } else {
    fprintf(stderr,
            "failed to setup XWayland X server, continuing without it\n");
  }
#endif
}
