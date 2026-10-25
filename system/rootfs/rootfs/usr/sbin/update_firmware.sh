#!/bin/sh
# i.MX6ULL eMMC updater. Run on the board, not on the build host.
set -eu

BOOT_PART=/dev/mmcblk1p1
# mx6ull_sen_emmc.h: the U-Boot environment starts at 12 * 64 KiB.
BOOT_END=786432
work=
mounted=0
force_ro=
old_ro=

fail() { echo "错误: $*" >&2; exit 1; }

usage() {
    cat <<'EOF'
用法:
  update_firmware.sh kernel 文件路径 [--device /dev/mmcblk1p1]
  update_firmware.sh dtb 文件路径 [--name 实际加载的.dtb] [--device /dev/mmcblk1p1]
  update_firmware.sh boot 文件路径 --device eMMC设备 --offset 字节偏移
选项:
  --dry-run          只检查，不更新镜像
  --backup-dir 目录  boot 原始区域备份目录，默认 /root/firmware-backup
  --help            显示帮助

kernel 更新分区中的 zImage；dtb 自动选择唯一现有 DTB，或用 --name 指定。
boot 设备和偏移必须明确指定；不会更改 eMMC 启动选择和 U-Boot 环境。
备份: kernel/dtb 为同目录的 文件名.bak；boot 为备份目录下的 boot.XXXXXX/boot.bin。
更新完成后手动 reboot。单份镜像更新期间请保持供电。
EOF
}

cleanup() {
    result=$?
    trap - EXIT HUP INT TERM
    if [ -n "$force_ro" ]; then
        if ! echo "$old_ro" > "$force_ro"; then
            echo "错误: 无法恢复 $force_ro" >&2
            result=1
        fi
    fi
    if [ "$mounted" = 1 ]; then
        if ! umount "$work/mount"; then
            echo "错误: 无法卸载 $work/mount，请手动处理" >&2
            exit 1
        fi
    fi
    [ -z "$work" ] || rm -rf "$work"
    exit "$result"
}

magic() {
    dd if="$1" bs=1 skip="$2" count=4 2>/dev/null | od -An -tx1 | tr -d ' \n'
}

validate_image() {
    [ -f "$image" ] && [ -r "$image" ] && [ -s "$image" ] || fail "镜像不存在、不可读或为空: $image"
    size=$(stat -c %s "$image")
    case "$target" in
        kernel) [ "$size" -ge 48 ] && [ "$(magic "$image" 36)" = 18286f01 ] || fail '镜像格式不是 ARM zImage' ;;
        dtb) [ "$size" -ge 40 ] && [ "$(magic "$image" 0)" = d00dfeed ] || fail '镜像格式不是 DTB' ;;
        boot) [ "$size" -ge 32 ] && [ "$(magic "$image" 0)" = d1002040 ] || fail '镜像格式不是本项目的 u-boot.imx' ;;
    esac
}

validate_boot_range() {
    # Limit decimal lengths before shell arithmetic (including on a 32-bit board).
    case "$offset" in ''|*[!0-9]*) fail 'boot 必须指定十进制 --offset 字节偏移' ;; esac
    [ "${#offset}" -le 6 ] && [ "${#size}" -le 6 ] || fail 'boot 写入范围过大'
    # Reject octal interpretation and ambiguous numeric input.
    case "$offset" in 0|0*) fail 'boot 偏移不能为 0 或含前导零' ;; esac
    [ "$offset" -ge 1024 ] && [ $((offset % 512)) -eq 0 ] || fail 'boot 偏移至少为 1024 且必须按 512 字节对齐'
    [ "$size" -gt 0 ] && [ $((offset + size)) -le "$BOOT_END" ] || fail 'boot 镜像越过 768 KiB 环境区边界'
}

