#!/bin/sh

set -e

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"

cd "$ROOT_DIR"
sh ./DbTest/go.build.sh
sh ./TcpServerTest/go.build.sh
sh ./TcpClientTest/go.build.sh
sh ./HttpTest/go.build.sh
sh ./FindModuleTest/go.build.sh
