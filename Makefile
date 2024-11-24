
all: CFLAGS ?= -O2 \
               -pedantic -Wall -Wextra -DNDEBUG \
               -D_FORTIFY_SOURCE=2 -fstack-protector-all
all: CFLAGS += -std=c99
all: hyx

debug: CFLAGS ?= -O0 -g \
                 -fsanitize=undefined \
                 -std=c99 -pedantic -Wall -Wextra -Werror \
                 -fstack-protector-all
debug: CFLAGS += -std=c99
debug: hyx

hyx: *.h *.c
	$(CC) \
		$(CFLAGS) \
		$(LDFLAGS) \
		hyx.c common.c blob.c history.c view.c input.c \
		-o hyx

clean:
	rm -f hyx

