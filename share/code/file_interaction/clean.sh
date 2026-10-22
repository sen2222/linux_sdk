#!/bin/bash
set -euo pipefail

project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

clean_outputs() {
    rm -rf -- "$project_dir/out"
    echo "Cleaned: $project_dir/out"
}

main() {
    clean_outputs
}

main "$@"
