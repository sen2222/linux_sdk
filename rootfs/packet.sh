#!/bin/bash


cd ./rootfs
tar -vcjf rootfs_nogpu.tar.bz2 *

mv -v rootfs_nogpu.tar.bz2 ../../share
cd -
