#!/bin/sh

set -u
set -e

CONFIGFS="configfs	/sys/kernel/config	configfs	defaults	0	0"
FSTAB="${TARGET_DIR}/etc/fstab"

# Add configfs to fstab
if ! grep -xq "$CONFIGFS" "$FSTAB"; then
	echo "$CONFIGFS" >> "$FSTAB"
fi

# No CDC ACM serial console in this composite (opening the ACM tty was shown to
# persistently degrade UVC). Ensure no getty on the now-absent ttyGS0.
sed -i '/^ttyGS0:/d' "${TARGET_DIR}/etc/inittab"
