#pragma once

/* macros */
#define BARF(fmt, ...)                                                         \
  do {                                                                         \
    fprintf(stderr, fmt "\n", ##__VA_ARGS__);                                  \
    exit(EXIT_FAILURE);                                                        \
  } while (0)
#define EBARF(fmt, ...) BARF(fmt ": %s", ##__VA_ARGS__, strerror(errno))
#define MAX(A, B) ((A) > (B) ? (A) : (B))
#define MIN(A, B) ((A) < (B) ? (A) : (B))
#define CLEANMASK(mask) (mask & ~WLR_MODIFIER_CAPS)
#define VISIBLEON(C, M)                                                        \
  ((C)->mon == (M) && ((C)->tags & (M)->tagset[(M)->seltags]))
#define LENGTH(X) (sizeof X / sizeof X[0])
#define END(A) ((A) + LENGTH(A))
#define TAGMASK ((1 << LENGTH(tags)) - 1)
#define ROUND(X) ((int)((X) + 0.5))
#define LISTEN(E, L, H) wl_signal_add((E), ((L)->notify = (H), (L)))
