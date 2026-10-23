#!/bin/sh

set -eu

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"

. "$ROOT_DIR/go.build.common.sh"
gfoundation_build_init "$@"

sh "$ROOT_DIR/GFoundation/go.build.sh" "$GF_BUILD_MODE"
sh "$ROOT_DIR/GFoundationTests/go.build.sh" "$GF_BUILD_MODE"
