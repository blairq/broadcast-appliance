#!/usr/bin/env bash
# ============================================================================
# tools/deps/herramientas.sh — herramientas de medios dentro del programa
#
# Para arreglar un video que nos mandan, sacar el audio, medir el desfase,
# bajar un clip de una página… sin que nadie (ni el asistente) tenga que
# bajar nada en el momento. Van ADENTRO del programa (regla de portabilidad):
#
#   ffmpeg, ffprobe  9.0.2   compilaciones estáticas, sin dependencias
#                            (macOS/Linux: ffmpeg.martin-riedl.de; Windows: gyan.dev)
#   yt-dlp           2026.08.19   el ejecutable autónomo oficial
#   deno             2.9.7   el motor de JavaScript que yt-dlp necesita para
#                            YouTube (yt-dlp lo busca en el PATH)
#
# Son programas aparte: el programa los llama, no los enlaza (sus licencias,
# GPL/LGPL/Unlicense/MIT, viajan en LICENCIAS.txt).
#
# Uso: tools/deps/herramientas.sh <plataforma> <descargas> <salida>
#      plataforma: macos-arm64 | linux-x64 | linux-arm64 | win-x64
#      (lo llama make deps-herramientas)
# ============================================================================
set -euo pipefail
PLAT="${1:?falta la plataforma}"
DL="${2:?falta la carpeta de descargas}"
OUT="${3:?falta la salida}"
log() { printf '\033[36m[herramientas]\033[0m %s\n' "$*"; }
sha() { if command -v sha256sum >/dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi | cut -d' ' -f1; }

FF_VER=9.0.2
YTDLP_VER=2026.08.19
DENO_VER=2.9.7
MR=https://ffmpeg.martin-riedl.de/download
YT=https://github.com/yt-dlp/yt-dlp/releases/download/$YTDLP_VER
DN=https://github.com/denoland/deno/releases/download/v$DENO_VER

# archivo-local|url|sha256 de cada cosa, por plataforma.
case "$PLAT" in
macos-arm64)
    FFMPEG="ffmpeg-$FF_VER-macos-arm64.zip|$MR/macos/arm64/1789931890_$FF_VER/ffmpeg.zip|c8ed4c4e6978a03c485edbfe4e0a5dc2380f8a30bba5150531b31b094492d924"
    FFPROBE="ffprobe-$FF_VER-macos-arm64.zip|$MR/macos/arm64/1789931890_$FF_VER/ffprobe.zip|fcbe839537485eaee7a7a8bc5cbc0f90d53617e80943e8a5b2e31cb851197ea6"
    YTDLP="yt-dlp-$YTDLP_VER-macos|$YT/yt-dlp_macos|0f192b7ec147ab6288885d6351d9ab67367640029b4377576ef46dd79cf7b202"
    DENO="deno-$DENO_VER-aarch64-apple-darwin.zip|$DN/deno-aarch64-apple-darwin.zip|5cd46d6268f6f78f5d88bdc7159d20bd44cdaa4b3303474839f87ec6fe7ae25c"
    EXE="" ;;
linux-x64)
    FFMPEG="ffmpeg-$FF_VER-linux-amd64.zip|$MR/linux/amd64/1789931100_$FF_VER/ffmpeg.zip|fa8ecf4abbd290d98f7d188b8649cc6b391ae209a98452be955a15aab1909d7f"
    FFPROBE="ffprobe-$FF_VER-linux-amd64.zip|$MR/linux/amd64/1789931100_$FF_VER/ffprobe.zip|3f428c49070be3d24ec338602b76d412e401ffcb8a5641ef0e729181a232fc32"
    YTDLP="yt-dlp-$YTDLP_VER-linux|$YT/yt-dlp_linux|58162f9bfdc27458ea47bfcb311cf47028f17d8154a8bf7d689861d46399230a"
    DENO="deno-$DENO_VER-x86_64-linux.zip|$DN/deno-x86_64-unknown-linux-gnu.zip|c6527f24f4b16031d3ae4fa9f658d5f11534c8d84ce7dc8502420280919c3490"
    EXE="" ;;
linux-arm64)
    FFMPEG="ffmpeg-$FF_VER-linux-arm64.zip|$MR/linux/arm64/1789931697_$FF_VER/ffmpeg.zip|93a76ae90db5474eecdf951a729857c64f3de23567228d6a7d5e6e8e3cd1021b"
    FFPROBE="ffprobe-$FF_VER-linux-arm64.zip|$MR/linux/arm64/1789931697_$FF_VER/ffprobe.zip|bcbe80fb741c180083327afaf5434812e006b33cacde2016b9aeaf6936128330"
    YTDLP="yt-dlp-$YTDLP_VER-linux-aarch64|$YT/yt-dlp_linux_aarch64|b16e4dab368a816cd05d477d698a605a6ae87ccee1c8ffd38fa21d7254141fcc"
    DENO="" # sin deno para Linux arm64 fijado todavía: yt-dlp anda, YouTube a medias
    EXE="" ;;
