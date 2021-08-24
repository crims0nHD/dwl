#pragma once
#ifndef VARIABLES_H_
#define VARIABLES_H_

#include "functions.h"
#include "types.h"

/* variables */
extern const char broken[];
extern struct wl_display *dpy;
extern struct wlr_backend *backend;
extern struct wlr_renderer *drw;
extern struct wlr_compositor *compositor;

extern struct wlr_xdg_shell *xdg_shell;
extern struct wlr_xdg_activation_v1 *activation;
extern struct wl_list clients; /* tiling order */
extern struct wl_list fstack;  /* focus order */
extern struct wl_list stack;   /* stacking z-order */
extern struct wl_list independents;
extern struct wlr_idle *idle;
extern struct wlr_layer_shell_v1 *layer_shell;
extern struct wlr_output_manager_v1 *output_mgr;
extern struct wlr_virtual_keyboard_manager_v1 *virtual_keyboard_mgr;

extern struct wlr_cursor *cursor;
extern struct wlr_xcursor_manager *cursor_mgr;
#ifdef XWAYLAND
extern struct wlr_xcursor *xcursor;
extern struct wlr_xcursor_manager *xcursor_mgr;
#endif

extern struct wlr_seat *seat;
extern struct wl_list keyboards;
extern unsigned int cursor_mode;
extern Client *grabc;
extern int grabcx, grabcy; /* client-relative */

extern struct wlr_output_layout *output_layout;
extern struct wlr_box sgeom;
extern struct wl_list mons;
extern Monitor *selmon;

/* global event handlers */
extern struct wl_listener cursor_axis;
extern struct wl_listener cursor_button;
extern struct wl_listener cursor_frame;
extern struct wl_listener cursor_motion;
extern struct wl_listener cursor_motion_absolute;
extern struct wl_listener layout_change;
extern struct wl_listener new_input;
extern struct wl_listener new_virtual_keyboard;
extern struct wl_listener new_output;
extern struct wl_listener new_xdg_surface;
extern struct wl_listener new_layer_shell_surface;
extern struct wl_listener output_mgr_apply;
extern struct wl_listener output_mgr_test;
extern struct wl_listener request_activate;
extern struct wl_listener request_cursor;
extern struct wl_listener request_set_psel;
extern struct wl_listener request_set_sel;

#ifdef XWAYLAND
#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wlr/xwayland.h>
extern struct wl_listener new_xwayland_surface;
extern struct wl_listener xwayland_ready;
extern struct wlr_xwayland *xwayland;
extern Atom netatom[NetLast];
#endif

#endif // VARIABLES_H_
