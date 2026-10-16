#!/bin/sh

set -eu

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Each directory is an independent Go module. go test also runs go vet.
for module_dir in \
    GFoundationApi \
    GFoundationUtils \
    GFoundation \
    GFoundationTests/Interface \
    GFoundationTests/DbTest \
    GFoundationTests/TcpServerTest \
    GFoundationTests/TcpClientTest \
    GFoundationTests/HttpTest \
    GFoundationTests/FindModuleTest
do
    echo "Checking $module_dir"
    (cd "$ROOT_DIR/$module_dir" && go test ./...)
done
