#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$ROOT_DIR/scripts/suvos-arch.sh"
ARCH="$(suvos_arch)"
LOCKFILE="${SUVOS_VENDORS_LOCKFILE:-$ROOT_DIR/third_party/vendors.lock.json}"

lock_value() {
  python3 "$ROOT_DIR/scripts/vendor-lock.py" --lockfile "$LOCKFILE" get chromium "$1" 2>/dev/null || true
}

resolve_chromium_repo() {
  if [ -n "${SUVOS_CHROMIUM_SOURCE_DIR:-}" ]; then
    printf '%s\n' "$SUVOS_CHROMIUM_SOURCE_DIR"
    return 0
  fi
  if [ -n "${SUVOS_CHROMIUM_REPO:-}" ]; then
    printf '%s\n' "$SUVOS_CHROMIUM_REPO"
    return 0
  fi

  locked_path="$(lock_value path)"
  if [ -n "$locked_path" ] && [ -d "$ROOT_DIR/$locked_path" ]; then
    printf '%s\n' "$ROOT_DIR/$locked_path"
    return 0
  fi

  echo "Chromium checkout not found. Run: $ROOT_DIR/scripts/bootstrap-vendors.sh chromium" >&2
  echo "Or set SUVOS_CHROMIUM_REPO." >&2
  exit 1
}

case "$ARCH" in
  x86_64)
    CHROMIUM_CPU="x64"
    DEB_ARCH="amd64"
    SYSROOT_DIR_NAME="debian_bullseye_amd64-sysroot"
    DEBIAN_MULTIARCH="x86_64-linux-gnu"
    GLIBC_LOADER="ld-linux-x86-64.so.2"
    DIST_TAR_NAME="chromium-rootfs.tar.gz"
    DIST_TAR_ALIAS="chromium-rootfs-x86_64.tar.gz"
    ;;
  aarch64)
    CHROMIUM_CPU="arm64"
    DEB_ARCH="arm64"
    SYSROOT_DIR_NAME="debian_bullseye_arm64-sysroot"
    DEBIAN_MULTIARCH="aarch64-linux-gnu"
    GLIBC_LOADER="ld-linux-aarch64.so.1"
    DIST_TAR_NAME="chromium-rootfs-aarch64.tar.gz"
    DIST_TAR_ALIAS=""
    ;;
  *)
    echo "unsupported SUVOS_ARCH for Chromium source artifact: $ARCH" >&2
    exit 2
    ;;
esac

CHROMIUM_REPO="$(resolve_chromium_repo)"
OUT_DIR="${SUVOS_CHROMIUM_OUT_DIR:-out/Linux_$CHROMIUM_CPU}"
if [ "${OUT_DIR#/}" = "$OUT_DIR" ]; then
  OUT_DIR="$CHROMIUM_REPO/$OUT_DIR"
fi

SYSROOT="$CHROMIUM_REPO/build/linux/$SYSROOT_DIR_NAME"
DIST_DIR="$CHROMIUM_REPO/dist"
DIST_TAR="$DIST_DIR/$DIST_TAR_NAME"
STAGE="$(mktemp -d "$ROOT_DIR/build/chromium-source-rootfs.XXXXXX")"
BUILD_DEB="${SUVOS_CHROMIUM_BUILD_DEB:-1}"
JOBS="${SUVOS_CHROMIUM_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 8)}"
NODE_MODULES_DIR="$ROOT_DIR/node_modules"
HIDDEN_NODE_MODULES_DIR="$ROOT_DIR/node_modules.hidden-during-chromium-build"
MOVED_NODE_MODULES=0

cleanup() {
  if [ "$MOVED_NODE_MODULES" -eq 1 ] && [ -d "$HIDDEN_NODE_MODULES_DIR" ]; then
    mv "$HIDDEN_NODE_MODULES_DIR" "$NODE_MODULES_DIR"
  fi
  chmod -R u+w "$STAGE" 2>/dev/null || true
  rm -rf "$STAGE"
}
trap cleanup EXIT

