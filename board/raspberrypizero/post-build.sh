#!/bin/sh

set -u
set -e

CONFIGFS="configfs	/sys/kernel/config	configfs	defaults	0	0"
FSTAB="${TARGET_DIR}/etc/fstab"

# Add configfs to fstab
if ! grep -xq "$CONFIGFS" "$FSTAB"; then
	echo "$CONFIGFS" >> "$FSTAB"
fi

# Debug shell over USB CDC ACM
if ! grep -q "^ttyGS0:" "${TARGET_DIR}/etc/inittab"; then
    echo "ttyGS0::respawn:/sbin/getty -L -n -l /bin/sh ttyGS0 115200 vt100" >> "${TARGET_DIR}/etc/inittab"
fi
