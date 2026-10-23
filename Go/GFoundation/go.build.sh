#!/bin/sh

set -eu

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN_DIR="$ROOT_DIR/../Bin"

. "$ROOT_DIR/../go.build.common.sh"
gfoundation_build_init "$@"

gfoundation_build "$ROOT_DIR" "$BIN_DIR/GFoundation" exe
