#pragma once

#include "types.h"

#include <stdint.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>

/* function declarations */
extern void applybounds(Client *c, struct wlr_box *bbox);
extern void applyexclusive(struct wlr_box *usable_area, uint32_t anchor,
                           int32_t exclusive, int32_t margin_top,
                           int32_t margin_right, int32_t margin_bottom,
                           int32_t margin_left);
extern void applyrules(Client *c);
extern void arrange(Monitor *m);
extern void arrangelayer(Monitor *m, struct wl_list *list,
                         struct wlr_box *usable_area, int exclusive);
extern void arrangelayers(Monitor *m);
extern void axisnotify(struct wl_listener *listener, void *data);
extern void buttonpress(struct wl_listener *listener, void *data);
extern void chvt(const Arg *arg);
extern void cleanup(void);
extern void cleanupkeyboard(struct wl_listener *listener, void *data);
extern void cleanupmon(struct wl_listener *listener, void *data);
extern void closemon(Monitor *m);
extern void commitlayersurfacenotify(struct wl_listener *listener, void *data);
extern void commitnotify(struct wl_listener *listener, void *data);
extern void createkeyboard(struct wlr_input_device *device);
extern void createmon(struct wl_listener *listener, void *data);
extern void createnotify(struct wl_listener *listener, void *data);
extern void createlayersurface(struct wl_listener *listener, void *data);
extern void createpointer(struct wlr_input_device *device);
extern void cursorframe(struct wl_listener *listener, void *data);
extern void destroylayersurfacenotify(struct wl_listener *listener, void *data);
extern void destroynotify(struct wl_listener *listener, void *data);
extern Monitor *dirtomon(enum wlr_direction dir);
extern void focusclient(Client *c, int lift);
extern void focusmon(const Arg *arg);
extern void focusstack(const Arg *arg);
extern void fullscreennotify(struct wl_listener *listener, void *data);
extern Client *focustop(Monitor *m);
extern void incnmaster(const Arg *arg);
extern void inputdevice(struct wl_listener *listener, void *data);
extern int keybinding(uint32_t mods, xkb_keysym_t sym);
extern void keypress(struct wl_listener *listener, void *data);
extern void keypressmod(struct wl_listener *listener, void *data);
extern void killclient(const Arg *arg);
extern void maplayersurfacenotify(struct wl_listener *listener, void *data);
extern void mapnotify(struct wl_listener *listener, void *data);
extern void monocle(Monitor *m);
extern void motionabsolute(struct wl_listener *listener, void *data);
extern void motionnotify(uint32_t time);
extern void motionrelative(struct wl_listener *listener, void *data);
extern void moveresize(const Arg *arg);
extern void outputmgrapply(struct wl_listener *listener, void *data);
extern void outputmgrapplyortest(struct wlr_output_configuration_v1 *config,
                                 int test);
extern void outputmgrtest(struct wl_listener *listener, void *data);
extern void pointerfocus(Client *c, struct wlr_surface *surface, double sx,
                         double sy, uint32_t time);
extern void printstatus(void);
extern void quit(const Arg *arg);
extern void quitsignal(int signo);
extern void render(struct wlr_surface *surface, int sx, int sy, void *data);
extern void renderclients(Monitor *m, struct timespec *now);
extern void renderlayer(struct wl_list *layer_surfaces, struct timespec *now);
extern void rendermon(struct wl_listener *listener, void *data);
extern void resize(Client *c, int x, int y, int w, int h, int interact);
extern void run(char *startup_cmd);
extern void scalebox(struct wlr_box *box, float scale);
extern Client *selclient(void);
extern void setcursor(struct wl_listener *listener, void *data);
extern void setpsel(struct wl_listener *listener, void *data);
extern void setsel(struct wl_listener *listener, void *data);
extern void setfloating(Client *c, int floating);
extern void setfullscreen(Client *c, int fullscreen);
extern void setlayout(const Arg *arg);
extern void setmfact(const Arg *arg);
extern void setmon(Client *c, Monitor *m, unsigned int newtags);
extern void setup(void);
extern void sigchld(int unused);
extern void spawn(const Arg *arg);
extern void tag(const Arg *arg);
extern void tagmon(const Arg *arg);
extern void tile(Monitor *m);
extern void togglefloating(const Arg *arg);
extern void togglefullscreen(const Arg *arg);
extern void toggletag(const Arg *arg);
extern void toggleview(const Arg *arg);
extern void unmaplayersurface(LayerSurface *layersurface);
extern void unmaplayersurfacenotify(struct wl_listener *listener, void *data);
extern void unmapnotify(struct wl_listener *listener, void *data);
extern void updatemons(struct wl_listener *listener, void *data);
extern void updatetitle(struct wl_listener *listener, void *data);
extern void urgent(struct wl_listener *listener, void *data);
extern void view(const Arg *arg);
extern void virtualkeyboard(struct wl_listener *listener, void *data);
extern Client *xytoclient(double x, double y);
extern struct wlr_surface *xytolayersurface(struct wl_list *layer_surfaces,
                                            double x, double y, double *sx,
                                            double *sy);
extern Monitor *xytomon(double x, double y);
extern void zoom(const Arg *arg);
