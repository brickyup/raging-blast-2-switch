#!/usr/bin/env bash
# Run by INSTALL.bat inside WSL as root, from the repository root: installs what the build needs, starts Docker, builds.
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
echo "Installing build tools..."
apt-get update -qq
apt-get install -y -qq git rsync file docker.io cmake ninja-build clang lld python3-pip pkg-config glslang-tools \
  libx11-xcb-dev libwayland-dev libxkbcommon-dev libgtk-3-dev libxrandr-dev libxi-dev libxcursor-dev \
  libxinerama-dev libxss-dev libasound2-dev libpulse-dev libudev-dev libdbus-1-dev libunwind-dev liblz4-dev >/dev/null
systemctl start docker 2>/dev/null || service docker start >/dev/null 2>&1 || true
for _ in $(seq 1 30); do docker info >/dev/null 2>&1 && break; sleep 2; done
docker info >/dev/null 2>&1 || { echo "Docker did not start."; exit 1; }
echo "Building (the full log is in build.log)..."
bash rb2/switch-p2/build_switch_p2.sh > build.log 2>&1 &
build=$!
while kill -0 "$build" 2>/dev/null; do
  sleep 30
  echo "  still building: $(grep -E '^\[[0-9]+/[0-9]+\]' build.log | tail -1 | cut -d' ' -f1)"
done
wait "$build" || { tail -15 build.log | cut -c1-160; exit 1; }
