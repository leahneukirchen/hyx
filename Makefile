
all: CFLAGS ?= -O2 -Wl,-s \
               -Wl,-z,relro,-z,now -fpic -pie -D_FORTIFY_SOURCE=2 -fstack-protector-all
all: CFLAGS += -std=c99 -pedantic -Wall -Wextra -DNDEBUG
all: hyx

debug: CFLAGS ?= -O0 -g \
                 -fsanitize=undefined \
                 -Wl,-z,relro,-z,now -fpic -pie -fstack-protector-all
debug: CFLAGS += -std=c99 -pedantic -Wall -Wextra -Werror
debug: hyx

hyx: *.h *.c
	$(CC) \
		$(CFLAGS) \
		hyx.c common.c blob.c history.c view.c input.c \
		-o hyx

clean:
	rm -f hyx

