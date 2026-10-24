#!/bin/bash
set -euo pipefail

module_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
project_dir=$(cd -- "$module_dir/../.." && pwd)
output_dir="$project_dir/build/tests/ds_stack"
mkdir -p -- "$output_dir"
cc -std=c99 -Wall -Wextra -Werror -pedantic -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$module_dir" "$module_dir/tests/test_ds_stack.c" "$module_dir/ds_stack.c" \
    -Wl,--wrap=malloc -o "$output_dir/test_ds_stack"
"$output_dir/test_ds_stack"
