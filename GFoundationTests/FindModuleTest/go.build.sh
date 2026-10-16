#!/bin/sh

set -e

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"

cd "$ROOT_DIR"
mkdir -p ../../Bin

set --
if [ "$(go env GOOS)" = "darwin" ]; then
    set -- "$@" "-ldflags=-extldflags=-Wl,-no_fixup_chains"
fi

echo "================================"
echo " Building FindModuleTest.so"
go build \
    -buildmode=plugin \
    "$@" \
    -o ../../Bin/FindModuleTest.so \
    .
echo " Building FindModuleTest.so Success"
echo "================================"

cd -
