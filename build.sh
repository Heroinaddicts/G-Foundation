#!/bin/bash

set -e

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN_DIR="$ROOT_DIR/Bin"

echo "================================"
echo " Building G-Foundation"
echo "================================"

# -------------------------------
# GFoundation
# -------------------------------

echo ""
echo "[1/3] Building GFoundation..."

cd "$ROOT_DIR/GFoundation"

go build -o ../Bin/GFoundation .


# -------------------------------
# GTests
# -------------------------------

echo ""
echo "[3/3] Building GTests..."

cd "$ROOT_DIR/GTests"

go build \
    -buildmode=plugin \
    -ldflags='-extldflags=-Wl,-no_fixup_chains' \
    -o ../Bin/GTests.so \
    .

# -------------------------------

echo ""
echo "================================"
echo " Build successful!"
echo "================================"
echo ""
echo "Output:"
echo "  $BIN_DIR/GFoundation.so"
echo "  $BIN_DIR/GTests"