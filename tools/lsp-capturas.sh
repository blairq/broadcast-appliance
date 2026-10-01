#!/usr/bin/env bash
# ============================================================================
# tools/lsp-capturas.sh — las capturas de los filtros LSP para el catálogo
#
# Las saca del código fuente OFICIAL de LSP (el mismo tarball, fijado por
# sha256, con el que tools/deps/lsp-*.sh compila los plugins): cada módulo
# trae res/doc/screenshots/, las mismas capturas de lsp-plug.in. Van con la
# licencia del proyecto (LGPL-3.0-or-later, © Linux Studio Plugins), no
# bajadas del sitio.
#
# Una por familia (la estéreo si hay), achicada a 760 px en JPEG, en
# assets/lsp-capturas/<familia>.jpg (se versionan: ~3 MB). Familias según
# datos/lsp-catalogo.json. La del Matrix Mixer sale de nuestro módulo
# (third_party/lsp-plugins-matrix-mixer/res/doc/screenshots).
#
# Uso: tools/lsp-capturas.sh      (en una Mac: usa sips para achicar)
# ============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VER=1.2.35
SHA=2c95ec7bb219d561ea3db36051b6c732133bcd76426fb836b1dd850dc4b5bb6c
TAR="$ROOT/extern/.downloads/lsp-plugins-src-$VER.tar.gz"
OUT="$ROOT/assets/lsp-capturas"
log() { echo "[lsp-capturas] $*" >&2; }

if [ ! -f "$TAR" ]; then
    mkdir -p "$(dirname "$TAR")"
    curl -fsSL -o "$TAR" "https://github.com/lsp-plugins/lsp-plugins/releases/download/$VER/lsp-plugins-src-$VER.tar.gz"
fi
[ "$(shasum -a 256 "$TAR" | cut -d' ' -f1)" = "$SHA" ] || { log "el tarball no coincide con el sha256"; exit 1; }
command -v sips >/dev/null || { log "falta sips (correr en una Mac)"; exit 1; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
tar xzf "$TAR" -C "$TMP" --wildcards '*/res/doc/screenshots/*.png' 2>/dev/null || tar xzf "$TAR" -C "$TMP"
# Y las de nuestro módulo (third_party/lsp-plugins-matrix-mixer: tools/lsp-matrix-mixer-doc.py).
mkdir -p "$TMP/propios" && cp -R "$ROOT/third_party/lsp-plugins-matrix-mixer/res" "$TMP/propios/"
mkdir -p "$OUT"
n=0
for clave in $(python3 -c "import json;print(' '.join(f['clave'] for f in json.load(open('$ROOT/datos/lsp-catalogo.json'))['familias']))"); do
    elegida=""
    for cand in "${clave}_stereo" "sc_${clave}_stereo" "${clave}_x8_stereo" "${clave}_x16_stereo" "${clave}_x32_stereo" \
                "${clave}_x2_stereo" "${clave}_x4_stereo" "${clave}_mono" "${clave}_x8_mono" "sc_${clave}_mono" "${clave}"; do
        f="$(find "$TMP" -path '*res/doc/screenshots/*' -name "$cand.png" | head -1)"
        [ -n "$f" ] && { elegida="$f"; break; }
    done
    # Último recurso: las familias con cantidades raras (spectrum_analyzer_x1, multisampler_x12...).
    [ -n "$elegida" ] || elegida="$(find "$TMP" -path '*res/doc/screenshots/*' -name "${clave}_x*.png" | sort | head -1)"
    [ -n "$elegida" ] || { log "sin captura: $clave"; continue; }
    sips -Z 760 -s format jpeg -s formatOptions 82 "$elegida" --out "$OUT/$clave.jpg" >/dev/null
    n=$((n + 1))
done
cp "$(find "$TMP" -maxdepth 2 -name COPYING.LESSER | head -1)" "$OUT/COPYING.LESSER"
cat >"$OUT/LEEME.txt" <<TXT
Capturas de los plugins LSP (Linux Studio Plugins) $VER, sacadas del código
fuente oficial (lsp-plugins-src-$VER.tar.gz, res/doc/screenshots de cada
módulo) y achicadas. © Linux Studio Plugins, LGPL-3.0-or-later (COPYING.LESSER).
matrix_mixer.jpg es de nuestro módulo (third_party/lsp-plugins-matrix-mixer),
con la misma licencia.
https://lsp-plug.in — las genera tools/lsp-capturas.sh.
TXT
log "$n capturas en $OUT ($(du -sh "$OUT" | cut -f1))"
