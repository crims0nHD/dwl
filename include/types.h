#pragma once

#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <xkbcommon/xkbcommon.h>

typedef struct {
  const char *id;
  const char *title;
  unsigned int tags;
  int isfloating;
  int monitor;
} Rule;

typedef union {
  int i;
  unsigned int ui;
  float f;
  const void *v;
} Arg;

typedef struct Layout Layout;
struct Monitor {
  struct wl_list link;
  struct wlr_output *wlr_output;
  struct wl_listener frame;
  struct wl_listener destroy;
  struct wlr_box m;         /* monitor area, layout-relative */
  struct wlr_box w;         /* window area, layout-relative */
  struct wl_list layers[4]; // LayerSurface::link
  const Layout *lt[2];
  unsigned int seltags;
  unsigned int sellt;
  unsigned int tagset[2];
  double mfact;
  int nmaster;
};
typedef struct Monitor Monitor;

struct Layout {
  const char *symbol;
  void (*arrange)(Monitor *);
};

typedef struct {
  const char *name;
  float mfact;
  int nmaster;
  float scale;
  const Layout *lt;
  enum wl_output_transform rr;
  int x;
  int y;
} MonitorRule;

typedef struct {
  uint32_t mod;
  xkb_keysym_t keysym;
  void (*func)(const Arg *);
  const Arg arg;
} Key;

typedef struct {
  struct wl_list link;
  struct wl_list flink;
  struct wl_list slink;
  union {
    struct wlr_xdg_surface *xdg;
    struct wlr_xwayland_surface *xwayland;
  } surface;
  struct wl_listener commit;
  struct wl_listener map;
  struct wl_listener unmap;
  struct wl_listener destroy;
  struct wl_listener set_title;
  struct wl_listener fullscreen;
  struct wlr_box geom; /* layout-relative, includes border */
  Monitor *mon;

#ifdef XWAYLAND
  unsigned int type;
  struct wl_listener activate;
  struct wl_listener configure;
#endif

  int bw;
  unsigned int tags;
  int isfloating, isurgent;
  uint32_t resize; /* configure serial of a pending resize */
  int prevx;
  int prevy;
  int prevwidth;
  int prevheight;
  int isfullscreen;
} Client;

typedef struct {
  struct wlr_layer_surface_v1 *layer_surface;
  struct wl_list link;

  struct wl_listener destroy;
  struct wl_listener map;
  struct wl_listener unmap;
  struct wl_listener surface_commit;

  struct wlr_box geo;
  enum zwlr_layer_shell_v1_layer layer;
} LayerSurface;

typedef struct {
  unsigned int mod;
  unsigned int button;
  void (*func)(const Arg *);
  const Arg arg;
} Button;

/* enums */
enum { CurNormal, CurMove, CurResize }; /* cursor */
#ifdef XWAYLAND
enum {
  NetWMWindowTypeDialog,
  NetWMWindowTypeSplash,
  NetWMWindowTypeToolbar,
  NetWMWindowTypeUtility,
  NetLast
};                                           /* EWMH atoms */
enum { XDGShell, X11Managed, X11Unmanaged }; /* client types */
#endif
