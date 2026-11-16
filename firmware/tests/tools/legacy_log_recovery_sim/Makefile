# Host build of the legacy log recovery firmware over the nandlog SPI simulator. No target toolchain, no hardware.
#   make                   build the harness
#   make test DASHBOARD=<path to a tottag-dashboard checkout>
#                          build it, then run the end-to-end test (Python/dashboard/test_legacy_recovery.py there)

CC       = cc
CFLAGS   = -std=c99 -Wall -Wextra -Wno-unused-parameter -O1 -g -fsanitize=address,undefined
NANDLOG  = ../../../src/external/nandlog
INCLUDES = -I$(NANDLOG) -I$(NANDLOG)/sim
SRC      = legacy_log_recovery_sim.c ../legacy_log_recovery.c $(NANDLOG)/sim/nandlog_port_sim.c
OUT      = legacy_log_recovery_sim

.PHONY: all test clean
all: $(OUT)

$(OUT): $(SRC) $(NANDLOG)/sim/nandlog_port_sim.h
	$(CC) $(CFLAGS) -DLEGACY_RECOVERY_HOST $(INCLUDES) $(SRC) -o $(OUT)

test: $(OUT)
	@test -n "$(DASHBOARD)" || { echo 'Set DASHBOARD to a tottag-dashboard checkout: make test DASHBOARD=<path>' >&2; exit 2; }
	python3 $(DASHBOARD)/Python/dashboard/test_legacy_recovery.py --harness ./$(OUT)

clean:
	rm -rf $(OUT) $(OUT).dSYM
