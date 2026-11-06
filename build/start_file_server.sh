#!/bin/bash

: "${SDK_ROOT:?请先 source build/config.sh}"

OUT_DIR="${SDK_ROOT}/out"
FILE_SERVER_APP="${SDK_ROOT}/share/code/file_interaction/out/host/file_server"

"${FILE_SERVER_APP}" "${OUT_DIR}" 9000 &
echo "File server started on port 9000."