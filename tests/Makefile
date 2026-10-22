# Host unit tests for firmware/Devices and firmware/App (ЛР2 §4.6).
# Plain gcc, no board.  Run from the repository root via Docker:
#   make test        build and run every test
#   make coverage    the same with gcov, then an lcov report for Devices/ and App/
# Options:
#   COVERAGE_MIN=<n>      minimum line coverage in %, `make coverage` fails below it

CC           ?= gcc
FW           := $(abspath ../firmware)
BUILD        := build
OBJ          := $(BUILD)/obj
# Why 95: tests/README.md, section "Покриття".
COVERAGE_MIN ?= 95

CFLAGS := -std=c11 -Wall -Wextra -Werror -O0 -g \
          -I$(FW)/App/Inc -I$(FW)/Devices/Inc -I$(FW)/Platform/Inc \
          -Ivendor/unity -Idoubles -Ivectors \
          -fno-builtin-printf -DUNITY_INCLUDE_DOUBLE
# printf() in firmware code goes to doubles/uart_capture.c instead of stdout.
LDFLAGS := -Wl,--wrap=printf -lm

ifeq ($(COV),1)
CFLAGS  += --coverage
LDFLAGS += --coverage
endif

COMMON := unity.c bus_log.c uart_capture.c fake_time.c

# test binary -> its sources (driver under test + the doubles it talks to)
test_mpu6050_SRC         := test_mpu6050.c mpu6050.c fake_i2c.c
test_gy89_SRC            := test_gy89.c gy89.c fake_i2c.c
test_i2c_scan_SRC        := test_i2c_scan.c i2c_scan.c fake_i2c.c
test_servo_SRC           := test_servo.c servo.c fake_pwm.c
test_w25q64_SRC          := test_w25q64.c w25q64.c fake_w25q64.c
test_w25q64_sequence_SRC := test_w25q64_sequence.c w25q64.c mock_spi.c
test_ov7670_SRC          := test_ov7670.c ov7670.c fake_sccb.c
# App: the Devices layer is replaced by a stub.
test_selftest_SRC        := test_selftest.c selftest.c stub_devices.c

TESTS := test_mpu6050 test_gy89 test_i2c_scan test_servo \
         test_w25q64 test_w25q64_sequence test_ov7670 \
         test_selftest

vpath %.c . doubles vendor/unity $(FW)/Devices/Src $(FW)/App/Src

.PHONY: all test coverage clean
all: test

# Objects built with and without --coverage cannot be linked together, so
# switching between `make coverage` and `make test` rebuilds them.
MODE := $(OBJ)/.mode-cov$(COV)

$(MODE): | $(OBJ)
	rm -f $(OBJ)/.mode-cov*
	touch $@

$(OBJ)/%.o: %.c $(MODE) | $(OBJ)
	$(CC) $(CFLAGS) -c $< -o $@

define TEST_template
$(BUILD)/$(1): $$(addprefix $(OBJ)/,$$($(1)_SRC:.c=.o) $(COMMON:.c=.o))
	$$(CC) $$^ $(LDFLAGS) -o $$@
endef
$(foreach t,$(TESTS),$(eval $(call TEST_template,$(t))))

$(OBJ):
	mkdir -p $@

# Runs every binary even if one fails, then prints one summary line.
test: $(addprefix $(BUILD)/,$(TESTS))
	@fail=0; for t in $(TESTS); do \
	    echo "== $$t"; \
	    ./$(BUILD)/$$t > $(BUILD)/$$t.log 2>&1 || fail=1; \
	    cat $(BUILD)/$$t.log; \
	done; \
	cat $(addprefix $(BUILD)/,$(addsuffix .log,$(TESTS))) | awk \
	    '/^[0-9]+ Tests [0-9]+ Failures [0-9]+ Ignored/ {t+=$$1; f+=$$3; i+=$$5} \
	     END {printf "\nTOTAL: %d tests, %d failures, %d ignored\n", t, f, i}'; \
	exit $$fail

LCOV := lcov --quiet --rc lcov_branch_coverage=1
# lcov 1.15 on Ubuntu 22.04 prints harmless Perl "Subroutine ... redefined" noise.
NOISE := grep -v 'Subroutine .* redefined' || true

coverage:
	$(MAKE) clean
	$(MAKE) COV=1 test
	@$(LCOV) --capture --directory $(OBJ) --output-file $(BUILD)/all.info 2>&1 | $(NOISE)
	@$(LCOV) --extract $(BUILD)/all.info '$(FW)/Devices/*' '$(FW)/App/*' --output-file $(BUILD)/coverage.info 2>&1 | $(NOISE)
	@lcov --rc lcov_branch_coverage=1 --list $(BUILD)/coverage.info
	@genhtml --quiet --branch-coverage --output-directory $(BUILD)/coverage $(BUILD)/coverage.info
	@lcov --rc lcov_branch_coverage=1 --summary $(BUILD)/coverage.info 2>&1 | awk -v min=$(COVERAGE_MIN) \
	    '/lines\.*:/ {pct = $$2 + 0} /branches\.*:/ {br = $$2 + 0} \
	     END {printf "\nLines %.1f%%, branches %.1f%%; minimum for lines %d%%: %s\n", \
	          pct, br, min, (pct >= min) ? "OK" : "TOO LOW"; \
	          print "HTML report: tests/$(BUILD)/coverage/index.html"; exit (pct < min)}'

clean:
	rm -rf $(BUILD)
