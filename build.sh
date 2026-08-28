#!/bin/sh

BR2_EXTERNAL="$(pwd)" make -C buildroot/ webcampi_raspberrypizero_defconfig

KBUILD_BUILD_USER=webcampi \
KBUILD_BUILD_HOST=webcampi \
make -C buildroot/ all
