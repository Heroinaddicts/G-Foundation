#!/bin/sh
set -eu
ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$ROOT_DIR/../../go.build.common.sh"
gfoundation_build_init "$@"
OUTPUT_DIR="${GFOUNDATION_TEST_OUTPUT:-$ROOT_DIR/../../Bin}"
gfoundation_build "$ROOT_DIR" "$OUTPUT_DIR/MemoryProbe.so" plugin
