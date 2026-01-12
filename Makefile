
MODE ?= release

SOURCES = hyx.c common.c blob.c history.c term.c view.c input.c
HEADERS = *.h

ifeq ($(MODE), release)
CFLAGS ?= -Wall -Wextra \
          -O2 -DNDEBUG \
          -flto \
          -D_FORTIFY_SOURCE=2 -fstack-protector-all
else ifeq ($(MODE), debug)
CFLAGS ?= -Wall -Wextra -Werror \
          -O0 -g \
          -fsanitize=address -fsanitize=undefined \
          -fstack-protector-all
endif

CFLAGS += -std=c99 -pedantic

hyx: $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(SOURCES) -o $@

.PHONY: clean
clean:
	rm -f hyx

