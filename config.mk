# paths
PREFIX = /usr/local

# c compiler
CC = clang

# Default compile flags (overridable by environment)
CFLAGS ?= -g -Wall -Wextra -Werror -Wno-unused-parameter -Wno-sign-compare -Wno-unused-function -Wno-unused-variable -Wdeclaration-after-statement

# Uncomment to build XWayland support
CFLAGS += -DXWAYLAND
