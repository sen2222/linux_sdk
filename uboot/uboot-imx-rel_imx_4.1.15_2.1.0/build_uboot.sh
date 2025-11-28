#!/bin/bash
make distclean
make mx6ull_sen_emmc_defconfig
make -j12
