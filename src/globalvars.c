#include "functions.h"
#include "types.h"

/* variables */
const char broken[] = "broken";
struct wl_display *dpy;
struct wlr_backend *backend;
struct wlr_renderer *drw;
struct wlr_compositor *compositor;

struct wlr_xdg_shell *xdg_shell;
struct wlr_xdg_activation_v1 *activation;
struct wl_list clients; /* tiling order */
struct wl_list fstack;  /* focus order */
struct wl_list stack;   /* stacking z-order */
struct wl_list independents;
struct wlr_idle *idle;
struct wlr_layer_shell_v1 *layer_shell;
struct wlr_output_manager_v1 *output_mgr;
struct wlr_virtual_keyboard_manager_v1 *virtual_keyboard_mgr;

struct wlr_cursor *cursor;
struct wlr_xcursor_manager *cursor_mgr;
#ifdef XWAYLAND
struct wlr_xcursor *xcursor;
struct wlr_xcursor_manager *xcursor_mgr;
#endif

struct wlr_seat *seat;
struct wl_list keyboards;
unsigned int cursor_mode;
Client *grabc;
int grabcx, grabcy; /* client-relative */

struct wlr_output_layout *output_layout;
struct wlr_box sgeom;
struct wl_list mons;
Monitor *selmon;

/* global event handlers */
struct wl_listener cursor_axis = {.notify = axisnotify};
struct wl_listener cursor_button = {.notify = buttonpress};
struct wl_listener cursor_frame = {.notify = cursorframe};
struct wl_listener cursor_motion = {.notify = motionrelative};
struct wl_listener cursor_motion_absolute = {.notify = motionabsolute};
struct wl_listener layout_change = {.notify = updatemons};
struct wl_listener new_input = {.notify = inputdevice};
struct wl_listener new_virtual_keyboard = {.notify = virtualkeyboard};
struct wl_listener new_output = {.notify = createmon};
struct wl_listener new_xdg_surface = {.notify = createnotify};
struct wl_listener new_layer_shell_surface = {.notify = createlayersurface};
struct wl_listener output_mgr_apply = {.notify = outputmgrapply};
struct wl_listener output_mgr_test = {.notify = outputmgrtest};
struct wl_listener request_activate = {.notify = urgent};
struct wl_listener request_cursor = {.notify = setcursor};
struct wl_listener request_set_psel = {.notify = setpsel};
struct wl_listener request_set_sel = {.notify = setsel};

#ifdef XWAYLAND
#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wlr/xwayland.h>
struct wl_listener new_xwayland_surface = {.notify = createnotifyx11};
struct wl_listener xwayland_ready = {.notify = xwaylandready};
struct wlr_xwayland *xwayland;
Atom netatom[NetLast];
#endif
