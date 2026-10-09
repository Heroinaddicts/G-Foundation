#!/bin/bash

set -e

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"

cd $ROOT_DIR

echo "================================"
echo " Building TcpClientTest.so"
go build \
    -buildmode=plugin \
    -ldflags='-extldflags=-Wl,-no_fixup_chains' \
    -o ../../Bin/TcpClientTest.so \
    .
echo " Building TcpClientTest.so Success"
echo "================================"

cd -
