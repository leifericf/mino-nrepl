CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wpedantic -Wextra -O2
MINO_INCS = -Imino/src -Imino/src/generated -Imino/src/public \
            -Imino/src/runtime -Imino/src/gc -Imino/src/eval \
            -Imino/src/read -Imino/src/print -Imino/src/names \
            -Imino/src/state -Imino/src/values -Imino/src/collections \
            -Imino/src/prim -Imino/src/async -Imino/src/interop \
            -Imino/src/diag -Imino/src/vendor/imath \
            -Imino/src/vendor/bearssl -Imino/src/vendor/bearssl/inc \
            -Imino/src/vendor/miniz -Imino/src/vendor/miniz/upstream
MINO_SRCS = $(wildcard mino/src/eval/*.c) \
            $(wildcard mino/src/eval/bc/*.c) \
            $(wildcard mino/src/eval/bc/jit/*.c) \
            $(wildcard mino/src/read/*.c) \
            $(wildcard mino/src/print/*.c) \
            $(wildcard mino/src/diag/*.c) \
            $(wildcard mino/src/names/*.c) \
            $(wildcard mino/src/state/*.c) \
            $(wildcard mino/src/gc/*.c) \
            $(wildcard mino/src/public/*.c) \
            $(wildcard mino/src/values/*.c) \
            $(wildcard mino/src/collections/*.c) \
            $(wildcard mino/src/prim/*/*.c) \
            $(wildcard mino/src/interop/*.c) \
            $(wildcard mino/src/regex/*.c) \
            $(wildcard mino/src/async/*.c) \
            $(wildcard mino/src/vendor/imath/*.c) \
            $(wildcard mino/src/vendor/bearssl/*.c) \
            $(wildcard mino/src/vendor/miniz/*.c)
SRCS     = src/main.c src/bencode.c src/session.c src/ops.c $(MINO_SRCS)
TARGET   = mino-nrepl

.DEFAULT_GOAL := $(TARGET)

# --- Bundled-stdlib generated headers ---
# install_stdlib.c #includes one C string-literal header per bundled
# namespace, emitted to mino/src/generated/<sym>.h and driven by
# mino/src/bundled.list. These are gitignored generated artifacts. Rather
# than duplicate the (evolving) bundle list here, delegate to mino's own
# bootstrap Makefile, which regenerates the whole set into
# mino/src/generated/ from the single source of truth.
MINO_GEN_SENTINEL = mino/src/generated/core_mino.h

$(MINO_GEN_SENTINEL): mino/src/bundled.list mino/src/bundle.awk
	$(MAKE) -C mino $(MINO_GEN_SENTINEL:mino/%=%)

$(TARGET): $(SRCS) src/bencode.h src/session.h src/ops.h \
           mino/src/mino.h mino/src/diag/diag.h $(MINO_GEN_SENTINEL)
	$(CC) $(CFLAGS) $(MINO_INCS) -Isrc -o $@ $(SRCS) -lm -lpthread

test: $(TARGET)
	tests/test_nrepl.sh

clean:
	rm -f $(TARGET)
	$(MAKE) -C mino clean

.PHONY: test clean
