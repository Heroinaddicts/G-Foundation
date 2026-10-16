#!/bin/sh

set -e

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN_DIR="$ROOT_DIR/../Bin"

echo "================================"
echo " Building GFoundation"
cd "$ROOT_DIR"
mkdir -p "$BIN_DIR"
go build -o "$BIN_DIR/GFoundation" .

echo " Building GFoundation Success"
echo "================================"
