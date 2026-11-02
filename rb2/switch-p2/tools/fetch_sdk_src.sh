#!/usr/bin/env bash
# Fetches rexglue-sdk v0.10.0 with all submodules into $WORK/rexglue-src (retries: git's submodule
# update sometimes dies with "waitpid ... No child processes" in WSL). build_switch_p2.sh copies from it.
set -euo pipefail
WORK="${WORK:-$HOME/nx}"
DST="$WORK/rexglue-src"
if [ ! -d "$DST/.git" ]; then
  git init -q "$DST"
  git -C "$DST" remote add origin https://github.com/rexglue/rexglue-sdk.git
  git -C "$DST" fetch -q --depth 1 origin c94f5ebdcb3c9d1a460ca48e04f9758448f8d518
  git -C "$DST" checkout -q FETCH_HEAD
fi
for i in 1 2 3 4 5 6; do
  git -C "$DST" submodule update --init --recursive --depth 1 -q && break
  echo "submodule update failed, retry $i"
done
missing=$(git -C "$DST" submodule status --recursive | grep -c '^-' || true)
echo "uninitialised submodules: $missing"
[ "$missing" = 0 ]
