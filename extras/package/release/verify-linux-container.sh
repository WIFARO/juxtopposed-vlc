#!/usr/bin/env bash
# CI-only: install runtime test tools in a disposable Ubuntu container, never a host.
set -euo pipefail
scripts=$(cd "$(dirname "$0")" && pwd)
if [[ ${1:-} != --run ]]; then
    [[ -f /.dockerenv ]] || { echo 'Run this check only in a disposable Docker container.' >&2; exit 1; }
    version=$1 appdir=$2
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq
    if ! apt-get install -y --no-install-recommends binutils python3 ffmpeg xvfb xauth weston \
        dbus-x11 libgl1-mesa-dri libegl1 libegl-mesa0 libglx-mesa0 >/tmp/runtime-install.log 2>&1; then
        cat /tmp/runtime-install.log
        exit 1
    fi
    dbus-uuidgen --ensure
    exec runuser -u nobody -- dbus-run-session -- bash "$0" --run "$version" "$appdir"
fi
version=$2 appdir=$3
XDG_RUNTIME_DIR=$(mktemp -d /tmp/vlc-wayland-XXXXXX)
export XDG_RUNTIME_DIR
export LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2
if weston --help 2>&1 | grep -q -- --renderer; then renderer=--renderer=pixman; else renderer=--use-pixman; fi
weston --backend=headless-backend.so "$renderer" --socket=wayland-release --idle-time=0 --log="$XDG_RUNTIME_DIR/weston.log" &
weston_pid=$!
trap 'kill "$weston_pid" 2>/dev/null || true; cat "$XDG_RUNTIME_DIR/weston.log"; rm -rf "$XDG_RUNTIME_DIR"' EXIT
for attempt in {1..100}; do
    [[ -S "$XDG_RUNTIME_DIR/wayland-release" ]] && break
    sleep .1
done
test -S "$XDG_RUNTIME_DIR/wayland-release"
python3 "$scripts/bundle-linux-runtime.py" --verify-only "$appdir"
QT_QPA_PLATFORM=wayland WAYLAND_DISPLAY=wayland-release QT_WAYLAND_CLIENT_BUFFER_INTEGRATION=wayland-egl \
    python3 "$scripts/verify.py" --expected-version "$version" --gui --video --require-opengl "$appdir/AppRun"
QT_QPA_PLATFORM=xcb xvfb-run -a \
    python3 "$scripts/verify.py" --expected-version "$version" --gui --video --require-opengl "$appdir/AppRun"
QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=software xvfb-run -a \
    python3 "$scripts/verify.py" --expected-version "$version" --gui --video "$appdir/AppRun"
