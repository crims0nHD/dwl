#include "functions.h"
#include "macros.h"
#include <getopt.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
  char *startup_cmd = NULL;
  int c;

  while ((c = getopt(argc, argv, "s:h")) != -1) {
    if (c == 's')
      startup_cmd = optarg;
    else
      goto usage;
  }
  if (optind < argc)
    goto usage;

  // Wayland requires XDG_RUNTIME_DIR for creating its communications
  // socket
  if (!getenv("XDG_RUNTIME_DIR"))
    BARF("XDG_RUNTIME_DIR must be set");
  setup();
  run(startup_cmd);
  cleanup();
  return EXIT_SUCCESS;

usage:
  BARF("Usage: %s [-s startup command]", argv[0]);
}
