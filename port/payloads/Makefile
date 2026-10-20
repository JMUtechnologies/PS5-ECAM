# SPDX-License-Identifier: GPL-3.0-or-later
# PS5-ECAM's payloads, built with the payload SDK (tools/package.sh):
#   tile-installer.elf   moves the home screen tiles the app made into /data/homebrew (tile_installer.c)

ifdef PS5_PAYLOAD_SDK
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
include /opt/ps5-payload-sdk/toolchain/prospero.mk
endif

OUTPUT ?= tile-installer.elf
CFLAGS := -std=gnu11 -O2 -Wall -Wextra -Werror
LDFLAGS := -Wl,--build-id=none

.PHONY: all clean

all: $(OUTPUT)

$(OUTPUT): tile_installer.c
	@mkdir -p -- $(@D)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

clean:
	rm -f -- $(OUTPUT)