hide_root_node_modules() {
  if [ ! -e "$NODE_MODULES_DIR" ] && [ -d "$HIDDEN_NODE_MODULES_DIR" ]; then
    echo "Restoring previously hidden node_modules: $HIDDEN_NODE_MODULES_DIR"
    mv "$HIDDEN_NODE_MODULES_DIR" "$NODE_MODULES_DIR"
  fi

  if [ -d "$NODE_MODULES_DIR" ]; then
    if [ -e "$HIDDEN_NODE_MODULES_DIR" ]; then
      echo "Refusing to move node_modules: already exists: $HIDDEN_NODE_MODULES_DIR" >&2
      exit 2
    fi
    mv "$NODE_MODULES_DIR" "$HIDDEN_NODE_MODULES_DIR"
    MOVED_NODE_MODULES=1
  fi
}

copy_with_target() {
  src="$1"
  dest_dir="$2"
  [ -e "$src" ] || return 0
  mkdir -p "$dest_dir"
  cp -a "$src" "$dest_dir/"
  resolved="$(readlink -f "$src" 2>/dev/null || true)"
  if [ -n "$resolved" ] && [ "$resolved" != "$src" ] && [ -e "$resolved" ]; then
    cp -a "$resolved" "$dest_dir/"
  fi
}

copy_host_runtime_lib() {
  src="$1"
  dest_dir="$2"
  [ -e "$src" ] || return 0
  mkdir -p "$dest_dir"
  cp -a "$src" "$dest_dir/" 2>/dev/null || true
  resolved="$(readlink -f "$src" 2>/dev/null || true)"
  if [ -n "$resolved" ] && [ "$resolved" != "$src" ] && [ -e "$resolved" ]; then
    cp -a "$resolved" "$dest_dir/" 2>/dev/null || true
  fi
}

copy_host_runtime_dir() {
  src="$1"
  dest="$2"
  [ -d "$src" ] || return 0
  mkdir -p "$dest"
  cp -a "$src/." "$dest/"
}

copy_host_ldd_dependencies() {
  binary="$1"
  runtime="$2"

  ldd "$binary" 2>/dev/null |
    awk '/=> \// || /^[[:space:]]*\// { for (i = 1; i <= NF; i++) if ($i ~ /^\//) print $i }' |
    sort -u |
    while IFS= read -r lib; do
      copy_host_runtime_lib "$lib" "$runtime/lib"
    done
}

package_host_runtime() {
  chrome_bin="$1"
  runtime="$2"
  host_multiarch="$(gcc -print-multiarch 2>/dev/null || printf '%s\n' "$DEBIAN_MULTIARCH")"

  command -v ldd >/dev/null 2>&1 || return 1
  ldd "$chrome_bin" >/dev/null 2>&1 || return 1

  mkdir -p "$runtime/lib" "$runtime/lib64" "$runtime/usr/lib/$host_multiarch" "$runtime/usr/share/glvnd"

  for loader in /lib64/$GLIBC_LOADER /lib/$host_multiarch/$GLIBC_LOADER /usr/lib/$host_multiarch/$GLIBC_LOADER; do
    [ -e "$loader" ] || continue
    cp -L "$loader" "$runtime/lib64/$GLIBC_LOADER"
    cp -L "$loader" "$runtime/lib/$GLIBC_LOADER"
    break
  done

  copy_host_ldd_dependencies "$chrome_bin" "$runtime"

  for lib_dir in /lib/$host_multiarch /usr/lib/$host_multiarch; do
    [ -d "$lib_dir" ] || continue
    for pattern in \
      "libEGL*.so*" \
      "libGL*.so*" \
      "libGLES*.so*" \
      "libgallium*.so*" \
      "libwayland*.so*" \
      "libdrm*.so*" \
      "libgbm*.so*" \
      "libxshmfence*.so*" \
      "libxcb*.so*"; do
      for lib in "$lib_dir"/$pattern; do
        [ -e "$lib" ] || continue
        copy_host_runtime_lib "$lib" "$runtime/lib"
      done
    done
  done

  copy_host_runtime_dir "/usr/lib/$host_multiarch/dri" "$runtime/usr/lib/$host_multiarch/dri"
  copy_host_runtime_dir "/usr/lib/$host_multiarch/gbm" "$runtime/usr/lib/$host_multiarch/gbm"
  copy_host_runtime_dir /usr/share/glvnd/egl_vendor.d "$runtime/usr/share/glvnd/egl_vendor.d"

  for _ in 1 2; do
    find "$runtime" -type f \( -name '*.so' -o -name '*.so.*' \) -print |
      while IFS= read -r runtime_lib; do
        copy_host_ldd_dependencies "$runtime_lib" "$runtime"
      done
  done

  [ -x "$runtime/lib64/$GLIBC_LOADER" ]
}