win-x64)
    FFMPEG="ffmpeg-$FF_VER-essentials_build.zip|https://github.com/GyanD/codexffmpeg/releases/download/$FF_VER/ffmpeg-$FF_VER-essentials_build.zip|60f467265b1e312373dbcd92200c2618a74850f98d3d078e94296bb3fa2047ba"
    FFPROBE="" # viene en el mismo zip
    YTDLP="yt-dlp-$YTDLP_VER.exe|$YT/yt-dlp.exe|66674953fe251b89f4d08c5f0e35e0728679bd67ab3d7d05c0562af101dd3e7a"
    DENO="deno-$DENO_VER-x86_64-windows.zip|$DN/deno-x86_64-pc-windows-msvc.zip|a0c3101b4158d1dfb7d6a78a7bf0f3de80c96bb423c152beec8beb22786f2238"
    EXE=".exe" ;;
*) echo "plataforma desconocida: $PLAT" >&2; exit 2 ;;
esac

# Bajar y verificar, o nada (como _ba_baja del Makefile). Devuelve la ruta.
baja() {
    local nombre url hash dest
    IFS='|' read -r nombre url hash <<<"$1"
    dest="$DL/$nombre"
    if [ -f "$dest" ] && [ "$(sha "$dest")" = "$hash" ]; then echo "$dest"; return; fi
    log "bajando $nombre" >&2
    curl -fL --retry 3 --connect-timeout 30 -s -o "$dest.part" "$url"
    if [ "$(sha "$dest.part")" != "$hash" ]; then
        rm -f "$dest.part"; echo "[herramientas] ERROR: hash de $nombre" >&2; exit 1
    fi
    mv "$dest.part" "$dest"
    echo "$dest"
}
# Saca un archivo de un zip (esté donde esté adentro) a la salida.
sacar() { # zip nombre-adentro
    local tmp; tmp="$(mktemp -d)"
    unzip -q -o "$1" -d "$tmp"
    local f; f="$(find "$tmp" -type f -name "$2" | head -1)"
    [ -n "$f" ] || { echo "[herramientas] ERROR: $2 no está en $(basename "$1")" >&2; exit 1; }
    cp "$f" "$OUT.tmp/$2"
    rm -rf "$tmp"
}

mkdir -p "$DL"
rm -rf "$OUT.tmp"
mkdir -p "$OUT.tmp"
z="$(baja "$FFMPEG")"; sacar "$z" "ffmpeg$EXE"
if [ -n "$FFPROBE" ]; then z="$(baja "$FFPROBE")"; fi
sacar "$z" "ffprobe$EXE"
cp "$(baja "$YTDLP")" "$OUT.tmp/yt-dlp$EXE"
if [ -n "$DENO" ]; then z="$(baja "$DENO")"; sacar "$z" "deno$EXE"; fi
chmod +x "$OUT.tmp"/*

cat >"$OUT.tmp/LICENCIAS.txt" <<EOF
Herramientas de medios incluidas (programas aparte; el programa las ejecuta, no las enlaza).

ffmpeg / ffprobe $FF_VER — https://ffmpeg.org — GPL v3 (compilación con x264/x265)
  Código fuente: https://ffmpeg.org/releases/ffmpeg-$FF_VER.tar.xz
  Compilación: macOS/Linux https://ffmpeg.martin-riedl.de · Windows https://www.gyan.dev/ffmpeg/builds/
yt-dlp $YTDLP_VER — https://github.com/yt-dlp/yt-dlp — Unlicense (dominio público)
deno $DENO_VER — https://github.com/denoland/deno — MIT
EOF

# Apple Silicon no corre código sin firma: firma ad-hoc (make app-macos vuelve
# a sellar la .app entera).
if [ "$(uname -s)" = Darwin ] && [ "$PLAT" = macos-arm64 ]; then
    for f in "$OUT.tmp"/*; do
        file -b "$f" | grep -q Mach-O && codesign -f -s - "$f" 2>/dev/null || true
    done
    "$OUT.tmp/ffmpeg" -hide_banner -version | head -1
    "$OUT.tmp/ffprobe" -hide_banner -version | head -1
    "$OUT.tmp/deno" --version | head -1
    "$OUT.tmp/yt-dlp" --version
fi
rm -rf "$OUT"
mv "$OUT.tmp" "$OUT"
log "listo: $OUT ($(du -sh "$OUT" | cut -f1))"
