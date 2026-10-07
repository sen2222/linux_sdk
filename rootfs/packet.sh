#!/bin/bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
rootfs_dir=""
archive=""
temporary_archive=""

prepare_paths() {
    if (( $# > 2 )); then
        echo "Usage: $0 [rootfs_directory] [output_archive]" >&2
        exit 1
    fi
    rootfs_dir=${1:-"$script_dir/rootfs"}
    archive=${2:-"$script_dir/../out/rootfs_nogpu.tar.bz2"}
    test -d "$rootfs_dir"
    rootfs_dir=$(cd -- "$rootfs_dir" && pwd -P)
    archive=$(realpath -m -- "$archive")
    case "$archive" in
        "$rootfs_dir"/*)
            echo "Error: the output archive must be outside the rootfs directory." >&2
            exit 1 ;;
    esac
    mkdir -p -- "$(dirname -- "$archive")"
}

cleanup() {
    rm -f -- "$temporary_archive"
}

check_rootfs() {
    local mount_point
    for mount_point in dev proc sys tmp; do
        if [[ ! -d "$rootfs_dir/$mount_point" || -L "$rootfs_dir/$mount_point" ]]; then
            echo "Error: required rootfs mount point /$mount_point is missing; create it before packaging." >&2
            exit 1
        fi
    done
}

pack_archive() {
    temporary_archive=$(mktemp "$(dirname -- "$archive")/.rootfs.XXXXXX")
    trap cleanup EXIT
    # '.' includes hidden files; preserve links and permissions, use root ownership and exclude old archives.
    tar --numeric-owner --owner=0 --group=0 \
        --exclude='./rootfs_nogpu.tar.bz2' --exclude='./rootfs.tar.bz2' \
        -cjf "$temporary_archive" -C "$rootfs_dir" .
    chmod 644 "$temporary_archive"
    mv -fT -- "$temporary_archive" "$archive"
}

main() {
    prepare_paths "$@"
    check_rootfs
    pack_archive
    echo "Rootfs package complete: $archive"
}

main "$@"
