#!/usr/bin/env bash
# Runtime entry for imprint-dev.
set -euo pipefail

# Host X share: DISPLAY + XAUTHORITY come from compose.
# snap Docker cannot bind /tmp/.X11-unix (private /tmp); host network mode
# reaches the abstract socket @/tmp/.X11-unix/X0 instead.
if [ -n "${DISPLAY:-}" ]; then
    export DISPLAY
    export XAUTHORITY="${XAUTHORITY:-/tmp/.docker.xauth}"
fi

# Optional GH token from compose environment only (no host keyring sharing).
if [ -n "${GH_TOKEN:-}" ]; then
    export GH_TOKEN
fi

exec "$@"
