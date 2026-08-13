#!/bin/sh
set -eu

SCRIPT_DIR="$(
    CDPATH= cd -- "$(dirname -- "$0")"
    pwd
)"

REPO_ROOT="$(
    CDPATH= cd -- "$SCRIPT_DIR/.."
    pwd
)"

src_lib="$REPO_ROOT/liblinuwux_runtime.so"
src_launcher="$REPO_ROOT/scripts/run-runtime.sh"

lib_dir="${HOME}/.local/lib"
bin_dir="${HOME}/.local/bin"

dst_lib="$lib_dir/liblinuwux.so"
dst_launcher="$bin_dir/linuwux"

if [ ! -f "$src_lib" ]; then
    echo "install-runtime: runtime not built: $src_lib" >&2
    echo "run: make -C $REPO_ROOT" >&2
    exit 1
fi

mkdir -p "$lib_dir" "$bin_dir"

install -m 0755 "$src_lib" "$dst_lib"
install -m 0755 "$src_launcher" "$dst_launcher"

echo "Installed:"
echo "  $dst_lib"
echo "  $dst_launcher"
echo
echo "Usage:"
echo "  linuwux COMMAND [ARG...]"
echo
echo "Steam launch option:"
echo "  ~/.local/bin/linuwux %command%"
