# Build: make PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk
# Needs ps5-payload-sdk v0.43 (https://github.com/ps5-payload-dev/sdk),
# clang/lld 18 and python3.

PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk
ifneq ($(MAKECMDGOALS),clean)
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
endif

XML    ?= ../src/PPSA38500.xml
OFF    := --off "60 FPS" --off "No clothing" --off "Mandy replaces Lucia"
CFLAGS := -Wall -Wextra -Os
ELF    := vi-patch-loader.elf

all: $(ELF)

$(ELF): vi-patch-loader.c ptwalk.c ptwalk.h patches.h
	$(CC) $(CFLAGS) -o $@ vi-patch-loader.c ptwalk.c -lkernel_sys

patches.h: $(XML) originals.txt gen_patches.py
	python3 gen_patches.py $(XML) originals.txt $(OFF) > $@.tmp && mv $@.tmp $@

clean:
	rm -f $(ELF) patches.h patches.h.tmp

.PHONY: all clean
