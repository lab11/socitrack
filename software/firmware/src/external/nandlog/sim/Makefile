# Host build of the nandlog core over the RAM-backed port. No target toolchain, no hardware.
#   make            run the tests
#   make VERBOSE=1  keep the library's diagnostics

CC       = cc
CFLAGS   = -std=c99 -Wall -Wextra -Wno-unused-parameter -O1 -g -fsanitize=address,undefined
INCLUDES = -I. -I.. -I../chips
SRC      = nandlog_port_sim.c nandlog_sim_test.c ../nandlog.c ../chips/nandlog_chip_AS5F18G04SND.c
OUT      = nandlog_sim_test

.PHONY: all run clean
all: run

$(OUT): $(SRC) ../nandlog.h ../nandlog_chip.h ../nandlog_conf.h nandlog_port_sim.h
	$(CC) $(CFLAGS) $(INCLUDES) $(SRC) -o $(OUT)

run: $(OUT)
	./$(OUT)

clean:
	rm -rf $(OUT) $(OUT).dSYM
