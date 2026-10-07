#!/bin/bash
# Internal helper: replace the firmware directory and restore it on failure.
sdk_publish_firmware() {
    local firmware_dir=$1 out_dir=$2 backup_dir=$3
    if [[ ! -d $firmware_dir || -L $firmware_dir || -L $out_dir ||
          -e $backup_dir || -L $backup_dir ]]; then
        echo "Error: invalid firmware publication directories." >&2
        return 1
    fi
    if [[ -e $out_dir ]]; then
        if [[ ! -d $out_dir ]]; then
            echo "Error: out must be a regular directory." >&2
            return 1
        fi
        mv -T -- "$out_dir" "$backup_dir" || return 1
    fi
    if ! mv -T -- "$firmware_dir" "$out_dir"; then
        if [[ -d $backup_dir ]]; then
            if ! mv -T -- "$backup_dir" "$out_dir"; then
                echo "Error: firmware restore failed; backup retained at $backup_dir." >&2
            fi
        fi
        return 1
    fi
}
