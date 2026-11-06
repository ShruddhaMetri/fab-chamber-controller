# tests/Makefile
# Builds the unit tests with the PC's own C compiler (gcc), not the ARM one,
# and runs them. Only "pure logic" modules are tested here; the hardware they
# call is replaced by fakes.c.
#
#   make            build and run the tests
#   make SAN=1      also use AddressSanitizer + UndefinedBehaviorSanitizer
#                   (catches out-of-bounds memory access and integer overflow;
#                    used on GitHub Actions; may not be available on Windows)
#   make clean

FW      := ../firmware/chamber_ctrl
CC      ?= gcc
CFLAGS  := -std=c11 -Wall -Wextra -Werror -O1 -g -I. -I$(FW)/Inc
LDLIBS  := -lm

ifeq ($(SAN),1)
CFLAGS  += -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all
LDFLAGS += -fsanitize=address,undefined
endif

# Firmware files under test - compiled exactly as they are in the product
UNDER_TEST := $(FW)/Src/crc16.c $(FW)/Src/crc32.c $(FW)/Src/temp_ctrl.c $(FW)/Src/recipe.c

build/run_tests: test_main.c fakes.c $(UNDER_TEST) test.h fakes.h | build
	$(CC) $(CFLAGS) $(LDFLAGS) test_main.c fakes.c $(UNDER_TEST) -o $@ $(LDLIBS)

build:
	mkdir -p build

.PHONY: test clean
test: build/run_tests
	./build/run_tests

.DEFAULT_GOAL := test

clean:
	rm -rf build
