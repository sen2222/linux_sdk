#!/bin/bash
set -euo pipefail
export LC_ALL=C

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
sdk_root=$(cd -- "$script_dir/../../../../.." && pwd)
source_dir=${1:-"$sdk_root/../source_library/lvgl-master"}
cross_compile="$sdk_root/system/tool/compile-chain/bin/arm-linux-gnueabihf-"
build_dir="$script_dir/build"
flags="-mcpu=cortex-a7 -mfpu=vfpv3-d16 -mfloat-abi=hard"
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN)}

if (( $# > 1 )) || [[ ! $jobs =~ ^[1-9][0-9]*$ ]]; then
    echo "Usage: JOBS=8 $0 [lvgl-source-directory]" >&2
    exit 1
fi
test -f "$source_dir/CMakeLists.txt"
"${cross_compile}gcc" --version >/dev/null
patch_file="$script_dir/patches/0001-tjpgd-stop-after-final-mcu.patch"
if patch --dry-run --silent --forward -p1 -d "$source_dir" < "$patch_file"; then
    patch --forward -p1 -d "$source_dir" < "$patch_file"
elif ! patch --dry-run --silent --reverse -p1 -d "$source_dir" < "$patch_file"; then
    echo "Error: LVGL source does not match the TJPGD completion fix." >&2
    exit 1
fi
cmake -S "$source_dir" -B "$build_dir" \
    -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=arm \
    -DCMAKE_C_COMPILER="${cross_compile}gcc" \
    -DCMAKE_CXX_COMPILER="${cross_compile}g++" \
    -DCMAKE_C_STANDARD=99 -DCMAKE_CXX_STANDARD=11 \
    -DCMAKE_C_FLAGS="$flags" -DCMAKE_CXX_FLAGS="$flags" \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
    -DLV_BUILD_CONF_PATH="$script_dir/lv_conf.h" \
    -DCONFIG_LV_BUILD_EXAMPLES=OFF -DCONFIG_LV_BUILD_DEMOS=OFF \
    -DCONFIG_LV_USE_THORVG=OFF -DCONFIG_LV_USE_THORVG_INTERNAL=OFF \
    -DLV_FETCH_DEPENDENCIES=OFF -DLV_USE_FIND_PACKAGE=OFF \
    -DLV_USE_PKG_CONFIG=OFF
cmake --build "$build_dir" --target lvgl -j"$jobs"
cmake --install "$build_dir" --prefix "$build_dir/install"
mkdir -p -- "$script_dir/lib" "$script_dir/include"
cp -a -- "$build_dir/install/include/." "$script_dir/include/"
cp -- "$build_dir/install/lib/liblvgl.a" "$script_dir/lib/"
cp -- "$source_dir/LICENCE.txt" "$script_dir/LICENCE.txt"
cp -- "$source_dir/COPYRIGHTS.md" "$script_dir/COPYRIGHTS.md"
(
    cd -- "$source_dir"
    sha256sum include/lvgl/lv_version.h env_support/cmake/version.cmake
) > "$script_dir/SOURCE_VERSION.sha256"
echo "LVGL installed: $script_dir/lib/liblvgl.a"
