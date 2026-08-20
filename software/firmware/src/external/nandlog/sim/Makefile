# Host build of the nandlog core over the RAM-backed port. No target toolchain, no hardware.
#   make            run the tests
#   make VERBOSE=1  keep the library's diagnostics

CC       = cc
CFLAGS   = -std=c99 -Wall -Wextra -Wno-unused-parameter -O1 -g -fsanitize=address,undefined
INCLUDES = -I. -I.. -I../chips
SRC      = nandlog_port_sim.c nandlog_sim_test.c ../nandlog.c ../chips/nandlog_chip_AS5F18G04SND.c
OUT      = nandlog_sim_test
OUT_F    = nandlog_sim_test_framed
DEPS     = ../nandlog.h ../nandlog_chip.h ../nandlog_conf.h nandlog_port_sim.h

.PHONY: all run clean
# Both settings of NANDLOG_RECORD_FRAMING are built and run: it is an on-flash format switch, so the one that
# is off by default is exactly the one that would otherwise rot
all: run

$(OUT): $(SRC) $(DEPS)
	$(CC) $(CFLAGS) $(INCLUDES) $(SRC) -o $(OUT)

$(OUT_F): $(SRC) $(DEPS)
	$(CC) $(CFLAGS) -DNANDLOG_RECORD_FRAMING=1 $(INCLUDES) $(SRC) -o $(OUT_F)

run: $(OUT) $(OUT_F)
	./$(OUT)
	@echo
	./$(OUT_F)

clean:
	rm -rf $(OUT) $(OUT_F) $(OUT).dSYM $(OUT_F).dSYM nandlog_image.bin nandlog_image_framed.bin
