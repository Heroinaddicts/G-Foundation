#!/bin/bash

set -e

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN_DIR="$ROOT_DIR/../Bin"

echo "================================"
echo " Building GFoundation"
go build -o $BIN_DIR/GFoundation .

echo " Building GFoundation Success"
echo "================================"