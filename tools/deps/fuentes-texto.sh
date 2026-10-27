#!/usr/bin/env bash
# ============================================================================
# tools/deps/fuentes-texto.sh — las tipografías de texto del bundle
#
# Para los gráficos y los rótulos: que una escena diseñada acá se vea igual en el
# equipo de respaldo, sin depender de las fuentes de la máquina. Liberation,
# Carlito, Caladea, DejaVu, Noto Sans y Serif, Alef, Rubik, Gentium Basic y Linux
# Libertine/Biolinum (licencias OFL, GPL con excepción de fuentes y similares), más
# Noto Sans SC para el chino (el cirílico ya lo traen Noto, DejaVu y Liberation).
#
# Antes venían de la descarga de LibreOffice; LibreOffice ya no viaja (lo reemplazó
# WordCraft/DeckCraft/GridCraft, tools/deps/craft.sh). Se bajan los MISMOS paquetes
# que usa LibreOffice para armarse, de su espejo de dependencias, con el nombre y el
# sha256 de su download.lst (Gentium, de una versión anterior de esa lista: el
# LibreOffice de hoy ya no la trae). Son los paquetes originales de cada proyecto.
#
# Uso: tools/deps/fuentes-texto.sh   -> extern/fuentes-texto/ (*.ttf y licencias/)
# ============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DL="$ROOT/extern/.downloads/fuentes-texto"
OUT="$ROOT/extern/fuentes-texto"
BASE="https://dev-www.libreoffice.org/src"

# paquete sha256
PAQUETES=(
  "alef-1.001.tar.gz b98b67602a2c8880a1770f0b9e37c190f29a7e2ade5616784f0b89fbdb75bf52"
  "368f114c078f94214a308a74c7e991bc-crosextrafonts-20130214.tar.gz c48d1c2fd613c9c06c959c34da7b8388059e2408d2bb19845dc3ed35f76e4d09"
  "c74b7223abe75949b4af367942d96c7a-crosextrafonts-carlito-20130920.tar.gz 4bd12b6cbc321c1cf16da76e2c585c925ce956a08067ae6f6c64eff6ccfdaf5a"
  "33e1e61fab06a547851ed308b4ffef42-dejavu-fonts-ttf-2.37.zip 7576310b219e04159d35ff61dd4a4ec4cdba4f35c00e002a136f00e96a908b0a"
  "liberation-fonts-ttf-2.1.5.tar.gz 7191c669bf38899f73a2094ed00f7b800553364f90e2637010a69c0e268f25d0"
  "liberation-narrow-fonts-ttf-1.07.6.tar.gz 8879d89b5ff7b506c9fc28efc31a5c0b954bbe9333e66e5283d27d20a8519ea3"
  "e7a384790b13c29113e22e596ade9687-LinLibertineG-20120116.zip 54adcb2bc8cac0927a647fbd9362f45eff48130ce6e2379dc3867643019e08c5"
  "NotoSans-v2.015.zip 0c34df072a3fa7efbb7cbf34950e1f971a4447cffe365d3a359e2d4089b958f5"
  "NotoSerif-v2.015.zip 0e9a43c8a4b94ac76f55069ed1d7385bbcaf6b99527a94deb5619e032b7e76c1"
  "libre-hebrew-1.0.tar.gz f596257c1db706ce35795b18d7f66a4db99d427725f20e9384914b534142579a"
  "1725634df4bb3dcb1b2c91a6175f8789-GentiumBasic_1102.zip 2f1a2c5491d7305dffd3520c6375d2f3e14931ee35c6d8ae1e8f098bf1a7b3cc"
  # El chino (la interfaz y los rótulos en chino): Noto Sans SC, del proyecto Noto (no está en
  # el espejo de LibreOffice). Con URL entera: se baja de ahí.
  "https://github.com/notofonts/noto-cjk/releases/download/Sans2.004/18_NotoSansSC.zip 4d107c09ada479d3e48b6e78c83835773cbd9214bf6e12cdb7b60f8e068292ec"
)

