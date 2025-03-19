#!/usr/bin/env bash

set -euo pipefail


if [ "$#" -ne 1  ]; then
  echo "Usage: $0 <build-dir>"
  exit 1
fi

VITE_WASM_BUILD_DIR=$(realpath "$1")
shift
export VITE_WASM_BUILD_DIR

cd "$(dirname "$0")" || exit
env NODE_ENV=development npx vite
