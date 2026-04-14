CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wpedantic -Wextra -O2
SRCS     = src/main.c src/bencode.c src/session.c src/ops.c mino/mino.c
TARGET   = mino-nrepl

$(TARGET): $(SRCS) src/bencode.h src/session.h src/ops.h mino/mino.h
	$(CC) $(CFLAGS) -Imino -Isrc -o $@ $(SRCS)

test: $(TARGET)
	tests/test_nrepl.sh

clean:
	rm -f $(TARGET)

.PHONY: test clean
