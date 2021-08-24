#include "tiling.h"
#include "functions.h"
#include "macros.h"
#include "variables.h"

/* Toggle the floating state on a Client */
void togglefloating(const Arg *arg) {
  Client *sel = selclient(); // Get the selected client
  if (!sel)
    return;
  /* return if fullscreen or none selected */
  setfloating(sel, !sel->isfloating /* || sel->isfixed */);
}

void tile(Monitor *m) {
  unsigned int i, n = 0, h, mw, my, ty;
  Client *c;

  wl_list_for_each(c, &clients, link) if (VISIBLEON(c, m) && !c->isfloating)
      n++;
  if (n == 0)
    return;

  if (n > m->nmaster)
    mw = m->nmaster ? m->w.width * m->mfact : 0;
  else
    mw = m->w.width;
  i = my = ty = 0;
  wl_list_for_each(c, &clients, link) {
    if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
      continue;
    if (i < m->nmaster) {
      h = (m->w.height - my) / (MIN(n, m->nmaster) - i);
      resize(c, m->w.x, m->w.y + my, mw, h, 0);
      my += c->geom.height;
    } else {
      h = (m->w.height - ty) / (n - i);
      resize(c, m->w.x + mw, m->w.y + ty, m->w.width - mw, h, 0);
      ty += c->geom.height;
    }
    i++;
  }
}
