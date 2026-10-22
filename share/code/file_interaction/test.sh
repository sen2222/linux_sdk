#!/bin/bash
set -euo pipefail
export LC_ALL=C

project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

build_host_programs() {
    make -C "$project_dir" host
}

run_transfer_tests() {
    python3 "$project_dir/tests/test_transfer.py"
}

main() {
    build_host_programs
    run_transfer_tests
}

main "$@"
