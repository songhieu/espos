# Host build of the wake front end + runtime (the same C files as the firmware).
CC ?= cc
WFLAGS := -O2 -std=c11 -Wall -Wextra -ffp-contract=off -I../../src/wake
SRC = ../../src/wake/wake_fe.c ../../src/wake/wake_rt.c
all: libwake.dylib wakecli
libwake.dylib: $(SRC) ../../src/wake/wake_fe.h ../../src/wake/wake_rt.h
	$(CC) $(WFLAGS) -shared -fPIC -o $@ $(SRC) -lm
wakecli: wakecli.c $(SRC)
	$(CC) $(WFLAGS) -o $@ wakecli.c $(SRC) -lm
clean:
	rm -f libwake.dylib wakecli
.PHONY: all clean
