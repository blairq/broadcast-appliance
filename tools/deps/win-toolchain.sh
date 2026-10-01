#!/usr/bin/env bash
# ============================================================================
# tools/deps/win-toolchain.sh — toolchain para compilar Windows DESDE Linux
#
# Todo el build de Windows usa el ABI de MSVC (clang-cl + lld-link): el
# libwebrtc precompilado para Windows (Shiguredo) esta compilado con la STL de
# MSVC y su CRT estatico (/MT), y OBS y el Qt msvc2019_64 tambien son MSVC.
# Nada se instala en el sistema; todo queda en extern/:
#
#   extern/toolchain/llvm   llvm-mingw (clang, clang-cl, lld-link, llvm-rc,
#                           llvm-lib, llvm-dlltool...) — solo se usan sus
#                           herramientas LLVM, con --target=x86_64-pc-windows-msvc
#   extern/toolchain/xwin   xwin (descarga y acomoda el CRT y el SDK)
#   extern/msvc             CRT de MSVC + Windows SDK, layout /winsysroot
#
# LICENCIA: el CRT y el SDK son de Microsoft y se bajan de sus servidores;
# xwin exige aceptar la licencia de Visual Studio Build Tools. Este script NO
# la acepta por su cuenta: hace falta BA_ACCEPT_MSVC_LICENSE=1.
#
# Idempotente: si cada pieza ya esta (y su hash coincide), se salta.
# ============================================================================
set -euo pipefail
# macOS no trae sha256sum (shasum -a 256 da el mismo formato).
command -v sha256sum >/dev/null 2>&1 || sha256sum() { shasum -a 256 "$@"; }

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
EXT="$ROOT/extern"
DL="$EXT/.downloads"
TC="$EXT/toolchain"
mkdir -p "$DL" "$TC"

log() { echo "[win-toolchain] $*" >&2; }

# --- Pins ---------------------------------------------------------------------
LLVM_MINGW_VER=20260908
LLVM_MINGW_NAME="llvm-mingw-${LLVM_MINGW_VER}-ucrt-ubuntu-22.04-x86_64"
LLVM_MINGW_URL="https://github.com/mstorsjo/llvm-mingw/releases/download/${LLVM_MINGW_VER}/${LLVM_MINGW_NAME}.tar.xz"
LLVM_MINGW_SHA=2258c745e3155870c80793f3e8c80b28fbde11b9ff73c4c78783635b3440b092

XWIN_VER=0.10.0
XWIN_NAME="xwin-${XWIN_VER}-x86_64-unknown-linux-musl"
XWIN_URL="https://github.com/Jake-Shadle/xwin/releases/download/${XWIN_VER}/${XWIN_NAME}.tar.gz"
XWIN_SHA=d870eb4b2f390878af6da1ccd3cf321d22fcb72720984853b4be732ae597fc88

# Versiones EXACTAS del CRT y del SDK (xwin toma las del manifiesto de VS 17).
MSVC_CRT_VER=14.44.17.14
WIN_SDK_VER=10.0.26100
# DLLs redistribuibles del CRT (vcruntime140, msvcp140...): van JUNTO a los
# .exe del bundle (despliegue app-local, permitido por la licencia), asi el
# sistema no necesita el instalador "VC++ Redistributable". Mismo manifiesto.
CRT_REDIST_URL="https://download.visualstudio.microsoft.com/download/pr/45d3b8dd-bced-4b37-9974-142f748d710c/4aaf54db0bfc9435f7c3660e1a00237a4b556042bfeea64bde44c2e0194e6ee5/Microsoft.VC.14.44.17.14.CRT.Redist.X64.base.vsix"
CRT_REDIST_SHA=4aaf54db0bfc9435f7c3660e1a00237a4b556042bfeea64bde44c2e0194e6ee5

fetch() { # url destino sha
    local url="$1" out="$2" sha="$3"
    if [ -f "$out" ] && [ "$(sha256sum "$out" | cut -d' ' -f1)" = "$sha" ]; then return 0; fi
    log "descargando $url"
    curl -fL --retry 3 --connect-timeout 30 -o "$out.part" "$url"
    [ "$(sha256sum "$out.part" | cut -d' ' -f1)" = "$sha" ] || { log "ERROR: hash de $out"; rm -f "$out.part"; exit 1; }
    mv "$out.part" "$out"
}

