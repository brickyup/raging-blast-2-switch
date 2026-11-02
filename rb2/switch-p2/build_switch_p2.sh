#!/usr/bin/env bash
# Builds rb2.nro with the native "p2" renderer (Prototype 2 / Silent Hill Switch ports, GPL-3.0): the game's GPU
# command ring is drawn straight with Vulkan instead of emulating the Xbox 360 GPU. Run inside WSL Ubuntu:
#   GAME="/mnt/c/path/to/your/extracted/game" bash rb2/switch-p2/build_switch_p2.sh
# Toolchain (each step skipped when done): masseffect-nx 46e6dbf + prototype-switch SDK patch, host rexglue,
# mesa-switch + prototype-switch NVK patch. Then codegen + speed passes + NRO.
# Output: rb2/switch-p2/out/rb2.nro and rb2.toml (SD card layout: see README.md).
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="${WORK:-$HOME/nx}"
: "${GAME:?Set GAME to the folder with your extracted game (default.xex inside)}"
# The hooks and function tables are addresses in this one executable (Europe disc).
XEX_SHA256=a43d7fa4404041deb82ecabb32a22d639b96dac351d4269114a7bc395e1b8973
[ "$(sha256sum < "$GAME/default.xex" | cut -d" " -f1)" = "$XEX_SHA256" ] ||
  { echo "$GAME/default.xex is not the supported Europe executable (see README.md)"; exit 1; }
PATCHES="$WORK/prototype-switch/patches"
JOBS="${JOBS:-8}"
REPO="$WORK/p2-repo"
SDK="$REPO/sdk"
MESA_SRC="$WORK/mesa-p2-src"
MESA_SDK="$WORK/mesa-p2-sdk"
APP="$WORK/rb2-p2-app"

mkdir -p "$WORK/patches"
if [ ! -d "$WORK/prototype-switch/.git" ]; then
  git clone -q https://github.com/fungalspores/prototype-switch "$WORK/prototype-switch"
  git -C "$WORK/prototype-switch" checkout -q ea65b2e
fi
# The Windows checkout of prototype-switch has CRLF patches; git apply needs LF.
for f in mesa-switch rexglue-sdk-masseffect-nx; do tr -d '\r' < "$PATCHES/$f.patch" > "$WORK/patches/p2-$f.patch"; done

# --- SDK: masseffect-nx initial commit (the patch's base) + the Prototype 2 SDK patch ---------------------------
if [ ! -d "$REPO/.git" ]; then
  git clone -q https://github.com/nebadasSwifty/masseffect-nx "$REPO"
  git -C "$REPO" checkout -q 46e6dbf
  git -C "$REPO" apply "$WORK/patches/p2-rexglue-sdk-masseffect-nx.patch"
fi
# The patch's last three files are written relative to sdk/ (no prefix), so git apply puts them at the repo root.
for f in include/rex/system/guest_stack_sampler.h src/system/guest_stack_sampler.cpp src/system/guest_stack_sampler.h; do
  [ -f "$REPO/$f" ] && { mkdir -p "$(dirname "$SDK/$f")"; mv "$REPO/$f" "$SDK/$f"; }
done
if [ ! -f "$SDK/thirdparty/fmt/include/fmt/format.h" ]; then
  WORK="$WORK" bash "$HERE/tools/fetch_sdk_src.sh"
  (cd "$REPO" && python3 tools/fetch_thirdparty.py --source "$WORK/rexglue-src")
fi
python3 "$HERE/tools/patch_sdk_joycon.py" "$SDK"
# The fork ships config_switch_aarch64.h but the upstream FFmpeg config.h has no Horizon case to select it.
python3 - "$SDK/thirdparty/FFmpeg/config.h" <<'EOF'
import sys
p = sys.argv[1]
s = open(p).read()
if "config_switch_aarch64" not in s:
    s = s.replace('#elif defined(__gnu_linux__)', '#elif defined(__SWITCH__)\n#include "config_switch_aarch64.h"\n#elif defined(__gnu_linux__)', 1)
    open(p, "w").write(s)
EOF
# The fork is developed on arm64 macOS: an x86-64 host needs SSSE3+ enabled for its byte-swap intrinsics.
[ -x "$SDK/out/host/rexglue" ] || (cd "$REPO" && CFLAGS="-march=x86-64-v3" CXXFLAGS="-march=x86-64-v3" JOBS="$JOBS" tools/build_host.sh)