patch_host_runtime_interpreters() {
  chrome_dir="$1"
  interpreter="$2"

  command -v patchelf >/dev/null 2>&1 || {
    echo "patchelf is required when SUVOS_CHROMIUM_RUNTIME_SOURCE=host" >&2
    return 1
  }

  find "$chrome_dir" -maxdepth 2 -type f -perm /111 -print |
    while IFS= read -r file; do
      if readelf -l "$file" 2>/dev/null | grep -q 'Requesting program interpreter'; then
        patchelf --set-interpreter "$interpreter" "$file"
      fi
    done
}

[ -d "$CHROMIUM_REPO" ] || {
  echo "Chromium checkout does not exist: $CHROMIUM_REPO" >&2
  exit 1
}
[ -d "$OUT_DIR" ] || {
  echo "Chromium output directory does not exist: $OUT_DIR" >&2
  exit 1
}
[ -d "$SYSROOT" ] || {
  echo "Chromium Linux sysroot does not exist: $SYSROOT" >&2
  exit 1
}
command -v dpkg-deb >/dev/null 2>&1 || {
  echo "dpkg-deb is required to extract the Chromium installer payload" >&2
  exit 1
}

mkdir -p "$DIST_DIR" "$ROOT_DIR/build"

if [ "$BUILD_DEB" = "1" ]; then
  hide_root_node_modules
  if [ -d "$ROOT_DIR/build/depot_tools" ]; then
    export PATH="$ROOT_DIR/build/depot_tools:$PATH"
  fi
  if [ -d "$CHROMIUM_REPO/buildtools/linux64" ]; then
    export PATH="$CHROMIUM_REPO/buildtools/linux64:$PATH"
  fi
  export DEPOT_TOOLS_UPDATE="${DEPOT_TOOLS_UPDATE:-0}"
  (cd "$CHROMIUM_REPO" && autoninja -C "$OUT_DIR" chrome/installer/linux:stable_deb -j "$JOBS")
fi

if [ -n "${SUVOS_CHROMIUM_SOURCE_DEB:-}" ]; then
  DEB="$SUVOS_CHROMIUM_SOURCE_DEB"
else
  DEB="$(
    find "$OUT_DIR" -maxdepth 1 -type f -name "chromium-browser-stable_*_${DEB_ARCH}.deb" -print |
      sort |
      tail -n 1
  )"
fi

[ -s "$DEB" ] || {
  echo "Chromium Debian package was not found in: $OUT_DIR" >&2
  exit 1
}

dpkg-deb -x "$DEB" "$STAGE"

rm -rf "$STAGE/etc/cron.daily" "$STAGE/opt/chromium.org/chromium/cron"
mkdir -p \
  "$STAGE/usr/bin" \
  "$STAGE/usr/lib" \
  "$STAGE/opt/suvos-chromium/sysroot" \
  "$STAGE/opt/suvos-chromium/runtime" \
  "$STAGE/usr/share/doc/suvos-chromium"

ln -sfn /opt/chromium.org/chromium "$STAGE/usr/lib/chromium"

