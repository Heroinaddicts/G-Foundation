#!/bin/bash

set -e

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"

cd $ROOT_DIR/GFoundation
sh ./go.build.sh
cd ..

cd $ROOT_DIR/GFoundationTests
sh ./go.build.sh
cd ..