# --- NVK driver with the Prototype 2 patch ---------------------------------------------------------------------
if [ ! -f "$MESA_SDK/opt/devkitpro/portlibs/switch/lib/libvulkan.a" ]; then
  sed -e "s#^PATCH=.*#PATCH=$WORK/patches/p2-mesa-switch.patch#" -e "s#^HERE=.*#HERE=$REPO/mesa#" \
      -e "s#^CONTAINER=.*#CONTAINER=mesa-switch-p2-build#" "$REPO/mesa/build_mesa_docker.sh" > "$WORK/build_mesa_p2.sh"
  APPLY_ONLY=1 SRC="$MESA_SRC" OUT="$MESA_SDK" bash "$WORK/build_mesa_p2.sh"
  # x86-64 host: Switch crates (those linked with devkitA64) need the aarch64 target; build-machine crates
  # (proc-macros and their deps) keep the host target. Upstream builds on arm64 hosts, where both coincide.
  sed -i 's/^RUST_TARGET=\${MESA_SWITCH_RUST_TARGET:-}$/RUST_TARGET=${MESA_SWITCH_RUST_TARGET:-aarch64-unknown-linux-gnu}/' \
    "$MESA_SRC/rustc-switch-wrapper.sh"
  python3 - "$MESA_SRC/rustc-switch-wrapper.sh" <<'EOF'
import sys
p = sys.argv[1]
s = open(p).read()
mark = 'if [ -n "$RUST_TARGET" ]; then\n    exec'
line = 'case " $* " in *aarch64-none-elf*) ;; *) RUST_TARGET= ;; esac\n\n'
if line not in s:
    s = s.replace(mark, line + mark, 1)
    open(p, "w").write(s)
EOF
  grep -q "rustup target add aarch64-unknown-linux-gnu" "$MESA_SRC/build-switch.sh" ||
    sed -i 's#^chmod +x /usr/local/libexec/bindgen /usr/local/libexec/rustc$#&\nrustup target add aarch64-unknown-linux-gnu#' \
      "$MESA_SRC/build-switch.sh"
  SRC="$MESA_SRC" OUT="$MESA_SDK" bash "$WORK/build_mesa_p2.sh"
  # Every object must be AArch64, or the NRO link skips the archive as incompatible.
  ! (cd "$(mktemp -d)" && ar x "$MESA_SDK/opt/devkitpro/portlibs/switch/lib/libvulkan.a" && file ./* | grep -q x86-64)
fi
[ "${TOOLCHAIN_ONLY:-0}" = 1 ] && { echo "toolchain ready"; exit 0; }

# --- App: rb2/switch-p2 + rb2/src, codegen, speed passes on the generated code ---------------------------------
mkdir -p "$APP/game"
rsync -a --delete --exclude out --exclude generated --exclude game "$HERE/" "$APP/rb2/"
rm -rf "$APP/src" && cp -r "$HERE/../src" "$APP/src"
ln -sf "$GAME/default.xex" "$APP/game/default.xex"
cd "$APP/rb2"
ls generated/default/*.cpp >/dev/null 2>&1 || "$SDK/out/host/rexglue" -f codegen rb2_manifest.toml
python3 tools/patch_generated.py generated/default
python3 tools/prepare_proto_codegen.py generated/default
python3 tools/prepare_no_volatile.py generated/default
python3 tools/prepare_direct_calls.py generated/default
# Speed passes from mgr-nx-deko (tuancapoo, GPL-3.0, via prototype-switch 1.4.0): guest CAS without the trailing
# dmb, vector byte reversal without a table lookup, constant shuffles as one permutation.
python3 tools/mgr/guest_cas.py --gen generated/default
python3 tools/mgr/vec_byteswap.py --gen generated/default
python3 tools/mgr/pch_neon_shuffles.py generated/default/rb2_pch.h

IMAGE=masseffect-nx-build
docker image inspect "$IMAGE" >/dev/null 2>&1 || docker build -t "$IMAGE" - <<'DOCKERFILE'
FROM devkitpro/devkita64:latest
RUN apt-get update && apt-get install -y --no-install-recommends glslang-tools && rm -rf /var/lib/apt/lists/*
DOCKERFILE

docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp -v "$WORK:$WORK" -w "$APP/rb2" "$IMAGE" bash -euo pipefail -c "
  cmake -S . -B out -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=$APP/rb2/cmake/switch-devkitA64.cmake \
    -DDEVKITPRO=/opt/devkitpro -DREXSDK_DIR=$SDK \
    -DREXGLUE_SWITCH_NVK_SDK=$MESA_SDK/opt/devkitpro/portlibs/switch \
    -DRB2_GUEST_LTO=${LTO:-ON} -DRB2_PGO=${PGO:-OFF} -DRB2_PGO_DATA=$APP/rb2/pgo-data
  cmake --build out -j $JOBS
  /opt/devkitpro/devkitA64/bin/aarch64-none-elf-nm out/rb2 | grep '^0000000000000000 T _start\$' >/dev/null"

mkdir -p "$HERE/out"
cp -f "$APP/rb2/out/rb2.nro" "$HERE/out/rb2.nro"
cp -f "$HERE/rb2.toml" "$HERE/out/rb2.toml"
ls -la "$HERE/out/rb2.nro"
