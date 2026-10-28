#!/usr/bin/env bash
# ============================================================================
# tools/deps/chrome-headless.sh — chrome-headless-shell (el Chrome sin ventana
# de Chrome for Testing) para sacarle fotos a las páginas web del aparato
#
# Lo usa la herramienta `web_captura` (web/CapturaWeb): la web del evento, la de
# los invitados, el teleprompter, la botonera… tal como se ven en un celular o en
# una computadora (los manuales de usuario, y el asistente para verificar lo que
# edita). Versión y sha256 fijados; se dejan solo los idiomas inglés y español.
#
# LINUX: el binario pide bibliotecas que no están en la base permitida
# (tools/deps/linux-baseline.txt): NSS, NSPR, ATK, Xcomposite, Xdamage, expat,
# at-spi y sqlite. Viajan ADENTRO del bundle, en su carpeta lib/ (la app lo lanza
# con LD_LIBRARY_PATH a esa carpeta): salen de los paquetes de Ubuntu 24.04 con
# versión y sha256 fijados. Si Ubuntu saca una versión del archivo, la descarga
# falla con el hash: hay que poner la nueva acá.
#
# Uso: tools/deps/chrome-headless.sh mac-arm64|win64|linux64
#      deja extern/chrome-headless/<plataforma>/ (con .version)
# ============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PLAT="${1:?uso: $0 mac-arm64|win64|linux64}"

VERSION="156.0.8078.73"
case "$PLAT" in
    mac-arm64) SHA="512c7d34cd5b65d617a39a48915b490eed4208a63e9d47b0fd7893e83be85765" ;;
    win64)     SHA="e6d26810df71efd0a3dfd0f7246d75d279ebdf8a8ec153d1ac44069657298628" ;;
    linux64)   SHA="77eb8d52798efc7a857ab61abcc62a456d8ea9635b02341d06dffee2761768c8" ;;
    *) echo "[chrome-headless] plataforma desconocida: $PLAT" >&2; exit 1 ;;
esac

# Los paquetes de Ubuntu 24.04 (noble, amd64) con las bibliotecas que faltan en Linux.
UBUNTU="https://archive.ubuntu.com/ubuntu"
DEBS=(
    "pool/main/n/nss/libnss3_3.98-1ubuntu0.3_amd64.deb e11fdf8c09a068d0f0c3fd90ed971253dfed7227cc20d3d487fd615d4771f1bb"
    "pool/main/n/nspr/libnspr4_4.35-1.1build1_amd64.deb e579e72d091f6c7a13f5a756c31065b15aae5b81840d61b069355aa2283c07b4"
    "pool/main/a/at-spi2-core/libatk1.0-0t64_2.52.0-1build1_amd64.deb 42c5d4b00954f17c2c3c4b866844f691eb8b6d57bf08b59203e65f12dc84a4f9"
    "pool/main/a/at-spi2-core/libatk-bridge2.0-0t64_2.52.0-1build1_amd64.deb 22b7d47e3c0f7953a78d3cfd309d1b20771085de9220fbe610c0989b9e808c9b"
    "pool/main/a/at-spi2-core/libatspi2.0-0t64_2.52.0-1build1_amd64.deb d686ace4080eca9c2d6ce8de69392a5ac56c846701894c0858922d4ce341a1d7"
    "pool/main/libx/libxcomposite/libxcomposite1_0.4.5-1build3_amd64.deb a12d5d9ae70b798cbbf284080eae32b87367561fc9c24492f641b8860ac0f308"
    "pool/main/libx/libxdamage/libxdamage1_1.1.6-1build1_amd64.deb 6891faf325e996ebd28ef53ebb9c043dc96f67327500490affc3e89d366d35ed"
    "pool/main/e/expat/libexpat1_2.6.1-2ubuntu0.6_amd64.deb 494b8e672f722130c6bca6a7bc4cc31a43ca891a31d60d868bfdd699a3c20b13"
    "pool/main/s/sqlite3/libsqlite3-0_3.45.1-1ubuntu2.8_amd64.deb b1190bb72359f5fcc47406aa46065eaf4f1ca208085c51224a52b04bedc0b4bb"
)

