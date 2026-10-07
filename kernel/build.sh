#!/bin/bash
set -euo pipefail

# Run from any directory; use JOBS=4 ./build.sh to set parallel jobs.
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
toolchain_dir=$(cd -- "$script_dir/../tool/compile-chain" && pwd)
cross_compile="$toolchain_dir/bin/arm-linux-gnueabihf-"
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN)}

if [[ ! $jobs =~ ^[1-9][0-9]*$ ]]; then
    echo "Error: JOBS must be a positive integer." >&2
    exit 1
fi

export PATH="$toolchain_dir/bin:$PATH"
# Prefer the installed lzop; otherwise use the SDK host tool.
if ! command -v lzop > /dev/null; then
    export PATH="$script_dir/../tool/host-tools/bin:$PATH"
fi

for tool in make gcc bc perl lzop; do
    if ! command -v "$tool" > /dev/null; then
        echo "Error: required host tool not found: $tool." >&2
        exit 1
    fi
done
for tool in gcc as ld ar objcopy; do
    if ! "${cross_compile}${tool}" --version > /dev/null; then
        echo "Error: cannot run ${cross_compile}${tool}." >&2
        exit 1
    fi
done
lzop --version > /dev/null

cd -- "$script_dir"
echo "Kernel configuration: imx_sen_emmc_defconfig; parallel jobs: $jobs"
make ARCH=arm CROSS_COMPILE="$cross_compile" distclean
make ARCH=arm CROSS_COMPILE="$cross_compile" imx_sen_emmc_defconfig
make -j"$jobs" ARCH=arm CROSS_COMPILE="$cross_compile" \
    zImage modules imx6ull-sen-emmc.dtb

test -s arch/arm/boot/zImage
test -s arch/arm/boot/dts/imx6ull-sen-emmc.dtb
echo "Build complete: $script_dir/arch/arm/boot/zImage"
echo "Device tree: $script_dir/arch/arm/boot/dts/imx6ull-sen-emmc.dtb"
