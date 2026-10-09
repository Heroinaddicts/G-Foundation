#!/bin/bash

set -e

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"

cd $ROOT_DIR

echo "================================"
echo " Building TcpServerTest.so"
go build \
    -buildmode=plugin \
    -ldflags='-extldflags=-Wl,-no_fixup_chains' \
    -o ../../Bin/TcpServerTest.so \
    .
echo " Building TcpServerTest.so Success"
echo "================================"

cd -