DEST="$ROOT/extern/chrome-headless/$PLAT"
SELLO="$VERSION $SHA"
if [ "$PLAT" = linux64 ]; then SELLO="$SELLO $(printf '%s\n' "${DEBS[@]}" | { sha256sum 2>/dev/null || shasum -a 256; } | cut -c1-16)"; fi
if [ -f "$DEST/.version" ] && [ "$(cat "$DEST/.version")" = "$SELLO" ]; then
    echo "[chrome-headless] $PLAT $VERSION: al día" >&2
    exit 0
fi

sha_ok() { # <archivo> <sha>
    local h
    if command -v sha256sum >/dev/null 2>&1; then h="$(sha256sum "$1" | cut -d' ' -f1)"; else h="$(shasum -a 256 "$1" | cut -d' ' -f1)"; fi
    [ "$h" = "$2" ]
}
bajar() { # <url> <destino> <sha>
    if [ -f "$2" ] && sha_ok "$2" "$3"; then return 0; fi
    curl -fL --retry 3 --connect-timeout 30 -o "$2.part" "$1" || { echo "[chrome-headless] ERROR: no se pudo bajar $1" >&2; exit 1; }
    sha_ok "$2.part" "$3" || { rm -f "$2.part"; echo "[chrome-headless] ERROR: el sha256 de $1 no coincide" >&2; exit 1; }
    mv "$2.part" "$2"
}

DL="$ROOT/extern/.downloads"
mkdir -p "$DL"
ZIP="$DL/chrome-headless-shell-$PLAT-$VERSION.zip"
bajar "https://storage.googleapis.com/chrome-for-testing-public/$VERSION/$PLAT/chrome-headless-shell-$PLAT.zip" "$ZIP" "$SHA"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
unzip -q "$ZIP" -d "$TMP"
rm -rf "$DEST"
mkdir -p "$(dirname "$DEST")"
mv "$TMP/chrome-headless-shell-$PLAT" "$DEST"
# Solo los idiomas que hacen falta (inglés para Chrome, español para el aparato): son ~50 MB.
if [ -d "$DEST/locales" ]; then
    find "$DEST/locales" -type f ! -name 'en-US.pak*' ! -name 'es.pak*' ! -name 'es-419.pak*' -delete
fi

if [ "$PLAT" = linux64 ]; then
    mkdir -p "$DEST/lib" "$TMP/debs"
    for d in "${DEBS[@]}"; do
        ruta="${d% *}"; sha="${d#* }"; archivo="$DL/$(basename "$ruta")"
        bajar "$UBUNTU/$ruta" "$archivo" "$sha"
        # Un .deb es un ar: data.tar.* adentro (con bsdtar o ar + tar, el que haya).
        rm -rf "$TMP/debs/x" && mkdir -p "$TMP/debs/x"
        (cd "$TMP/debs/x" && { ar x "$archivo" 2>/dev/null || bsdtar -xf "$archivo"; } && tar -xf data.tar.* )
        find "$TMP/debs/x/usr/lib/x86_64-linux-gnu" -name '*.so*' \( -type f -o -type l \) -exec cp -a {} "$DEST/lib/" \;
        # NSS carga sus módulos (softokn, freebl, ckbi) desde nss/ al lado de libnss3.
        if [ -d "$TMP/debs/x/usr/lib/x86_64-linux-gnu/nss" ]; then
            mkdir -p "$DEST/lib/nss" && cp -a "$TMP/debs/x/usr/lib/x86_64-linux-gnu/nss/." "$DEST/lib/nss/"
        fi
    done
fi

echo "$SELLO" > "$DEST/.version"
echo "[chrome-headless] $PLAT $VERSION listo en extern/chrome-headless/$PLAT" >&2
