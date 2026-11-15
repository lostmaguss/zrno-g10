CC ?= cc
PYTHON ?= python3
CFLAGS = -O2 -std=c99 -Wall -Wextra
all: build/colour.dylib build/grain.dylib build/bloom.dylib luts
build:
	mkdir -p build
build/colour.dylib: camera/film_colour.c camera/film_colour.h camera/film_retro.c camera/film_retro.h camera/film_toy.c camera/film_toy.h camera/film_print.c camera/film_print.h camera/film_font.h | build
	$(CC) $(CFLAGS) -shared -fPIC camera/film_colour.c camera/film_retro.c camera/film_toy.c camera/film_print.c -o $@
build/grain.dylib: camera/film_grain.c camera/film_grain.h | build
	$(CC) $(CFLAGS) -shared -fPIC camera/film_grain.c -o $@
build/bloom.dylib: camera/film_bloom.c camera/film_bloom.h | build
	$(CC) $(CFLAGS) -shared -fPIC camera/film_bloom.c -o $@
luts:
	$(PYTHON) tools/build_lut_pack.py
test:
	$(PYTHON) tools/test_native.py
.PHONY: all luts test
