#!/bin/sh
set -eu

lib="${LINUWUX_PRELOAD:-${HOME}/.local/lib/liblinuwux.so}"

if [ ! -f "$lib" ]; then
    echo "linuwux: runtime not found: $lib" >&2
    exit 1
fi

if [ "$#" -eq 0 ]; then
    echo "linuwux: no command given" >&2
    echo "usage: linuwux COMMAND [ARG...]" >&2
    exit 2
fi

#
# Preserve any preload chain already supplied by Steam, a launcher,
# MangoHud, overlays, etc. LinUwUx is appended rather than replacing it.
#
export LD_PRELOAD="${LD_PRELOAD:+$LD_PRELOAD:}$lib"

if [ "${LINUWUX_DEBUG:-0}" != "0" ]; then
    echo "[linuwux-launch] runtime=$lib" >&2
    echo "[linuwux-launch] LD_PRELOAD=$LD_PRELOAD" >&2
fi

exec "$@"
