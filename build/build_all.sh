#!/bin/bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
sdk_root=$(cd -- "$script_dir/.." && pwd)
source "$script_dir/publish.sh"

work_dir=""
firmware_dir=""

# Output filenames expected by the flashing tool.
uboot_name="u-boot-imx6ull14x14evk_emmc.imx"
dtb_name="zImage-imx6ull-14x14-evk-emmc.dtb"
rootfs_name="rootfs_nogpu.tar.bz2"

# Check the SDK environment and required build and packaging tools.
check_environment() {
    if [[ ${SDK_ROOT:-} != "$sdk_root" || ${ARCH:-} != arm ||
          ${CROSS_COMPILE:-} != "$sdk_root/system/tool/compile-chain/bin/arm-linux-gnueabihf-" ||
          ${OUT_DIR:-} != "$sdk_root/out" ]]; then
        echo "Error: source $script_dir/config.sh before running this script." >&2
        exit 1
    fi
    if [[ ! ${JOBS:-} =~ ^[1-9][0-9]*$ ]]; then
        echo "Error: JOBS must be a positive integer." >&2
        exit 1
    fi
    local tool
    for tool in make gcc bc perl tar bzip2 lzop sha256sum; do
        if ! command -v "$tool" > /dev/null; then
            echo "Error: required host tool not found: $tool." >&2
            exit 1
        fi
    done
    test -d "$sdk_root/system/rootfs/rootfs"
    if [[ -L $OUT_DIR || ( -e $OUT_DIR && ! -d $OUT_DIR ) ]]; then
        echo "Error: out must be a regular directory inside the SDK." >&2
        exit 1
    fi
}

# Stage the firmware and update out only after every step succeeds.
prepare_output() {
    mkdir -p -- "$OUT_DIR"
    work_dir=$(mktemp -d "$script_dir/.build-all.XXXXXX")
    trap cleanup EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM
    firmware_dir="$work_dir/firmware"
    mkdir -p -- "$firmware_dir"
}

# Remove temporary files on exit and restore out if publication is interrupted.
cleanup() {
    local status=$?
    if [[ -d "$work_dir/previous-out" && ! -e $OUT_DIR && ! -L $OUT_DIR ]]; then
        if ! mv -T -- "$work_dir/previous-out" "$OUT_DIR"; then
            echo "Error: firmware restore failed; check $work_dir/previous-out." >&2
            return 1
        fi
    fi
    rm -rf -- "$work_dir"
    return "$status"
}

# Reuse nonempty firmware files from the current output directory.
reuse_firmware() {
    local name=$1
    if [[ -f "$OUT_DIR/$name" && -s "$OUT_DIR/$name" ]]; then
        cp -p -- "$OUT_DIR/$name" "$firmware_dir/$name" || exit 1
        echo "Skipping existing firmware: $name"
        return 0
    fi
    return 1
}

build_uboot() {
    if reuse_firmware "$uboot_name"; then
        return 0
    fi
    echo "[1/4] Building U-Boot"
    "$sdk_root/system/uboot/build.sh"
    cp -- "$sdk_root/system/uboot/u-boot.imx" "$firmware_dir/$uboot_name"
}

build_kernel() {
    local kernel_dir="$sdk_root/system/kernel"
    local targets=()
    if ! reuse_firmware zImage; then
        targets+=(zImage modules)
    fi
    if ! reuse_firmware "$dtb_name"; then
        targets+=(imx6ull-sen-emmc.dtb)
    fi
    if (( ${#targets[@]} == 0 )); then
        return 0
    fi

    echo "[2/4] Building missing kernel outputs: ${targets[*]}"
    if (( ${#targets[@]} == 3 )); then
        "$kernel_dir/build.sh"
    else
        # Build only the missing image or DTB, keeping the existing kernel configuration.
        if [[ ! -s "$kernel_dir/.config" ]]; then
            make -C "$kernel_dir" ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" \
                imx_sen_emmc_defconfig
        fi
        make -C "$kernel_dir" -j"$JOBS" ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" \
            "${targets[@]}"
    fi
    if [[ ! -s "$firmware_dir/zImage" ]]; then
        cp -- "$kernel_dir/arch/arm/boot/zImage" "$firmware_dir/"
    fi
    if [[ ! -s "$firmware_dir/$dtb_name" ]]; then
        cp -- "$kernel_dir/arch/arm/boot/dts/imx6ull-sen-emmc.dtb" "$firmware_dir/$dtb_name"
    fi
}

pack_rootfs() {
    if reuse_firmware "$rootfs_name"; then
        return 0
    fi
    echo "[3/4] Packaging the existing rootfs"
    "$sdk_root/system/rootfs/packet.sh" \
        "$sdk_root/system/rootfs/rootfs" "$firmware_dir/$rootfs_name"
}

publish_output() {
    echo "[4/4] Generating checksums and publishing firmware"
    (
        cd -- "$firmware_dir"
        sha256sum "$uboot_name" zImage "$dtb_name" "$rootfs_name" > SHA256SUMS
    )
    sdk_publish_firmware "$firmware_dir" "$OUT_DIR" "$work_dir/previous-out"
    echo "Build complete. Firmware directory: $OUT_DIR"
}

main() {
    check_environment
    prepare_output
    build_uboot
    build_kernel
    pack_rootfs
    publish_output
}

main "$@"
