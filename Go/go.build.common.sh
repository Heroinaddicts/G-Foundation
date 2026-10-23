#!/bin/sh

# Sourced by every build entry point so the executable and plugins share a mode.
gfoundation_build_init() {
    if [ "$#" -gt 1 ]; then
        printf 'Usage: sh %s [release|profile|debug|race]\n' "$0" >&2
        return 2
    fi

    GF_BUILD_MODE="${1:-release}"
    case "$GF_BUILD_MODE" in
        release|profile|debug|race) ;;
        -h|--help)
            printf 'Usage: sh %s [release|profile|debug|race]\n' "$0"
            exit 0
            ;;
        *)
            printf 'Unknown build mode: %s\n' "$GF_BUILD_MODE" >&2
            printf 'Usage: sh %s [release|profile|debug|race]\n' "$0" >&2
            return 2
            ;;
    esac
}

# Run in a subshell to keep working directory and argument changes local.
gfoundation_build() (
    gf_module_dir="$1"
    gf_output="$2"
    gf_kind="$3"

    cd "$gf_module_dir"
    mkdir -p "$(dirname "$gf_output")"

    set -- "-buildmode=$gf_kind"
    gf_ldflags=""
    case "$GF_BUILD_MODE" in
        release) gf_ldflags="-s -w" ;;
        profile) ;;
        debug) set -- "$@" "-gcflags=all=-N -l" ;;
        race) set -- "$@" -race ;;
    esac

    if [ "$gf_kind" = plugin ]; then
        gf_goos="$(go env GOOS)"
        if [ "$gf_goos" = darwin ]; then
            gf_ldflags="${gf_ldflags:+$gf_ldflags }-extldflags=-Wl,-no_fixup_chains"
        fi
    fi
    if [ -n "$gf_ldflags" ]; then
        set -- "$@" "-ldflags=$gf_ldflags"
    fi

    printf 'Building %s [%s]\n' "${gf_output##*/}" "$GF_BUILD_MODE"
    go build "$@" -o "$gf_output" .
    printf 'Built %s [%s]\n' "${gf_output##*/}" "$GF_BUILD_MODE"
)