validate_mmc_device() {
    disk=${device##*/}
    disk=${disk%%boot*}
    disk=${disk%%p*}
    [ "$(cat "/sys/class/block/$disk/device/type")" = MMC ] || fail '目标必须是 MMC/eMMC，不能是 SD 卡'
}

update_file() {
    [ -f "$destination" ] && [ ! -L "$destination" ] || fail "启动文件不存在或为软链接: $destination"
    [ ! -L "$destination.bak" ] && [ ! -L "$destination.new" ] || fail '备份或暂存目标为软链接'
    cp "$destination" "$destination.bak"
    cmp -s "$destination" "$destination.bak" || fail '旧启动文件备份校验失败'
    cp "$image" "$destination.new"
    cmp -s "$image" "$destination.new" || fail '新启动文件校验失败'
    sync
    mv -f "$destination.new" "$destination"
    sync
    cmp -s "$image" "$destination" || fail '启动文件写入后校验失败'
    echo "更新完成: $destination；备份: $destination.bak"
}

update_boot() {
    blocks=$(((size + 511) / 512))
    skip=$((offset / 512))
    dd if="$device" of="$backup" bs=512 skip="$skip" count="$blocks" 2>/dev/null
    [ "$(stat -c %s "$backup")" -eq $((blocks * 512)) ] || fail 'boot 备份长度不符'
    sync
    echo "boot 原区域备份: $backup (offset=$offset，恢复长度=$((blocks * 512)))"
    if ! dd if="$image" of="$device" bs=512 seek="$skip" conv=notrunc,fsync; then
        fail "boot 写入失败！保留备份 $backup，请勿重启"
    fi
    sync
    dd if="$device" of="$work/readback" bs=512 skip="$skip" count="$blocks" 2>/dev/null
    head -c "$size" "$work/readback" > "$work/verify"
    cmp -s "$image" "$work/verify" || fail "boot 回读校验失败！保留备份 $backup，请勿重启"
    echo 'boot 更新及回读校验完成'
}

prepare_partition() {
    # Only mount a currently unmounted FAT partition; never unmount someone else's mount.
    dev_number=$(cat "/sys/class/block/${device##*/}/dev")
    if awk -v dev="$dev_number" '$3 == dev {found=1} END {exit !found}' /proc/self/mountinfo; then
        fail "$device 已挂载，请先卸载该启动分区"
    fi
    mkdir "$work/mount"
    options=rw
    [ "$dry_run" = 0 ] || options=ro
    mount -t vfat -o "$options" "$device" "$work/mount"
    mounted=1
    if [ "$target" = kernel ]; then
        name=zImage
    elif [ -z "$name" ]; then
        set -- "$work/mount/"*.dtb
        [ "$#" -eq 1 ] && [ -f "$1" ] || fail '启动分区中没有唯一的 DTB，请用 --name 指定实际加载的文件名'
        name=${1##*/}
    fi
    case "$name" in ''|.|..|*/*) fail '目标名称只能是文件名，不能带目录' ;; esac
    if [ "$target" = dtb ]; then
        case "$name" in *.dtb) ;; *) fail '设备树目标名称必须以 .dtb 结尾' ;; esac
    fi
    destination=$work/mount/$name
    [ -f "$destination" ] && [ ! -L "$destination" ] || fail "启动分区没有现有文件: $name"
    echo "目标: $device / $name"
    [ "$dry_run" = 1 ] || update_file
}

prepare_boot() {
    validate_boot_range
    base=${device##*/}
    [ -r "/sys/class/block/$base/size" ] || fail '无法读取设备容量'
    sectors=$(cat "/sys/class/block/$base/size")
    [ $(((offset + size + 511) / 512)) -le "$sectors" ] || fail 'boot 镜像超出设备容量'
    # User-area writes must also stay before EVERY actual disk partition.
    for entry in /sys/class/block/"$base"/"$base"p*/start; do
        [ -f "$entry" ] || continue
        start=$(cat "$entry")
        [ $(((offset + size + 511) / 512)) -le "$start" ] || fail 'boot 镜像与磁盘分区重叠'
    done
    [ "$(magic "$device" "$offset")" = d1002040 ] || fail '指定位置没有现有 U-Boot 镜像头，请核对设备和偏移'
    echo "目标: $device，偏移: $offset，长度: $size"
    [ "$dry_run" = 0 ] || return 0
    mkdir -p "$backup_dir"
    backup_folder=$(mktemp -d "$backup_dir/boot.XXXXXX")
    backup=$backup_folder/boot.bin
    printf 'device=%s\noffset=%s\nimage_size=%s\n' "$device" "$offset" "$size" > "$backup_folder/info.txt"
    # boot0/boot1 may be read-only; restore the original setting even on failure.
    ro_path=/sys/class/block/$base/force_ro
    if [ -f "$ro_path" ]; then
        old_ro=$(cat "$ro_path")
        force_ro=$ro_path
        echo 0 > "$force_ro"
    fi
    update_boot
}

main() {
    [ "${1:-}" != --help ] || { usage; exit 0; }
    [ "$#" -ge 2 ] || { usage; exit 1; }
    target=$1
    image=$2
    shift 2
    case "$target" in boot|kernel|dtb) ;; *) fail '更新目标必须为 boot、kernel 或 dtb' ;; esac
    device=
    offset=
    name=
    dry_run=0
    backup_dir=/root/firmware-backup
    while [ "$#" -gt 0 ]; do
        case "$1" in
            --dry-run) dry_run=1; shift ;;
            --device|--offset|--name|--backup-dir)
                [ "$#" -ge 2 ] && [ -n "$2" ] || fail "$1 缺少参数"
                case "$1" in
                    --device) device=$2 ;; --offset) offset=$2 ;;
                    --name) name=$2 ;; --backup-dir) backup_dir=$2 ;;
                esac
                shift 2 ;;
            *) fail "未知参数: $1" ;;
        esac
    done
    [ "$target" = boot ] || [ -z "$offset" ] || fail '--offset 仅用于 boot'
    [ "$target" = dtb ] || [ -z "$name" ] || fail '--name 仅用于 dtb'
    case "$backup_dir" in /*) ;; *) backup_dir=$PWD/$backup_dir ;; esac
    # Make relative paths and filenames beginning with '-' safe for all applets.
    case "$image" in /*) ;; *) image=$PWD/$image ;; esac
    validate_image
    if [ "$target" = boot ]; then
        [ -n "$device" ] || fail 'boot 必须明确指定 --device，不自动猜测启动区域'
        validate_boot_range
    else
        device=${device:-$BOOT_PART}
    fi
    device=$(readlink -f "$device")
    [ -b "$device" ] || fail "不是块设备: $device"
    case "$target:$device" in
        boot:/dev/mmcblk[0-9]|boot:/dev/mmcblk[0-9]boot[01]) ;;
        kernel:/dev/mmcblk[0-9]p[0-9]|dtb:/dev/mmcblk[0-9]p[0-9]) ;;
        *) fail '设备类型不符合所选更新目标' ;;
    esac
    validate_mmc_device
    [ "$(id -u)" -eq 0 ] || fail '请用 root 运行'
    # Serialize all updater instances; the kernel releases the lock on exit.
    exec 9>/tmp/update_firmware.lock
    flock -n 9 || fail '另一个固件更新正在进行'
    work=$(mktemp -d /tmp/update_firmware.XXXXXX)
    trap cleanup EXIT
    trap 'exit 1' HUP INT TERM
    cp "$image" "$work/image"
    cmp -s "$image" "$work/image" || fail '镜像暂存校验失败'
    image=$work/image
    validate_image
    if [ "$target" = boot ]; then prepare_boot; else prepare_partition; fi
    if [ "$dry_run" = 1 ]; then
        echo '检查完成，未更新镜像'
    else
        echo '请手动 reboot 使新镜像生效'
    fi
}

main "$@"
