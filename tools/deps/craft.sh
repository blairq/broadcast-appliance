#!/usr/bin/env bash
# ============================================================================
# tools/deps/craft.sh — WordCraft, DeckCraft y GridCraft (sus herramientas de línea de comandos)
#
# Lo que reemplaza a LibreOffice: pasar a PDF (o a imágenes) lo que trae la gente
# —un Word, un PowerPoint, una planilla— y, por su MCP, que el asistente arme
# documentos, presentaciones y gráficos. Son reimplementaciones de Word,
# PowerPoint y Excel en Rust (https://github.com/storytold, Apache-2.0).
#
# NO SE COMPILAN: se bajan los releases oficiales, con versión y sha256 fijos, y
# de cada paquete se saca SOLO la herramienta de línea de comandos (la app con
# ventana no viaja) y sus licencias. Las tipografías van adentro del binario.
#   - macOS: el zip «-cli-…-macos-universal» (solo la CLI); se deja solo la parte
#     de Apple Silicon (lipo -thin arm64): la mitad de tamaño.
#   - Windows: el portable x64 trae la CLI aparte (<app>-cli.exe).
#   - Linux: el tarball x86_64 trae bin/<app>-cli.
#
# Uso: tools/deps/craft.sh [mac|win|linux|todo]   -> extern/craft/<plataforma>/
# ============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DL="$ROOT/extern/.downloads/craft"
OUT="$ROOT/extern/craft"
QUE="${1:-todo}"

# app versión sha256(mac-cli) sha256(windows-x64-portable) sha256(linux-x86_64)
APPS=(
  "wordcraft 0.11.0 dc6815a57a06fc5a5e4aa777f074a4d8d8010ec4ff813d00b6e2c13559aa7bcf 761a832f91a4bedcfacc62126ac31f78d5172fa10932b55bb6a587c797e38bea 034c247bf6dab172a2edfb0767de9065125b857826c6915f7ef39b9f43951d5a"
  "deckcraft 0.11.1 02ef9ea0600499b36036a4a4426f6903e86a24748705c72e8c8ad73da03a1ab6 8123f1eceb048b60f34b8cbe3ee49048c96b713c8354022a98485267a7f9a386 7bf57a10a4fcf7673ecb669b64e0e545475780ef3e4ee6bf603aeb00f544e69c"
  "gridcraft 0.12.1 23a0b7b4c2bcb92fb9ba2967d0b50608d98f731601ef653ade645d010f1215ea 4e478a735736281704c8362d95964eaaa6071331851d609367d33245531ab7c8 1c1750dd6d26376cf8c8af22b46dc0ed3d8d5cbb115e555f0fc7d0cf7a9aea9a"
)

log() { echo "[craft] $*"; }

sha() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }

bajar() { # url archivo sha
    local url="$1" f="$DL/$2" esperado="$3"
    mkdir -p "$DL"
    if [ ! -f "$f" ] || [ "$(sha "$f")" != "$esperado" ]; then
        log "bajando $2"
        curl -fL --retry 3 -o "$f.tmp" "$url"
        mv "$f.tmp" "$f"
    fi
    local h; h="$(sha "$f")"
    [ "$h" = "$esperado" ] || { echo "ERROR: $2: sha256 $h, se esperaba $esperado" >&2; rm -f "$f"; exit 1; }
}

# Una plataforma: la CLI de cada app y sus licencias a extern/craft/<plataforma>/.
plataforma() {
    local plat="$1" destino="$OUT/$1"
    local tmp; tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' RETURN
    mkdir -p "$destino/licencias"
    for linea in "${APPS[@]}"; do
        read -r app ver sha_mac sha_win sha_linux <<<"$linea"
        local base="https://github.com/storytold/$app/releases/download/v$ver"
        local marca="$destino/.$app-$ver"
        if [ -f "$marca" ]; then log "$plat: $app $ver ya está"; continue; fi
        rm -rf "$tmp/x" && mkdir -p "$tmp/x"
        case "$plat" in
            mac)
                local z="$app-cli-$ver-macos-universal.zip"
                bajar "$base/$z" "$z" "$sha_mac"
                unzip -q "$DL/$z" -d "$tmp/x"
                local d="$tmp/x/$app-cli-$ver-macos-universal"
                if command -v lipo >/dev/null 2>&1; then
                    lipo -thin arm64 "$d/$app-cli" -output "$destino/$app-cli"
                else
                    cp "$d/$app-cli" "$destino/$app-cli" # sin lipo (no es una Mac): queda universal
                fi
                chmod +x "$destino/$app-cli"
                ;;
            win)
                local z="$app-$ver-windows-x64-portable.zip"
                bajar "$base/$z" "$z" "$sha_win"
                unzip -q "$DL/$z" -d "$tmp/x"
                local d; d="$(find "$tmp/x" -maxdepth 2 -name "$app-cli.exe" -exec dirname {} \; | head -1)"
                [ -n "$d" ] || { echo "ERROR: $z no trae $app-cli.exe" >&2; exit 1; }
                cp "$d/$app-cli.exe" "$destino/"
                ;;
            linux)
                local z="$app-$ver-linux-x86_64.tar.gz"
                bajar "$base/$z" "$z" "$sha_linux"
                tar -xzf "$DL/$z" -C "$tmp/x"
                local b; b="$(find "$tmp/x" -path "*/bin/$app-cli" -type f | head -1)"
                [ -n "$b" ] || { echo "ERROR: $z no trae bin/$app-cli" >&2; exit 1; }
                cp "$b" "$destino/$app-cli" && chmod +x "$destino/$app-cli"
                ;;
        esac
        # Las licencias: la del programa (Apache-2.0, y MIT si la trae) y las OFL de
        # las tipografías que lleva adentro.
        mkdir -p "$destino/licencias/$app"
        find "$tmp/x" \( -name 'LICENSE*' -o -name 'OFL-*.txt' -o -name 'NOTICE*' \) -type f \
            -exec cp {} "$destino/licencias/$app/" \;
        touch "$marca"
        log "$plat: $app $ver listo"
    done
}

case "$QUE" in
    mac) plataforma mac ;;
    win) plataforma win ;;
    linux) plataforma linux ;;
    todo) plataforma linux; plataforma win; [ "$(uname -s)" = Darwin ] && plataforma mac || true ;;
    *) echo "uso: $0 [mac|win|linux|todo]" >&2; exit 2 ;;
esac
