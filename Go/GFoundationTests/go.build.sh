#!/bin/sh

set -eu

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"

. "$ROOT_DIR/../go.build.common.sh"
gfoundation_build_init "$@"

for module in DbTest TcpServerTest TcpClientTest HttpTest FindModuleTest RedisTest TimerTest TaskTest; do
    sh "$ROOT_DIR/$module/go.build.sh" "$GF_BUILD_MODE"
done
