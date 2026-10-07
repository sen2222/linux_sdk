#!/bin/bash
set -euo pipefail

# Run from any directory; remove build outputs and generated configuration.
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cd -- "$script_dir"

# Cleanup uses the host compiler for Makefile probes and does not compile target code.
make ARCH=arm CROSS_COMPILE= distclean
echo "Cleanup complete: $script_dir"