# --- LLVM (llvm-mingw) --------------------------------------------------------
if [ "$(cat "$TC/llvm/.version" 2>/dev/null)" != "$LLVM_MINGW_VER" ]; then
    fetch "$LLVM_MINGW_URL" "$DL/${LLVM_MINGW_NAME}.tar.xz" "$LLVM_MINGW_SHA"
    rm -rf "$TC/llvm"
    mkdir -p "$TC/llvm"
    tar xJf "$DL/${LLVM_MINGW_NAME}.tar.xz" -C "$TC/llvm" --strip-components=1 --no-same-owner
    # El driver de MSVC es el mismo clang llamado como clang-cl.
    ln -sf clang "$TC/llvm/bin/clang-cl"
    echo "$LLVM_MINGW_VER" >"$TC/llvm/.version"
    log "LLVM listo ($("$TC/llvm/bin/clang" --version | head -1))"
else
    log "LLVM: ya presente"
fi

# --- xwin ---------------------------------------------------------------------
if [ "$(cat "$TC/xwin/.version" 2>/dev/null)" != "$XWIN_VER" ]; then
    fetch "$XWIN_URL" "$DL/${XWIN_NAME}.tar.gz" "$XWIN_SHA"
    rm -rf "$TC/xwin"
    mkdir -p "$TC/xwin"
    tar xzf "$DL/${XWIN_NAME}.tar.gz" -C "$TC/xwin" --strip-components=1 --no-same-owner
    echo "$XWIN_VER" >"$TC/xwin/.version"
else
    log "xwin: ya presente"
fi

# --- CRT + SDK de Microsoft ---------------------------------------------------
STAMP="crt=${MSVC_CRT_VER} sdk=${WIN_SDK_VER}"
if [ "$(cat "$EXT/msvc/.version" 2>/dev/null)" = "$STAMP" ]; then
    log "CRT/SDK de MSVC: ya presentes ($STAMP)"
    exit 0
fi
if [ "${BA_ACCEPT_MSVC_LICENSE:-}" != 1 ]; then
    log "Falta el CRT y el SDK de Microsoft (extern/msvc)."
    log "Bajarlos implica aceptar la licencia de Visual Studio Build Tools:"
    log "  https://go.microsoft.com/fwlink/?LinkId=2086102"
    log "Si la aceptas: make win BA_ACCEPT_MSVC_LICENSE=1"
    exit 2
fi
rm -rf "$EXT/msvc"
"$TC/xwin/xwin" --accept-license --http-retry 3 --cache-dir "$DL/xwin-cache" \
    --crt-version "$MSVC_CRT_VER" --sdk-version "$WIN_SDK_VER" --arch x86_64 \
    splat --use-winsysroot-style --preserve-ms-arch-notation --output "$EXT/msvc"
# xwin agrega enlaces en minusculas para los nombres mal escritos, pero Qt
# pide <GL/gl.h> (carpeta en mayusculas): en un disco que distingue
# mayusculas hace falta el enlace.
UM="$(ls -d "$EXT/msvc/Windows Kits/10/Include/"*/um | head -1)"
[ -e "$UM/GL" ] || ln -s gl "$UM/GL"
fetch "$CRT_REDIST_URL" "$DL/crt-redist-x64-${MSVC_CRT_VER}.vsix" "$CRT_REDIST_SHA"
mkdir -p "$EXT/msvc/redist"
unzip -j -o -q "$DL/crt-redist-x64-${MSVC_CRT_VER}.vsix" '*/x64/Microsoft.VC143.CRT/*.dll' -d "$EXT/msvc/redist"
# El cache de xwin (paquetes desempaquetados, ~1 GB) ya no hace falta.
rm -rf "$DL/xwin-cache/unpack"
echo "$STAMP" >"$EXT/msvc/.version"
log "CRT/SDK de MSVC listos en extern/msvc ($STAMP)"
