# Host build of the nandlog core over the RAM-backed port. No target toolchain, no hardware.
#   make            run the tests
#   make VERBOSE=1  keep the library's diagnostics

CC       = cc
CFLAGS   = -std=c99 -Wall -Wextra -Wno-unused-parameter -O1 -g -fsanitize=address,undefined
INCLUDES = -I. -I.. -I../chips
SRC      = nandlog_port_sim.c nandlog_sim_test.c ../nandlog.c ../chips/nandlog_chip_AS5F18G04SND.c
OUT      = nandlog_sim_test
OUT_F    = nandlog_sim_test_framed
OUT_C    = nandlog_sim_test_no_chip_copy
DEPS     = ../nandlog.h ../nandlog_chip.h ../nandlog_conf.h nandlog_port_sim.h

.PHONY: all run clean
# Both settings of NANDLOG_RECORD_FRAMING are built and run, and a third build relocates pages by reading
# and writing them, so the fallback for a part with no internal data move is exercised too
all: run

$(OUT): $(SRC) $(DEPS)
	$(CC) $(CFLAGS) -DNANDLOG_RECORD_FRAMING=0 $(INCLUDES) $(SRC) -o $(OUT)

$(OUT_F): $(SRC) $(DEPS)
	$(CC) $(CFLAGS) -DNANDLOG_RECORD_FRAMING=1 $(INCLUDES) $(SRC) -o $(OUT_F)

$(OUT_C): $(SRC) $(DEPS)
	$(CC) $(CFLAGS) -DNANDLOG_RECORD_FRAMING=1 -DNANDLOG_CHIP_PAGE_COPY=0 $(INCLUDES) $(SRC) -o $(OUT_C)

run: $(OUT) $(OUT_F) $(OUT_C)
	./$(OUT)
	@echo
	./$(OUT_F)
	@echo
	./$(OUT_C)

clean:
	rm -rf $(OUT) $(OUT_F) $(OUT_C) $(OUT).dSYM $(OUT_F).dSYM $(OUT_C).dSYM nandlog_image.bin nandlog_image_framed.bin