# Las que viajan (las mismas que traía LibreOffice: los gráficos y las plantillas las nombran).
FUENTES=(
  Alef-Bold.ttf Alef-Regular.ttf
  Caladea-Bold.ttf Caladea-BoldItalic.ttf Caladea-Italic.ttf Caladea-Regular.ttf
  Carlito-Bold.ttf Carlito-BoldItalic.ttf Carlito-Italic.ttf Carlito-Regular.ttf
  DejaVuSans-Bold.ttf DejaVuSans-BoldOblique.ttf DejaVuSans-ExtraLight.ttf DejaVuSans-Oblique.ttf DejaVuSans.ttf
  DejaVuSansCondensed-Bold.ttf DejaVuSansCondensed-BoldOblique.ttf DejaVuSansCondensed-Oblique.ttf DejaVuSansCondensed.ttf
  DejaVuSansMono-Bold.ttf DejaVuSansMono-BoldOblique.ttf DejaVuSansMono-Oblique.ttf DejaVuSansMono.ttf
  DejaVuSerif-Bold.ttf DejaVuSerif-BoldItalic.ttf DejaVuSerif-Italic.ttf DejaVuSerif.ttf
  DejaVuSerifCondensed-Bold.ttf DejaVuSerifCondensed-BoldItalic.ttf DejaVuSerifCondensed-Italic.ttf DejaVuSerifCondensed.ttf
  GenBasB.ttf GenBasBI.ttf GenBasI.ttf GenBasR.ttf GenBkBasB.ttf GenBkBasBI.ttf GenBkBasI.ttf GenBkBasR.ttf
  LiberationMono-Bold.ttf LiberationMono-BoldItalic.ttf LiberationMono-Italic.ttf LiberationMono-Regular.ttf
  LiberationSans-Bold.ttf LiberationSans-BoldItalic.ttf LiberationSans-Italic.ttf LiberationSans-Regular.ttf
  LiberationSansNarrow-Bold.ttf LiberationSansNarrow-BoldItalic.ttf LiberationSansNarrow-Italic.ttf LiberationSansNarrow-Regular.ttf
  LiberationSerif-Bold.ttf LiberationSerif-BoldItalic.ttf LiberationSerif-Italic.ttf LiberationSerif-Regular.ttf
  LinBiolinum_R_G.ttf LinBiolinum_RB_G.ttf LinBiolinum_RI_G.ttf
  LinLibertine_DR_G.ttf LinLibertine_R_G.ttf LinLibertine_RB_G.ttf LinLibertine_RBI_G.ttf LinLibertine_RI_G.ttf
  LinLibertine_RZ_G.ttf LinLibertine_RZI_G.ttf
  NotoSans-Bold.ttf NotoSans-BoldItalic.ttf NotoSans-Italic.ttf NotoSans-Regular.ttf
  NotoSerif-Bold.ttf NotoSerif-BoldItalic.ttf NotoSerif-Italic.ttf NotoSerif-Regular.ttf
  Rubik-Bold.ttf Rubik-BoldItalic.ttf Rubik-Italic.ttf Rubik-Regular.ttf
  NotoSansSC-Regular.otf NotoSansSC-Bold.otf
)

log() { echo "[fuentes] $*"; }
sha() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }

marca="$OUT/.lista-$(printf '%s\n' "${PAQUETES[@]}" | sha /dev/stdin 2>/dev/null || printf '%s\n' "${PAQUETES[@]}" | shasum -a 256 | cut -d' ' -f1)"
if [ -f "$marca" ]; then log "ya presentes"; exit 0; fi

mkdir -p "$DL"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
for linea in "${PAQUETES[@]}"; do
    read -r p esperado <<<"$linea"
    url="$BASE/$p"
    case "$p" in *://*) url="$p"; p="$(basename "$p")" ;; esac
    f="$DL/$p"
    if [ ! -f "$f" ] || [ "$(sha "$f")" != "$esperado" ]; then
        log "bajando $p"
        curl -fsSL --retry 3 -o "$f.tmp" "$url"
        mv "$f.tmp" "$f"
    fi
    h="$(sha "$f")"
    [ "$h" = "$esperado" ] || { echo "ERROR: $p: sha256 $h, se esperaba $esperado" >&2; rm -f "$f"; exit 1; }
    d="$tmp/$p.d"; mkdir -p "$d"
    case "$p" in
        *.zip) unzip -q "$f" -d "$d" ;;
        *) tar -xzf "$f" -C "$d" ;;
    esac
done

rm -rf "$OUT" && mkdir -p "$OUT/licencias"
faltan=0
for n in "${FUENTES[@]}"; do
    e="$(find "$tmp" -name "$n" -type f | head -1)"
    if [ -z "$e" ]; then echo "ERROR: falta $n" >&2; faltan=1; continue; fi
    cp "$e" "$OUT/$n"
done
[ "$faltan" = 0 ] || exit 1
# Las licencias de cada paquete (OFL, LICENSE, COPYING…), en una carpeta por paquete.
for d in "$tmp"/*.d; do
    p="$(basename "$d" .d)"; p="${p%.tar.gz}"; p="${p%.zip}"
    mkdir -p "$OUT/licencias/$p"
    find "$d" -maxdepth 3 -type f \( -iname 'OFL*' -o -iname 'LICENSE*' -o -iname 'COPYING*' -o -iname 'GPL*' -o -iname 'AUTHORS*' -o -iname 'FONTLOG*' \) \
        -exec cp {} "$OUT/licencias/$p/" \;
done
# El paquete de Caladea no trae el texto de su licencia (Apache-2.0): se deja dicho.
for d in "$OUT"/licencias/*crosextrafonts-2013*; do
    [ -n "$(ls -A "$d" 2>/dev/null)" ] || printf '%s\n' \
        "Caladea — Huerta Tipográfica (Andrés Torresi, Carolina Giovagnoli)." \
        "Apache License 2.0: https://www.apache.org/licenses/LICENSE-2.0" \
        "Origen: https://github.com/huertatipografica/Caladea" > "$d/LICENCIA.txt"
done
touch "$marca"
log "$(ls "$OUT"/*.ttf "$OUT"/*.otf 2>/dev/null | wc -l | tr -d ' ') tipografías en extern/fuentes-texto"
