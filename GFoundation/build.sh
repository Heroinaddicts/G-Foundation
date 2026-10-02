go build \
    -buildmode=plugin \
    -ldflags='-extldflags=-Wl,-no_fixup_chains' \
    -o GFoundation.so \
    ./Plugin