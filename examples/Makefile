# Smart2Raw
# Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
# SPDX-License-Identifier: AGPL-3.0-or-later
# Build all examples. Usage: make  (needs gcc)
CC     ?= gcc
CFLAGS ?= -O2 -std=c11 -pedantic -Wall -Wextra -I ../include
SRCS    = $(wildcard *.c)
BINS    = $(SRCS:.c=)
all: $(BINS)
%: %.c ; $(CC) $(CFLAGS) -o $@ $<
clean: ; rm -f $(BINS)
.PHONY: all clean
