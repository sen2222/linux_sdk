#!/bin/bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
share_dir="$script_dir/../../share"

if [[ ! -s "$script_dir/arch/arm/boot/zImage" ||
      ! -s "$script_dir/arch/arm/boot/dts/imx6ull-sen-emmc.dtb" ]]; then
    echo "Error: run build.sh first to generate zImage and the device tree." >&2
    exit 1
fi
mkdir -p -- "$share_dir"
cp -v -- "$script_dir/arch/arm/boot/zImage" "$share_dir/"
cp -v -- "$script_dir/arch/arm/boot/dts/imx6ull-sen-emmc.dtb" "$share_dir/"
