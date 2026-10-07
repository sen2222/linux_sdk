#!/bin/bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
sdk_root=$(cd -- "$script_dir/.." && pwd)

clean_uboot() {
    "$sdk_root/uboot/clean.sh"
}

clean_kernel() {
    "$sdk_root/kernel/clean.sh"
}

clean_output() {
    rm -rf -- "$sdk_root/out" "$script_dir"/.build-all.*
}

main() {
    local failed=0

    # Continue after a cleanup failure; sourcing the environment is not required.
    clean_uboot || failed=1
    clean_kernel || failed=1
    clean_output || failed=1

    if (( failed )); then
        echo "Error: some cleanup steps failed; check the messages above." >&2
        exit 1
    fi
    echo "Cleanup complete: U-Boot and kernel build outputs and the out directory."
}

main "$@"
