#!/usr/bin/env bash
# ============================================================================
# tools/deps/pinggy.sh — libpinggy (túnel de pinggy.io) a extern/pinggy/
#
# Proposito:
#   El acceso remoto por pinggy.io (docs/planes/PINGGY.md, docs/funciones/PINGGY.md):
#   ba-pinggy (src/pinggy) es un proceso hijo del gateway que levanta el túnel
#   con la biblioteca oficial de pinggy (C++ con API en C, Apache-2.0). Va en
#   un proceso aparte para no mezclar su OpenSSL con el BoringSSL de libwebrtc.
#
# Se baja el binario PUBLICADO (https://github.com/Pinggy-io/libpinggy/releases),
# fijado por versión y SHA-256, y se deja aplanado en
# extern/pinggy/<plataforma>/ (pinggy.h + las bibliotecas):
#   macos           libpinggy.dylib + libssl.3.dylib + libcrypto.3.dylib (universal,
#                   la variante «-ssl-», que trae su OpenSSL: el sistema no lo tiene)
#   linux-x86_64    libpinggy.so + libssl.so.3 + libcrypto.so.3 (variante «-ssl-»)
#   linux-aarch64   idem
#   windows-x86_64  pinggy.dll + pinggy.lib (variante MT: OpenSSL y la CRT
#                   adentro de la DLL; solo pide DLL de la base de Windows)
#
# Uso: tools/deps/pinggy.sh [macos|linux-x86_64|linux-aarch64|windows-x86_64|todas]
#      (sin argumento: la del host; idempotente)
# ============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VER="0.3.1"
BASE="https://github.com/Pinggy-io/libpinggy/releases/download/v$VER"
DL="$ROOT/extern/.downloads"

log()   { printf '\033[36m[pinggy]\033[0m %s\n' "$*"; }
morir() { printf '\033[31m[pinggy] ERROR:\033[0m %s\n' "$*" >&2; exit 1; }

sha256() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }

# <plataforma> -> "<asset> <sha256>"
asset() {
    case "$1" in
        macos)          echo "libpinggy-$VER-ssl-macos-universal.tgz 8e1bdc1b0f12b691d8e1c24615d8f792ace3196105f1360ea9869d635fb7b057" ;;
        linux-x86_64)   echo "libpinggy-$VER-ssl-linux-x86_64.tgz 2855e1530e35f4bec7276e57f23d1773cfe0673e403160c4e8a69ca5f9096fff" ;;
        linux-aarch64)  echo "libpinggy-$VER-ssl-linux-aarch64.tgz 1f17efd0dcb378640f190e3db9d8f7211e4af9bf6c82980810584a78ea9551d2" ;;
        windows-x86_64) echo "libpinggy-$VER-windows-x86_64-MT.zip 618f58f32152bed4fd6b4039bfeb8acdf58b913fe2c13c2281deb75eefc62772" ;;
        *) morir "plataforma desconocida: $1" ;;
    esac
}

host() {
    case "$(uname -s)-$(uname -m)" in
        Darwin-*)                      echo macos ;;
        Linux-x86_64)                  echo linux-x86_64 ;;
        Linux-aarch64 | Linux-arm64)   echo linux-aarch64 ;;
        *) morir "host sin libpinggy conocida: $(uname -s)-$(uname -m)" ;;
    esac
}

desempacar_zip() { # <zip> <destino>
    if command -v unzip >/dev/null 2>&1; then unzip -qo "$1" -d "$2"
    elif command -v 7z >/dev/null 2>&1; then 7z x -y -bso0 -o"$2" "$1"
    else python3 -I -m zipfile -e "$1" "$2"; fi
}

bajar() { # <plataforma>
    local plat="$1" out="$ROOT/extern/pinggy/$1" a sha tmp
    read -r a sha <<<"$(asset "$plat")"
    if [ -f "$out/.ok" ] && [ "$(cat "$out/.ok")" = "$sha" ]; then
        log "$plat: ya estaba (v$VER)"
        return 0
    fi
    mkdir -p "$DL"
    if [ ! -f "$DL/$a" ] || [ "$(sha256 "$DL/$a")" != "$sha" ]; then
        log "$plat: bajando $a"
        curl -fL --retry 3 --connect-timeout 30 -o "$DL/$a.part" "$BASE/$a" || morir "falló la descarga de $a"
        mv "$DL/$a.part" "$DL/$a"
    fi
    [ "$(sha256 "$DL/$a")" = "$sha" ] || { rm -f "$DL/$a"; morir "$a: el SHA-256 no coincide"; }

    # Se desempaca aparte y se copia solo lo que hace falta (nada del archivo se ejecuta).
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' RETURN
    case "$a" in
        *.zip) desempacar_zip "$DL/$a" "$tmp" ;;
        *)     tar -xzf "$DL/$a" -C "$tmp" ;;
    esac
    rm -rf "$out"; mkdir -p "$out"
    cp "$tmp/pinggy.h" "$out/"
    case "$plat" in
        macos)
            cp "$tmp/libpinggy.dylib" "$tmp/openssl/lib/libssl.3.dylib" "$tmp/openssl/lib/libcrypto.3.dylib" "$out/" ;;
        linux-*)
            local ssl="$tmp/openssl/lib"
            [ -f "$tmp/openssl/lib64/libssl.so.3" ] && ssl="$tmp/openssl/lib64"
            cp "$tmp/libpinggy.so" "$ssl/libssl.so.3" "$ssl/libcrypto.so.3" "$out/" ;;
        windows-*)
            cp "$tmp/pinggy.dll" "$tmp/pinggy.lib" "$out/" ;;
    esac
    printf '%s' "$sha" >"$out/.ok"
    log "$plat: listo en extern/pinggy/$plat (v$VER)"
}

case "${1:-}" in
    "")    bajar "$(host)" ;;
    todas) for p in macos linux-x86_64 linux-aarch64 windows-x86_64; do bajar "$p"; done ;;
    *)     bajar "$1" ;;
esac
