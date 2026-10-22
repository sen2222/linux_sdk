#!/bin/bash
set -euo pipefail
export LC_ALL=C

project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

check_environment() {
    : "${CROSS_COMPILE:?Please source build/config.sh first.}"
    : "${JOBS:=8}"
    [[ $JOBS =~ ^[1-9][0-9]*$ ]] || { echo "Error: invalid JOBS." >&2; exit 1; }
    "${CROSS_COMPILE}gcc" --version >/dev/null
    "${CROSS_COMPILE}strip" --version >/dev/null
}

compile_programs() {
    make -C "$project_dir" -j"$JOBS" CROSS_COMPILE="$CROSS_COMPILE" \
        CFLAGS="-O2 -std=c99 -Wall -Wextra -Werror -mcpu=cortex-a7 -mfpu=vfpv3-d16 -mfloat-abi=hard"
    make -C "$project_dir" -j"$JOBS" host
}

package_programs() {
    "${CROSS_COMPILE}strip" --strip-unneeded "$project_dir/out/file_server" "$project_dir/out/file_client"
    tar -czf "$project_dir/out/file-interaction-imx6ull.tar.gz" \
        -C "$project_dir/out" file_server file_client -C "$project_dir" README.md
    (
        cd -- "$project_dir/out"
        sha256sum file_server file_client file-interaction-imx6ull.tar.gz > SHA256SUMS
    )
    echo "ARM binaries: $project_dir/out/file_server, $project_dir/out/file_client"
    echo "Host binaries: $project_dir/out/host/file_server, $project_dir/out/host/file_client"
}

main() {
    check_environment
    compile_programs
    package_programs
}

main "$@"
