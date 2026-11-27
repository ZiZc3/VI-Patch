include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
CFLAGS := -Wall -Wextra -O2
all:
	$(CC) $(CFLAGS) -o vi-patch.elf vipatch.c
	$(CC) $(CFLAGS) -DPRIVATE -o vi-patch-private.elf vipatch.c
