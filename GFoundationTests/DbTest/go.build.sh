#!/bin/bash

set -e

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"

cd $ROOT_DIR

echo "================================"
echo " Building DbTest.so"
go build \
    -buildmode=plugin \
    -ldflags='-extldflags=-Wl,-no_fixup_chains' \
    -o ../../Bin/DbTest.so \
    .
echo " Building DbTest.so Success"
echo "================================"

cd -
