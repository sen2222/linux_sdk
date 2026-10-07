#!/bin/bash
# Source this file; preserve the current directory and shell options.
if [[ ${BASH_SOURCE[0]} == "$0" ]]; then
    echo "Please run: source build/config.sh" >&2
    exit 1
fi

_sdk_configure_environment() {
    local sdk_root toolchain_dir cross_compile jobs sdk_path
    sdk_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd) || return 1
    toolchain_dir="$sdk_root/tool/compile-chain"
    cross_compile="$toolchain_dir/bin/arm-linux-gnueabihf-"
    jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN)}
    if [[ ! $jobs =~ ^[1-9][0-9]*$ ]]; then
        echo "Error: JOBS must be a positive integer." >&2
        return 1
    fi
    if ! "${cross_compile}gcc" --version > /dev/null; then
        echo "Error: cannot run SDK compiler ${cross_compile}gcc." >&2
        return 1
    fi
    sdk_path=$PATH
    case ":$sdk_path:" in
        *":$sdk_root/tool/host-tools/bin:"*) ;;
        *) sdk_path="$sdk_root/tool/host-tools/bin:$sdk_path" ;;
    esac
    case ":$sdk_path:" in
        *":$toolchain_dir/bin:"*) ;;
        *) sdk_path="$toolchain_dir/bin:$sdk_path" ;;
    esac
    export SDK_ROOT="$sdk_root" TOOLCHAIN_DIR="$toolchain_dir"
    export ARCH=arm CROSS_COMPILE="$cross_compile"
    export OUT_DIR="$sdk_root/out" JOBS="$jobs" PATH="$sdk_path"
    echo "SDK environment configured: $sdk_root (JOBS=$jobs)"
}

if _sdk_configure_environment; then
    unset -f _sdk_configure_environment
else
    unset -f _sdk_configure_environment
    return 1
fi