for sysroot_path in lib lib64 usr/lib usr/share; do
  if [ -e "$SYSROOT/$sysroot_path" ]; then
    mkdir -p "$(dirname "$STAGE/opt/suvos-chromium/sysroot/$sysroot_path")"
    cp -a "$SYSROOT/$sysroot_path" "$STAGE/opt/suvos-chromium/sysroot/$sysroot_path"
  fi
done

mkdir -p "$STAGE/$(dirname "lib64/$GLIBC_LOADER")" "$STAGE/lib/$DEBIAN_MULTIARCH"
copy_with_target "$SYSROOT/lib64/$GLIBC_LOADER" "$STAGE/lib64"
for name in \
  "$GLIBC_LOADER" \
  libc.so.6 \
  libdl.so.2 \
  libgcc_s.so.1 \
  libm.so.6 \
  libnss_dns.so.2 \
  libnss_files.so.2 \
  libpthread.so.0 \
  libresolv.so.2 \
  librt.so.1 \
  libstdc++.so.6 \
  libutil.so.1; do
  copy_with_target "$SYSROOT/lib/$DEBIAN_MULTIARCH/$name" "$STAGE/lib/$DEBIAN_MULTIARCH"
  copy_with_target "$SYSROOT/usr/lib/$DEBIAN_MULTIARCH/$name" "$STAGE/lib/$DEBIAN_MULTIARCH"
done

CHROMIUM_RUNTIME_SOURCE="${SUVOS_CHROMIUM_RUNTIME_SOURCE:-host}"
CHROMIUM_RUNTIME_STATUS=none
if [ "$CHROMIUM_RUNTIME_SOURCE" = "host" ]; then
  if package_host_runtime "$STAGE/opt/chromium.org/chromium/chrome" "$STAGE/opt/suvos-chromium/runtime"; then
    patch_host_runtime_interpreters \
      "$STAGE/opt/chromium.org/chromium" \
      "/opt/suvos-chromium/runtime/lib64/$GLIBC_LOADER"
    CHROMIUM_RUNTIME_STATUS=host
  else
    rm -rf "$STAGE/opt/suvos-chromium/runtime"
    mkdir -p "$STAGE/opt/suvos-chromium/runtime"
    CHROMIUM_RUNTIME_STATUS=sysroot
    echo "warning: could not package host Chromium runtime; falling back to Chromium sysroot" >&2
  fi
elif [ "$CHROMIUM_RUNTIME_SOURCE" != "sysroot" ]; then
  echo "unsupported SUVOS_CHROMIUM_RUNTIME_SOURCE: $CHROMIUM_RUNTIME_SOURCE" >&2
  exit 2
else
  CHROMIUM_RUNTIME_STATUS=sysroot
fi

cat >"$STAGE/usr/bin/chromium" <<EOF
#!/bin/sh
CHROME_DIR=/opt/chromium.org/chromium
SYSROOT=/opt/suvos-chromium/sysroot
RUNTIME=/opt/suvos-chromium/runtime
DEBIAN_MULTIARCH=$DEBIAN_MULTIARCH
LOADER="\$RUNTIME/lib64/$GLIBC_LOADER"
if [ -x "\$LOADER" ]; then
  LIB_PATH="\$CHROME_DIR:\$CHROME_DIR/lib:\$RUNTIME/lib:\$RUNTIME/usr/lib/\$DEBIAN_MULTIARCH:\$RUNTIME/usr/lib/\$DEBIAN_MULTIARCH/dri:\$RUNTIME/usr/lib/\$DEBIAN_MULTIARCH/gbm:\$RUNTIME/usr/lib"
else
  LOADER="\$SYSROOT/lib64/$GLIBC_LOADER"
  [ -x "\$LOADER" ] || LOADER=/lib64/$GLIBC_LOADER
  LIB_PATH="\$CHROME_DIR:\$CHROME_DIR/lib:\$SYSROOT/lib/\$DEBIAN_MULTIARCH:\$SYSROOT/usr/lib/\$DEBIAN_MULTIARCH:\$SYSROOT/usr/lib/\$DEBIAN_MULTIARCH/dri:\$SYSROOT/usr/lib/\$DEBIAN_MULTIARCH/gbm:\$SYSROOT/usr/lib"
