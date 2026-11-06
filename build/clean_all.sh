#!/bin/bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
sdk_root=$(cd -- "$script_dir/.." && pwd)

clean_uboot() {
    "$sdk_root/system/uboot/clean.sh"
}

clean_kernel() {
    "$sdk_root/system/kernel/clean.sh"
}

clean_output() {
    if [[ -d "${sdk_root}/out" && ! -L "${sdk_root}/out" ]]; then
        find "${sdk_root}/out" -mindepth 1 -maxdepth 1 -exec rm -rf -- {} + || return 1
    fi
    rm -rf -- "${script_dir}"/.build-all.*
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
    echo "Cleanup complete: U-Boot and kernel build outputs and the contents of out."
}

main "$@"
