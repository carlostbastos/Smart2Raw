# Build das ferramentas. Uso: make  (precisa de gcc)
CC      ?= gcc
CFLAGS  ?= -O2 -std=c11 -pedantic -Wall -Wextra -I ../include
BINS     = s2r s2r_verify s2r_convert
all: $(BINS)
s2r: s2r.c ;          $(CC) $(CFLAGS) -o $@ $<
s2r_verify: s2r_verify.c ; $(CC) $(CFLAGS) -o $@ $<
s2r_convert: s2r_convert.c ; $(CC) $(CFLAGS) -o $@ $<
clean: ; rm -f $(BINS)
.PHONY: all clean
