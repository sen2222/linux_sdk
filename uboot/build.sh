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

# Check the toolchain and its host libraries before cleaning the source tree.
for tool in gcc as ld ar objcopy; do
    if ! "${cross_compile}${tool}" --version > /dev/null; then
        echo "Error: cannot run ${cross_compile}${tool}." >&2
        echo "Check the toolchain in tool/compile-chain and its host runtime libraries." >&2
        exit 1
    fi
done

cd -- "$script_dir"
echo "Board configuration: mx6ull_sen_emmc_defconfig; parallel jobs: $jobs"
make ARCH=arm CROSS_COMPILE="$cross_compile" distclean
make ARCH=arm CROSS_COMPILE="$cross_compile" mx6ull_sen_emmc_defconfig
make -j"$jobs" ARCH=arm CROSS_COMPILE="$cross_compile"

test -s u-boot.imx
echo "Build complete: $script_dir/u-boot.imx"
