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
echo " Building HttpTest.so"
go build \
    -buildmode=plugin \
    "$@" \
    -o ../../Bin/HttpTest.so \
    .
echo " Building HttpTest.so Success"
echo "================================"

cd -