fi
export CHROME_WRAPPER=/usr/bin/chromium
export CHROME_VERSION_EXTRA="\${CHROME_VERSION_EXTRA:-suvos}"
export GNOME_DISABLE_CRASH_DIALOG=SET_BY_SUVOS
export LD_LIBRARY_PATH="\$LIB_PATH\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"

if [ -x "\$RUNTIME/lib64/$GLIBC_LOADER" ]; then
  [ -d "\$RUNTIME/usr/lib/\$DEBIAN_MULTIARCH/dri" ] && export LIBGL_DRIVERS_PATH="\${LIBGL_DRIVERS_PATH:-\$RUNTIME/usr/lib/\$DEBIAN_MULTIARCH/dri}"
  [ -d "\$RUNTIME/usr/lib/\$DEBIAN_MULTIARCH/gbm" ] && export GBM_BACKENDS_PATH="\${GBM_BACKENDS_PATH:-\$RUNTIME/usr/lib/\$DEBIAN_MULTIARCH/gbm}"
  [ -d "\$RUNTIME/usr/share/glvnd/egl_vendor.d" ] && export __EGL_VENDOR_LIBRARY_DIRS="\${__EGL_VENDOR_LIBRARY_DIRS:-\$RUNTIME/usr/share/glvnd/egl_vendor.d}"
  exec "\$CHROME_DIR/chrome" "\$@"
else
  [ -d "\$SYSROOT/usr/lib/\$DEBIAN_MULTIARCH/dri" ] && export LIBGL_DRIVERS_PATH="\${LIBGL_DRIVERS_PATH:-\$SYSROOT/usr/lib/\$DEBIAN_MULTIARCH/dri}"
  [ -d "\$SYSROOT/usr/lib/\$DEBIAN_MULTIARCH/gbm" ] && export GBM_BACKENDS_PATH="\${GBM_BACKENDS_PATH:-\$SYSROOT/usr/lib/\$DEBIAN_MULTIARCH/gbm}"
  [ -d "\$SYSROOT/usr/share/glvnd/egl_vendor.d" ] && export __EGL_VENDOR_LIBRARY_DIRS="\${__EGL_VENDOR_LIBRARY_DIRS:-\$SYSROOT/usr/share/glvnd/egl_vendor.d}"
  exec "\$LOADER" --library-path "\$LIB_PATH" "\$CHROME_DIR/chrome" "\$@"
fi
EOF
chmod 0755 "$STAGE/usr/bin/chromium"
ln -sfn chromium "$STAGE/usr/bin/chromium-browser"

for sandbox in \
  "$STAGE/opt/chromium.org/chromium/chrome-sandbox" \
  "$STAGE/opt/chromium.org/chromium/chrome_sandbox"; do
  [ -f "$sandbox" ] || continue
  chmod 4755 "$sandbox"
done

cat >"$STAGE/usr/share/doc/suvos-chromium/build-info.txt" <<EOF
vendor=suvos-chromium-source
arch=$ARCH
chromium_repo=$CHROMIUM_REPO
chromium_commit=$(git -C "$CHROMIUM_REPO" rev-parse HEAD 2>/dev/null || echo unknown)
chromium_out=$OUT_DIR
chromium_deb=$DEB
sysroot=$SYSROOT
runtime=$CHROMIUM_RUNTIME_STATUS
created_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)
EOF

tar -C "$STAGE" --sort=name --numeric-owner --owner=0 --group=0 -czf "$DIST_TAR" .
if [ -n "$DIST_TAR_ALIAS" ]; then
  cp "$DIST_TAR" "$DIST_DIR/$DIST_TAR_ALIAS"
fi

echo "Chromium source artifact: $DIST_TAR"
if [ -n "$DIST_TAR_ALIAS" ]; then
  echo "Chromium source artifact: $DIST_DIR/$DIST_TAR_ALIAS"
fi
