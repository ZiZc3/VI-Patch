# Build: make PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk   (https://github.com/ps5-payload-dev/sdk)
PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
CFLAGS := -Wall -Wextra -O2
all:
	$(CC) $(CFLAGS) -o vi-patch.elf vipatch.c
clean:
	rm -f vi-patch.elf
